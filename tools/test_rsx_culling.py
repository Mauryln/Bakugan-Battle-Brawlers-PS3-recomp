"""Exercise the actual RSX/D3D rasterizer winding conversion and diagnostics."""
from pathlib import Path
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "ps3recomp-src/tools"))
from msvc_env import environ

out = ROOT / "diagnostics/arena/culling"
out.mkdir(parents=True, exist_ok=True)
source = (ROOT / "ps3recomp-src/libs/video/rsx_d3d12_backend.c").read_text(encoding="utf-8")
start = source.index("    /* Guest face culling.")
end = source.index("    /* Blend per the guest", start)
(out / "culling.inc").write_text(source[start:end], encoding="utf-8")
cc = next(Path("C:/Program Files (x86)/Microsoft Visual Studio").glob(
    "*/BuildTools/VC/Tools/Llvm/x64/bin/clang-cl.exe"))
subprocess.run([str(cc), "/nologo", "/std:c++20", "/MD", "/O2", "/EHsc",
    "/I" + str(out), str(ROOT / "tools/tests/rsx_culling_test.cpp"),
    "/Fe:culling_test.exe"], cwd=out, env=environ(), check=True)
for mode in ("default", "flip", "off"):
    env = os.environ.copy()
    env.pop("CULL_OFF", None)
    env.pop("CULL_FLIP", None)
    if mode != "default":
        env["CULL_" + mode.upper()] = "1"
    subprocess.run([str(out / "culling_test.exe"), mode], env=env, check=True, timeout=15)
