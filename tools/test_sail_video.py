"""Exercise port SAIL hooks using the real MP4 decoder and a guest-memory fixture."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "ps3recomp-src/tools"))
from msvc_env import environ


def main():
    out = ROOT / "diagnostics/video"
    out.mkdir(parents=True, exist_ok=True)
    env = environ()
    compiler = next(Path(r"C:/Program Files (x86)/Microsoft Visual Studio").glob(
        "*/BuildTools/VC/Tools/Llvm/x64/bin/clang-cl.exe"))
    with tempfile.TemporaryDirectory() as temp:
        Path(temp, "ppu_recomp.h").write_text('''#include "ppu_context.h"
extern "C" {
extern uint8_t* vm_base;
uint8_t vm_read8(uint64_t);
uint32_t vm_read32(uint64_t);
void vm_write8(uint64_t,uint8_t);
void vm_write16(uint64_t,uint16_t);
void vm_write32(uint64_t,uint32_t);
void vm_write64(uint64_t,uint64_t);
}
''')
        includes = ["/I" + str(p) for p in [Path(temp), ROOT / "bakugan-recomp",
            ROOT / "ps3recomp-src/include", ROOT / "ps3recomp-src/libs/codec", ROOT / "ps3recomp-src/runtime/ppu"]]
        subprocess.run([str(compiler), "/nologo", "/TC", "/std:c17", "/MD", "/c", *includes,
            str(ROOT / "ps3recomp-src/libs/codec/cellSail.c"), "/Fo:sail_video_test.obj"], cwd=out, env=env, check=True)
        exe = out / "sail_video_test.exe"
        subprocess.run([str(compiler), "/nologo", "/std:c++20", "/EHsc", "/MD", "/O2", *includes,
            str(ROOT / "tools/tests/sail_video_test.cpp"), str(ROOT / "bakugan-recomp/sail_video.cpp"),
            str(ROOT / "bakugan-recomp/video_decoder.cpp"), str(ROOT / "bakugan-recomp/movie_video.cpp"), str(out / "sail_video_test.obj"), "/Fe:" + str(exe),
            "/link", "mfplat.lib", "mfreadwrite.lib", "mfuuid.lib", "ole32.lib"], cwd=out, env=env, check=True)
        subprocess.run([str(exe), str(ROOT / "game")], check=True, timeout=25)


if __name__ == "__main__":
    main()
