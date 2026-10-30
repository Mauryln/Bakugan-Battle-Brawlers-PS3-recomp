"""Exercise guest polygon-offset command decoding and the backend depth-bias conversion."""
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "ps3recomp-src/tools"))
from msvc_env import environ

out = ROOT / "diagnostics/arena/polygon-offset"
out.mkdir(parents=True, exist_ok=True)
backend = (ROOT / "ps3recomp-src/libs/video/rsx_d3d12_backend.c").read_text(encoding="utf-8")
commands = (ROOT / "ps3recomp-src/libs/video/rsx_commands.c").read_text(encoding="utf-8")
header = (ROOT / "ps3recomp-src/libs/video/rsx_commands.h").read_text(encoding="utf-8")


def block(source: str, start: int) -> str:
    body = source.index("{", start)
    depth, end = 1, body + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


helper = "static void rsx_polygon_offset("
if helper in backend:
    extracted = block(backend, backend.index(helper))
else:
    extracted = """static void rsx_polygon_offset(const rsx_state* st, int* bias, float* slope)
{ *bias = 0; *slope = 0; }
"""
(out / "polygon_offset.inc").write_text(extracted, encoding="utf-8")
decoded = ""
for match in re.finditer(r"\bif\s*\(method\s*==\s*NV4097_[A-Z_]+\)", commands):
    candidate = block(commands, match.start())
    if "state->polygon_offset_" in candidate:
        decoded += candidate + "\n"
(out / "polygon_commands.inc").write_text(decoded, encoding="utf-8")
if "polygon_offset_fill_enable" in header:
    types = '#include "rsx_commands.h"\n'
else:
    types = """typedef unsigned int u32;
typedef struct { int polygon_offset_fill_enable;
    float polygon_offset_scale, polygon_offset_bias; } rsx_state;
"""
(out / "polygon_types.inc").write_text(types, encoding="utf-8")
cc = next(Path("C:/Program Files (x86)/Microsoft Visual Studio").glob(
    "*/BuildTools/VC/Tools/Llvm/x64/bin/clang-cl.exe"))
subprocess.run([str(cc), "/nologo", "/std:c++20", "/MD", "/O2", "/EHsc",
    "/I" + str(out), "/I" + str(ROOT / "ps3recomp-src/include"),
    "/I" + str(ROOT / "ps3recomp-src/libs/video"),
    str(ROOT / "tools/tests/rsx_polygon_offset_test.cpp"),
    "/Fe:polygon_offset_test.exe"], cwd=out, env=environ(), check=True)
subprocess.run([str(out / "polygon_offset_test.exe")], check=True, timeout=15)
