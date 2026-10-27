#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Install a Felucca package (.fwsc) on an FM-1 (or an SMK-37) over USB-MIDI.

The same update as the web installer (web/fm1ota.js): step 1, the
running firmware reads parts of the package and starts the update loader;
step 2, the loader reads the whole package and writes it. In both steps the
device asks (SysEx read requests on the logical image) and we answer. Then
the FM-1 restarts and the installed identity is checked.

  fm1_install.py PACKAGE.fwsc [--port NAME] [--yes] [--force]
  fm1_install.py FM-1.fwsc            (the official V15 or V16 file: back to the stock firmware)
  fm1_install.py SMK-37_Pro_016.fwsc (the official SMK-37 Pro V016 file: back to the stock firmware)
  fm1_install.py --info [--port NAME]
  fm1_install.py --uboot PACKAGE.fwsc (the flash backup's way in: step 1 only, nothing written, then the loader is
                                      sent the UBOOT key: the device comes back as the USB disk "WL82 UBOOT1.00")

If the FM-1 is still in update mode (an earlier install was cut off), the
install finishes the write. If the device drops out during the write (cable,
power), the install waits for its update loader to come back and resumes.
Needs mido with python-rtmidi.

Exit codes: 0 done, 1 cancelled or other error, 2 bad arguments or package,
3 FM-1 not found, 4 connection lost or the device stopped, 5 timeout (no
loader / no restart), 6 wrong model, or another identity after the install,
7 a stock SMK-37 older than V016 (update it to V016 first; --force skips the check).

