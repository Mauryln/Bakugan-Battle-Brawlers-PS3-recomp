"""Compile the real slot-name filter without booting the guest runtime."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", default="clang-cl")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    module = root / "ps3recomp-src/libs/system"
    source = (module / "cellSaveData.c").read_text(encoding="utf-8")
    function = re.search(r"^static int savedata_dir_matches\([^\n]*\n\{.*?^\}", source, re.M | re.S)
    header = (module / "cellSaveData.h").read_text(encoding="utf-8")
    size = re.search(r"^#define CELL_SAVEDATA_DIRNAME_SIZE\s+(\d+)", header, re.M)
    if not function or not size:
        raise RuntimeError("Cannot locate the real slot filter and its name limit")
    with tempfile.TemporaryDirectory(prefix="bakugan-save-filter-") as directory:
        work = Path(directory)
        (work / "savedata_dir_matches.inc").write_text(
            f"#define CELL_SAVEDATA_DIRNAME_SIZE {size[1]}\n" + function[0], encoding="utf-8")
        subprocess.run([args.compiler, "/nologo", "/O2", "/D_CRT_SECURE_NO_WARNINGS",
                        "/I" + str(work), str(root / "tools/tests/savedata_dir_name_test.c"),
                        "/Fe" + str(work / "test.exe"), "/Fo" + str(work / "test.obj")],
                       cwd=work, check=True)
        subprocess.run([str(work / "test.exe")], check=True)


if __name__ == "__main__":
    main()
