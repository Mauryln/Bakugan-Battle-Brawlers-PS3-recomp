"""Build and exercise the Windows decoder against a local, user-owned MP4."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "ps3recomp-src/tools"))
from msvc_env import environ


def main():
    output = ROOT / "diagnostics/video"
    output.mkdir(parents=True, exist_ok=True)
    exe = output / "video_decoder_test.exe"
    env = environ()
    compiler = next(Path(r"C:/Program Files (x86)/Microsoft Visual Studio").glob(
        "*/BuildTools/VC/Tools/Llvm/x64/bin/clang-cl.exe"))
    subprocess.run([
        str(compiler), "/nologo", "/std:c++20", "/EHsc", "/MD", "/O2",
        "/I" + str(ROOT / "bakugan-recomp"),
        str(ROOT / "tools/tests/video_decoder_test.cpp"),
        str(ROOT / "bakugan-recomp/video_decoder.cpp"),
        "/Fe:" + str(exe), "/link", "mfplat.lib", "mfreadwrite.lib", "mfuuid.lib", "ole32.lib",
    ], cwd=output, env=env, check=True)
    movie = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "game/PS3_GAME/USRDIR/MOVIE/CLIENT_16_9.MP4"
    subprocess.run([str(exe), str(movie.resolve())], check=True, timeout=120)


if __name__ == "__main__":
    main()