Packages: 20 interleave blocks (FM-1, Felucca, the SMK build) or more (the stock SMK-37 Pro v016 has 36: the extra
markers are 0x7D); the count is detected and checked with the files' CRCs.
"""
import argparse
import hashlib
import os
import queue
import re
import sys
import time

HS_QUERY = bytes([0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7])
UPGRADE = bytes([0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7])
UBOOT = bytes([0xF0, 0x22, 0x24, 0x35, 0x7D, 0xF7])        # the loader's soft key: mask-ROM UBOOT (usb.c UBOOT_KEY)
FINISH_CHECK, FINISH_WRITE = 0xE0000000, 0xF0000000
MAXDATA = 512
BLOCKS, BLK, KEEP = 20, 0x30, 0x2F         # BLOCKS: the identity blocks (more may follow, see block_count)
MAX_BLOCKS = 64
LOADER_MARK = b"FELUCCA-LOADER-1"
# the unmodified official FM-1 V15 (FM-1.fwsc from M-VAVE): returning to it is allowed without --force,
# as the web installer's "Return to official V15" (web/fm1pkg.js validateStockPackage)
STOCK_V15_SHA256 = "db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a"
# (2.0.2, #64) the unmodified official FM-1 V16 (M-VAVE's FM-1.fwsc since 2026-10-21, FM-1_016): a unit that ran V16
# goes back to it (Skiff stages an official package only when its head is the unit's: firmware/src/ota.c)
STOCK_V16_SHA256 = "d017fb65e70aca97ee29d944470c3515a74581df5c0576bf70de917c7b2b61d5"
# the unmodified official SMK-37 Pro V016 (SMK-37_Pro_016.fwsc, 36 interleave blocks): the return of an SMK-37 Pro
STOCK_SMK016_SHA256 = "2d98ce72530e71d617384963423820536f094501299e2cfec0a8d3c3fb6bcfb0"
PORT_RE = re.compile(r"fm-1|smk|elite|mke-p37|starry|felucca|ota|composite|sinco|usb-midi", re.I)   # never probe other gear

# seconds; the tests shorten them
DELAY = {"open": 0.3, "start": 2.0, "reply": 0.01, "loader": 3.0, "reboot": 3.0, "retry": 1.0,
         "poll": 1.0, "hs": 1.0, "idle_check": 8.0, "idle_write": 180.0,
         "wait_loader": 30.0, "wait_reboot": 40.0, "wait_resume": 120.0}
RESUME_MAX = 5                              # write attempts (the first and the resumes after a drop)

SMK_MODELS = ("SMK-37 Pro", "SMK-37 Elite", "MKE-P37")


def needs_stock_update(text):
    """a stock SMK-37 Pro / Elite / MKE-P37 older than V016 (identity _011.._015): it is updated with M-VAVE's tool
    first, the SMK port's install was verified from V016 (as web/fm1ota.js needsStockUpdate). FM-1s, Skiff (7xx),
    other ports (9xx) and the recovery screen (000) pass"""
    model, _, num = text.rpartition("_")
    return model in SMK_MODELS and num.isdigit() and 0 < int(num) < 16


EXIT = {"usage": 2, "badpkg": 2, "notfound": 3, "model": 6, "lost": 4, "stopped": 4, "badreq": 4, "stock": 7,
        "noloader": 5, "noreturn": 5, "mismatch": 6}


class InstallError(Exception):
    def __init__(self, code, msg):
        super().__init__(msg)
        self.code = code


# ------------------------------------------------------------ wire format ---

def pack7(data):
    out, acc, nb = [], 0, 0
    for b in data:
        acc |= b << nb
        nb += 8
        while nb >= 7:
            out.append(acc & 0x7F)
            acc >>= 7
            nb -= 7
    if nb:
        out.append(acc & 0x7F)
    return bytes(out)


def unpack7(s):
    out, acc, nb = [], 0, 0
    for b in s:
        acc |= (b & 0x7F) << nb
        nb += 7
        while nb >= 8:
            out.append(acc & 0xFF)
            acc >>= 8
            nb -= 8
    return bytes(out)


def response(addr, data, fl=0):
    n = len(data)
    body = bytearray([0x00, 0x59, 0x30, (n + 8) & 0xFF, ((n + 8) >> 8) & 0xFF, 0, fl])
    body += addr.to_bytes(4, "little") + bytes([n & 0xFF, (n >> 8) & 0xFF, 0]) + bytes(data)
    body.append(~sum(body[6:]) & 0xFF)
    return b"\xF0" + pack7(body) + b"\xF7"


def parse_request(pkt):
    """-> (flags, addr, len) or None"""
    if len(pkt) < 2 or pkt[0] != 0xF0 or pkt[-1] != 0xF7:
        return None
    u = unpack7(pkt[1:-1])
    if len(u) != 15 or u[:3] != b"\x00\x59\x30" or ~sum(u[6:14]) & 0xFF != u[14]:
        return None
    return u[6], int.from_bytes(u[7:11], "little"), int.from_bytes(u[11:14], "little")


class Identity:
    def __init__(self, text, model, version):
        self.text, self.model, self.version = text, model, version

    @property
    def loader(self):
        return self.model.lower().startswith("ota-")


def printable(b):
    """bytes from the device as text for the terminal: printable ASCII only (no escape sequences)"""
    return "".join(chr(c) if 32 <= c < 127 else "?" for c in b)


def parse_identity(pkt):
    if len(pkt) < 2 or pkt[0] != 0xF0 or pkt[-1] != 0xF7:
        return None
    d = unpack7(pkt[1:-1])
    if len(d) != 34 or d[:3] != b"\x00\x59\x11":
        return None
    txt = printable(d[6:33].rstrip(b"\0"))
    m = re.fullmatch(r"([^_]+)_(\d+)", txt)
    return Identity(txt, m[1], int(m[2])) if m else None


# the SMK loader's queries while it waits for a host (firmware/loader/ldr_crumb.h, hal/fm1_clk_regs.h): its start
# record (why it started, warm or cold, how far the run before got) and eight clock registers, 32 bytes each
LDR_RECORD, LDR_CLOCKS = 0x7B, 0x7C
LDR_CLOCK_REGS = (0x10008, 0x1000C, 0x10014, 0x119A0, 0x119A8, 0x119AC, 0x40204, 0x16A08)
P3_RST_BITS = ("power-on", "VDDIO low", "watchdog", "VCM", "long press", "1.2 V low", "soft")
LDR_STAGES = {0: "none", 1: "waiting", 2: "checking the package", 3: "writing", 4: "finishing", 5: "done"}


def query(cmd):
    """a host query with no body (HS_QUERY is query(0x11))"""
    return bytes([0xF0, *pack7(bytes([0, 0x59, cmd, 0, 0, 0, 0xFF])), 0xF7])


def parse_reply(pkt, cmd):
    """the body of the device's reply to cmd (00 59 cmd len24 body ~sum), or None"""
    if len(pkt) < 2 or pkt[0] != 0xF0 or pkt[-1] != 0xF7:
        return None
    d = unpack7(pkt[1:-1])
    if len(d) < 7 or d[0] != 0 or d[1] != 0x59 or d[2] != cmd:
        return None
    n = d[3] | d[4] << 8 | d[5] << 16
    if len(d) < 7 + n or (~sum(d[6:6 + n]) & 0xFF) != d[6 + n]:
        return None
    return bytes(d[6:6 + n])


