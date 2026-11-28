# Bakugan Battle Brawlers Recomp v1.4.0

Windows 10/11 x64 with Direct3D 12 (**AVX2 + SSE4.1 CPU required**; dynamic CRT `/MD` with bundled Microsoft-signed runtime DLLs — no external VC++ Redistributable needed). **100% native static recompilation** at runtime with no emulator installed or executed when playing. Requires your own European PlayStation 3 disc copy (`BLES00758`).

---

## English

### What Changed (v1.4.0)

- **Full Controller Rumble (SDL3 / DualSense / Switch Pro / XInput)**: Native vibration support via `cellPadSetActDirect` for PS5 DualSense, DualShock 4, Nintendo Switch Pro, and Xbox controllers (USB & Bluetooth), plus a vibration intensity slider (0–100%) in `BakuganLauncher.exe` and `player.ini` (Issues #10 and #8).
- **Xbox & Nintendo Button Prompts (`button_prompts`)**: Automatic connected-controller detection (`Auto`) and manual selector (`PlayStation` / `Xbox` / `Nintendo`) in the launcher, updating in-game 2D UI prompts (`assets/button_prompts/`), the launcher controller diagram, and the save-data overlay (Issue #5).
- **Sixaxis Calibration in Hybrid Mode**: True `±113`-count calibration and Z-axis (`sensors[2]`) acceleration/braking on the Right Stick without accidental jumps, keeping **Hybrid Mode** (Physical Gyroscope + Right Stick) alongside the **Standard** profile by default (Issue #8).
- **FMV Audio/Video Sync & FSR 3.1 Pacing**: High-precision QPC tail anchor in `sys_timer_usleep`, smoother FSR 3.1 Frame Generation transitions, and `.MP4` cutscene playback synchronized against `waveOutGetPosition` (Issue #9).
- **Zero-Emulator Folder (`PS3_GAME`) Installer & Safe Uninstaller**:
  - Added **Folder...** (`--folder`) support in `BakuganSetup.exe` for extracted PS3 dumps (`PS3_GAME\` or dump root) alongside `.iso` (Issue #8), with recursive/overlap checks, `%LOCALAPPDATA%\BakuganRecomp` save-data protection, and DPI-scaled layout.
  - Removed all emulator binaries from the release package in favor of native C# `SelfExtract` (`EBOOT.BIN` -> `EBOOT.elf`) extraction inside `BakuganSetup.exe` (keeping `--eboot` as an optional silent CLI fallback).
  - Hardened `Uninstall.exe` (WinForms UI + `/S` silent mode) with allowlisted cleanup that preserves save data and `player.ini` in `%LOCALAPPDATA%\BakuganRecomp`, plus clean `1.4.0.0` Release executables (`/O2` / `/optimize+`, CFG, valid PE checksums, embedded Windows 10/11 manifests, and full `VERSIONINFO`).

### Install / Upgrade
- **Fresh install**: Extract the complete ZIP archive (`BakuganSetup.exe` requires the extracted `payload` folder beside it), run `BakuganSetup.exe`, select your European PS3 `.iso` or extracted `PS3_GAME` folder (`BLES00758`), and click **Install / Update**.
- **Upgrade from v1.2.0 / v1.3.x**: Close the game and launcher, extract the ZIP, run `BakuganSetup.exe`, select your existing install folder, and leave the ISO/folder field blank to reuse your decrypted `EBOOT.elf` and game data while keeping saves and `player.ini` intact.

### Security Verification (VirusTotal — 0 Detections)

All release binaries are compiled directly from this public repository and verified clean with **0 detections** on VirusTotal:

| File | VirusTotal Result | SHA-256 | Report |
| :--- | :--- | :--- | :--- |
| `Bakugan-Battle-Brawlers-Recomp-1.4.0-win64.zip` | **0 / 64** (Clean) | `770F6375EDBC8833144F2CE247A05E1090D8BF862171A71654DC171FB0E0CCD5` | [View report](https://www.virustotal.com/gui/file/770f6375edbc8833144f2ce247a05e1090d8bf862171a71654dc171fb0e0ccd5) |
| `BakuganSetup.exe` | **0 / 68** (Clean) | `2EDE9B140891F884E95244B24A34FBFF8CAE012AF3F7C4EFDEBD19FFFDE42CE4` | [View report](https://www.virustotal.com/gui/file/2ede9b140891f884e95244b24a34fbff8cae012af3f7c4efdebd19fffde42ce4) |
| `BakuganRecomp.exe` | **0 / 67** (Clean) | `A455679A8B58C95A1680658805F6B79ED1AADEB2D1110AB42B5DF4F47E8ADC97` | [View report](https://www.virustotal.com/gui/file/a455679a8b58c95a1680658805f6b79ed1aadeb2d1110ab42b5df4f47e8adc97) |
| `BakuganLauncher.exe` | **0 / 68** (Clean) | `8988A3AB37F71CDE3CD11982EB9E41BB8E777E62C406C517132F3E176BBC2B96` | [View report](https://www.virustotal.com/gui/file/8988a3ab37f71cde3cd11982eb9e41bb8e777e62c406c517132f3e176bbc2b96) |
| `Uninstall.exe` | **0 / 68** (Clean) | `042D1B5EEBD339DCFDBD8AC1DFDA06FB2CAAA6425683D4D2CB188481187D3452` | [View report](https://www.virustotal.com/gui/file/042d1b5eebd339dcfdbd8ac1dfda06fb2caaa6425683d4d2cb188481187d3452) |

---

## Español

### Cambios (v1.4.0)

- **Vibración completa en mandos (SDL3 / DualSense / Switch Pro / XInput)**: Soporte nativo mediante `cellPadSetActDirect` para PS5 DualSense, DualShock 4, Nintendo Switch Pro y Xbox (USB y Bluetooth), además de un control deslizante de intensidad de vibración (0–100 %) en `BakuganLauncher.exe` y `player.ini` (Issues #10 y #8).
- **Iconos de botones de Xbox y Nintendo (`button_prompts`)**: Detección automática del mando conectado (`Auto`) y selector manual (`PlayStation` / `Xbox` / `Nintendo`) en el lanzador, actualizando los iconos 2D en juego (`assets/button_prompts/`), el diagrama del mando en el lanzador y el overlay de guardado (Issue #5).
- **Calibración Sixaxis en Modo Híbrido**: Rango real de `±113` cuentas y eje Z (`sensors[2]`) para acelerar/frenar en el Stick Derecho sin saltos accidentales, conservando el **Modo Híbrido** (Giroscopio + Stick Derecho) junto al perfil **Estándar** por defecto (Issue #8).
- **Sincronización A/V en cinemáticas y pacing FSR 3.1**: Anclaje QPC de alta precisión en `sys_timer_usleep`, transiciones más suaves en FSR 3.1 y sincronización de vídeos `.MP4` frente a `waveOutGetPosition` (Issue #9).
- **Instalador sin emuladores para carpetas (`PS3_GAME`) y Desinstalador seguro**:
  - Botón **Carpeta...** (`--folder`) en `BakuganSetup.exe` para instalar desde carpetas extraídas (`PS3_GAME\` o raíz del dump) además de `.iso` (Issue #8), con validación anti-solapamiento, protección de partidas en `%LOCALAPPDATA%\BakuganRecomp` y diseño adaptado a DPI.
  - Eliminación total de binarios de emuladores en el paquete, usando extracción SELF nativa en C# mediante `SelfExtract` (`EBOOT.BIN` -> `EBOOT.elf`) y respaldo silencioso `--eboot` por CLI.
  - Nuevo `Uninstall.exe` endurecido (interfaz WinForms + modo silencioso `/S`) con limpieza por lista blanca que protege tus partidas y `player.ini` en `%LOCALAPPDATA%\BakuganRecomp`, y ejecutables Release `1.4.0.0` limpios (`/O2` / `/optimize+`, CFG, checksum PE válido, manifiesto Windows 10/11 y `VERSIONINFO` completo).

### Instalación / Actualización
- **Instalación nueva**: Descomprime todo el archivo ZIP (`BakuganSetup.exe` necesita la carpeta `payload` extraída a su lado), ejecuta `BakuganSetup.exe`, selecciona tu `.iso` europea (`BLES00758`) o carpeta `PS3_GAME` extraída y pulsa **Instalar / Actualizar**.
- **Actualizar desde v1.2.0 / v1.3.x**: Cierra el juego y el launcher, descomprime el ZIP, ejecuta `BakuganSetup.exe`, selecciona tu carpeta instalada y deja el campo de origen en blanco para reutilizar los datos del juego y conservar tus partidas y `player.ini`.

### Verificación de Seguridad (VirusTotal — 0 Detecciones)

Todos los binarios de esta versión están compilados directamente desde este repositorio público y verificados con **0 detecciones** en VirusTotal:

| Archivo | Resultado VirusTotal | SHA-256 | Reporte |
| :--- | :--- | :--- | :--- |
| `Bakugan-Battle-Brawlers-Recomp-1.4.0-win64.zip` | **0 / 64** (Limpio) | `770F6375EDBC8833144F2CE247A05E1090D8BF862171A71654DC171FB0E0CCD5` | [Ver reporte](https://www.virustotal.com/gui/file/770f6375edbc8833144f2ce247a05e1090d8bf862171a71654dc171fb0e0ccd5) |
| `BakuganSetup.exe` | **0 / 68** (Limpio) | `2EDE9B140891F884E95244B24A34FBFF8CAE012AF3F7C4EFDEBD19FFFDE42CE4` | [Ver reporte](https://www.virustotal.com/gui/file/2ede9b140891f884e95244b24a34fbff8cae012af3f7c4efdebd19fffde42ce4) |
| `BakuganRecomp.exe` | **0 / 67** (Limpio) | `A455679A8B58C95A1680658805F6B79ED1AADEB2D1110AB42B5DF4F47E8ADC97` | [Ver reporte](https://www.virustotal.com/gui/file/a455679a8b58c95a1680658805f6b79ed1aadeb2d1110ab42b5df4f47e8adc97) |
| `BakuganLauncher.exe` | **0 / 68** (Limpio) | `8988A3AB37F71CDE3CD11982EB9E41BB8E777E62C406C517132F3E176BBC2B96` | [Ver reporte](https://www.virustotal.com/gui/file/8988a3ab37f71cde3cd11982eb9e41bb8e777e62c406c517132f3e176bbc2b96) |
| `Uninstall.exe` | **0 / 68** (Limpio) | `042D1B5EEBD339DCFDBD8AC1DFDA06FB2CAAA6425683D4D2CB188481187D3452` | [Ver reporte](https://www.virustotal.com/gui/file/042d1b5eebd339dcfdbd8ac1dfda06fb2caaa6425683d4d2cb188481187d3452) |

---

### Checksum (SHA-256)
```text
770F6375EDBC8833144F2CE247A05E1090D8BF862171A71654DC171FB0E0CCD5  Bakugan-Battle-Brawlers-Recomp-1.4.0-win64.zip
```
