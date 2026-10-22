#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Build Felucca: the app, the update loader and an installable .fwsc package.

  tools/build.py [--release X.Y[-suffix]] [--board fm1|smk37] [--model pro|elite|mke]

Outputs in build/: felucca.bin (app), loader/ota.bin (update loader),
felucca.fwsc (package). A release build (--release X.Y) writes skiff-X.Y.fwsc and a folder
release-X.Y/ with the package, the app, SHA256SUMS, the samples' attributions and the licence files.
See BUILDING.md for the toolchain and the SDK.

BOARD=smk37 (or --board smk37) builds Skiff for the M-VAVE SMK-37 Pro (after genkigenki's SMK-37 layer for Felucca):
-DBOARD_SMK37=1 for the app and the loader, identity SMK-37 Pro_700 (release SMK-37 Pro_7XY), the SMK flash layout
(fm1pkg_make.py), the package build/skiff-smk37.fwsc (skiff-smk37-X.Y.fwsc for a release). MODEL=elite / mke (or
--model) with smk37: the same build under the SMK-37 Elite's or the MKE-P37's model name (identity, loader, update
record; -DSMK_MODEL), so the stock updater still takes that unit's factory package: skiff-smk37-elite.fwsc, -mke.fwsc.
The default stays the FM-1, whose build gets no new flag.

The JieLi toolchain is Linux x86-64 only. JIELI_TOOLCHAIN points at it; on
macOS (or with JIELI_DOCKER=1) each tool runs in a linux/amd64 container.
"""
import argparse
import hashlib
import os
import platform
import re
import shutil
import struct
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]
FW = SRC / "firmware"
OUT = SRC / "build"
GEN = OUT / "gen"
LDR = OUT / "loader"
sys.path.insert(0, str(SRC / "tools"))
import fm1pkg_make  # noqa: E402
import lz4blk  # noqa: E402

APP_XIP = 0x02000120                # app.bin offset 0 in the XIP map; the SPL jumps here
APP_SLOT = fm1pkg_make.APP_SLOT
LOADER_LOAD = 0x01C0A800
LOADER_NAME = b"usb_hid_ota.bin"    # the file name the SPL looks for
# the staged loader (its ota.bin, the package's UFW entry) must fit what the stock app leaves for one: on the SMK-37
# its staging at 0xBD000, its update record at 0xBEF00 (SMK37.md), where a bigger one may spill into BTIF / USRTRIM.
# The FM-1's stock V15 gap is not measured yet: the same limit holds for it until it is
LDR_MAX = 0xBEF00 - 0xBD000
DOCKER_IMAGE = os.environ.get("JIELI_DOCKER_IMAGE", "debian:bookworm-slim")
CFLAGS = ["-Os", "-ffunction-sections", "-fno-builtin", "-Wall", "-Wno-unused-function"]
LINE = re.compile(r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*\t(.*)$")

# SDK files of AC79NN_SDK_V1.2.1_2023-12-13 (the tested version)
SDK_SHA256 = {
    "uboot.boot": "4e3b4c220dc96641cb5a723f41e68ce41d5261ae9434bb33fbd7f2c59976ded4",
    "cfg_tool.bin": "276579954f076886a6a7694f65dc71c034a63a2c204b76749065c0ac7b010d1b",
    "cfg/eq_cfg_hw.bin": "41167491bffed4651750719c973d2758adeb9021a5670d02d6a53c85ed80ea7d",
}

PRODUCT = "FM-1_700"                # package identity; release builds are FM-1_7XY (Felucca's are FM-1_9XY)
BOARD = "fm1"                       # fm1 (default) or smk37: BOARD env or --board
BOARD_PRODUCT = {"fm1": "FM-1_700", "smk37": "SMK-37 Pro_700"}   # a dev build's identity
BOARD_PREFIX = {"fm1": "FM-1_7", "smk37": "SMK-37 Pro_7"}        # a release build's: prefix + X + Y
BOARD_DEFS = {"fm1": [], "smk37": ["-DBOARD_SMK37=1"]}           # the FM-1 build gets no new flag (byte-identical)
BOARD_STEM = {"fm1": "skiff", "smk37": "skiff-smk37"}            # a release package: STEM-X.Y.fwsc
# smk37: the keyboard's model name (MODEL env or --model); one hardware, three names. pro adds no flag
MODELS = {"pro": "SMK-37 Pro", "elite": "SMK-37 Elite", "mke": "MKE-P37"}
VERSION = None                      # FELUCCA_VERSION for release builds (default: firmware/src/felucca.c)
# FELUCCA_CPU2 (firmware/src/felucca.c), unset or 1, the default (Skiff 1.3, as Felucca 1.6.1): the second core is built
# in, its entry and stacks (firmware/hal/fm1_cpu1.S) linked; MENU > SYSTEM > CPU 2 CORES starts it (1 CORE by default:
# as 1.2). FELUCCA_CPU2=0: none of it is built
CPU2 = os.environ.get("FELUCCA_CPU2") != "0"


def toolchain():
    tc = os.environ.get("JIELI_TOOLCHAIN")
    if not tc or not (Path(tc) / "pi32v2" / "bin" / "clang").exists():
        raise SystemExit("JIELI_TOOLCHAIN must point at the JieLi Linux toolchain "
                         "(the directory with pi32v2/ and common/; see tools/get_toolchain.sh)")
    return Path(tc).resolve()


def use_docker():
    native = platform.system() == "Linux" and platform.machine() in ("x86_64", "AMD64")
    return os.environ.get("JIELI_DOCKER", "0" if native else "1") == "1"


def tc(tool, *args):
    """run a toolchain binary (pi32v2/bin/..., common/bin/...) with cwd SRC; paths relative to SRC"""
    rel = [str(Path(a).resolve().relative_to(SRC)) if isinstance(a, Path) else a for a in args]
    if tool == "cc":                # the toolchain's cc wrapper needs python3; call clang directly
        tool, rel = "pi32v2/bin/clang", ["-target", "pi32v2", *rel]
    if use_docker():
        cmd = ["docker", "run", "--rm", "--platform", "linux/amd64", "-v", f"{SRC}:/work",
               "-v", f"{toolchain()}:/opt/jieli:ro", "-w", "/work", DOCKER_IMAGE, f"/opt/jieli/{tool}", *rel]
    else:
        cmd = [str(toolchain() / tool), *rel]
    r = subprocess.run(cmd, cwd=SRC, capture_output=True, text=True)
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit(f"build: {tool} failed")
    if r.stderr.strip():
        sys.stderr.write(r.stderr)
    return r.stdout


def tc_all(*cmds):
    with ThreadPoolExecutor(len(cmds)) as ex:
        return list(ex.map(lambda c: tc(*c), cmds))


def generate():
    """generated headers (UI fonts, icons, keycaps, palettes, tables, samples)"""
    GEN.mkdir(parents=True, exist_ok=True)
    for old in ("felucca_font.h", "felucca_icons.h"):     # headers of the bitmap font and icon atlas
        (GEN / old).unlink(missing_ok=True)
    tools = SRC / "tools"
    font_args = ["--huff", "S,M,L,V"] if os.environ.get("FELUCCA_USB_ROUTING", "1") != "0" else []
    cmds = [[tools / "gen_aa_font.py", GEN / "ui_fonts.h", "--preset", "barlow-condensed", *font_args],
            [tools / "gen_aa_icons.py", GEN / "ui_icons.h"],
            [tools / "gen_aa_keycaps.py", GEN / "ui_keycaps.h"],
            [tools / "gen_ui_palettes.py", GEN / "ui_palettes.h"],
            [tools / "gen_pixel_font.py", GEN / "ui_pixfont.h"],
            [tools / "gen_tables.py", GEN / "felucca_tables.h"],
            [tools / "gen_fm6_patches.py", GEN / "felucca_fm6.h"],
            [tools / "gen_samples.py", GEN / "felucca_samples.h"]]
    procs = [subprocess.Popen([sys.executable, *map(str, c)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True) for c in cmds]
    failed = []
    for c, p in zip(cmds, procs):
        sys.stdout.write(p.communicate()[0])
        if p.returncode:
            failed.append(c[0].name)
    if failed:
        raise SystemExit(f"build: {', '.join(failed)} failed")


# ---- update loader

def crc16(d, c=0):
    for b in d:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) if c & 0x8000 else c << 1
        c &= 0xFFFF
    return c


def ldr_head(dcrc, a, b, attr, name):
    body = struct.pack("<HIIBBH16s", dcrc, a, b, attr, 0, 0, name)
    return struct.pack("<H", crc16(body)) + body


def ldr_wrap(image):
    """ota.bin = outer JLFS head (32 B) + inner head (32 B, load address) +
    blocks [clen u32][dlen u32][LZ4 block], dlen 4096 except the last"""
    blocks = bytearray()
    for i in range(0, len(image), 4096):
        raw = image[i:i + 4096]
        c = lz4blk.compress(raw)
        if len(c) > 4096 or lz4blk.decompress(c) != raw:
            raise SystemExit(f"loader: block {i // 4096} does not compress below 4096 B")
        blocks += struct.pack("<II", len(c), len(raw)) + c
    inner = ldr_head(crc16(image), len(image), LOADER_LOAD, 0, LOADER_NAME)
    body = inner + bytes(blocks)
    return ldr_head(crc16(body), 0x20, len(body), 0x41, LOADER_NAME) + body


def build_loader():
    LDR.mkdir(parents=True, exist_ok=True)
    src = FW / "loader"
    flags = [*CFLAGS, *BOARD_DEFS[BOARD], "-Ifirmware/hal", "-Ifirmware/src", f'-DFELUCCA_ID="ota-{PRODUCT}"']
    flags = ["-Oz" if f == "-Os" else f for f in flags]   # (LDR_MAX: the smallest code the compiler makes)
    tc_all(("cc", "-c", src / "crt0_ldr.S", "-o", LDR / "crt0_ldr.o"),
           ("cc", *flags, "-c", src / "loader.c", "-o", LDR / "loader.o"))
    elf = LDR / "loader.elf"
    tc("pi32v2/bin/ld", "--gc-sections", "-e", "_start", "-T", src / "loader.ld",
       LDR / "crt0_ldr.o", LDR / "loader.o", "-o", elf)
    _, dis, hdr, syms = tc_all(("common/bin/objcopy", "-O", "binary", "-j", ".text", elf, LDR / "loader.bin"),
                               ("common/bin/objdump", "-d", elf),
                               ("common/bin/objdump", "-h", elf),
                               ("common/bin/objdump", "-t", elf))
    (LDR / "loader.dis").write_text(dis)
    for ln in hdr.splitlines():     # the loader rewrites the flash: nothing may run from XIP
        p = ln.split()
        if len(p) > 4 and p[1].startswith(".") and int(p[3], 16) >= 0x02000000 and int(p[2], 16):
            raise SystemExit(f"loader: section {p[1]} at {p[3]} is outside RAM")
    image = (LDR / "loader.bin").read_bytes()
    if not image or len(image) > 0x14000:
        raise SystemExit("loader: image empty or too big")
    ota = ldr_wrap(image)
    if len(ota) > LDR_MAX:
        raise SystemExit(f"loader: ota.bin {len(ota)} B, over the {LDR_MAX} B the stock app stages")
    (LDR / "ota.bin").write_bytes(ota)
    bss = [int(ln.split()[0], 16) for ln in syms.splitlines() if ln.rstrip().endswith("_bss_end")]
    print(f"loader: image {len(image)} B at {LOADER_LOAD:#x}" + (f", bss end {bss[0]:#x}" if bss else ""))
    print(f"loader: ota.bin {len(ota)} B, {LDR_MAX - len(ota)} B under the stock staging")
    return ota


def write_smk_ldr(ota):
    """SMK-37: the app carries its own loader (build/gen/smk_ldr.h, ota.c) and stages it for M-VAVE's packages"""
    rows = ",\n".join("    " + ", ".join(f"0x{b:02x}" for b in ota[i:i + 16]) for i in range(0, len(ota), 16))
    (GEN / "smk_ldr.h").write_text(
        "/* generated by tools/build.py: this build's update loader (build/loader/ota.bin), staged by ota.c in place of\n"
        " * M-VAVE's for an official package (SMK37.md) */\n"
        f"#define SMK_LDR_LEN {len(ota)}u\n"
        f"_Static_assert(SMK_LDR_LEN <= {LDR_MAX}u, \"the SMK loader must fit the stock app's staging\");\n"
        f"static const uint8_t SMK_LDR[SMK_LDR_LEN] = {{\n{rows}\n}};\n")


# ---- app

def build_app():
    flags = [*CFLAGS, *BOARD_DEFS[BOARD], "-Ifirmware/hal", "-Ifirmware/src", "-Ibuild/gen"]
    for flag in ("FELUCCA_FLASH", "FELUCCA_OTA", "FELUCCA_OTA_DRYRUN", "FELUCCA_OTA_RAMONLY", "FELUCCA_CDC",
                 "FELUCCA_UART", "FELUCCA_UAC", "FELUCCA_UAC_TONE", "FELUCCA_UAC_48K", "FELUCCA_ICONS", "FELUCCA_SLICE", "FELUCCA_FM4",
                 "FELUCCA_USB_6CH", "FELUCCA_USB_12CH", "FELUCCA_UAC_MAC_COMPAT", "FELUCCA_CDC_DEFAULT", "FELUCCA_CPU2", "FELUCCA_USB_TRACE", "FELUCCA_USB_ROUTING"):
        v = os.environ.get(flag)    # unset: the default in firmware/src/felucca.c
        if v in ("0", "1"):
            flags.append(f"-D{flag}={v}")
    channels=os.environ.get("FELUCCA_USB_CHANNELS")
    if channels is not None:
        if channels not in ("6","8","10"): raise SystemExit("FELUCCA_USB_CHANNELS must be 6, 8 or 10")
        flags.append(f"-DFELUCCA_USB_CHANNELS={channels}")
    v = os.environ.get("FELUCCA_USB_LAYOUT")   # firmware/src/usb.c: how the USB composite presents itself (#67), 0..3
    if v in ("0", "1", "2", "3"):
        flags.append(f"-DFELUCCA_USB_LAYOUT={v}")
    flags.append(f'-DFELUCCA_ID="{PRODUCT}"')
    if VERSION:
        flags.append(f'-DFELUCCA_VERSION="{VERSION}"')
    # felucca.c goes to LLVM IR without the optimizer, the main-loop functions (UI, stores, editor) are
    # marked minsize (tools/size_fns.py), then the IR is compiled at -Os. FELUCCA_SIZE=0: -Os everywhere
    size = os.environ.get("FELUCCA_SIZE") != "0"
    cmain = (("cc", *flags, "-S", "-emit-llvm", "-Xclang", "-disable-llvm-optzns", "-c",
              FW / "src" / "felucca.c", "-o", OUT / "felucca.ll") if size else
             ("cc", *flags, "-c", FW / "src" / "felucca.c", "-o", OUT / "felucca.o"))
    (OUT / "fm1_cpu1.o").unlink(missing_ok=True)
    tc_all(("cc", "-c", FW / "crt0.S", "-o", OUT / "crt0.o"),
           ("cc", "-c", FW / "hal" / "fm1_vec.S", "-o", OUT / "fm1_vec.o"),
           ("cc", "-c", FW / "hal" / "fm1_isr.S", "-o", OUT / "fm1_isr.o"),
           *([("cc", "-c", FW / "hal" / "fm1_cpu1.S", "-o", OUT / "fm1_cpu1.o")] if CPU2 else []),
           cmain)
    if size:
        subprocess.run([sys.executable, SRC / "tools" / "size_fns.py", OUT / "felucca.ll", OUT / "felucca_size.ll"],
                       check=True)
        tc("cc", *[f for f in flags if not f.startswith(("-I", "-D", "-W"))], "-c",
           OUT / "felucca_size.ll", "-o", OUT / "felucca.o")
    elf = OUT / "felucca.elf"
    ld = FW / "app.ld"
    if APP_SLOT != 0x8DFBC:          # another board's app slot (SMK-37 Pro): the same script, its XIP length
        txt = ld.read_text()
        if txt.count("LENGTH = 0x8DFBC") != 1:
            raise SystemExit("app.ld: the XIP LENGTH line was not found")
        ld = OUT / "app.ld"
        ld.write_text(txt.replace("LENGTH = 0x8DFBC", f"LENGTH = {APP_SLOT:#x}"))
    tc("pi32v2/bin/ld", "-T", ld, OUT / "crt0.o", OUT / "fm1_vec.o", OUT / "fm1_isr.o",
       *([OUT / "fm1_cpu1.o"] if CPU2 else []), OUT / "felucca.o", "-o", elf)
    for sect in ("text.bin", "data.bin", "ramtext.bin"):
        (OUT / sect).unlink(missing_ok=True)
    *_, syms, dis, rt = tc_all(("common/bin/objcopy", "-O", "binary", "-j", ".text", elf, OUT / "text.bin"),
                               ("common/bin/objcopy", "-O", "binary", "-j", ".data", elf, OUT / "data.bin"),
                               ("common/bin/objcopy", "-O", "binary", "-j", ".ram_text", elf, OUT / "ramtext.bin"),
                               ("common/bin/objdump", "-t", elf),
                               ("common/bin/objdump", "-d", elf),
                               ("common/bin/objdump", "-d", "-j", ".ram_text", elf))
    (OUT / "felucca.dis").write_text(dis)

    def symv(name):
        return int(re.search(r"^([0-9a-f]+) .*\s" + name + r"$", syms, re.M).group(1), 16)
    img = bytearray((OUT / "text.bin").read_bytes())
    # .ram_text and .data follow .text at their load addresses; crt0 copies them by words
    for sect, lname in (("ramtext.bin", "_rt_load"), ("data.bin", "_data_load")):
        load = symv(lname)
        if load % 4:
            raise SystemExit(f"{lname} {load:#x} is not word aligned")
        blob = (OUT / sect).read_bytes() if (OUT / sect).exists() else b""
        if blob:
            if load - APP_XIP < len(img):
                raise SystemExit(f"{lname} overlaps the image")
            img += b"\xff" * (load - APP_XIP - len(img))
            img += blob
    img += b"\xff" * (-len(img) % 4)
    (OUT / "felucca.bin").write_bytes(img)
    return bytes(img), syms, dis, rt


def check(img, syms, dis, rt):
    errors, notes = [], []
    m = re.search(r"^([0-9a-f]+) .*\s_start$", syms, re.M)
    if not m or int(m.group(1), 16) != APP_XIP:
        errors.append(f"_start is not at {APP_XIP:#x}")
    if img[:4] != bytes.fromhex("04818000"):
        errors.append(f"image starts with {img[:4].hex()}, not the entry stub")
    m = re.search(r"^([0-9a-f]+) .*\s_rt_hot$", syms, re.M)   # (Skiff) from _rt_hot: .ram_hot, audio code that
    hot = int(m.group(1), 16) if m else 1 << 32                  # never runs with the flash busy (app.ld): it may call
    rt_ins = [(int(mm.group(1), 16), ln) for ln in rt.splitlines() if (mm := LINE.match(ln))]
    rt_calls = [ln for a, ln in rt_ins if a < hot and re.search(r"\bcall\b", ln)]
    if rt_calls:                    # RAM code runs with the flash off: no calls into XIP
        errors.append(f".ram_text contains calls: {rt_calls[:3]}")
    else:
        notes.append(f".ram_text: {sum(a < hot for a, _ in rt_ins)} insns, no calls; "
                     f".ram_hot: {sum(a >= hot for a, _ in rt_ins)} insns")
    for ln in dis.splitlines():     # nothing may call or load an address in the chip ROM
        mm = LINE.match(ln)
        if not mm:
            continue
        for v in re.findall(r"= (-?\d+) <|call -?\d+ <[^:>]*: ([0-9a-f]+) >", mm.group(3)):
            val = (int(v[0]) & 0xFFFFFFFF) if v[0] else int(v[1], 16) & 0xFFFFFFFF
            if 0xFFC00000 <= val < 0xFFD00000:
                errors.append(f"reference to ROM address {val:#010x}")
    if len(img) > APP_SLOT:
        errors.append(f"image {len(img)} B exceeds the app slot")

    def sym(name):
        mm = re.search(r"^([0-9a-f]+) .*\s" + name + r"$", syms, re.M)
        return int(mm.group(1), 16) if mm else 0
    bss = sym("_bss_end") - 0x01C08000
    pool = sym("_pool_end") - sym("_pool_start")
    rtx = sym("_ramx_end") - sym("_rt_start")      # (Skiff) the RAM code and .ramx after it (app.ld)
    notes.append(f"image {len(img)} B; RAM .data+.bss {bss} B of 98304; pool {pool} B of {0x57000}; "
                 f"RAM code + .ramx {rtx} B of {0x6000}")
    if bss > 96 * 1024:
        errors.append("RAM region overflow")
    if 0x57000 - pool < 8192:                     # keep >= 8 KiB of the pool spare (app.ld POOL)
        errors.append(f"pool headroom {0x57000 - pool} B < 8192 B")
    return errors, notes


# Every hardware register access lives in hal/. In src/ and loader/ (comments stripped):
#  - no volatile pointer cast, except of a C object's address (`*(volatile T *)&x`: a read-once
#    of a RAM flag shared with an ISR, e.g. usb.c ota_wire_send);
#  - no literal in a register or reserved window (core SFRs 0x10000-0x13FFF, SFC 0x40000-0x43FFF,
#    GPIO/IOMAP 0x50000-0x51FFF, CPU 0x1EE0000-0x1EEFFFF, RAM top 0x01C7F000- (boot info,
#    mailbox, vectors), XIP 0x02000000-0x020FFFFF);
#  - no inline asm, except the empty compiler barrier RING_PUBLISH() (emits no instruction);
#  - no HAL register macro (a hal/ #define that is, or expands to, a volatile access).
# Linker symbols (_bss_start[], _rt_load[], ...) are plain C objects and pass.
MMIO_LIT = re.compile(r"\b0x0*(1[0-3][0-9a-f]{3}|4[0-3][0-9a-f]{3}|5[01][0-9a-f]{3}|1ee[0-9a-f]{4}|"
                      r"1c7f[0-9a-f]{3}|20[0-9a-f]{5})u?l?\b", re.I)
MMIO_CAST = re.compile(r"\(\s*(?:const\s+)?volatile\b[^()]*\*\s*\)(?!\s*&)")
MMIO_ASM = re.compile(r"\b(?:__asm__|asm)\b(?!\s+volatile\s*\(\s*\"\"\s*:::\s*\"memory\"\s*\))")


def mmio_check():
    def strip(s):
        s = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), s, flags=re.S)
        return re.sub(r"//[^\n]*", "", s)
    defs = {}
    for h in sorted((FW / "hal").glob("*.h")):
        for m in re.finditer(r"^#define\s+(\w+)(?:\([^)]*\))?\s+(.*)$", strip(h.read_text()), re.M):
            defs[m.group(1)] = m.group(2)
    regs = {n for n, b in defs.items() if re.search(r"\bvolatile\b", b)}
    while True:                                   # macros built on register macros (FM1_WR_LIMIT_H -> FM1_X2)
        more = {n for n, b in defs.items() if n not in regs and set(re.findall(r"\w+", b)) & regs}
        if not more:
            break
        regs |= more
    errors = []
    for f in sorted([*(FW / "src").glob("*.[ch]"), *(FW / "loader").glob("*.c")]):
        for no, ln in enumerate(strip(f.read_text()).splitlines(), 1):
            where = f"{f.relative_to(FW)}:{no}"
            for rx, what in ((MMIO_LIT, "register/window address"), (MMIO_CAST, "volatile pointer cast"),
                             (MMIO_ASM, "inline asm")):
                m = rx.search(ln)
                if m:
                    errors.append(f"{where}: {what} outside hal/ ({m.group(0).strip()}); add a hal/ helper")
            used = set(re.findall(r"\b[A-Z_][A-Z0-9_]*\b", ln)) & regs
            if used:
                errors.append(f"{where}: HAL register macro {sorted(used)[0]} outside hal/; use a hal/ helper")
    return errors


