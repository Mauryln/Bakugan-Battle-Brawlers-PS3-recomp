"""Verify per-draw constant attribute planning preserves canonical vertex values."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "ps3recomp-src/tools"))
from msvc_env import environ

out = ROOT / "diagnostics/arena/vertex-fetch-cache"
out.mkdir(parents=True, exist_ok=True)
source = (ROOT / "ps3recomp-src/libs/video/rsx_d3d12_backend.c").read_text(encoding="utf-8")
parts = []
for name in ["static void vp_prepare_vertex_fetch(","static void vp_fetch_vertex("]:
    if name in source:
        start = source.index(name)
        end = source.index("\n}", start) + 2
        parts.append(source[start:end])
extracted = "\n".join(parts)
(out / "vertex_fetch_cache.inc").write_text(extracted, encoding="utf-8")
cc = next(Path("C:/Program Files (x86)/Microsoft Visual Studio").glob(
    "*/BuildTools/VC/Tools/Llvm/x64/bin/clang-cl.exe"))
subprocess.run([str(cc), "/nologo", "/std:c++20", "/MD", "/O2", "/EHsc",
    "/I" + str(out), str(ROOT / "tools/tests/rsx_vertex_fetch_cache_test.cpp"),
    "/Fe:vertex_fetch_cache_test.exe"], cwd=out, env=environ(), check=True)
subprocess.run([str(out / "vertex_fetch_cache_test.exe")], check=True, timeout=15)
