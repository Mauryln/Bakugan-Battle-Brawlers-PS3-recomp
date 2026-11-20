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
        contents = (
            "[Player]\nVersion=1\nLanguage=es\n[Graphics]\n"
            "Output=1080p\nFSR=fsr1\nAAMode=smaa\nSharpness=0.8\nAnisotropy=8\n"
            "Window=windowed\nFPS=30\n{frame_gen}[Keyboard]\n14=key:66\n")
        for frame_gen, overrides, expected_frame_gen in (
            ("", {}, "off"),
            ("frame_gen=fsr3\n", {}, "fsr3"),
            ("frame_gen=fsr3\n", {"PS3_FRAME_GEN": "off", "PS3_OUTPUT": "720p", "PS3_FSR": "0",
                                  "PS3_AA": "off", "PS3_FSR_SHARPNESS": "0.25", "PS3_ANISO": "4",
                                  "PS3_LANG": "de"}, "off"),
            ("frame_gen=off\n", {"PS3_FRAME_GEN": "fsr3"}, "fsr3"),
            ("frame_gen=fsr3\n", {"PS3_FRAME_GEN": "invalid"}, "fsr3"),
            ("frame_gen=invalid\n", {"PS3_FRAME_GEN": "invalid"}, "off"),
        ):
            config.write_text(contents.format(frame_gen=frame_gen), encoding="ascii")
            report_path = work / "boot.txt"
            report_path.unlink(missing_ok=True)
            env = {k: v for k, v in os.environ.items()
                   if not k.startswith(("PS3_", "BAKUGAN_", "PAD_"))}
            env.update(LOCALAPPDATA=str(work), PS3_GAMEDATA_ROOT=str(work / "gamedata"),
                       PS3_VERBOSE="0", BAKUGAN_BOOT_REPORT=str(report_path))
            env.update(overrides)
            result = subprocess.run([str(exe), str(work / "missing.elf")],
                                    env=env, cwd=work, capture_output=True, timeout=20)
            assert result.returncode != 0, "The fixture must reject the missing ELF"
            report = dict(line.split("=", 1) for line in
                          report_path.read_text().splitlines() if "=" in line)
            expected = dict(PS3_OUTPUT="1080p", PS3_FSR="fsr1", PS3_AA="smaa", PS3_LANG="es",
                            PS3_ANISO="8", PS3_WINDOW_MODE="windowed", PS3_FPS_CAP="30")
            expected.update(overrides)
            expected["PS3_FRAME_GEN"] = expected_frame_gen
            for key, value in expected.items():
                assert report[key] == value, f"{key}: expected {value}, got {report[key]}"
            expected_sharp = float(overrides.get("PS3_FSR_SHARPNESS", "0.8"))
            assert abs(float(report["PS3_FSR_SHARPNESS"]) - expected_sharp) < 0.001
            assert report["PS3_PLAYER_CONFIG"] == str(config), "Custom keys in Auto mode must be applied"
            for effect in ("fsr3", "opticalflow", "frameinterpolation"):
                assert int(report[f"ffx_{effect}_version"]) > 0
            assert int(report["ffx_dx12_scratch_bytes"]) > 0
    print("PASS: player.ini graphics, environment precedence, and FidelityFX linkage")


if __name__ == "__main__":
    main()
