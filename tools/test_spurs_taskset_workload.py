"""Compile the real taskset creation path and check its guest workload lookup."""
from pathlib import Path
import argparse
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parent.parent
    sdk = root / "ps3recomp-src"
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", default="clang-cl")
    compiler = parser.parse_args().compiler
    with tempfile.TemporaryDirectory(prefix="bakugan-taskset-") as directory:
        work = Path(directory)
        args = [str(compiler), "/nologo", "/O2", "/MD", "/Gy", "/D_CRT_SECURE_NO_WARNINGS"]
        args += ["/I" + str(sdk / p) for p in
                 (".", "include", "runtime/spu", "runtime/ppu", "runtime/memory", "runtime/syscalls")]
        args += [str(root / "tools/tests/spurs_taskset_workload_test.c"),
                 str(sdk / "runtime/host/host_stubs.c"),
                 "/Fe" + str(work / "test.exe"),
                 "/link", "/OPT:REF",
                 str(root / "bakugan-recomp/build/ps3recomp_sdk/ps3recomp_runtime.lib"),
                 "user32.lib", "winmm.lib", "ws2_32.lib", "bcrypt.lib"]
        subprocess.run(args, cwd=work, check=True)
        subprocess.run([str(work / "test.exe")], check=True)


if __name__ == "__main__":
    main()
