"""Exercise the actual LV2 SPU queue binding and stop service."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "ps3recomp-src/tools"))
from msvc_env import environ

out = ROOT / "diagnostics/arena/tests"
out.mkdir(parents=True, exist_ok=True)
source = (ROOT / "ps3recomp-src/runtime/syscalls/lv2_register.c").read_text(encoding="utf-8")
start = source.index("static int64_t spu_queue_result(")
end = source.index("static int64_t sys_spu_thread_group_disconnect_event_handler(", start)
channels = (ROOT / "ps3recomp-src/runtime/spu/spu_channels.c").read_text(encoding="utf-8")
stop_start = channels.index("void spu_stop(")
stop_end = channels.index("\n/* Run a lifted SPU entry", stop_start)
(out / "receive_entry.inc").write_text(source[start:end] + channels[stop_start:stop_end])
cc = next(Path("C:/Program Files (x86)/Microsoft Visual Studio").glob(
    "*/BuildTools/VC/Tools/Llvm/x64/bin/clang-cl.exe"))
includes = ["/I" + str(ROOT / p) for p in [
    "ps3recomp-src/include", "ps3recomp-src/runtime", "ps3recomp-src/runtime/spu",
    "ps3recomp-src/runtime/syscalls", "ps3recomp-src/libs/video", "diagnostics/arena/tests"]]
subprocess.run([str(cc), "/nologo", "/TC", "/std:c17", "/MD", "/O2", "/Gy",
    *includes, str(ROOT / "tools/tests/spu_receive_event_test.c"),
    "/Fe:spu_receive_event_test.exe", "/link", "/OPT:REF"],
    cwd=out, env=environ(), check=True)
subprocess.run([str(out / "spu_receive_event_test.exe")], check=True, timeout=10)
