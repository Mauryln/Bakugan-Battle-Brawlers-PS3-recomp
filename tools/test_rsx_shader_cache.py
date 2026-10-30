"""Check the backend's disk cache identity against final shader source variants."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "ps3recomp-src/tools"))
from msvc_env import environ

out = ROOT / "diagnostics/arena/shader-cache"
out.mkdir(parents=True, exist_ok=True)
source = (ROOT / "ps3recomp-src/libs/video/rsx_d3d12_backend.c").read_text(encoding="utf-8")
helper = "static void fp_shader_cache_path("
if helper in source:
    start = source.index(helper)
    body = source.index("{", start)
    depth = 1
    end = body + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    extracted = source[start:end]
else:
    # Run the current backend's actual path construction to reproduce the bug.
    start = source.index("    snprintf(ps_cache_path,")
    end = source.index(";", start) + 1
    extracted = """static void fp_shader_cache_path(char* path, size_t size, const char* hlsl)
{
    unsigned uhash = 0x12345678, alpha_ctl = 0;
    int exp32 = 1;
    char ps_cache_path[520];
""" + source[start:end] + "\n    snprintf(path, size, \"%s\", ps_cache_path);\n}\n"
(out / "shader_cache_path.inc").write_text(extracted, encoding="utf-8")
cc = next(Path("C:/Program Files (x86)/Microsoft Visual Studio").glob(
    "*/BuildTools/VC/Tools/Llvm/x64/bin/clang-cl.exe"))
subprocess.run([str(cc), "/nologo", "/std:c++20", "/MD", "/O2", "/EHsc",
    "/I" + str(out), str(ROOT / "tools/tests/rsx_shader_cache_test.cpp"),
    "/Fe:shader_cache_test.exe"], cwd=out, env=environ(), check=True)
subprocess.run([str(out / "shader_cache_test.exe")], check=True, timeout=15)