def main():
    global PRODUCT, VERSION, BOARD, APP_SLOT
    ap = argparse.ArgumentParser()
    ap.add_argument("--release", metavar="X.Y", help="release build: identity FM-1_7XY, version string vX.Y")
    ap.add_argument("--board", choices=sorted(BOARD_PRODUCT), default=None,
                    help="target board: fm1 (default) or smk37 (default: $BOARD, else fm1)")
    ap.add_argument("--model", choices=sorted(MODELS), default=None,
                    help="smk37: the model name, pro (default), elite or mke (default: $MODEL, else pro)")
    ap.add_argument("--sdk", type=Path, help="JieLi AC79 SDK checkout (default: $AC79_SDK)")
    a = ap.parse_args()
    BOARD = (a.board or os.environ.get("BOARD") or "fm1").lower().replace("-", "")
    if BOARD not in BOARD_PRODUCT:
        raise SystemExit(f"BOARD={BOARD}: use fm1 or smk37")
    model = (a.model or (os.environ.get("MODEL") if BOARD == "smk37" else None) or "pro").lower()
    if model not in MODELS or (model != "pro" and BOARD != "smk37"):
        raise SystemExit(f"MODEL={model}: pro, elite or mke, with BOARD=smk37")
    fm1pkg_make.set_board(BOARD)
    APP_SLOT = fm1pkg_make.APP_SLOT
    PRODUCT = BOARD_PRODUCT[BOARD]
    stem = BOARD_STEM[BOARD]
    name = "felucca.fwsc" if BOARD == "fm1" else f"{stem}.fwsc"
    if model != "pro":              # another model name: identity, loader and update record (fm1_cc.h SMK_MODEL)
        BOARD_DEFS[BOARD] = [*BOARD_DEFS[BOARD], f'-DSMK_MODEL="{MODELS[model]}"']
        PRODUCT = PRODUCT.replace(MODELS["pro"], MODELS[model])
        BOARD_PREFIX[BOARD] = BOARD_PREFIX[BOARD].replace(MODELS["pro"], MODELS[model])
        stem += "-" + model
        name = f"{stem}.fwsc"
    if a.release:                   # one digit each: the identity has room for two (X.Y.Z and X.Y.Z.W keep X.Y's)
        m = re.fullmatch(r"(\d)\.(\d)(?:\.\d){0,2}(-[A-Za-z0-9]+)?", a.release)
        if not m:
            raise SystemExit(f"--release {a.release}: use X.Y, X.Y.Z, X.Y.Z.W or X.Y-suffix, one digit each")
        PRODUCT = BOARD_PREFIX[BOARD] + m[1] + m[2]
        VERSION = "v" + a.release.lower()      # e.g. v1.0, v1.1-rc1
        name = f"{stem}-{a.release}.fwsc"
    fm1pkg_make.SDK = a.sdk
    for rel, sha in SDK_SHA256.items():          # fail early without the SDK
        if hashlib.sha256(fm1pkg_make.sdk_file(rel)).hexdigest() != sha:
            print(f"warning: SDK {rel} differs from AC79NN_SDK_V1.2.1; the package will not match the reference")
    OUT.mkdir(parents=True, exist_ok=True)
    with ThreadPoolExecutor(2) as ex:
        gen, ldr = ex.submit(generate), ex.submit(build_loader)
        gen.result()
        ota = ldr.result()
    if BOARD == "smk37":
        write_smk_ldr(ota)
    img, syms, dis, rt = build_app()
    errors, notes = check(img, syms, dis, rt)
    hal_err = mmio_check()
    errors += hal_err
    if not hal_err:
        notes.append("register access: hal/ only (src/, loader/ clean)")
    for n in notes:
        print("  ok   ", n)
    for e in errors:
        print("  FAIL ", e)
    if errors:
        raise SystemExit("build: checks failed")
    pkg = fm1pkg_make.ufw(fm1pkg_make.flash_image(img, fm1pkg_make.KEY), ota, PRODUCT)
    (OUT / name).write_bytes(pkg)
    att = SRC / "assets" / "samples-cc0" / "ATTRIBUTION.txt"
    if att.exists():
        shutil.copy(att, OUT / "ATTRIBUTION.txt")
    print(f"app      {OUT / 'felucca.bin'}  {len(img)} B")
    print(f"loader   {LDR / 'ota.bin'}  {len(ota)} B")
    print(f"package  {OUT / name}  {len(pkg)} B, identity {PRODUCT}" + (f", board {BOARD}" if BOARD != "fm1" else ""))
    if a.release:                   # what a release carries: the package, the app and every licence they need
        rel = OUT / f"release-{a.release}"
        shutil.rmtree(rel, ignore_errors=True)
        (rel / "LICENSES").mkdir(parents=True)
        app = f"{stem}-{a.release}-app.bin"
        (rel / name).write_bytes(pkg)
        (rel / app).write_bytes(img)
        (rel / "SHA256SUMS").write_text("".join(f"{hashlib.sha256(b).hexdigest()}  {n}\n"
                                                for n, b in ((name, pkg), (app, bytes(img)))))
        for f in sorted((SRC / "LICENSES").glob("*.txt")):
            shutil.copy(f, rel / "LICENSES" / f.name)
        for doc in ("LICENSE", "LICENSING.md"):
            shutil.copy(SRC / doc, rel / doc)
        if att.exists():
            shutil.copy(att, rel / "ATTRIBUTION.txt")
        shutil.copy(SRC / "assets" / "breaks" / "ATTRIBUTION.txt", rel / "ATTRIBUTION-BREAKS.txt")   # (Skiff phase 9)
        print(f"release  {rel}/")
    return 0


if __name__ == "__main__":
    sys.exit(main())
