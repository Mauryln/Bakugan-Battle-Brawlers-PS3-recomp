"""A queued FIFO flip ends a frame even when backlogged, and presents once."""
from pathlib import Path
import subprocess, sys, os
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "ps3recomp-src/tools"))
from msvc_env import environ
out = ROOT / "diagnostics/arena/frame-flips"
out.mkdir(parents=True, exist_ok=True)
gcm = (ROOT / "ps3recomp-src/libs/video/cellGcmSys.c").read_text()
main = (ROOT / "bakugan-recomp/main.cpp").read_text()
name = "static int gcm_fifo_stop_at_flip("
if name in gcm:
    a = gcm.index(name); helper = gcm[a:gcm.index("\n}", a)+2]
else:
    helper = "static int gcm_fifo_stop_at_flip(u32 backlog) { return backlog < GCM_FIFO_CATCHUP_BYTES; }"
a = main.index("            if (fc != last_flip) {")
b = main.index("            } else if", a)+13
(out / "frame_flip.inc").write_text(helper)
(out / "ticker_flip.inc").write_text(main[a:b])
cc = next(Path("C:/Program Files (x86)/Microsoft Visual Studio").glob("*/BuildTools/VC/Tools/Llvm/x64/bin/clang-cl.exe"))
subprocess.run([str(cc), "/nologo", "/std:c++20", "/MD", "/O2", "/EHsc", "/I"+str(out),
    str(ROOT/"tools/tests/gcm_frame_boundary_test.cpp"), "/Fe:frame_flip_test.exe"],cwd=out,env=environ(),check=True)
for strict in ["1", "0"]:
    env = os.environ.copy(); env["GCM_FULL_FRAMES"] = strict
    subprocess.run([str(out/"frame_flip_test.exe"), strict], env=env,check=True,timeout=10)
assert 'if (gcm_fifo_stop_at_flip(gcm_fifo_backlog(put, s_fifo_getoff)))' in gcm
assert 'if (!getenv("GCM_FULL_FRAMES")) _putenv_s("GCM_FULL_FRAMES", "1");' in main
