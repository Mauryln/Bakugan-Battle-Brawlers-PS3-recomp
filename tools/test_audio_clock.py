"""Run the real Windows mixer loop against a deterministic clock and an empty device."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "ps3recomp-src/tools"))
from msvc_env import environ

out = ROOT / "diagnostics/arena/audio-clock"
out.mkdir(parents=True, exist_ok=True)
source = (ROOT / "ps3recomp-src/libs/audio/cellAudio.c").read_text(encoding="utf-8")
start = source.index("static unsigned __stdcall audio_mix_thread_func(")
end = source.index("\n#else\nstatic void* audio_mix_thread_func", start)
(out / "audio_clock_entry.inc").write_text(source[start:end])
cc = next(Path("C:/Program Files (x86)/Microsoft Visual Studio").glob(
    "*/BuildTools/VC/Tools/Llvm/x64/bin/clang-cl.exe"))
env = environ()
for key in ["AUDIO_PEAK", "AUDIO_RATE", "AUDIO_WAV"]:
    env.pop(key, None)
subprocess.run([str(cc), "/nologo", "/std:c17", "/MD", "/O2", "/w",
    "/I" + str(ROOT / "ps3recomp-src/include"),
    "/I" + str(ROOT / "ps3recomp-src/libs/audio"), "/I" + str(out),
    str(ROOT / "tools/tests/audio_clock_test.c"), "/Fe:audio_clock_test.exe"],
    cwd=out, env=env, check=True)
subprocess.run([str(out / "audio_clock_test.exe")], env=env, check=True, timeout=10)
