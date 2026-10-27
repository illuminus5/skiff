#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Make the site (GitHub Pages):

  index.html                  redirect to the installer (the old URL keeps working)
  firmware/skiff-VER.fwsc     the package (+ LICENSE, LICENSING.md, LICENSES/: the package holds
                              JieLi SDK files under Apache-2.0, see LICENSING.md)
  webapp/installer/index.html index_pkg.html, self-contained (fm1pkg.js, fm1ota.js, metadata inlined)
  webapp/editor/index.html    editor.html (+ fukiai.ttf, FUKIAI-LICENSE.txt; (2.0) conv/: conv.js and build/conv/conv.wasm,
                              the converter its Restore and the installer use)
  learn/                      Learn Skiff, the manual in chapters (web/learn, without its local/ review videos)
  try/                        the browser emulator (build/emu from web/emu/build.sh, EMU_PREVIEW=0)
  panel.js, fonts/            the panel's drawing and the fonts the Learn page shares, at the paths it has in web/
  src/                        not touched

  web/make_site.py build/skiff-X.Y.fwsc X.Y OUT_DIR [SMK_PACKAGE ...]

The packages must be ones made by tools/fm1pkg_make.py (Felucca's own loader, no vendor files).
Their identities are read from them (FM-1_7xx, and for the SMK-37 builds SMK-37 Pro_7xx,
SMK-37 Elite_7xx, MKE-P37_7xx, one per model); the installer picks the one for the connected
model, and the device must report it after the install. Names: skiff-X.Y.fwsc (FM-1),
skiff-X.Y-smk37.fwsc, skiff-X.Y-smk37-elite.fwsc, skiff-X.Y-smk37-mke.fwsc.
"""
import json
import re
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
BLOCKS, BLK, KEEP = 20, 0x30, 0x2F


def strip_module(src):
    src = re.sub(r"^export\s+", "", src, flags=re.M)
    return re.sub(r"^import .*?;\n", "", src, flags=re.M)


def product_of(raw):
    """the package identity: one marker byte after each of the first 20 blocks (fm1pkg.js productOf)"""
    return "".join(chr((m - i - 1) & 0xFF) for i in range(BLOCKS) if (m := raw[i * BLK + KEEP]) != 0x7D)


MODELS = {"FM-1": "", "SMK-37 Pro": "-smk37", "SMK-37 Elite": "-smk37-elite", "MKE-P37": "-smk37-mke"}


def package(path, version):
    """one of our packages -> (product, raw, published name); the model sets the name"""
    raw = Path(path).read_bytes()
    product = product_of(raw)
    model, _, num = product.rpartition("_")
    if model not in MODELS or not re.fullmatch(r"7\d\d", num):
        raise SystemExit(f"{path}: identity {product!r} is not one of our packages "
                         f"(FM-1_7xx, SMK-37 Pro_7xx, SMK-37 Elite_7xx, MKE-P37_7xx)")
    if b"FELUCCA-LOADER-1" not in raw:              # Felucca loader marker: never publish a package with vendor files
        raise SystemExit(f"{path}: no Felucca loader in it; the site ships only fm1pkg_make.py packages "
                         "(a package patched from an official one carries vendor files)")
    return product, raw, f"skiff-{re.sub(r'[^A-Za-z0-9.-]', '-', version)}{MODELS[model]}.fwsc"


def main(pkg, version, out, *more):
    out = Path(out)
    pkgs = [package(p, version) for p in (pkg, *more)]
    product, raw, name = pkgs[0]
    if not product.startswith("FM-1_"):
        raise SystemExit(f"{pkg}: the first package is the FM-1's (FM-1_7xx), not {product!r}")
    models = [p.rpartition("_")[0] for p, _, _ in pkgs]
    if len(set(models)) != len(models):
        raise SystemExit(f"one package per model: {models}")
    html = (HERE / "index_pkg.html").read_text(encoding="utf-8")
    lib = strip_module((HERE / "fm1pkg.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((HERE / "fm1ota.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((HERE / "fm1backup.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((HERE / "conv" / "conv.js").read_text(encoding="utf-8"))   # ((2.0) its wasm: webapp/editor/conv/
    conv = HERE.parent / "build" / "conv" / "conv.wasm"   # ((2.0, Tier B 10) the converter: web/conv/build.sh's)
    if not conv.exists():
        raise SystemExit(f"{conv}: no converter build (sh web/conv/build.sh, after the release build)")
    meta = json.dumps({"version": version, "product": product, "pkg": "../../firmware/" + name,
                       "packages": [{"product": p, "pkg": "../../firmware/" + n} for p, _, n in pkgs]})
    for mark in ("/*LIB*/", "/*META*/"):
        if html.count(mark) != 1:
            raise SystemExit(f"index_pkg.html must contain {mark} once; update make_site.py")
    html = html.replace("/*LIB*/", lib).replace("/*META*/", meta)
    inst, ed, fw = out / "webapp" / "installer", out / "webapp" / "editor", out / "firmware"
    for d in (inst, ed, fw):
        d.mkdir(parents=True, exist_ok=True)
    for old in fw.glob("skiff-*.fwsc"):          # the current packages only
        old.unlink()
    (inst / "index.html").write_text(html, encoding="utf-8")
    for _, data, n in pkgs:
        (fw / n).write_bytes(data)
    lic = HERE.parent / "LICENSES"                  # the package holds JieLi SDK files (Apache-2.0): their
    (fw / "LICENSES").mkdir(exist_ok=True)          # licence travels next to it, with Felucca's own
    names = sorted(f.name for f in lic.glob("*.txt"))
    for n in names:
        shutil.copy(lic / n, fw / "LICENSES" / n)
    (fw / "LICENSES" / "index.html").write_text(    # the installer links this folder: Pages lists no folders
        '<!doctype html><meta charset="utf-8"><title>Skiff licences</title><h1>Licence texts</h1><ul>'
        + "".join(f'<li><a href="{n}">{n}</a></li>' for n in names)
        + '</ul><p><a href="../LICENSING.md">LICENSING.md</a> · <a href="../LICENSE">LICENSE (GPL-3.0)</a></p>\n',
        encoding="utf-8")
    for doc in ("LICENSE", "LICENSING.md"):
        shutil.copy(HERE.parent / doc, fw / doc)
    shutil.copy(HERE / "editor.html", ed / "index.html")
    for f in ("fukiai.ttf", "FUKIAI-LICENSE.txt", "fm1backup.js"):
        if (HERE / f).exists():
            shutil.copy(HERE / f, ed / f)
    (ed / "conv").mkdir(exist_ok=True)              # ((2.0) the converter: the editor's Restore, the installer's write-back)
    shutil.copy(HERE / "conv" / "conv.js", ed / "conv" / "conv.js")
    shutil.copy(conv, ed / "conv" / "conv.wasm")
    if (HERE / "fonts").is_dir():                   # ((Skiff 1.0) the editor's Barlow Condensed, its OFL beside it)
        shutil.copytree(HERE / "fonts", ed / "fonts", dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns("InterTight*"))   # (the Learn page's, not the editor's)
    shutil.rmtree(out / "learn", ignore_errors=True)  # ((Skiff) Learn Skiff: ../panel.js and ../fonts/, as in web/)
    shutil.copytree(HERE / "learn", out / "learn", ignore=shutil.ignore_patterns("local"))
    shutil.copy(HERE / "panel.js", out / "panel.js")
    shutil.copytree(HERE / "fonts", out / "fonts", dirs_exist_ok=True)
    emu = HERE.parent / "build" / "emu"             # ((Skiff #18) the browser emulator: web/emu/build.sh's, ../panel.js)
    page = (emu / "index.html").read_text(encoding="utf-8") if (emu / "index.html").exists() else ""
    if f">v{version}<" not in page:
        raise SystemExit(f"{emu}: no browser build of v{version} without the preview mark "
                         "(EMU_PREVIEW=0 sh web/emu/build.sh, after the release build)")
    shutil.rmtree(out / "try", ignore_errors=True)
    shutil.copytree(emu, out / "try")
    (out / "index.html").write_text(
        '<!doctype html><meta charset="utf-8"><title>Skiff</title>'
        '<meta http-equiv="refresh" content="0; url=webapp/installer/">'
        '<a href="webapp/installer/">Skiff installer</a>\n', encoding="utf-8")
    print(f"site: {out}: webapp/installer ({len(html)} B), webapp/editor, learn, try, "
          + ", ".join(f"firmware/{n} ({len(d)} B, {p})" for p, d, n in pkgs))


if __name__ == "__main__":
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    main(*sys.argv[1:])
