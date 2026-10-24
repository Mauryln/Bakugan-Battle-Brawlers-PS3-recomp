#!/usr/bin/env python
"""Configure and build the Bakugan port with clang-cl + Ninja.

    .venv\\Scripts\\python.exe tools\\build.py             # configure + build
    .venv\\Scripts\\python.exe tools\\build.py --clean     # wipe build/ first
    .venv\\Scripts\\python.exe tools\\build.py --debug     # -O0 + symbols

clang-cl needs the MSVC headers and import libraries, so the environment is
taken from vcvars64.bat through the toolkit's tools/msvc_env.py. CMake and
Ninja come from the project venv, which keeps the build independent of what is
on PATH.
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PORT = ROOT / "bakugan-recomp"
BUILD = PORT / "build"
TOOLKIT_TOOLS = ROOT / "ps3recomp-src" / "tools"
VENV_SCRIPTS = ROOT / ".venv" / "Scripts"

sys.path.insert(0, str(TOOLKIT_TOOLS))
from msvc_env import environ, find_vcvars  # noqa: E402


def find_tool(name: str) -> str:
    for cand in (VENV_SCRIPTS / f"{name}.exe", VENV_SCRIPTS / name):
        if cand.exists():
            return str(cand)
    found = shutil.which(name)
    if not found:
        sys.exit(f"{name} not found (pip install cmake ninja, or add it to PATH)")
    return found


def find_clang_cl(vcvars: str | None) -> str | None:
    """clang-cl ships inside VS (VC\\Tools\\Llvm\\x64\\bin) but vcvars does not
    always put it on PATH, and the LLVM installer path is a different one."""
    cands = []
    if vcvars:
        vs = Path(vcvars).parents[3]              # .../Microsoft Visual Studio/2022/BuildTools
        cands += [vs / "VC" / "Tools" / "Llvm" / "x64" / "bin" / "clang-cl.exe",
                  vs / "VC" / "Tools" / "Llvm" / "bin" / "clang-cl.exe"]
    cands.append(Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "LLVM" / "bin" / "clang-cl.exe")
    for c in cands:
        if c.exists():
            return str(c)
    found = shutil.which("clang-cl")
    return found


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--clean", action="store_true", help="delete build/ first")
    ap.add_argument("--debug", action="store_true", help="Debug build")
    ap.add_argument("--target", default=None, help="build a single target")
    ap.add_argument("--jobs", type=int, default=min(8, os.cpu_count() or 4),
                    help="parallel compile jobs (default: min(8, cpus); the lifted "
                         "translation units are ~600k lines each and need 1-2 GB)")
    args = ap.parse_args()

    cmake = find_tool("cmake")
    ninja = find_tool("ninja")

    env = environ()
    env["PATH"] = os.pathsep.join([str(VENV_SCRIPTS), env.get("PATH", "")])

    vcvars = find_vcvars()
    if os.name == "nt" and not vcvars:
        print("warning: vcvars64.bat not found -- clang-cl will not find the "
              "Windows SDK. Install Visual Studio Build Tools with the C++ "
              "workload.", file=sys.stderr)
    if os.name == "nt":
        clang = find_clang_cl(vcvars)
        if not clang:
            sys.exit("clang-cl not found -- install the 'C++ Clang Compiler for "
                     "Windows' component of VS Build Tools")
        env["PATH"] = os.pathsep.join([str(Path(clang).parent), env["PATH"]])
        print(f"clang-cl: {clang}")
    env.setdefault("CMAKE_GENERATOR", "Ninja")

    if args.clean and BUILD.exists():
        print(f"removing {BUILD}")
        shutil.rmtree(BUILD)

    cfg = [cmake, "-S", str(PORT), "-B", str(BUILD), "-G", "Ninja",
           f"-DCMAKE_MAKE_PROGRAM={ninja}",
           f"-DCMAKE_BUILD_TYPE={'Debug' if args.debug else 'Release'}"]
    if os.name == "nt":
        cfg += ["-DCMAKE_C_COMPILER=clang-cl", "-DCMAKE_CXX_COMPILER=clang-cl"]
    print("+ " + " ".join(cfg))
    subprocess.run(cfg, check=True, env=env)

    build = [cmake, "--build", str(BUILD), "--parallel", str(args.jobs)]
    if args.target:
        build += ["--target", args.target]
    print("+ " + " ".join(build))
    subprocess.run(build, check=True, env=env)

    exe = BUILD / ("BakuganRecomp.exe" if os.name == "nt" else "BakuganRecomp")
    if exe.exists():
        print(f"\nbuilt: {exe}")
        print(f"run:   tools\\run.ps1   (or PS3_VFS_ROOT={ROOT / 'game'} {exe} "
              f"{ROOT / 'game' / 'PS3_GAME' / 'USRDIR' / 'EBOOT.elf'})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
