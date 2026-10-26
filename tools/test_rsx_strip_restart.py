"""Exercise the actual D3D12 indexed-strip uploader without a GPU."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "ps3recomp-src/tools"))
from msvc_env import environ

out = ROOT / "diagnostics/arena/strip-restart"
out.mkdir(parents=True, exist_ok=True)
source = (ROOT / "ps3recomp-src/libs/video/rsx_d3d12_backend.c").read_text(encoding="utf-8")
start = source.index("static u32 upload_strip_vp_indexed(")
end = source.index("\nstatic void d3d12_draw_arrays", start)
(out / "strip_upload.inc").write_text(source[start:end], encoding="utf-8")
cc = next(Path("C:/Program Files (x86)/Microsoft Visual Studio").glob(
    "*/BuildTools/VC/Tools/Llvm/x64/bin/clang-cl.exe"))
subprocess.run([str(cc), "/nologo", "/std:c++20", "/MD", "/Od", "/EHsc",
    "/I" + str(out), str(ROOT / "tools/tests/rsx_strip_restart_test.cpp"),
    "/Fe:strip_restart_test.exe"], cwd=out, env=environ(), check=True)
subprocess.run([str(out / "strip_restart_test.exe")], check=True, timeout=15)