def ask_device(link, cmd, secs=1.0):
    """send query(cmd) and wait for its reply's body (None: no answer, e.g. a loader without the query)"""
    link.drain()
    if not link.send(query(cmd)):
        return None
    end = time.monotonic() + secs
    while (left := end - time.monotonic()) > 0:
        p = link.read(left)
        if p is None:
            break
        body = parse_reply(p, cmd)
        if body is not None:
            return body
    return None


def describe_record(r):
    """the SMK loader's start record (32 bytes, ldr_crumb.h) in lines of words"""
    u = lambda o, n: int.from_bytes(r[o:o + n], "little")

    def run(o):
        stage, warm, sector = r[o], r[o + 1], u(o + 4, 4)
        return (f"{LDR_STAGES.get(stage, stage)}" + (f" sector {sector:#07x}" if stage == 3 else "")
                + f" at {u(o + 8, 4) / 1000:.1f} s, " + ("warm (started by the app)" if warm else
                                                       "cold (started by the boot stage)")
                + f", slowest erase {u(o + 2, 2)} ms")
    why = ", ".join(n for i, n in enumerate(P3_RST_BITS) if r[0] >> i & 1) or "none"
    lines = [f"loader start {u(2, 2)}: reset by {why} (p3_rst {r[0]:#04x}, rst_src {u(4, 4):#010x}, "
             f"wdt_con {r[1]:#04x})", f"  this start: {run(8)}"]
    if r[20]:
        lines.append(f"  the run before: {run(20)}")
    return lines


# --------------------------------------------------------------- package ---

def product_of(raw):
    """the package identity ("FM-1_906", "SMK-37 Pro_016"): one marker byte after each of the first 20 blocks"""
    if len(raw) < BLOCKS * BLK:
        raise InstallError("badpkg", "not an FM-1 package (too short)")
    return "".join(chr((m - i - 1) & 0xFF) for i in range(BLOCKS) if (m := raw[i * BLK + KEEP]) != 0x7D)


def block_count(raw):
    """interleave blocks: 20, then more while the marker is the unused 0x7D (SMK-37 Pro v016: 36).
    A 20-block package can have 0x7D there by chance: deinterleave checks the CRCs and falls back."""
    n = BLOCKS
    while n < MAX_BLOCKS and (n + 1) * BLK <= len(raw) and raw[n * BLK + KEEP] == 0x7D:
        n += 1
    return n


def logical_image(raw, blocks=BLOCKS):
    """the image the device reads during an update: the package without the marker bytes"""
    out = bytearray()
    for i in range(blocks):
        out += raw[i * BLK:i * BLK + KEEP]
    return bytes(out + raw[blocks * BLK:])


