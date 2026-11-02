"""Exercise packet-local indexed vertex reuse against uncached decoding."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "ps3recomp-src/tools"))
from msvc_env import environ

source = (ROOT / "ps3recomp-src/libs/video/rsx_d3d12_backend.c").read_text(encoding="utf8")
out = ROOT / "diagnostics/arena/index-decode-cache"
out.mkdir(parents=True, exist_ok=True)
start = source.index("static u32 vp_fetch_triangle_vertices(")
(out / "index_decode.inc").write_text(source[start:source.index("\n}", start) + 2], encoding="utf8")
cc = next(Path("C:/Program Files (x86)/Microsoft Visual Studio").glob(
    "*/BuildTools/VC/Tools/Llvm/x64/bin/clang-cl.exe"))
subprocess.run([str(cc), "/nologo", "/std:c++20", "/MD", "/O2", "/EHsc", "/w",
    "/I" + str(out), str(ROOT / "tools/tests/rsx_index_decode_cache_test.cpp"),
    "/Fe:index_decode_test.exe"], cwd=out, env=environ(), check=True)
env = environ()
env.pop("VP_INDEX_CACHE", None)
subprocess.run([str(out / "index_decode_test.exe")], env=env, check=True, timeout=10)
