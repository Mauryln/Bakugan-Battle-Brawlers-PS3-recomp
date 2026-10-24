#!/usr/bin/env python
"""Fetch a portable RPCS3 build into tools/rpcs3.

RPCS3 is used exactly once per install: `rpcs3 --decrypt EBOOT.BIN` writes the
decrypted PPU ELF (EBOOT.elf) next to it. The port never runs the game under
RPCS3 -- the recompiled executable is a native Windows program.

    .venv\\Scripts\\python.exe tools\\fetch_rpcs3.py

Downloads the latest Windows build from rpcs3-binaries-win, unpacks it with
bsdtar (tar.exe, shipped with Windows 10+) and leaves tools/rpcs3/rpcs3.exe.
"""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEST = ROOT / "tools" / "rpcs3"
ARCHIVE = ROOT / "tools" / "rpcs3.7z"
API = "https://api.github.com/repos/RPCS3/rpcs3-binaries-win/releases/latest"


def main() -> int:
    if (DEST / "rpcs3.exe").exists():
        print(f"already present: {DEST / 'rpcs3.exe'}")
        return 0

    print("asking GitHub for the latest RPCS3 Windows build ...")
    with urllib.request.urlopen(API, timeout=60) as r:
        rel = json.load(r)
    asset = next(a for a in rel["assets"] if a["name"].endswith("_win64_msvc.7z"))
    print(f"downloading {asset['name']} ({asset['size'] / 1e6:.1f} MB) ...")
    urllib.request.urlretrieve(asset["browser_download_url"], ARCHIVE)

    DEST.mkdir(parents=True, exist_ok=True)
    # py7zr cannot unpack the BCJ2 filter RPCS3's archive uses; bsdtar can.
    tar = shutil.which("tar")
    if not tar:
        sys.exit("tar.exe not found -- extract tools/rpcs3.7z manually into tools/rpcs3")
    subprocess.run([tar, "-xf", str(ARCHIVE), "-C", str(DEST)], check=True)
    ARCHIVE.unlink(missing_ok=True)
    (DEST / "log").mkdir(exist_ok=True)

    exe = DEST / "rpcs3.exe"
    if not exe.exists():
        sys.exit("extraction finished but rpcs3.exe is missing")
    print(f"ready: {exe}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