def _crc16(data, crc=0):
    """CRC-16/XMODEM, as the package and the device use"""
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def _dec(b):
    """JieLi ENC stream cipher (its own inverse)"""
    out, key = bytearray(b), 0xFFFF
    for i in range(len(out)):
        out[i] ^= key & 0xFF
        key = ((key << 1) ^ (0x1021 if key & 0x8000 else 0)) & 0xFFFF
    return bytes(out)


def files_ok(image):
    """the logical image's header and file list are intact and flash.bin (0) / ota.bin (100) match their CRCs.
    The stock packer's other entries store a CRC of something else: checked for their bounds only."""
    if len(image) < 0x40:
        return False
    hdr = _dec(image[0:0x40])
    if _crc16(hdr[2:0x40]) != int.from_bytes(hdr[0:2], "little"):
        return False
    count = int.from_bytes(hdr[8:10], "little")
    if not 1 <= count <= 8 or len(image) < 0x40 + 0x50 * count:
        return False
    for i in range(count):
        e = _dec(image[0x40 + i * 0x50:0x90 + i * 0x50])
        typ = int.from_bytes(e[0:2], "little")
        off, size = int.from_bytes(e[8:12], "little"), int.from_bytes(e[12:16], "little")
        if off + size > len(image):
            return False
        if typ in (0, 100) and _crc16(image[off:off + size]) != int.from_bytes(e[4:6], "little"):
            return False
    return True


def deinterleave(raw):
    """the logical image: 20 blocks, or the detected count when the files' CRCs agree"""
    n = block_count(raw)
    if n != BLOCKS:
        image = logical_image(raw, n)
        if files_ok(image) or not files_ok(logical_image(raw, BLOCKS)):
            return image
    return logical_image(raw, BLOCKS)


def flash_span(image):
    """(offset, size) of flash.bin (entry type 0) in a logical image, or None (no file list)"""
    if len(image) < 0x40:
        return None
    count = int.from_bytes(_dec(image[0:0x40])[8:10], "little")
    for i in range(min(count, 8)):
        e = _dec(image[0x40 + i * 0x50:0x90 + i * 0x50])
        off, size = int.from_bytes(e[8:12], "little"), int.from_bytes(e[12:16], "little")
        if int.from_bytes(e[0:2], "little") == 0 and size and off + size <= len(image):
            return off, size
    return None


class WriteMeter:
    """step 2's percentage, forward only, from the flash.bin bytes served. Felucca's (and Skiff's) loader reads
    flash.bin twice, upward: a check pass, then the write pass without the 16 KiB head, the app's header sector last.
    M-VAVE's reads it once, downward. The first reads past the head tell which; until then it shows 0."""
    HEAD = 0x4000
    SAMPLE = 8

    def __init__(self, image):
        self.span, self.size = flash_span(image), len(image)
        self.done, self.total, self.pct, self.seen = 0, None, 0, []

    def add(self, addr, n):
        if not self.span:                               # no file list: how far up the image the reads are
            p = (addr + n) * 100 // self.size
        else:
            lo, size = self.span
            if not lo <= addr < lo + size:
                return self.pct
            self.done += n
            off = addr - lo
            if self.total is None and off >= self.HEAD and (not self.seen or off != self.seen[-1]):
                self.seen.append(off)
                if len(self.seen) > self.SAMPLE:
                    up = sum(b > a for a, b in zip(self.seen, self.seen[1:]))
                    self.total = 2 * size - self.HEAD if up * 2 > self.SAMPLE else size
            p = self.done * 100 // self.total if self.total else 0
        self.pct = max(self.pct, min(99, p))
        return self.pct


def model_of(text):
    return text.split("_")[0].removeprefix("ota-")


