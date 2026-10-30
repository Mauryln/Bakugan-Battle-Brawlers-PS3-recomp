"""Exercise actual descriptor source selection before texture upload fallback."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "ps3recomp-src/tools"))
from msvc_env import environ

out = ROOT / "diagnostics/arena/texture-source"
out.mkdir(parents=True, exist_ok=True)
source = (ROOT / "ps3recomp-src/libs/video/rsx_d3d12_backend.c").read_text(encoding="utf-8")
start = source.index("            dr->tex_rt[_u] = -1;")
end = source.index("                u32 _bf = dr->tex[_u].fmt", start)
(out / "texture_source.inc").write_text(source[start:end], encoding="utf-8")
cc = next(Path("C:/Program Files (x86)/Microsoft Visual Studio").glob(
    "*/BuildTools/VC/Tools/Llvm/x64/bin/clang-cl.exe"))
subprocess.run([str(cc), "/nologo", "/std:c++20", "/MD", "/O2", "/EHsc",
    "/I" + str(out), str(ROOT / "tools/tests/rsx_texture_source_test.cpp"),
    "/Fe:texture_source_test.exe"], cwd=out, env=environ(), check=True)
subprocess.run([str(out / "texture_source_test.exe")], check=True, timeout=15)
