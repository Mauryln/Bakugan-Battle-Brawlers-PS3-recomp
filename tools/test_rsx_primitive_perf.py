"""Keep primitive timing totals exact and safe for unknown method values."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "ps3recomp-src/tools"))
from msvc_env import environ

out = ROOT / "diagnostics/arena/primitive-perf"
out.mkdir(parents=True, exist_ok=True)
source = (ROOT / "ps3recomp-src/libs/video/rsx_d3d12_backend.c").read_text(encoding="utf8")
start = source.index("static void perf_record_primitive_sample(")
(out / "primitive_perf.inc").write_text(source[start:source.index("\n}", start) + 2], encoding="utf8")
cc = next(Path("C:/Program Files (x86)/Microsoft Visual Studio").glob(
    "*/BuildTools/VC/Tools/Llvm/x64/bin/clang-cl.exe"))
subprocess.run([str(cc), "/nologo", "/std:c++20", "/MD", "/O2", "/EHsc",
    "/I" + str(out), str(ROOT / "tools/tests/rsx_primitive_perf_test.cpp"),
    "/Fe:primitive_perf_test.exe"], cwd=out, env=environ(), check=True)
subprocess.run([str(out / "primitive_perf_test.exe")], check=True, timeout=10)
