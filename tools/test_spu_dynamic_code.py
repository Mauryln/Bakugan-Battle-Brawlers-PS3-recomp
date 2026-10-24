"""Check dynamic high-LS code rejoins and nested returns in the real SPU runtime."""
from pathlib import Path
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "ps3recomp-src/tools"))
from msvc_env import environ

out = ROOT / "diagnostics/arena/dynamic-tests"
out.mkdir(parents=True, exist_ok=True)
cc = next(Path("C:/Program Files (x86)/Microsoft Visual Studio").glob(
    "*/BuildTools/VC/Tools/Llvm/x64/bin/clang-cl.exe"))
units = ["spu_channels", "spu_drain", "spu_lockstep", "spu_coherency", "spu_workload",
         "spurs_policy", "spurs_job", "spu_tsp_weak", "spu_vm_pagemap",
         "spurs_policy_blob_weak", "spu_interp"]
includes = ["/I" + str(ROOT / p) for p in [
    "ps3recomp-src/include", "ps3recomp-src/runtime/spu", "ps3recomp-src/runtime/platform"]]
env = environ()
subprocess.run([str(cc), "/nologo", "/std:c17", "/MD", "/O2", "/Gy", "/w", *includes,
    str(ROOT / "tools/tests/spu_dynamic_code_test.c"),
    *[str(ROOT / "ps3recomp-src/runtime/spu" / f"{unit}.c") for unit in units],
    "/Fe:spu_dynamic_code_test.exe", "/link", "/OPT:REF", "user32.lib", "winmm.lib"],
    cwd=out, env=env, check=True)
env["SPU_INTERP_UNLIFTED"] = "1"
for mode in ["top", "nested-return", "nested-return-unlifted", "nested-calls"]:
    subprocess.run([str(out / "spu_dynamic_code_test.exe"), mode], env=env,
                   check=True, timeout=10)
