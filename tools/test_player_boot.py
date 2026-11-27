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
            "Window=windowed\n{graphics}[Input]\n{input_cfg}[Keyboard]\n14=key:66\n")
        for graphics, input_cfg, overrides, expected_frame_gen, expected_vib, expected_prompts in (
            ("", "", {}, "off", "80", "auto"),
            ("FPS=60\n", "", {}, "off", "80", "auto"),
            ("FPS=120\nframe_gen=fsr3\n", "", {}, "fsr3", "80", "auto"),
            ("FPS=60\n", "", {"PS3_FPS_CAP": "90"}, "off", "80", "auto"),
            ("FPS=invalid\n", "", {}, "off", "80", "auto"),
            ("frame_gen=fsr3\ndisplay_mode=fullscreen\n", "", {}, "fsr3", "80", "auto"),
            ("display_mode=borderless\n", "", {"PS3_WINDOW_MODE": "windowed"}, "off", "80", "auto"),
            ("frame_gen=fsr3\n", "", {"PS3_FRAME_GEN": "off", "PS3_OUTPUT": "720p", "PS3_FSR": "0",
                                      "PS3_AA": "off", "PS3_FSR_SHARPNESS": "0.25", "PS3_ANISO": "4",
                                      "PS3_LANG": "de"}, "off", "80", "auto"),
            ("frame_gen=off\n", "", {"PS3_FRAME_GEN": "fsr3"}, "fsr3", "80", "auto"),
            ("frame_gen=fsr3\n", "", {"PS3_FRAME_GEN": "invalid"}, "fsr3", "80", "auto"),
            ("frame_gen=invalid\n", "", {"PS3_FRAME_GEN": "invalid"}, "off", "80", "auto"),
            ("", "VibrationStrength=50\n", {}, "off", "50", "auto"),
            ("", "VibrationStrength=50\n", {"PS3_VIBRATION_STRENGTH": "25"}, "off", "25", "auto"),
            ("", "button_prompts=auto\n", {}, "off", "80", "auto"),
            ("", "button_prompts=playstation\n", {}, "off", "80", "playstation"),
            ("", "button_prompts=xbox\n", {}, "off", "80", "xbox"),
            ("", "button_prompts=nintendo\n", {}, "off", "80", "nintendo"),
            ("", "button_prompts=invalid\n", {}, "off", "80", "auto"),
            ("", "button_prompts=xbox\n", {"PS3_BUTTON_PROMPTS": "Nintendo"}, "off", "80", "nintendo"),
            ("", "button_prompts=xbox\n", {"PS3_BUTTON_PROMPTS": "playstation"}, "off", "80", "playstation"),
            ("", "button_prompts=xbox\n", {"PS3_BUTTON_PROMPTS": "invalid"}, "off", "80", "xbox"),
        ):
            config.write_text(contents.format(graphics=graphics, input_cfg=input_cfg), encoding="ascii")
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
                            PS3_ANISO="8", PS3_WINDOW_MODE="windowed", PS3_FPS_CAP="30",
                            PS3_VIBRATION_STRENGTH=expected_vib)
            if "display_mode=fullscreen" in graphics:
                expected["PS3_WINDOW_MODE"] = "fullscreen"
            expected.update(overrides)
            expected["PS3_FRAME_GEN"] = expected_frame_gen
            expected["PS3_BUTTON_PROMPTS"] = expected_prompts
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