def same_model(a, b):
    """identities a and b name exactly the same model ("ota-" aside): a package installs only on its own model
    (the SMK-37 Pro, SMK-37 Elite and MKE-P37 builds each on their own model, never an FM-1 and an SMK)"""
    return model_of(a) == model_of(b)


# ------------------------------------------------------------------ MIDI ---

class MidoLink:
    """one MIDI in/out pair with a SysEx queue"""

    def __init__(self, backend, in_name, out_name):
        import mido
        self.backend, self.name, self.mido = backend, in_name, mido
        self.q = queue.Queue()
        self.inp = mido.open_input(in_name, callback=self._rx)
        try:
            self.out = mido.open_output(out_name)
        except Exception:
            self.inp.close()
            raise
        time.sleep(DELAY["open"])   # CoreMIDI may drop a message sent just after opening

    def _rx(self, msg):
        if msg.type == "sysex":
            self.q.put(bytes([0xF0, *msg.data, 0xF7]))

    @property
    def lost(self):
        return self.name not in self.backend.input_names()

    def send(self, pkt):
        try:
            self.out.send(self.mido.Message("sysex", data=pkt[1:-1]))
            return True
        except Exception:
            return False

    def read(self, timeout):
        try:
            return self.q.get(timeout=max(timeout, 0))
        except queue.Empty:
            return None

    def drain(self):
        while not self.q.empty():
            self.q.get_nowait()

    def close(self):
        if self.lost:                       # the device is gone: closing its ports crashes rtmidi on Windows
            _ABANDONED.extend((self.inp, self.out))     # (heap corruption, 0xC0000374); main() exits past them
            return
        for p in (self.inp, self.out):
            try:
                p.close()
            except Exception:
                pass


_ABANDONED = []                             # the open ports of a device that vanished (MidoLink.close)


class MidoBackend:
    def __init__(self):
        import mido
        self.mido = mido

    def input_names(self):
        return list(dict.fromkeys(self.mido.get_input_names()))

    def output_names(self):
        return list(dict.fromkeys(self.mido.get_output_names()))

    def open(self, in_name, out_name):
        return MidoLink(self, in_name, out_name)


def pair_output(name, outs):
    if name in outs:
        return name
    base = re.sub(r"\s+\d+$", "", name)     # Windows numbers each port
    same = [o for o in outs if re.sub(r"\s+\d+$", "", o) == base]
    if len(same) == 1:
        return same[0]
    return outs[0] if len(outs) == 1 else None


# --------------------------------------------------------------- updater ---

class Device:
    def __init__(self, link, ident, name):
        self.link, self.id, self.name = link, ident, name


def handshake(link, tries=3):
    link.drain()
    for _ in range(tries):
        if not link.send(HS_QUERY):
            return None
        end = time.monotonic() + DELAY["hs"]
        while (left := end - time.monotonic()) > 0:
            p = link.read(left)
            if p is None:
                break
            ident = parse_identity(p)
            if ident:
                return ident
    return None


