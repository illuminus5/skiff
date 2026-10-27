#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""tools/fm1_install.py against a simulated FM-1 (no hardware, no mido).
The fake device is the one in web/test_web.mjs: identity on the handshake,
then "device asks, host answers" reads of the logical image. Run from the repo root:
  python3 tests/install_test.py
Also checks logical_image/product_of against web/fm1pkg.js (needs node and
build/felucca.fwsc, skipped otherwise), and (BOARD_SMK37) the SMK-37 Pro identities and port names and the
20 / 36 block deinterleave (the stock SMK-37 Pro V016 from a directory named by SMK_STOCK_DIR, skipped when unset)."""
import io
import os
import queue
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import fm1_install as I  # noqa: E402

I.DELAY.update(open=0, start=0.01, reply=0, loader=0.01, reboot=0.01, retry=0.02, poll=0.05, hs=0.1,
               idle_check=0.4, idle_write=0.4, wait_loader=2, wait_reboot=2, wait_resume=0.5)
failed = 0


def ok(cond, what):
    global failed
    print(f"{what:<64} {'ok' if cond else 'FAIL'}")
    failed += not cond


# ---------------------------------------------------------- simulated FM-1 ---

class FakeLink:
    def __init__(self, dev, gen):
        self.dev, self.gen, self.q, self.closed = dev, gen, queue.Queue(), False

    @property
    def lost(self):
        return self.gen != self.dev.gen or not self.dev.connected

    def send(self, pkt):
        if self.lost:
            return False
        self.dev.sent.append(bytes(pkt))
        self.dev.rx(bytes(pkt))
        return True

    def read(self, timeout):
        try:
            return self.q.get(timeout=max(timeout, 0))
        except queue.Empty:
            return None

    def drain(self):
        while not self.q.empty():
            self.q.get_nowait()

    def close(self):
        self.closed = True


class FakeFM1:
    """a MIDI backend with one FM-1 on it"""

    def __init__(self, image, identity="FM-1_015", name="FM-1", unplug_after=None, after_write="FM-1_900",
                 stall_after=None, bad_addr=None, loader=("ota-FM-1_900", "Felucca Update"), write_reads=None,
                 record=None, clocks=None):
        self.image, self.unplug_after, self.after_write = image, unplug_after, after_write
        self.loader, self.write_reads = loader, write_reads
        self.record, self.clocks = record, clocks          # (an SMK loader's answers to 0x7B / 0x7C)
        self.stall_after, self.bad_addr = stall_after, bad_addr
        self.served = self.bad = self.upgrades = 0
        self.sent, self.links, self.gen, self.lock = [], [], 0, threading.Lock()
        self.boot(identity, name)

    def boot(self, identity, name):
        with self.lock:
            self.gen += 1
            self.identity, self.name, self.connected = identity, name, True
            self.waiting, self.queue = None, []

    def input_names(self):
        return [self.name] if self.connected else []

    output_names = input_names

    def open(self, in_name, out_name):
        if in_name != self.name or not self.connected:
            raise IOError("no such port")
        link = FakeLink(self, self.gen)
        self.links.append(link)
        return link

    def tx(self, pkt):
        for link in self.links:
            if link.gen == self.gen and not link.closed:
                link.q.put(bytes(pkt))

    def rx(self, d):
        if d == I.HS_QUERY:
            t = self.identity.encode()
            self.tx(b"\xF0" + I.pack7(bytes([0, 0x59, 0x11, 0, 0, 0]) + t + bytes(28 - len(t))) + b"\xF7")
        elif self.identity.startswith("ota-") and (self.record, self.clocks) != (None, None) and \
                d in (I.query(I.LDR_RECORD), I.query(I.LDR_CLOCKS)):
            cmd = I.LDR_RECORD if d == I.query(I.LDR_RECORD) else I.LDR_CLOCKS
            body = self.record if cmd == I.LDR_RECORD else self.clocks
            if body is not None:
                m = bytes([0, 0x59, cmd, len(body), 0, 0]) + body + bytes([~sum(body) & 0xFF])
                self.tx(b"\xF0" + I.pack7(m) + b"\xF7")
        elif d == I.UPGRADE:
            self.upgrades += 1
            first = self.bad_addr if self.bad_addr is not None else 0
            reads = list(self.write_reads) if self.write_reads else [(first + k * 512, 512) for k in range(6)]
            self.queue = (reads + [(I.FINISH_WRITE, 8)] if self.identity.startswith("ota-")
                          else [(0, 64), (0x40, 160), (0x1000, 512), (I.FINISH_CHECK, 8)])
            self.next()
        elif self.waiting:
            u = I.unpack7(d[1:-1])
            addr, n = self.waiting
            fin = addr >= I.FINISH_CHECK
            want = b"success\0" if fin else self.image[addr:addr + n]
            if u[14:14 + len(want)] != want or len(u) != 15 + len(want):
                self.bad += 1
            self.waiting = None
            self.served += 1
            if self.unplug_after and self.served >= self.unplug_after:
                self.connected = False
                return
            if self.stall_after and self.served >= self.stall_after:
                return
            if addr == I.FINISH_CHECK:
                threading.Timer(0.05, self.boot, self.loader).start()
            elif addr == I.FINISH_WRITE:
                threading.Timer(0.05, self.boot, (self.after_write, "Felucca")).start()
            else:
                self.next()

    def next(self):
        if not self.queue:
            return
        self.waiting = addr, n = self.queue.pop(0)
        u = bytearray([0, 0x59, 0x30, 0, 0, 0, 0]) + addr.to_bytes(4, "little") + bytes([n & 0xFF, n >> 8, 0])
        u.append(~sum(u[6:14]) & 0xFF)
        self.tx(b"\xF0" + I.pack7(u) + b"\xF7")


# ----------------------------------------------------------------- helpers ---

def package(product="FM-1_900", marker=True, size=0x2000):
    raw = bytearray((i * 7) & 0xFF for i in range(size + I.BLOCKS))
    for i in range(I.BLOCKS):
        raw[i * I.BLK + I.KEEP] = (ord(product[i]) + i + 1) & 0xFF if i < len(product) else 0x7D
    raw[0x1800:0x1800 + 16] = I.LOADER_MARK if marker else bytes(16)
    return bytes(raw)


def package_crc(product="FM-1_900", size=0x3000, blocks=20, seed=7):
    """a package with a valid header and file list (one file, flash.bin: 0x400 to the end), as fm1pkg_make writes,
    in `blocks` interleave blocks (the stock SMK-37 Pro V016: 36)"""
    img = bytearray((i * seed) & 0xFF for i in range(size))
    img[0x1800:0x1800 + 16] = I.LOADER_MARK
    data = bytes(img[0x400:])
    e = bytearray(0x50)
    e[0:16] = bytes(4) + I._crc16(data).to_bytes(2, "little") + bytes(2) + \
        (0x400).to_bytes(4, "little") + len(data).to_bytes(4, "little")
    e[0x40:0x49] = b"flash.bin"
    lst = I._dec(bytes(e))
    hdr = bytearray(0x40)
    hdr[4:8] = size.to_bytes(4, "little")
    hdr[8:10] = (1).to_bytes(2, "little")
    hdr[2:4] = I._crc16(lst).to_bytes(2, "little")
    hdr[0:2] = I._crc16(bytes(hdr[2:0x40])).to_bytes(2, "little")
    img[0:0x40] = I._dec(bytes(hdr))
    img[0x40:0x90] = lst
    raw = bytearray()
    for i in range(blocks):
        m = (ord(product[i]) + i + 1) & 0xFF if i < len(product) else 0x7D
        raw += img[i * I.KEEP:(i + 1) * I.KEEP] + bytes([m])
    raw += img[blocks * I.KEEP:]
    return bytes(raw)


TMP = Path(tempfile.mkdtemp(prefix="felucca-install-"))


def pkgfile(name, raw):
    p = TMP / name
    p.write_bytes(raw)
    return str(p)


def cli(args, dev, answer=True):
    out, err = io.StringIO(), io.StringIO()
    old, sys.stderr = sys.stderr, err
    try:
        rc = I.main(args, backend=dev, out=out, ask=lambda _p: answer)
    finally:
        sys.stderr = old
    return rc, out.getvalue(), err.getvalue()


# ------------------------------------------------------------------- tests ---

def wire():
    data = bytes(range(256)) * 3
    ok(I.unpack7(I.pack7(data))[:len(data)] == data, "pack7 / unpack7 round trip")
    pkt = I.response(0x12345, b"\x01\x02\x03", fl=5)
    u = I.unpack7(pkt[1:-1])
    ok(u[:3] == b"\x00\x59\x30" and int.from_bytes(u[7:11], "little") == 0x12345 and u[14:17] == b"\x01\x02\x03"
       and ~sum(u[6:17]) & 0xFF == u[17], "response: header, address, data, checksum")
    raw = package("FM-1_906")
    ok(I.product_of(raw) == "FM-1_906" and len(I.logical_image(raw)) == len(raw) - 20, "product_of / logical_image (synthetic)")


def installs():
    raw = package()
    image = I.logical_image(raw)
    p = pkgfile("ok.fwsc", raw)

    dev = FakeFM1(image)
    rc, out, err = cli([p, "--yes"], dev)
    ok(rc == 0 and dev.bad == 0 and dev.upgrades == 2 and dev.served == 11 and "done: the device runs FM-1_900" in out
       and "100%" in out, f"install: running -> loader -> Felucca ({dev.served} reads)")

    dev = FakeFM1(image, identity="ota-FM-1_900", name="Felucca Update")
    rc, out, err = cli([p, "--yes"], dev)
    ok(rc == 0 and dev.bad == 0 and dev.upgrades == 1 and "update mode" in out, "install: device already in update mode -> finishes the write")

    dev = FakeFM1(image)
    rc, out, err = cli([p], dev, answer=False)
    ok(rc == 1 and dev.upgrades == 0 and "cancelled" in out, "install: answer no -> nothing sent but the handshake")

    dev = FakeFM1(image, identity="FM-1_905", name="Felucca")
    rc, out, err = cli(["--info"], dev)
    ok(rc == 0 and "FM-1_905" in out and "running" in out and dev.upgrades == 0, "--info: identity of the connected FM-1")

    dev = FakeFM1(image, identity="ota-FM-1_900", name="Felucca Update")
    rc, out, err = cli(["--info"], dev)
    ok(rc == 0 and "update loader" in out, "--info: device in update mode")

    dev = FakeFM1(image, identity="\x1b[2J\x1b]0;x\x07FM-1_905", name="Felucca")
    rc, out, err = cli(["--info"], dev)
    ok(rc == 0 and "FM-1_905" in out and not any(c in out for c in "\x1b\x07"),
       "--info: control bytes in the device's identity are not printed")
    import fm1_sample_upload as U
    lines = U.slot_info([3, 80, 2, 0x41, 0x1B, 0x5B, 0x32, 0x4A, 0, 40, 0, 0, 0, 0, 0, 0])
    ok(lines[0] == "USR1: 2 zones, A?[2J, 40 KiB" and lines[1] == "USR2: empty", "sample upload: slot names printable only")
    try:
        U.slot_info([3, 80, 2, 0x41])
        ok(False, "sample upload: a malformed SMP_INFO reply is an error")
    except ValueError:
        ok(True, "sample upload: a malformed SMP_INFO reply is an error")


def errors():
    raw = package()
    image = I.logical_image(raw)
    p = pkgfile("ok.fwsc", raw)

    rc, out, err = cli([p, "--yes"], FakeFM1(image, name="IAC Driver Bus 1"))
    ok(rc == 3 and "not found" in err and "IAC Driver" in err, "no FM-1 port -> exit 3, lists the MIDI inputs")
    dev = FakeFM1(image)
    rc, out, err = cli([p, "--yes", "--port", "Felucca"], dev)
    ok(rc == 3 and dev.upgrades == 0, "--port that matches nothing -> exit 3")
    dev = FakeFM1(image, name="My Interface")
    rc, out, err = cli(["--info", "--port", "my int"], dev)
    ok(rc == 0 and "FM-1_015" in out, "--port picks a port the default match skips")

    t0 = time.monotonic()
    rc, out, err = cli([p, "--yes"], FakeFM1(image, unplug_after=2))
    ok(rc == 4 and "disconnected" in err and "nothing was written" in err and time.monotonic() - t0 < 3,
       "unplugged in step 1 -> exit 4 'lost', nothing written")
    t0 = time.monotonic()
    rc, out, err = cli([p, "--yes"], FakeFM1(image, identity="ota-FM-1_900", name="Felucca Update", unplug_after=3))
    ok(rc == 4 and "run the install again" in err and time.monotonic() - t0 < 3, "unplugged during the write -> exit 4 at once")
    rc, out, err = cli([p, "--yes"], FakeFM1(image, identity="ota-FM-1_900", name="Felucca Update", stall_after=3))
    ok(rc == 4 and "stopped answering" in err, "loader stops answering -> exit 4 after the idle time")
    rc, out, err = cli([p, "--yes"], FakeFM1(image, identity="ota-FM-1_900", name="Felucca Update", bad_addr=len(image)))
    ok(rc == 4 and "outside the package" in err, "read past the package -> exit 4")

    rc, out, err = cli([p, "--yes"], FakeFM1(image, after_write="FM-1_015"))
    ok(rc == 6 and "reports FM-1_015" in err, "another identity after the restart -> exit 6")
    dev = FakeFM1(image, identity="XY-9_001", name="usb-midi")
    rc, out, err = cli([p, "--yes"], dev)
    ok(rc == 6 and dev.upgrades == 0, "another model -> exit 6, nothing sent")
    dev = FakeFM1(image, identity="ota-SMK-37 Pro_900", name="Felucca Update")
    rc, out, err = cli([p, "--yes"], dev)
    ok(rc == 6 and dev.upgrades == 0, "FM-1 package on an SMK in update mode -> exit 6 (wrong model), nothing sent")

    class Stuck(FakeFM1):          # the loader never shows up
        def boot(self, identity, name):
            super().boot(identity, name)
            if identity.startswith("ota-"):
                self.connected = False
    rc, out, err = cli([p, "--yes"], Stuck(image))
    ok(rc == 5 and "loader did not appear" in err, "no loader after step 1 -> exit 5")

    plain = pkgfile("plain.fwsc", package(marker=False))
    dev = FakeFM1(image)
    rc, out, err = cli([plain, "--yes"], dev)
    ok(rc == 2 and "no Felucca update loader" in err and dev.sent == [], "package without the loader marker -> exit 2, no MIDI")
    dev = FakeFM1(I.logical_image(package(marker=False)))
    rc, out, err = cli([plain, "--yes", "--force"], dev)
    ok(rc == 0 and dev.bad == 0, "... installs with --force")
    rc, out, err = cli([pkgfile("short.fwsc", b"\0" * 100), "--yes"], FakeFM1(image))
    ok(rc == 2 and "too short" in err, "not a package -> exit 2")
    rc, out, err = cli([str(TMP / "missing.fwsc"), "--yes"], FakeFM1(image))
    ok(rc == 2, "missing file -> exit 2")


def against_js():
    clean = ROOT / "build/felucca.fwsc"
    if not shutil.which("node") or not clean.exists():
        print("logical image vs fm1pkg.js: skipped (needs node and build/felucca.fwsc)")
        return
    js = ("import { logicalImage, productOf } from %r; import { readFileSync } from 'node:fs';"
          "const p = readFileSync(process.argv[1]); process.stderr.write(productOf(p));"
          "process.stdout.write(logicalImage(p));") % str(ROOT / "web/fm1pkg.js")
    r = subprocess.run(["node", "--input-type=module", "-e", js, str(clean)], capture_output=True, check=True)
    raw = clean.read_bytes()
    ok(I.logical_image(raw) == r.stdout and I.product_of(raw) == r.stderr.decode(),
       f"logical_image / product_of == fm1pkg.js ({clean.name}, {I.product_of(raw)})")
    ok(I.LOADER_MARK in raw, f"{clean.name} carries the Felucca loader marker")


def official():
    """#32: back to the official V15 from Felucca, without --force (only the unmodified file); (2.0.2, #64) V16 too.
    The files: FM-1_v15.fwsc in the repo root, or FM-1.fwsc (V15) and FM-1_016.fwsc in FM1_STOCK_DIR (never in the repo)"""
    stock = Path(os.environ.get("FM1_STOCK_DIR") or "/nonexistent")
    v15 = ROOT / "FM-1_v15.fwsc" if (ROOT / "FM-1_v15.fwsc").exists() else stock / "FM-1.fwsc"
    for f, product in ((v15, "FM-1_015"), (stock / "FM-1_016.fwsc", "FM-1_016")):
        if not f.exists():
            print(f"official {product} restore: skipped (needs FM-1_v15.fwsc in the repo root, or FM1_STOCK_DIR)")
            continue
        raw = f.read_bytes()
        dev = FakeFM1(I.logical_image(raw), identity="FM-1_900", name="Felucca", after_write=product)
        rc, out, err = cli([str(f), "--yes"], dev)
        ok(rc == 0 and dev.bad == 0 and product in out and dev.identity == product,
           f"official {product} ({f.name}) from Felucca: installed without --force, back as {product}")
        bad = bytearray(raw)
        bad[-1] ^= 1
        dev = FakeFM1(I.logical_image(bytes(bad)), identity="FM-1_900", name="Felucca")
        rc, out, err = cli([pkgfile("stockmod.fwsc", bytes(bad)), "--yes"], dev)
        ok(rc == 2 and "official V15 or V16" in err and dev.sent == [], f"a modified {product} is still refused (no MIDI)")


def smk():
    """SMK-37 Pro: identities, port names, the 36-block stock package, the SMK install path"""
    ok(I.PORT_RE.search("SMK-37 Pro Midi") and I.PORT_RE.search("FM-1") and not I.PORT_RE.search("IAC Driver Bus 1"),
       "port names: SMK-37 Pro ports probed, FM-1 too, other gear not")
    ident = I.parse_identity(b"\xF0" + I.pack7(bytes([0, 0x59, 0x11, 0, 0, 0]) + b"SMK-37 Pro_016" + bytes(14)) + b"\xF7")
    ok(ident and ident.model == "SMK-37 Pro" and ident.version == 16 and I.model_of("ota-SMK-37 Pro_900") == "SMK-37 Pro",
       "identity SMK-37 Pro_016 parses (model SMK-37 Pro, the space kept)")
    raw36 = package_crc("SMK-37 Pro_900", blocks=36)
    image36 = I.deinterleave(raw36)
    ok(I.block_count(raw36) == 36 and len(image36) == len(raw36) - 36 and I.files_ok(image36)
       and I.product_of(raw36) == "SMK-37 Pro_900", "36 blocks (synthetic): detected, logical image valid")
    ok(not I.files_ok(I.logical_image(raw36)), "... a fixed 20-block deinterleave of it fails the CRCs")
    for seed in range(1, 256):                  # a 20-block package with 0x7D at the 21st marker by chance
        raw = package_crc("FM-1_900", seed=seed)
        if raw[20 * I.BLK + I.KEEP] == 0x7D:
            break
    image = I.deinterleave(raw)
    ok(I.block_count(raw) > 20 and image == I.logical_image(raw) and I.files_ok(image),
       f"20 blocks with a 0x7D data byte at the 21st marker (seed {seed}): falls back to 20")
    raw = package("FM-1_900")
    ok(I.deinterleave(raw) == I.logical_image(raw), "a plain 20-block package: deinterleave == logical_image")

    p = pkgfile("smk.fwsc", package("SMK-37 Pro_900"))
    image = I.logical_image(package("SMK-37 Pro_900"))
    dev = FakeFM1(image, identity="SMK-37 Pro_016", name="SMK-37 Pro Midi", after_write="SMK-37 Pro_900",
                  loader=("ota-SMK-37 Pro_900", "Felucca Update"))
    rc, out, err = cli([p, "--yes"], dev)
    ok(rc == 0 and dev.bad == 0 and dev.upgrades == 2 and "SMK-37 Pro_900" in out,
       "install on an SMK-37 Pro: running -> loader -> SMK-37 Pro_900")
    dev = FakeFM1(image, identity="ota-SMK-37 Pro_900", name="Felucca Update", after_write="SMK-37 Pro_900")
    rc, out, err = cli([p, "--yes"], dev)
    ok(rc == 0 and dev.upgrades == 1, "SMK-37 Pro in Felucca's update mode -> finishes the write")
    dev = FakeFM1(image, identity="SMK-37 Pro_016", name="SMK-37 Pro Midi")
    rc, out, err = cli([pkgfile("fm1.fwsc", package("FM-1_900")), "--yes"], dev)
    ok(rc == 6 and dev.upgrades == 0, "FM-1 package on an SMK-37 Pro -> exit 6 (wrong model), nothing sent")
    dev = FakeFM1(image, identity="FM-1_015", name="FM-1")
    rc, out, err = cli([p, "--yes"], dev)
    ok(rc == 6 and dev.upgrades == 0, "SMK-37 Pro package on an FM-1 -> exit 6, nothing sent")

    # a stock SMK older than V016 is updated with M-VAVE's tool (or the official V016 file) first
    ok(I.needs_stock_update("SMK-37 Pro_012") and I.needs_stock_update("SMK-37 Elite_015") and I.needs_stock_update("MKE-P37_013")
       and not I.needs_stock_update("SMK-37 Pro_016") and not I.needs_stock_update("SMK-37 Pro_700")
       and not I.needs_stock_update("SMK-37 Pro_000") and not I.needs_stock_update("FM-1_015"),
       "stock check: SMK-37 Pro / Elite / MKE-P37 below V016 only")
    dev = FakeFM1(image, identity="SMK-37 Pro_012", name="SMK-37 Pro Midi")
    rc, out, err = cli([p, "--yes"], dev)
    ok(rc == 7 and dev.upgrades == 0 and "V016" in err, "Skiff on a stock SMK-37 Pro older than V016 -> exit 7, nothing sent")
    dev = FakeFM1(image, identity="SMK-37 Pro_012", name="SMK-37 Pro Midi", after_write="SMK-37 Pro_900",
                  loader=("ota-SMK-37 Pro_900", "Felucca Update"))
    rc, out, err = cli([p, "--yes", "--force"], dev)
    ok(rc == 0 and dev.upgrades == 2, "... --force skips the check")
    v012 = FakeFM1(image, identity="SMK-37 Pro_012", name="SMK-37 Pro Midi", after_write="SMK-37 Pro_016",
                   loader=("ota-SMK-37 Pro_016", "Felucca Update"))
    rc, out, err = cli([pkgfile("v016ish.fwsc", package("SMK-37 Pro_016")), "--yes"], v012)
    ok(rc == 0 and v012.upgrades == 2, "... and a stock package (the V016 update itself) is not refused")

    # --info on an SMK loader: its start record (why it started, how far the run before got) and its clocks
    ok(I.query(0x11) == I.HS_QUERY, "query(0x11) is the handshake")
    rec = bytearray(32)
    rec[0], rec[1], rec[2:4], rec[4:8] = 0x04, 0x0D, (3).to_bytes(2, "little"), (0x20).to_bytes(4, "little")
    rec[8], rec[9] = 1, 0                                   # this start: waiting, cold
    rec[20], rec[21], rec[22:24] = 3, 0, (61).to_bytes(2, "little")   # the run before: writing, cold, 61 ms
    rec[24:28], rec[28:32] = (0x5A000).to_bytes(4, "little"), (38200).to_bytes(4, "little")
    dev = FakeFM1(image, identity="ota-SMK-37 Pro_700", name="Felucca Update", record=bytes(rec), clocks=bytes(range(32)))
    rc, out, err = cli(["--info"], dev)
    ok(rc == 0 and "start 3: reset by watchdog" in out and "the run before: writing sector 0x5a000 at 38.2 s" in out
       and "cold (started by the boot stage)" in out and "clocks: 10008=03020100" in out and dev.upgrades == 0,
       "--info: an SMK loader's start record and clocks, in words")
    dev = FakeFM1(image, identity="ota-FM-1_900", name="Felucca Update")
    rc, out, err = cli(["--info"], dev)
    ok(rc == 0 and "update loader" in out and "start record" not in out and "reset by" not in out,
       "--info: a loader without the query prints its identity only")

    # --uboot (the flash backup's way in): step 1 only, then the UBOOT key to the loader, never a write
    dev = FakeFM1(image, identity="SMK-37 Pro_016", name="SMK-37 Pro Midi", loader=("ota-SMK-37 Pro_900", "Felucca Update"))
    rc, out, err = cli(["--uboot", p, "--yes"], dev)
    ok(rc == 0 and dev.upgrades == 1 and dev.bad == 0 and dev.identity == "ota-SMK-37 Pro_900"
       and dev.sent[-1] == I.UBOOT and dev.sent.count(I.UBOOT) == 1 and "WL82 UBOOT1.00" in out,
       "--uboot on a stock SMK-37 Pro: the check step, the loader up, the UBOOT key sent to it, no write")
    dev = FakeFM1(image, identity="ota-SMK-37 Pro_900", name="Felucca Update")
    rc, out, err = cli(["--uboot", p, "--yes"], dev)
    ok(rc == 0 and dev.upgrades == 0 and dev.sent[-1] == I.UBOOT, "--uboot in update mode: only the UBOOT key")
    dev = FakeFM1(image, identity="FM-1_015", name="FM-1")
    rc, out, err = cli(["--uboot", p, "--yes"], dev)
    ok(rc == 6 and dev.upgrades == 0 and I.UBOOT not in dev.sent, "--uboot with another model's package -> exit 6, nothing sent")
    dev = FakeFM1(image, identity="SMK-37 Pro_016", name="SMK-37 Pro Midi")
    rc, out, err = cli(["--uboot", p], dev, answer=False)
    ok(rc == 1 and dev.upgrades == 0 and I.UBOOT not in dev.sent, "--uboot answered no -> nothing sent")

    import os                                     # the stock package (outside this repo; read only)
    v016 = Path(os.environ.get("SMK_STOCK_DIR") or ROOT / "no-stock-packages") / "SMK-37_Pro_016.fwsc"
    if not v016.exists():
        print("stock SMK-37 Pro V016: skipped (SMK_STOCK_DIR not set)")
        return
    raw = v016.read_bytes()
    image = I.deinterleave(raw)
    ok(I.block_count(raw) == 36 and I.product_of(raw) == "SMK-37 Pro_016" and I.files_ok(image),
       "stock SMK-37_Pro_016.fwsc: 36 blocks, identity SMK-37 Pro_016, CRCs ok")
    product, image2 = I.load_package(str(v016), False)
    ok(product == "SMK-37 Pro_016" and image2 == image, "... accepted for the return to stock (pinned SHA-256)")


def hardening():
    """steady progress for both loaders' read orders, the resume after a drop, a vanished device's ports"""
    import re
    raw = package_crc("FM-1_900", size=0x20000)
    image = I.deinterleave(raw)
    p = pkgfile("big.fwsc", raw)
    lo, size = I.flash_span(image)
    ours = ([(lo + 0x4000, 32)] + [(lo + k, 512) for k in range(0, size, 512)]
            + [(lo + k, 512) for k in range(0x5000, size, 512)] + [(lo + k, 512) for k in range(0x4000, 0x5000, 512)])
    mvave = [(0, 64)] + [(lo + k, 512) for k in range(size - 512, -1, -512)]
    for name, reads in (("Felucca/Skiff loader: check pass, write pass, header last", ours),
                        ("M-VAVE's loader: one pass, top down", mvave)):
        dev = FakeFM1(image, identity="ota-FM-1_900", name="Felucca Update", write_reads=reads)
        rc, out, err = cli([p, "--yes"], dev)
        v = [int(x) for x in re.findall(r"writing\s+(\d+)%", out)]
        ok(rc == 0 and dev.bad == 0 and v and v[-1] == 100 and all(b >= a for a, b in zip(v, v[1:]))
           and max(v[:-1]) >= 90, f"progress forward only, 0..100: {name}")

    class Flaky(FakeFM1):                  # drops out at these request counts, back in update mode 0.1 s later
        def __init__(self, image, drops):
            super().__init__(image, identity="ota-FM-1_900", name="Felucca Update")
            self.drops = set(drops)

        def rx(self, d):
            if self.waiting and self.served + 1 in self.drops:
                self.drops.discard(self.served + 1)
                self.served += 1
                self.connected = False
                threading.Timer(0.1, self.boot, ("ota-FM-1_900", "Felucca Update")).start()
                return
            super().rx(d)

    small = package()
    dev = Flaky(I.logical_image(small), drops=[2, 5])
    rc, out, err = cli([pkgfile("ok2.fwsc", small), "--yes"], dev)
    ok(rc == 0 and dev.bad == 0 and out.count("dropped out") == 2 and "resuming (try 3 of 5)" in out
       and "done: the device runs FM-1_900" in out, "dropped twice during the write -> waits, resumes, finishes")
    dev = Flaky(I.logical_image(small), drops=range(1, 40))
    rc, out, err = cli([pkgfile("ok3.fwsc", small), "--yes"], dev)
    ok(rc == 4 and out.count("dropped out") == I.RESUME_MAX - 1 and "run the install again" in err,
       f"dropping on every try -> gives up after {I.RESUME_MAX} tries, exit 4")

    class Stub:
        def __init__(self, names):
            self.names, self.closed = names, 0

        def input_names(self):
            return self.names

        def close(self):
            self.closed += 1
    for names, closes, kept in (([], 0, 2), (["SMK-37 Pro Midi 0"], 1, 0)):
        link = I.MidoLink.__new__(I.MidoLink)
        link.backend, link.name, link.inp, link.out = Stub(names), "SMK-37 Pro Midi 0", Stub([]), Stub([])
        before = len(I._ABANDONED)
        link.close()
        ok(link.inp.closed == closes and link.out.closed == closes and len(I._ABANDONED) == before + kept,
           "MIDI ports: " + ("a vanished device's are left open (rtmidi crashes closing them)" if kept
                             else "a present device's are closed"))
    I._ABANDONED.clear()


def two_units():
    """an FM-1 and an SMK-37 Pro on one computer, the SMK's port listed first: an FM-1 package goes to the FM-1, its
    loader and its return only, whether the SMK runs or waits in update mode"""
    class Both:
        def __init__(self, *units):
            self.units = units

        def input_names(self):
            return [n for u in self.units for n in u.input_names()]

        output_names = input_names

        def open(self, in_name, out_name):
            for u in self.units:
                if in_name in u.input_names():
                    return u.open(in_name, out_name)
            raise IOError("no such port")
    raw = package()
    image = I.logical_image(raw)
    p = pkgfile("two.fwsc", raw)
    for smk_id, smk_port, what in (("ota-SMK-37 Pro_900", "Felucca Update 1", "in update mode"),
                                   ("SMK-37 Pro_016", "SMK-37 Pro Midi", "running")):
        smk = FakeFM1(image, identity=smk_id, name=smk_port, loader=("ota-SMK-37 Pro_900", "Felucca Update 1"))
        fm1 = FakeFM1(image)
        rc, out, err = cli([p, "--yes"], Both(smk, fm1))
        ok(rc == 0 and fm1.upgrades == 2 and fm1.bad == 0 and smk.upgrades == 0 and "FM-1_900" in out,
           f"two units, the other one {what} and listed first: the package's own unit, loader and return")


wire()
installs()
errors()
smk()
hardening()
two_units()
# a package installs only on exactly its own model: each SMK model's build on that model, never FM-1 <-> SMK
ok(I.same_model("SMK-37 Pro_016", "SMK-37 Pro_900") and I.same_model("ota-SMK-37 Elite_900", "SMK-37 Elite_900")
   and I.same_model("MKE-P37_016", "MKE-P37_900") and I.same_model("FM-1_015", "FM-1_900")
   and not I.same_model("SMK-37 Elite_016", "SMK-37 Pro_900") and not I.same_model("ota-MKE-P37_900", "SMK-37 Elite_900")
   and not I.same_model("SMK-37 Pro_016", "MKE-P37_900") and not I.same_model("FM-1_015", "SMK-37 Pro_900")
   and not I.same_model("ota-SMK-37 Pro_900", "FM-1_900") and not I.same_model("XY-9_001", "SMK-37 Pro_900"),
   "models: a package only for exactly its own model (Pro, Elite, MKE-P37, FM-1)")
ok(all(I.PORT_RE.search(n) for n in ("SMK-37 Elite", "MKE-P37", "STARRYKEY-37 PLAY", "Felucca", "Felucca Update")),
   "port names: every SMK model and Felucca probed")
against_js()
official()
shutil.rmtree(TMP, ignore_errors=True)
print(f"INSTALL TESTS FAILED ({failed})" if failed else "install tests passed")
sys.exit(1 if failed else 0)
