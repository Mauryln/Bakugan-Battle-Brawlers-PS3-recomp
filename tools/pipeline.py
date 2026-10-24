#!/usr/bin/env python
"""Bakugan: Battle Brawlers -- regeneration pipeline.

Reproduces every generated artefact of the port from the user's own disc image:

    1. extract the plaintext PS3 ISO            -> game/
    2. decrypt PS3_GAME/USRDIR/EBOOT.BIN        -> game/PS3_GAME/USRDIR/EBOOT.elf
    3. PPU loader (functions/imports/image)     -> analysis/
    4. embedded SPU images                      -> analysis/spu_eboot/
    5. PPU lift                                 -> bakugan-recomp/recompiled/
    6. HLE NID -> handler table                 -> bakugan-recomp/recompiled/ppu_hle_nids.cpp
    7. SPU lift + workload registry             -> bakugan-recomp/spu/

Run with the project venv:

    .venv\\Scripts\\python.exe tools\\pipeline.py            # skip finished steps
    .venv\\Scripts\\python.exe tools\\pipeline.py --force    # redo everything
    .venv\\Scripts\\python.exe tools\\pipeline.py --from lift

Nothing here needs the disc key: this release is a plaintext (JB-style) image,
so only EBOOT.BIN is a SELF and RPCS3's --decrypt unlocks it.
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ISO_DIR = ROOT / "iso"


def find_iso() -> Path:
    """The dump is user-supplied, so its filename is not fixed."""
    found = sorted(ISO_DIR.glob("*.iso"))
    if not found:
        raise SystemExit("no .iso in iso/ -- put your own disc dump there (see README)")
    return found[0]
GAME = ROOT / "game"
EBOOT_BIN = GAME / "PS3_GAME" / "USRDIR" / "EBOOT.BIN"
EBOOT_ELF = GAME / "PS3_GAME" / "USRDIR" / "EBOOT.elf"
ANALYSIS = ROOT / "analysis"
TOOLKIT = ROOT / "ps3recomp-src"
TOOLS = TOOLKIT / "tools"
PORT = ROOT / "bakugan-recomp"
RECOMP = PORT / "recompiled"
SPU = PORT / "spu"
RPCS3 = ROOT / "tools" / "rpcs3" / "rpcs3.exe"

PY = sys.executable
STEPS = ["patch", "extract", "decrypt", "load", "spu-images", "lift", "hle-nids", "spu-lift"]

# End of the title's real code, exclusive. EBOOT.elf section table:
#   .text           0x00010230 + 0x544234 = 0x554464   (executable)
#   (pad)           0x554464 + 0x24
#   import stubs    0x554488 + 0x1860                 (executable, 195 x 0x20)
#   .rodata         0x555CE8 + 0x84BD4                (data)
# without --code-end the lifter promotes branch/jump-table targets in .rodata
# to functions, emits disassembly of asset data, and leaves dangling calls to
# addresses it never emitted (16 undefined symbols at link time).
CODE_END = 0x554488


def log(msg: str) -> None:
    print(f"\n=== {msg}", flush=True)


def run(cmd: list, **kw) -> None:
    print("+ " + " ".join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], check=True, **kw)


def done(path: Path) -> bool:
    return path.exists() and (path.is_file() and path.stat().st_size > 0 or path.is_dir())


def applied_patch_count(toolkit: Path, patches: list[Path]) -> int:
    """Find an applied prefix without modifying the checkout.

    Later patches can change earlier patches' context. Reverse each candidate
    series in a scratch tree, so overlapping patches are checked in order.
    """
    paths = set()
    for patch in patches:
        for line in patch.read_text(encoding="utf-8").splitlines():
            if line.startswith("+++ b/") or line.startswith("--- a/"):
                relative = Path(line[6:].split("\t", 1)[0])
                if relative.is_absolute() or ".." in relative.parts:
                    raise ValueError(f"Invalid patch path: {relative}")
                paths.add(relative)
    with tempfile.TemporaryDirectory(prefix="bakugan-patches-") as temporary:
        scratch = Path(temporary)
        for count in range(len(patches), 0, -1):
            candidate = scratch / str(count)
            candidate.mkdir()
            for relative in paths:
                source = toolkit / relative
                if source.is_file():
                    target = candidate / relative
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(source, target)
            for patch in reversed(patches[:count]):
                result = subprocess.run(
                    ["git", "-C", str(candidate), "apply", "--reverse", str(patch.resolve())],
                    capture_output=True,
                )
                if result.returncode:
                    break
            else:
                return count
    return 0


def step_patch(force: bool) -> None:
    """Apply this port's fixes to the toolkit checkout (idempotent)."""
    if not (TOOLKIT / ".git").exists():
        print("toolkit is not a git checkout -- apply patches/ by hand")
        return
    patches = sorted((ROOT / "patches").glob("*.patch"))
    if not patches:
        return
    count = applied_patch_count(TOOLKIT, patches)
    for p in patches[:count]:
        print(f"already applied: {p.name}")
    for p in patches[count:]:
        log(f"applying {p.name}")
        run(["git", "-C", str(TOOLKIT), "apply", str(p)])


