# Bakugan: Battle Brawlers — Native PC Recompilation v1.0.0

**Official First Release**  
**Title:** Bakugan: Battle Brawlers (PlayStation 3 — `BLES00758` European Disc Edition)  
**Target Platform:** Windows 10/11 x64 (Direct3D 12)

---

## English Release Notes

### Highlights & What's New

- **Full Native Windows x64 Port**: Ahead-of-time static recompilation of game PPU/SPU machine code into optimized C++ running on a native Direct3D 12 HLE runtime. Playable end to end: story mode, all battle arenas, card minigames, cutscenes, and cinematics.
- **Tri-Modal Sixaxis Emulation**:
  - **Mouse Motion Control**: Steer the Bakugan ball in arenas with mouse movement. Features exponential spring-to-center physics, deadzone noise filtering, and smooth cursor re-centering in focused gameplay (auto-released on Alt+Tab).
  - **Right Analog Stick**: Intuitive right-stick tilt for standard gamepads.
  - **Hardware Gyroscope**: Native motion sensor support on DualShock 4 and DualSense controllers via SDL3.
  - *All three input methods work simultaneously with zero configuration.*
- **Modernized Bilingual Launcher**:
  - Interactive graphical Gamepad Remapper with visual controller diagram.
  - Borderless Windowed mode set by default at 1080p for seamless multitasking.
  - AMD FidelityFX Super Resolution 1.0 (FSR1) spatial upscaler and RCAS sharpness controls.
  - Automatic memory and process cleanup when launching the game.
- **Automated Bilingual Installer**:
  - Automatically detects Windows UI language (Spanish / English).
  - One-click extraction from retail European ISO (`BLES00758`) or folder dump.
  - Automated `EBOOT.BIN` decryption via bundled portable RPCS3 tools.
  - Clean uninstaller keeping user saves and preferences intact in `%LOCALAPPDATA%\BakuganRecomp`.
- **Pre-warmed Shader Container (`fp_v3.cache`)**: Ships with pre-compiled arena pixel shaders to prevent first-entry battle stuttering.
- **WASAPI Low-Latency Audio**: SPU-driven audio stream mixing and Media Foundation AAC cinematics.

### System Requirements

- **OS**: Windows 10 64-bit (1909+) or Windows 11 64-bit.
- **CPU**: x86-64 with AVX and SSE4.2 support (Intel 6th Gen / AMD Ryzen 1000+).
- **RAM**: 4 GB minimum (8 GB recommended).
- **GPU**: Direct3D 12 Feature Level 11_0 / 12_0 (GTX 960 / RX 460 / Iris Xe minimum; RTX 2060/3050+ recommended).
- **Storage**: ~6 GB free disk space.
- **Game Media**: Legally acquired European retail PS3 disc image (`BLES00758`).

### Special Acknowledgments

- **PS3Recomp Team & Ned Heller ([arnold9401/ps3recomp](https://github.com/arnold9401/ps3recomp) / [sp00nznet/ps3recomp](https://github.com/sp00nznet/ps3recomp))**: For the foundational open-source static recompilation framework and HLE runtime architecture.
- **RPCS3 Developers**: For the portable decryption utility.
- **SDL3 & AMD FidelityFX**: For controller and upscaling technologies.

---

## Notas de Lanzamiento en Español

### Novedades y Características Principales

- **Port Nativo para Windows x64**: Recompilación estática por adelantado del código máquina PPU/SPU a C++ optimizado sobre un runtime HLE Direct3D 12. Jugable de principio a fin: modo historia, todas las arenas de combate, minijuegos de cartas, cinemáticas y videos.
- **Simulación Tri-Modal de Sixaxis**:
  - **Control por Movimiento de Ratón**: Guía la esfera en batalla con el ratón. Incorpora retorno al centro automático con amortiguación suave, filtro de ruido y captura/liberación de cursor al alternar ventanas.
  - **Stick Analógico Derecho**: Conducción suave para mandos convencionales.
  - **Giroscopio Físico**: Soporte 1:1 de sensores de movimiento nativos en DualShock 4 y DualSense mediante SDL3.
  - *Los tres métodos funcionan simultáneamente sin necesidad de cambiar selectores.*
- **Launcher Modernizado y Bilingüe**:
  - Remapeador interactivo con esquema gráfico de mando.
  - Modo Ventana Sin Bordes (*Borderless Windowed*) predeterminado a 1080p.
  - Reescalado oficial AMD FidelityFX Super Resolution 1.0 (FSR1) con nitidez RCAS ajustable.
  - Cierre automático del launcher al iniciar el juego para optimizar recursos.
- **Instalador Automatizado Bilingüe**:
  - Detección automática del idioma de Windows (Español / Inglés).
  - Extracción en un clic desde la ISO europea original (`BLES00758`) o carpeta del juego.
  - Descifrado automatizado de `EBOOT.BIN` mediante herramientas portables de RPCS3.
  - Desinstalador limpio que conserva partidas y ajustes en `%LOCALAPPDATA%\BakuganRecomp`.
- **Caché de Shaders Precargada (`fp_v3.cache`)**: Incluye shaders de arena precompilados para eliminar tirones al entrar a batalla.
- **Audio WASAPI de Baja Latencia**: Mezcla de audio impulsada por SPU y cinemáticas AAC por Media Foundation.

### Requisitos del Sistema

- **SO**: Windows 10 de 64 bits (1909+) o Windows 11 de 64 bits.
- **Procesador**: x86-64 con soporte AVX y SSE4.2 (Intel 6ª Gen / AMD Ryzen 1000+).
- **RAM**: 4 GB mínimo (8 GB recomendado).
- **Tarjeta Gráfica**: Direct3D 12 Feature Level 11_0 / 12_0 (GTX 960 / RX 460 / Iris Xe mínimo; RTX 2060/3050+ recomendada).
- **Espacio en disco**: ~6 GB disponibles.
- **Copia del Juego**: Disco europeo original de PlayStation 3 (`BLES00758`).

---

## Verification & File Hashes

All release packages have been compiled with LLVM clang-cl `/O2` and packaged using the automated release pipeline.

| File | SHA-256 Checksum | Size |
| :--- | :--- | :--- |
| `Bakugan-Battle-Brawlers-Recomp-1.0-win64.zip` | `FEF040AE663A2D3A047C3990791B2F2450CC4D8F8F473A148A58D696EFD036E8` | ~67.7 MB |
| `BakuganSetup.exe` | `73D2787C8365F1AAE366A4274ADF61864D5375669389E3A0A16850AEFD43E515` | ~255 KB |