class Updater:
    def __init__(self, backend, port=None):
        self.backend = backend
        self.port = port

    def candidates(self, only_port):
        names = self.backend.input_names()
        if self.port:
            mine = [n for n in names if self.port.lower() in n.lower()]
            if only_port:
                return mine
            return mine + [n for n in names if n not in mine and PORT_RE.search(n)]
        return [n for n in names if PORT_RE.search(n)]

    def find(self, want=None, only_port=True):
        """the first matching port that answers the handshake (and want(identity))"""
        outs = self.backend.output_names()
        for name in self.candidates(only_port):
            out = pair_output(name, outs)
            if not out:
                continue
            try:
                link = self.backend.open(name, out)
            except Exception:
                continue
            ident = handshake(link, 2)
            if ident and (want is None or want(ident)):
                return Device(link, ident, name)
            link.close()
        return None

    def wait_for(self, want, secs):
        end = time.monotonic() + secs
        while time.monotonic() < end:
            dev = self.find(want, only_port=False)
            if dev:
                return dev
            time.sleep(DELAY["retry"])
        return None

    def serve(self, link, image, finish, idle, progress):
        """answer read requests until the device asks for `finish`; -> (served, finished, lost)"""
        served, last = 0, time.monotonic()
        while True:
            pkt = link.read(DELAY["poll"])
            if pkt is None:
                if link.lost:
                    return served, False, True
                if time.monotonic() - last > idle:
                    return served, False, False
                continue
            r = parse_request(pkt)
            if not r:
                continue
            fl, addr, n = r
            last = time.monotonic()
            if addr in (FINISH_CHECK, FINISH_WRITE):
                link.send(response(addr, b"success\0"))
                if addr == finish:
                    return served, True, False
                continue
            if n > MAXDATA or addr + n > len(image):
                raise InstallError("badreq", f"the device asked for {addr:#x}+{n}, outside the package")
            time.sleep(DELAY["reply"])
            if not link.send(response(addr, image[addr:addr + n], fl)):
                return served, False, True
            served += 1
            progress(served, addr, n)

    def check(self, dev, image, product, step):
        """step 1: the running firmware checks the package and starts the loader (the package's model's: another
        unit on the same computer may be in update mode too)"""
        step("start", dev.id.text)
        dev.link.send(UPGRADE)
        time.sleep(DELAY["start"])
        served, finished, lost = self.serve(dev.link, image, FINISH_CHECK, DELAY["idle_check"],
                                            lambda k, addr, n: step("check", k))
        dev.link.close()
        if not finished:
            raise InstallError("lost" if lost else "stopped",
                               f"the device {'was disconnected' if lost else 'stopped answering'} after "
                               f"{served} requests; nothing was written")
        step("loader")
        time.sleep(DELAY["loader"])
        ota = self.wait_for(lambda i: i.loader and same_model(i.text, product), DELAY["wait_loader"])
        if not ota:
            raise InstallError("noloader", "the update loader did not appear. Replug the USB cable and "
                                           "run the install again: the device stays in update mode until it is done")
        return ota

    def write(self, ota, image, step):
        """step 2: the loader reads and writes the whole package, then restarts"""
        step("write", 0)
        ota.link.send(UPGRADE)
        time.sleep(DELAY["start"])
        meter = WriteMeter(image)
        served, finished, lost = self.serve(ota.link, image, FINISH_WRITE, DELAY["idle_write"],
                                            lambda k, addr, n: step("write", meter.add(addr, n)))
        ota.link.close()
        if not finished:
            raise InstallError("lost" if lost else "stopped",
                               f"the loader {'was disconnected' if lost else 'stopped answering'} after "
                               f"{served} requests. Replug the USB cable and run the install again to finish")
        step("write", 100)

    def verify(self, product, step):
        step("reboot")
        time.sleep(DELAY["reboot"])
        back = self.wait_for(lambda i: not i.loader and same_model(i.text, product), DELAY["wait_reboot"])
        if not back:
            raise InstallError("noreturn", "the device did not come back: power-cycle it")
        back.link.close()
        if back.id.text != product:
            raise InstallError("mismatch", f"written, but the device reports {back.id.text} (expected {product})")
        step("done", back.id.text)
        return back.id.text

    def staged(self, dev, image, product, step):
        """the package's model checked, then step 1 unless the device is already in update mode -> the loader"""
        if not same_model(dev.id.text, product):
            dev.link.close()
            raise InstallError("model", f"the device is {dev.id.text}, the package is for {product}")
        return dev if dev.id.loader else self.check(dev, image, product, step)

    def install(self, dev, image, product, step=lambda *a: None):
        """running firmware -> loader -> new firmware; dev from find(). A device already in
        update mode only needs the write. A write cut off (cable, power, a loader that resets or stops
        answering) is resumed when the same model's loader answers again within DELAY["wait_resume"]: the
        update record keeps the device in update mode, and the loader writes the app's header last."""
        ota = self.staged(dev, image, product, step)
        for attempt in range(1, RESUME_MAX + 1):
            try:
                self.write(ota, image, step)
                break
            except InstallError as e:
                if e.code not in ("lost", "stopped") or attempt == RESUME_MAX:
                    raise
                step("dropped")
                ota = self.wait_for(lambda i: i.loader and same_model(i.text, product), DELAY["wait_resume"])
                if not ota:
                    raise
                step("resume", attempt + 1)
        return self.verify(product, step)

    def uboot(self, dev, image, product, step=lambda *a: None):
        """the full flash backup's way in (fm1-to-smk37 kit, Method A): the running firmware starts the package's
        loader (step 1 only: the app area is not written), and the loader gets the UBOOT key. The update record
        stays: after the backup and a power cycle the device is in update mode until a package is installed."""
        ota = self.staged(dev, image, product, step)
        step("uboot")
        ok = ota.link.send(UBOOT)
        ota.link.close()
        if not ok:
            raise InstallError("lost", "the loader was disconnected before the UBOOT key was sent")