def step_extract(force: bool) -> None:
    if done(GAME / "PS3_GAME" / "USRDIR" / "EBOOT.BIN") and not force:
        print("game/ already extracted")
        return
    log("1/8 extracting the ISO")
    run([PY, ROOT / "tools" / "extract_iso.py", find_iso(), GAME])


def step_decrypt(force: bool) -> None:
    if done(EBOOT_ELF) and not force:
        print("EBOOT.elf already decrypted")
        return
    if not RPCS3.exists():
        sys.exit(f"RPCS3 not found at {RPCS3} -- see README (tools/fetch_rpcs3.py)")
    log("2/8 decrypting EBOOT.BIN with RPCS3")
    run([RPCS3, "--decrypt", EBOOT_BIN])
    if not EBOOT_ELF.exists():
        sys.exit("RPCS3 did not produce EBOOT.elf")


def step_load(force: bool) -> None:
    out = ANALYSIS / "EBOOT.functions.json"
    if done(out) and not force:
        print("analysis/ already has the loader output")
        return
    log("3/8 PPU loader")
    ANALYSIS.mkdir(exist_ok=True)
    run([PY, TOOLS / "ppu_loader.py", EBOOT_ELF, "-o", ANALYSIS])


def step_spu_images(force: bool) -> None:
    out = ANALYSIS / "spu_eboot"
    if done(out) and any(out.glob("*.elf")) and not force:
        print("SPU images already extracted")
        return
    log("4/8 extracting embedded SPU images")
    run([PY, TOOLS / "extract_spu_images.py", EBOOT_ELF, "-o", out])


def step_lift(force: bool) -> None:
    if done(RECOMP / "ppu_recomp.h") and not force:
        print("PPU lift already present")
        return
    log("5/8 lifting the PPU code")
    RECOMP.mkdir(parents=True, exist_ok=True)
    run([PY, TOOLS / "ppu_lifter.py", EBOOT_ELF,
         "--functions", ANALYSIS / "EBOOT.functions.json",
         "--hle-stubs", ANALYSIS / "EBOOT.imports.json",
         "--code-end", hex(CODE_END),
         "--chunks", "16",
         "--output", RECOMP])


def step_hle_nids(force: bool) -> None:
    if done(RECOMP / "ppu_hle_nids.cpp") and not force:
        print("HLE NID table already generated")
        return
    log("6/8 HLE NID -> handler table")
    run([PY, TOOLS / "gen_hle_nids.py", "--all", "--out", RECOMP / "ppu_hle_nids.cpp"])


def step_spu_lift(force: bool) -> None:
    if done(SPU / "spu_workloads.c") and not force:
        print("SPU workloads already lifted")
        return
    log("7/8 lifting SPU images + workload registry")
    SPU.mkdir(parents=True, exist_ok=True)
    run([PY, TOOLS / "build_spu_workloads.py",
         "--images", ANALYSIS / "spu_eboot",
         "--lifted", SPU,
         "--out", SPU / "spu_workloads.c",
         "--register-fn", "bakugan_spu_register_all",
         "--title", "bakugan",
         "--constructor"] + (["--relift"] if force else []))


FUNCS = {
    "patch": step_patch,
    "extract": step_extract,
    "decrypt": step_decrypt,
    "load": step_load,
    "spu-images": step_spu_images,
    "lift": step_lift,
    "hle-nids": step_hle_nids,
    "spu-lift": step_spu_lift,
}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--force", action="store_true", help="redo every step")
    ap.add_argument("--from", dest="start", choices=STEPS, help="start at this step")
    ap.add_argument("--only", choices=STEPS, help="run a single step")
    args = ap.parse_args()

    steps = STEPS if not args.start else STEPS[STEPS.index(args.start):]
    if args.only:
        steps = [args.only]

    for name in steps:
        FUNCS[name](args.force)

    print("\nPipeline complete.")
    print("Next: .venv\\Scripts\\python.exe tools\\build.py")
    return 0


if __name__ == "__main__":
    sys.exit(main())
