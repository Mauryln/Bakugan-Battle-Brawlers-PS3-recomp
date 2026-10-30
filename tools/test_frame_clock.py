"""Verify a long rendering stall cannot create an unbounded tick replay debt."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "ps3recomp-src/tools"))
from msvc_env import environ

out = ROOT / "diagnostics/arena/frame-clock"
out.mkdir(parents=True, exist_ok=True)
source = (ROOT / "bakugan-recomp/main.cpp").read_text(encoding="utf-8")
helper = "static unsigned frame_clock_due_ticks("
if helper in source:
    start = source.index(helper)
    body = source.index("{", start)
    depth, end = 1, body + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    extracted = source[start:end]
else:
    start = source.index("        int fired = 0;")
    end = source.index("\n\n", source.index("        if (fired >= 240)", start))
    extracted = """static unsigned frame_clock_due_ticks(unsigned long long now,
    unsigned long long* next)
{
    unsigned long long next_tick = *next;
    int rsx_ok = 0;
    unsigned last_flip = 0;
""" + source[start:end] + "\n    *next = next_tick; return (unsigned)fired;\n}\n"
(out / "frame_clock.inc").write_text(extracted, encoding="utf-8")
cc = next(Path("C:/Program Files (x86)/Microsoft Visual Studio").glob(
    "*/BuildTools/VC/Tools/Llvm/x64/bin/clang-cl.exe"))
subprocess.run([str(cc), "/nologo", "/std:c++20", "/MD", "/O2", "/EHsc",
    "/I" + str(out), str(ROOT / "tools/tests/frame_clock_test.cpp"),
    "/Fe:frame_clock_test.exe"], cwd=out, env=environ(), check=True)
subprocess.run([str(out / "frame_clock_test.exe")], check=True, timeout=15)