# ------------------------------------------------------------------- CLI ---

def load_package(path, force):
    try:
        raw = open(path, "rb").read()
    except OSError as e:
        raise InstallError("badpkg", f"cannot read {path}: {e.strerror}")
    product = product_of(raw)
    if not re.fullmatch(r"[^_]+_\d+", product):
        raise InstallError("badpkg", f"{path}: not an FM-1 package (identity {product!r})")
    official = hashlib.sha256(raw).hexdigest() in (STOCK_V15_SHA256, STOCK_V16_SHA256, STOCK_SMK016_SHA256)
    if LOADER_MARK not in raw and not official and not force:
        raise InstallError("badpkg", f"{path}: no Felucca update loader in this package; only Felucca's own "
                                     "packages and the unmodified official V15 or V16 (FM-1.fwsc) or SMK-37 Pro V016 "
                                     "(SMK-37_Pro_016.fwsc) are installed (--force overrides)")
    return product, deinterleave(raw)


def not_found(up):
    names = up.backend.input_names()
    where = f"no MIDI port matching {up.port!r} answered" if up.port else "FM-1 or SMK-37 not found"
    return InstallError("notfound", f"{where} (USB data cable? another app using it?)"
                        f"\n  MIDI inputs: {', '.join(names) if names else '(none)'}")


class Progress:
    def __init__(self, out):
        self.out, self.line = out, False

    def __call__(self, k, a=None):
        if k == "check":
            self.put(f"checking the package ({a} reads)")
        elif k == "write":
            self.put(f"writing {a:3d}%")
        else:
            self.end()
            msg = {"start": f"starting the update on {a}", "loader": "switching to update mode",
                   "uboot": "UBOOT key sent: the device should now show up as the USB disk \"WL82 UBOOT1.00\"",
                   "dropped": "the device dropped out during the write. It stays in update mode: plug the cable "
                              "back in or switch it back on, and the install goes on (waiting up to "
                              f"{int(DELAY['wait_resume'])} s)",
                   "resume": f"its update loader is back: resuming (try {a} of {RESUME_MAX})",
                   "reboot": "restarting", "done": f"done: the device runs {a}"}[k]
            print(msg, file=self.out, flush=True)

    def put(self, s):
        print(f"\r{s}\033[K", end="", file=self.out, flush=True)
        self.line = True

    def end(self):
        if self.line:
            print(file=self.out, flush=True)
            self.line = False


