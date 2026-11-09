"""Check the cellAudio notification API without a game or audio device."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", default="clang-cl")
    compiler = parser.parse_args().compiler
    root = Path(__file__).resolve().parent.parent
    source = (root / "ps3recomp-src/libs/audio/cellAudio.c").read_text(encoding="utf-8")
    functions = []
    for name in ("audio_notify_event_queues", "cellAudioCreateNotifyEventQueue",
                 "cellAudioSetNotifyEventQueue", "cellAudioRemoveNotifyEventQueue"):
        match = re.search(rf"^(?:static void|s32) {name}\([^\n]*\n\{{.*?^\}}",
                          source, re.M | re.S)
        if not match:
            raise RuntimeError(f"Missing audio function: {name}")
        functions.append(match[0])
    with tempfile.TemporaryDirectory(prefix="bakugan-audio-notify-") as directory:
        work = Path(directory)
        (work / "audio_notify.inc").write_text("\n\n".join(functions), encoding="utf-8")
        subprocess.run([compiler, "/nologo", "/O2", "/I" + str(work),
                        str(root / "tools/tests/audio_notify_queue_test.c"),
                        "/Fe" + str(work / "test.exe")], cwd=work, check=True)
        subprocess.run([str(work / "test.exe")], check=True)


if __name__ == "__main__":
    main()
