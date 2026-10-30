#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Yoann
"""ZINZIN: the web installer for a release package, without the rest of a site (no browser emulator to build).
The same page fm1-base's tools/make_site.py makes (base/web/installer.html, with fm1pkg.js and fm1ota.js inlined),
and m-bootloader's package beside the firmware: the page offers to write it right after ZINZIN.

    python3 tools/make_installer.py zinzin/build/zinzin-0.1.fwsc 0.1 zinzin/build/installer

Then serve the folder (python3 -m http.server 8000 --directory zinzin/build/installer) and open
http://localhost:8000/install/ in Chrome or Edge (Web MIDI wants a page from a server, not a file)."""
import hashlib
import json
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "base"
MBOOT = "1.2"                                     # base/web/m-bootloader/fm1-uboot-1.2.fwsc (MIT)
BLOCKS, BLK, KEEP = 20, 0x30, 0x2F


def product_of(raw):
    """the package identity: one marker byte after each of the first 20 blocks (as fm1pkg.js reads it)"""
    return "".join(chr((m - i - 1) & 0xFF) for i in range(BLOCKS) if (m := raw[i * BLK + KEEP]) != 0x7D)


def as_script(src):
    """an ES module's text as plain script, to inline it: no export, no import"""
    src = re.sub(r"^export\s+", "", src, flags=re.M)
    return re.sub(r"^import .*?;\n", "", src, flags=re.M)


def conf_value(text, key, default=""):
    m = re.search(r'^\s*' + key + r'\s*=\s*"([^"]*)"', text, re.M)
    return m.group(1) if m else default


def main(pkg, version, out):
    pkg, out = Path(pkg), Path(out)
    conf = (ROOT / "zinzin" / "fm1.toml").read_text(encoding="utf-8")
    name, prefix = conf_value(conf, "name", "ZINZIN"), conf_value(conf, "id_prefix")
    raw = pkg.read_bytes()
    product = product_of(raw)
    if not re.fullmatch(re.escape(prefix) + r"\d{6}", product):
        raise SystemExit(f"{pkg}: identity {product!r}: a release build is needed (tools/build.py ../zinzin --release X.Y)")
    if b"FELUCCA-LOADER-1" not in raw:
        raise SystemExit(f"{pkg}: no update loader in it")
    file_name = f"{name.lower()}-{re.sub(r'[^A-Za-z0-9.-]', '-', version)}.fwsc"
    boot = BASE / "web" / "m-bootloader" / f"fm1-uboot-{MBOOT}.fwsc"
    boot_name = f"m-bootloader-{MBOOT}.fwsc"
    if out.exists():
        shutil.rmtree(out)
    (out / "install").mkdir(parents=True)
    (out / "firmware").mkdir()
    shutil.copy(pkg, out / "firmware" / file_name)
    shutil.copy(boot, out / "firmware" / boot_name)
    shutil.copy(BASE / "web" / "m-bootloader" / "LICENSE", out / "firmware" / "m-bootloader-LICENSE.txt")
    for lic in sorted((BASE / "LICENSES").glob("*.txt")):
        shutil.copy(lic, out / "firmware" / lic.name)
    html = (BASE / "web" / "installer.html").read_text(encoding="utf-8")
    lib = as_script((BASE / "web" / "fm1pkg.js").read_text(encoding="utf-8")) + "\n" + \
        as_script((BASE / "web" / "fm1ota.js").read_text(encoding="utf-8"))
    meta = json.dumps({"version": version, "product": product, "pkg": "../firmware/" + file_name, "backup": False,
                       "boot": {"pkg": "../firmware/" + boot_name, "version": MBOOT,
                                "sha256": hashlib.sha256(boot.read_bytes()).hexdigest()}})
    for mark in ("/*LIB*/", "/*META*/", "/*APP*/"):
        if html.count(mark) != 1:
            raise SystemExit(f"base/web/installer.html: {mark} expected once (another fm1-base version?)")
    repo = conf_value(conf, "repo")
    credits = conf_value(conf, "credits")
    for key, val in (("NAME", name), ("REPO", repo), ("REPO_SHORT", re.sub(r"^https?://", "", repo)),
                     ("LICENSE", conf_value(conf, "licence", "MPL-2.0")), ("CREDITS", " " + credits if credits else "")):
        html = html.replace("{{" + key + "}}", val)
    html = html.replace("/*LIB*/", lib).replace("/*META*/", meta).replace("/*APP*/", "null")
    (out / "install" / "index.html").write_text(html, encoding="utf-8")
    print(f"installer: {out}/install/ ({product}, {file_name}, with m-bootloader {MBOOT})")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    main(*sys.argv[1:])
