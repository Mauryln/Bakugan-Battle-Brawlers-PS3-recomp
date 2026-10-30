"""Reproduce a depth-only shadow pass incorrectly completing the visible frame."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "ps3recomp-src/tools"))
from msvc_env import environ

out = ROOT / "diagnostics/arena/present-gate"
out.mkdir(parents=True, exist_ok=True)
source = (ROOT / "ps3recomp-src/libs/video/rsx_d3d12_backend.c").read_text(encoding="utf-8")
helper = "static int rsx_draw_has_display_color("
if helper in source:
    start=source.index(helper); end=source.index("\n}",start)+2
    extracted=source[start:end]
else:
    extracted="static int rsx_draw_has_display_color(const D3D12DrawRecord* draw) { return !draw->is_clear && draw->rt_off == 0; }"
(out / "present_gate.inc").write_text(extracted, encoding="utf-8")
cc = next(Path("C:/Program Files (x86)/Microsoft Visual Studio").glob(
    "*/BuildTools/VC/Tools/Llvm/x64/bin/clang-cl.exe"))
subprocess.run([str(cc), "/nologo", "/std:c++20", "/MD", "/O2", "/EHsc",
    "/I" + str(out), str(ROOT / "tools/tests/rsx_present_gate_test.cpp"),
    "/Fe:present_gate_test.exe"], cwd=out, env=environ(), check=True)
subprocess.run([str(out / "present_gate_test.exe")], check=True, timeout=15)
