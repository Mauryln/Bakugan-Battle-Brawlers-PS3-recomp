"""Exercise the D3D12 backend's tiled depth bookkeeping without a GPU."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", default="clang-cl")
    compiler = parser.parse_args().compiler
    root = Path(__file__).resolve().parent.parent
    source = (root / "ps3recomp-src/libs/video/rsx_d3d12_backend.c").read_text(encoding="utf-8")
    end = source.index("} PackedDepthResolve;")
    start = source.rindex("typedef struct {", 0, end)
    end = source.index("/* Ensure an RT resource exists", end)
    with tempfile.TemporaryDirectory(prefix="bakugan-depth-resolve-") as directory:
        work = Path(directory)
        (work / "packed_depth_resolve.inc").write_text(source[start:end], encoding="utf-8")
        subprocess.run([compiler, "/nologo", "/O2", "/I" + str(work),
                        str(root / "tools/tests/packed_depth_resolve_test.c"),
                        "/Fe" + str(work / "test.exe")], cwd=work, check=True)
        subprocess.run([str(work / "test.exe")], check=True)


if __name__ == "__main__":
    main()
