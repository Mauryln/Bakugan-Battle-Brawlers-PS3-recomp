"""Exercise the real Windows guest sleep path with coarse and precise timers."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "ps3recomp-src/tools"))
from msvc_env import environ

out = ROOT / "diagnostics/arena/ppu-sleep"
out.mkdir(parents=True, exist_ok=True)
source = (ROOT / "ps3recomp-src/runtime/syscalls/sys_timer.c").read_text(encoding="utf-8")
start = source.index("    /* Use high-resolution sleep via waitable timer")
end = source.index("\n#else\n    if (usec > 0)", start)
(out / "ppu_sleep_entry.inc").write_text(source[start:end], encoding="utf-8")
cc = next(Path("C:/Program Files (x86)/Microsoft Visual Studio").glob(
    "*/BuildTools/VC/Tools/Llvm/x64/bin/clang-cl.exe"))
subprocess.run([str(cc), "/nologo", "/std:c17", "/MD", "/O2", "/w", "/I" + str(out),
    str(ROOT / "tools/tests/ppu_sleep_test.c"), "/Fe:ppu_sleep_test.exe"],
    cwd=out, env=environ(), check=True)
subprocess.run([str(out / "ppu_sleep_test.exe")], check=True, timeout=10)
