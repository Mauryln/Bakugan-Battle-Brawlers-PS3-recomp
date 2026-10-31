"""Render near-opaque exports through the real generated UNORM and float alpha tests."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "ps3recomp-src/tools"))
from msvc_env import environ

out = ROOT / "diagnostics/arena/alpha-precision"
out.mkdir(parents=True, exist_ok=True)
source = (ROOT / "ps3recomp-src/libs/video/rsx_d3d12_backend.c").read_text(encoding="utf-8")
functions = []
for name in ("static void hlsl_replace_all(", "static int fp_apply_alpha_test_for_draw("):
    if name not in source:
        functions.append("static int fp_apply_alpha_test_for_draw(char* h, u32 size, u32 ctl, DXGI_FORMAT) { return rsx_fp_apply_alpha_test(h, size, 0x200u | ((ctl >> 8) & 7u), (ctl & 255u) / 255.0f); }")
        continue
    start = source.index(name)
    end = source.index("\n}", start) + 2
    functions.append(source[start:end])
(out / "alpha_draw.inc").write_text("\n".join(functions), encoding="utf-8")
cc = next(Path("C:/Program Files (x86)/Microsoft Visual Studio").glob(
    "*/BuildTools/VC/Tools/Llvm/x64/bin/clang-cl.exe"))
subprocess.run([str(cc), "/nologo", "/std:c++20", "/MD", "/O2", "/EHsc",
    "/I" + str(out), "/I" + str(ROOT / "ps3recomp-src/include"),
    "/I" + str(ROOT / "ps3recomp-src/libs/video"),
    str(ROOT / "tools/tests/rsx_alpha_precision_test.cpp"),
    str(ROOT / "ps3recomp-src/libs/video/rsx_fp_decompiler.c"),
    "/Fe:alpha_precision_test.exe", "/link", "d3d11.lib", "d3dcompiler.lib"],
    cwd=out, env=environ(), check=True)
subprocess.run([str(out / "alpha_precision_test.exe")], check=True, timeout=30)
