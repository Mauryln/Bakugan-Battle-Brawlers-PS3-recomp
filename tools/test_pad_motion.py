"""Exercise Sixaxis launch pulses with a deterministic clock, without Windows input."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "ps3recomp-src/tools"))
from msvc_env import environ

out = ROOT / "diagnostics/arena/pad-motion"
out.mkdir(parents=True, exist_ok=True)
source = (ROOT / "ps3recomp-src/libs/input/cellPad.c").read_text(encoding="utf-8")
helper = "static void pad_forward_motion("
if helper in source:
    start = source.index(helper)
    body = source.index("{", start)
    depth, end = 1, body + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    extracted = source[start:end]
    if "static void pad_direction_motion(" in source:
        ds = source.index("static void pad_direction_motion(")
        de = source.index("\n}", ds) + 2
        extracted += "\n" + source[ds:de]
else:
    # Current SENSOR_ON behavior always reports a stationary controller.
    start = source.index("        data->button[CELL_PAD_BTN_OFFSET_SENSOR_X] = 512;")
    end = source.index(";", source.index("data->button[CELL_PAD_BTN_OFFSET_SENSOR_G]", start)) + 1
    assignments = source[start:end]
    for index, axis in enumerate("XYZG"):
        assignments = assignments.replace(f"data->button[CELL_PAD_BTN_OFFSET_SENSOR_{axis}]", f"sensors[{index}]")
    extracted = """static void pad_forward_motion(u16* sensors, int pressed, u64 now_ms,
    PadMotionPulse* pulse)
{
""" + assignments + "\n}\n"
(out / "pad_motion.inc").write_text(extracted, encoding="utf-8")
cc = next(Path("C:/Program Files (x86)/Microsoft Visual Studio").glob(
    "*/BuildTools/VC/Tools/Llvm/x64/bin/clang-cl.exe"))
subprocess.run([str(cc), "/nologo", "/std:c++20", "/MD", "/O2", "/EHsc",
    "/I" + str(out), str(ROOT / "tools/tests/pad_motion_test.cpp"),
    "/Fe:pad_motion_test.exe"], cwd=out, env=environ(), check=True)
subprocess.run([str(out / "pad_motion_test.exe")], check=True, timeout=15)
