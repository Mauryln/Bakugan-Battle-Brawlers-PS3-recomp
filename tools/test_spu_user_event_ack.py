"""Exercise the actual LV2 delivery function without a running game."""
from pathlib import Path
import argparse
import re
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parent.parent
    sdk = root / "ps3recomp-src"
    source = (sdk / "runtime/syscalls/lv2_register.c").read_text(encoding="utf-8")
    function = re.search(r"^static int spu_deliver_user_event\([^\n]*\n\{.*?^\}", source, re.M | re.S)
    if not function:
        raise RuntimeError("Cannot locate the LV2 user-event delivery function")
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", default="clang-cl")
    compiler = parser.parse_args().compiler
    with tempfile.TemporaryDirectory(prefix="bakugan-spu-event-") as directory:
        work = Path(directory)
        (work / "spu_deliver_user_event.inc").write_text(function[0], encoding="utf-8")
        args = [compiler, "/nologo", "/O2", "/D_CRT_SECURE_NO_WARNINGS"]
        args += ["/I" + str(p) for p in (work, sdk / "include", sdk / "runtime/spu", sdk / "runtime/ppu")]
        args += [str(root / "tools/tests/spu_user_event_ack_test.c"), "/Fe" + str(work / "test.exe")]
        subprocess.run(args, cwd=work, check=True)
        subprocess.run([str(work / "test.exe")], check=True)


if __name__ == "__main__":
    main()
