"""Verify backend translation keeps dynamic constants in its fixed b1 layout."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "ps3recomp-src/tools"))
from msvc_env import environ

out = ROOT / "diagnostics/arena/fp-draw"
out.mkdir(parents=True, exist_ok=True)
source = (ROOT / "ps3recomp-src/libs/video/rsx_d3d12_backend.c").read_text(encoding="utf-8")


def function(name: str) -> str:
    start = source.index(name)
    body = source.index("{", start)
    depth, end = 1, body + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


extracted = function("static void hlsl_replace_all(") + "\n"
if "static int fp_decompile_for_draw(" in source:
    extracted += function("static int fp_decompile_for_draw(")
else:
    # The backend currently calls the literal API directly; preserve that
    # behavior as a RED baseline until its integration helper is introduced.
    extracted += """static int fp_decompile_for_draw(const u8* ucode, u32 max_bytes,
    u32 ctrl, u32 cube_mask, char* hlsl, u32 out_size)
{
    return rsx_fp_decompile(ucode, max_bytes, ctrl, hlsl, out_size);
}
"""
(out / "fp_draw.inc").write_text(extracted, encoding="utf-8")
cc = next(Path("C:/Program Files (x86)/Microsoft Visual Studio").glob(
    "*/BuildTools/VC/Tools/Llvm/x64/bin/clang-cl.exe"))
subprocess.run([str(cc), "/nologo", "/std:c++20", "/MD", "/O2", "/EHsc",
    "/I" + str(out), "/I" + str(ROOT / "ps3recomp-src/include"),
    "/I" + str(ROOT / "ps3recomp-src/libs/video"),
    str(ROOT / "tools/tests/rsx_fp_draw_test.cpp"),
    str(ROOT / "ps3recomp-src/libs/video/rsx_fp_decompiler.c"),
    "/Fe:fp_draw_test.exe", "/link", "d3dcompiler.lib"],
    cwd=out, env=environ(), check=True)
subprocess.run([str(out / "fp_draw_test.exe")], check=True, timeout=15)
