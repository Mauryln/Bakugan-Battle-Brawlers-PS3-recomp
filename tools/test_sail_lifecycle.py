"""Windows regression for the real SAIL C implementation and guest callbacks."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "ps3recomp-src/tools"))
from msvc_env import environ


def main():
    out = ROOT / "diagnostics/video"
    out.mkdir(parents=True, exist_ok=True)
    env = environ()
    compiler = next(Path(r"C:/Program Files (x86)/Microsoft Visual Studio").glob(
        "*/BuildTools/VC/Tools/Llvm/x64/bin/clang-cl.exe"))
    includes = ["/I" + str(ROOT / p) for p in [
        "ps3recomp-src/include", "ps3recomp-src/libs/codec", "ps3recomp-src/runtime/ppu"]]
    source = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "ps3recomp-src/libs/codec/cellSail.c"
    subprocess.run([str(compiler), "/nologo", "/TC", "/std:c17", "/MD", "/c", *includes,
                    str(source), "/Fo:sail_test.obj"], cwd=out, env=env, check=True)
    exe = out / "sail_lifecycle_test.exe"
    subprocess.run([str(compiler), "/nologo", "/std:c++20", "/EHsc", "/MD", *includes,
                    str(ROOT / "tools/tests/sail_lifecycle_test.cpp"), str(out / "sail_test.obj"),
                    "/Fe:" + str(exe)], cwd=out, env=env, check=True)
    subprocess.run([str(exe)], check=True, timeout=15)


if __name__ == "__main__":
    main()
