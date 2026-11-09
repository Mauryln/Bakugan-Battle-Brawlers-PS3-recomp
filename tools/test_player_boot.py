"""Check player.ini through the real runner before loading any game data."""
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parent.parent
    exe = root / "bakugan-recomp/build/BakuganRecomp.exe"
    with tempfile.TemporaryDirectory(prefix="bakugan-player-boot-") as directory:
        work = Path(directory)
        config = work / "BakuganRecomp/player.ini"
        config.parent.mkdir()
        config.write_text(
            "[Player]\nVersion=1\nLanguage=es\n[Graphics]\n"
            "Output=1080p\nFSR=fsr1\nSharpness=0.8\nAnisotropy=8\n"
            "Window=windowed\nFPS=30\n[Keyboard]\n14=key:66\n", encoding="ascii")
        for overrides in ({}, {"PS3_OUTPUT": "720p", "PS3_FSR": "0", "PS3_LANG": "de"}):
            env = {k: v for k, v in os.environ.items()
                   if not k.startswith(("PS3_", "BAKUGAN_", "PAD_"))}
            env.update(LOCALAPPDATA=str(work), PS3_GAMEDATA_ROOT=str(work / "gamedata"),
                       PS3_VERBOSE="0", BAKUGAN_BOOT_REPORT=str(work / "boot.txt"))
            env.update(overrides)
            result = subprocess.run([str(exe), str(work / "missing.elf")],
                                    env=env, cwd=work, capture_output=True, timeout=20)
            assert result.returncode != 0, "The fixture must reject the missing ELF"
            report = dict(line.split("=", 1) for line in
                          (work / "boot.txt").read_text().splitlines() if "=" in line)
            expected = dict(PS3_OUTPUT="1080p", PS3_FSR="fsr1", PS3_LANG="es",
                            PS3_ANISO="8", PS3_WINDOW_MODE="windowed", PS3_FPS_CAP="30")
            expected.update(overrides)
            for key, value in expected.items():
                assert report[key] == value, f"{key}: expected {value}, got {report[key]}"
            assert abs(float(report["PS3_FSR_SHARPNESS"]) - 0.8) < 0.001
            assert report["PS3_PLAYER_CONFIG"] == str(config), "Custom keys in Auto mode must be applied"
    print("PASS: player.ini graphics and explicit environment precedence")


if __name__ == "__main__":
    main()
