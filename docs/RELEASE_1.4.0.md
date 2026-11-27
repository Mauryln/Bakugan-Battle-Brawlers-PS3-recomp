# Bakugan Battle Brawlers Recomp v1.4.0

Windows 10/11 x64 with Direct3D 12 (**AVX2 + SSE4.1 CPU required**; dynamic CRT `/MD` with bundled Microsoft-signed runtime DLLs — no external VC++ Redistributable needed). **100% native static recompilation** at runtime with no emulator installed or executed when playing. Requires your own European PlayStation 3 disc copy (`BLES00758`).

---

## English

### What Changed (v1.4.0)

- **SDL3 Controller Rumble & Sixaxis Calibration**: Added native SDL3 actuator rumble support (configurable vibration strength in `BakuganLauncher.exe` and `player.ini`) alongside improved Sixaxis gyro/analog calibration with the **Standard** Sixaxis profile set as default.
- **Xbox & Nintendo Switch Button Prompts (`button_prompts`)**: Automatic or manually selectable PlayStation, Xbox, and Nintendo controller prompt atlases (`assets/button_prompts/`) for in-game 2D UI.
- **FMV Audio/Video Sync & FSR 3.1 Pacing**: Synchronized cutscene video playback to the audio clock, added QPC `usleep` tail precision, and refined FSR 3.1 frame pacing.
- **Zero-Emulator Folder & ISO Installer (`BakuganSetup.exe`)**:
  - Added **Folder...** (`--folder`) support for extracted PS3 dumps (`PS3_GAME\` or dump root) with recursive/overlap checks, `%LOCALAPPDATA%\BakuganRecomp` save-data protection, and DPI-scaled layout.
  - Removed all emulator binaries from the release package in favor of native C# `SelfExtract` (`EBOOT.BIN` -> `EBOOT.elf`) extraction inside `BakuganSetup.exe`, while keeping `--eboot` as an optional silent CLI fallback.
  - Clean `1.4.0.0` Release executables (`/O2` / `/optimize+`, `/RELEASE` PE checksums, no embedded debug info, full version/copyright metadata).

### Install / Upgrade
- **Fresh install**: Extract the ZIP archive, run `BakuganSetup.exe` beside `payload`, select your European PS3 `.iso` or extracted `PS3_GAME` folder (`BLES00758`), and click **Install / Update**.
- **Upgrade from v1.2.0 / v1.3.x**: Close the game and launcher, extract the ZIP, run `BakuganSetup.exe`, select your existing install folder, and leave the ISO/folder field blank to reuse your decrypted `EBOOT.elf` and game data while keeping saves and `player.ini` intact.

---

## Español

### Cambios (v1.4.0)

- **Vibración SDL3 y Calibración Sixaxis**: Soporte nativo de vibración mediante SDL3 (intensidad configurable en `BakuganLauncher.exe` y `player.ini`) junto con mejoras de calibración de giroscopio y sticks Sixaxis, estableciendo el perfil **Estándar** como predeterminado.
- **Iconos de Botones Xbox y Nintendo (`button_prompts`)**: Atlas de botones UI 2D para PlayStation, Xbox y Nintendo Switch (`assets/button_prompts/`), con detección automática o selección manual.
- **Sincronización A/V en Cinemáticas y Pacing FSR 3.1**: Vídeo FMV sincronizado al reloj de audio, mayor precisión QPC en `usleep` y pacing refinado para FSR 3.1.
- **Instalador Sin Emuladores para ISO y Carpetas (`BakuganSetup.exe`)**:
  - Soporte para carpetas extraídas (**Carpeta...** / `--folder`, aceptando la raíz del dump o `PS3_GAME\` directamente) con validación anti-solapamiento, protección de partidas en `%LOCALAPPDATA%\BakuganRecomp` y diseño adaptado a DPI.
  - Eliminación total de binarios de emuladores en el paquete de lanzamiento, usando extracción SELF nativa en C# mediante `SelfExtract` dentro de `BakuganSetup.exe` (manteniendo `--eboot` como respaldo silencioso por CLI).
  - Ejecutables Release `1.4.0.0` limpios (`/O2` / `/optimize+`, checksum PE `/RELEASE`, sin información de depuración embebida y con metadatos completos de versión y copyright).

### Instalación / Actualización
- **Instalación nueva**: Descomprime el ZIP, ejecuta `BakuganSetup.exe` junto a `payload`, selecciona tu `.iso` europea (`BLES00758`) o carpeta `PS3_GAME` extraída y pulsa **Instalar / Actualizar**.
- **Actualizar desde v1.2.0 / v1.3.x**: Cierra el juego y el launcher, descomprime el ZIP, ejecuta `BakuganSetup.exe`, selecciona tu carpeta instalada y deja el campo de origen en blanco para reutilizar los datos del juego y conservar tus partidas y `player.ini`.
