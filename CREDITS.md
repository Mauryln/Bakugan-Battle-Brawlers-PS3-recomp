# Credits and Acknowledgments

## PS3Recomp Toolkit

Special thanks and sincere gratitude to **Ned Heller (sp00nznet)** and the developers of the **PS3Recomp** project:
- Official Toolkit Repositories:
  - <https://github.com/sp00nznet/ps3recomp>
  - <https://github.com/arnold9401/ps3recomp>

This project is built upon the pioneering static recompilation research and HLE runtime architecture established by PS3Recomp (licensed under MIT). Without their foundational work, this native PC port would not exist.

## Bakugan Native PC Port

- Static recompilation of PPU (15,682 functions across 17 translation units) and SPU jobs (5 images, 1,331 functions).
- Direct3D 12 graphics backend, custom frame orchestration, vertex staging, and parallel upload workers.
- Custom Media Foundation video decoder for narrative cinematics and publisher FMVs.
- Multi-channel audio mixing via SPU and WASAPI shared streams.
- Comprehensive input system: simultaneous Keyboard, Mouse, XInput/DirectInput/SDL3 gamepads, and 3-way Sixaxis emulation (Mouse motion, Analog stick, and physical Gyro sensors).
- Modernized bilingual Launcher with interactive visual controller configurator, AMD FSR 1.0 upscaling, and borderless windowed presentation.
- Automated bilingual installer with integrated disc extraction and self-contained decryption.

## Third-Party Libraries and Tools

- **RPCS3** (<https://rpcs3.net> / <https://github.com/RPCS3/rpcs3>, GPLv2): Used locally as a portable tool (`rpcs3.exe --decrypt`) to allow users to decrypt `EBOOT.BIN` from their own legally owned disc dump. No emulator core, firmware, or copyright keys are bundled or used at runtime.
- **AMD FidelityFX Super Resolution (FSR 1.0 / RCAS)**: High-quality spatial upscaling and robust contrast-adaptive sharpening.
- **SDL3 (Simple DirectMedia Layer)** (<https://github.com/libsdl-org/SDL>, zlib license): Hardware gamepad enumeration, hotplugging, and motion sensor support.
- **Microsoft Media Foundation**: Hardware-accelerated H.264 video and AAC audio decoding.

## Legal Disclaimer

Bakugan: Battle Brawlers (PlayStation 3, 2009, `BLES00758`) is © Activision, Nowpro, Nelvana, Spin Master, Sega Sammy, and its respective rights holders. This repository and its releases ship **no game code, no assets, no encryption keys, no lifted sources, and no copyrighted game binaries**. Everything is generated or extracted from the user's own legally owned retail disc dump.
