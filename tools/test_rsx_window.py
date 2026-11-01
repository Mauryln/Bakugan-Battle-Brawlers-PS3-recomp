"""Render actual translated WPOS and verify guest shader-window coordinates."""
from pathlib import Path
import subprocess, sys
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "ps3recomp-src/tools"))
from msvc_env import environ
out = ROOT / "diagnostics/arena/window"
out.mkdir(parents=True, exist_ok=True)
backend = (ROOT / "ps3recomp-src/libs/video/rsx_d3d12_backend.c").read_text()
commands = (ROOT / "ps3recomp-src/libs/video/rsx_commands.c").read_text()
def function(name):
    start = backend.index(name)
    return backend[start:backend.index("\n}", start) + 2]
parts = [function(n) for n in ["static void hlsl_replace_all(", "static int fp_decompile_for_draw("]]
if "static void rsx_window_transform(" in backend:
    parts.append(function("static void rsx_window_transform("))
else:
    parts.append("static void rsx_window_transform(u32, float* p) {p[0]=1; p[1]=p[2]=0;}")
(out / "window.inc").write_text("\n".join(parts))
needle = "    if (method == NV4097_SET_SHADER_WINDOW) {"
if needle in commands:
    start = commands.index(needle)
    decoded = commands[start:commands.index("\n    }", start)+6]
else:
    decoded = ""
(out / "window_commands.inc").write_text(decoded)
cc = next(Path("C:/Program Files (x86)/Microsoft Visual Studio").glob("*/BuildTools/VC/Tools/Llvm/x64/bin/clang-cl.exe"))
subprocess.run([str(cc), "/nologo", "/std:c++20", "/MD", "/O2", "/EHsc", "/I"+str(out),
    "/I"+str(ROOT/"ps3recomp-src/include"), "/I"+str(ROOT/"ps3recomp-src/libs/video"),
    str(ROOT/"tools/tests/rsx_window_test.cpp"), str(ROOT/"ps3recomp-src/libs/video/rsx_fp_decompiler.c"),
    "/Fe:window_test.exe", "/link", "d3d11.lib", "d3dcompiler.lib"], cwd=out, env=environ(), check=True)
subprocess.run([str(out/"window_test.exe")], check=True, timeout=20)
assert "state->shader_window = data;" in commands
assert "rsx_window_transform(st->shader_window" in backend