def run(a, backend, out, ask):
    up = Updater(backend, a.port)
    if a.info:
        dev = up.find()
        if not dev:
            raise not_found(up)
        mode = "update loader (update not finished)" if dev.id.loader else "running"
        print(f"{dev.id.text}  [{mode}]  port: {dev.name}", file=out)
        if dev.id.loader:                           # (the SMK loader answers these; others stay silent)
            rec = ask_device(dev.link, LDR_RECORD)
            if rec and len(rec) == 32:
                for line in describe_record(rec):
                    print(line, file=out)
                clk = ask_device(dev.link, LDR_CLOCKS)
                if clk and len(clk) == 32:
                    print("  clocks: " + " ".join(f"{a:05X}={int.from_bytes(clk[i * 4:i * 4 + 4], 'little'):08X}"
                                                  for i, a in enumerate(LDR_CLOCK_REGS)), file=out)
        dev.link.close()
        return 0
    product, image = load_package(a.package, a.force)
    print(f"package: {product}  ({a.package})", file=out)
    dev = up.find(lambda i: same_model(i.text, product)) or up.find()     # the package's model first: two units
    if not dev:
        raise not_found(up)
    print(f"device:  {dev.id.text}  ({dev.name})" + ("  in update mode" + ("" if a.uboot else ": the write will be finished")
                                                      if dev.id.loader else ""), file=out)
    ours = product.rpartition("_")[2].isdigit() and int(product.rpartition("_")[2]) >= 700
    if ours and not dev.id.loader and needs_stock_update(dev.id.text) and not a.force:
        dev.link.close()
        raise InstallError("stock", f"the device runs {dev.id.text}, M-VAVE's firmware older than V016. Update it to "
                                    "V016 first (M-VAVE's M-UPGRADE, or this tool with the unmodified "
                                    "SMK-37_Pro_016.fwsc), then install this package (--force skips this check)")
    q = ("Start this package's update loader and send it the UBOOT key (for a flash backup)? The device stays in "
         "update mode until a package is installed. [y/N] " if a.uboot else
         "Install? Keep the device plugged in and switched on while it writes. [y/N] ")
    if not a.yes and not ask(q):
        dev.link.close()
        print("cancelled", file=out)
        return 1
    prog = Progress(out)
    try:
        (up.uboot if a.uboot else up.install)(dev, image, product, prog)
    finally:
        prog.end()
    return 0


def ask_tty(prompt):
    try:
        return input(prompt).strip().lower() in ("y", "yes")
    except EOFError:
        return False


def main(argv=None, backend=None, out=sys.stdout, ask=ask_tty):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("package", nargs="?", help="Felucca package (.fwsc)")
    ap.add_argument("--info", action="store_true", help="print the identity of the connected FM-1")
    ap.add_argument("--port", metavar="NAME", help="MIDI port to use (part of its name)")
    ap.add_argument("--yes", action="store_true", help="do not ask for confirmation")
    ap.add_argument("--force", action="store_true", help="install a package without the Felucca loader marker")
    ap.add_argument("--uboot", action="store_true",
                    help="only start the package's update loader, then send it the UBOOT key (a flash backup)")
    a = ap.parse_args(argv)
    if bool(a.info) == bool(a.package) or (a.uboot and not a.package):
        ap.error("give a PACKAGE.fwsc or --info (--uboot needs a PACKAGE.fwsc)")
    real = backend is None
    try:
        if backend is None:
            try:
                backend = MidoBackend()
            except ImportError:
                raise InstallError("usage", "needs mido and python-rtmidi (pip install mido python-rtmidi)")
        rc = run(a, backend, out, ask)
    except InstallError as e:
        print(f"error: {e}", file=sys.stderr)
        rc = EXIT.get(e.code, 1)
    except KeyboardInterrupt:
        print("\ninterrupted: if the device is in update mode, run the install again to finish", file=sys.stderr)
        rc = 130
    if real and _ABANDONED:        # ports of a vanished device stay open (MidoLink.close): leave without the
        for f in (out, sys.stdout, sys.stderr):     # clean-up that would close them, with the exit code intact
            f.flush()
        os._exit(rc)
    return rc


if __name__ == "__main__":
    sys.exit(main())
