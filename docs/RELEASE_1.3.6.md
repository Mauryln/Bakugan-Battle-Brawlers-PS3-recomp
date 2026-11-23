# Bakugan Battle Brawlers Recomp v1.3.6

Windows 10/11 x64 with Direct3D 12 (**AVX2 + SSE4.1 CPU required**; dynamic CRT `/MD` with bundled Microsoft-signed runtime DLLs — no external VC++ Redistributable needed). Requires your own European PlayStation 3 disc copy (`BLES00758`).

---

## English

> **Optional maintenance update:** Updating is **not required** if you are already playing **v1.3.5**. This release contains the exact same game and features as v1.3.5; it strictly cleans up packaging, installer, and launcher build hygiene to eliminate antivirus false positives on VirusTotal (**0 detections** across the ZIP and every executable).

### What Changed (v1.3.6)

- **Native Win32 Installer (`BakuganSetup.exe`)**: Replaced PowerShell (`-ExecutionPolicy Bypass`) and `WScript.Shell` with native Windows APIs (`virtdisk.dll` for ISO mount/unmount and `IShellLinkW` for shortcuts), removed `kernel32.dll` P/Invoke, and added full `AssemblyInfo`, explicit Windows 10/11 `asInvoker` manifest, `HIGH_ENTROPY_VA`, and a valid PE header checksum.
- **Clean Uninstaller (`Uninstall.exe`)**: Removes game files, shortcuts, and registry entries directly from C# with no temporary `.cmd` scripts or hidden processes.
- **Launcher Metadata & Input (`BakuganLauncher.exe`)**: Added a dedicated `VERSIONINFO` (`OriginalFilename: BakuganLauncher.exe`), switched focused key-binding capture from `GetAsyncKeyState` to `GetKeyState`, and removed the unused FidelityFX link.
- **Runtime & AMD FidelityFX FSR 3.1 (`BakuganRecomp.exe`)**: Built AMD FidelityFX FSR 3.1 into its dedicated DLLs (`ffx_*.dll`) and switched binaries to dynamic CRT (`/MD`) while bundling the official Microsoft-signed CRT DLLs (`msvcp140.dll`, `vcruntime140.dll`, `vcruntime140_1.dll`).
- **Compact Icon (`bakugan.ico`)**: Rebuilt as standard uncompressed DIB entries (16/24/32/48 px, 17.5 KB) instead of high-entropy PNG layers.

### Security Verification (VirusTotal)

All release binaries are built directly from this public repository and verified clean with **0 detections**:

| File | VirusTotal Result | SHA-256 | Report |
| :--- | :--- | :--- | :--- |
| `Bakugan-Battle-Brawlers-Recomp-1.3.6-win64.zip` | **0 / 65** (Clean) | `5b9f26c2340069cd8283288c1f148128f003d4651cd2831f35b3852711057d4a` | [View report](https://www.virustotal.com/gui/file/5b9f26c2340069cd8283288c1f148128f003d4651cd2831f35b3852711057d4a) |
| `BakuganSetup.exe` | **0 / 70** (Clean) | `02bc5681e579c92b68744c73a7d469733164f7e1790dc4caff1c473ac6cd77d1` | [View report](https://www.virustotal.com/gui/file/02bc5681e579c92b68744c73a7d469733164f7e1790dc4caff1c473ac6cd77d1) |
| `BakuganRecomp.exe` | **0 detections** (Clean) | `ca30896a101bce94deed75bcbaa470cf171324342d1b28402ac1cb33858797b3` | [View report](https://www.virustotal.com/gui/file/ca30896a101bce94deed75bcbaa470cf171324342d1b28402ac1cb33858797b3) |
| `BakuganLauncher.exe` | **0 detections** (Clean) | `fb6c8fe2a86239ef988616ff82e882c3f4690d8b9ef6715223177ee717c0106b` | [View report](https://www.virustotal.com/gui/file/fb6c8fe2a86239ef988616ff82e882c3f4690d8b9ef6715223177ee717c0106b) |
| `Uninstall.exe` | **0 detections** (Clean) | `bf641c5853e29d53e08bfbd76bab1704145c0fc9251ec980633284546de5ecf7` | [View report](https://www.virustotal.com/gui/file/bf641c5853e29d53e08bfbd76bab1704145c0fc9251ec980633284546de5ecf7) |

### Install / Optional Upgrade
- **Fresh install**: Extract the entire ZIP archive, run `BakuganSetup.exe` beside the `payload` folder, and select your European PS3 ISO/disc dump (`BLES00758`).
- **Upgrade from v1.2.0 / v1.3.0 / v1.3.5**: Close the game and launcher, extract the ZIP, run `BakuganSetup.exe`, select your existing install folder, and **leave the ISO field blank** to reuse your decrypted game data while keeping saves and `player.ini` intact.

---

## Español

> **Actualización opcional de mantenimiento:** No es obligatorio actualizar si ya estás jugando en la **v1.3.5**. Esta versión contiene exactamente el mismo juego y funciones que la `1.3.5`; únicamente limpia el empaquetado, instalador y launcher para eliminar los falsos positivos de antivirus en VirusTotal (**0 detecciones** en el ZIP y en todos los ejecutables).

### Cambios (v1.3.6)

- **Instalador nativo Win32 (`BakuganSetup.exe`)**: reemplaza PowerShell (`-ExecutionPolicy Bypass`) y `WScript.Shell` por APIs nativas de Windows (`virtdisk.dll` para montar/desmontar la ISO e `IShellLinkW` para accesos directos), elimina P/Invoke a `kernel32.dll` y añade `AssemblyInfo`, manifiesto explícito de Windows 10/11 (`asInvoker`), `HIGH_ENTROPY_VA` y checksum PE válido.
- **Desinstalador limpio (`Uninstall.exe`)**: elimina los archivos del juego, accesos directos y entrada del Registro directamente desde C#, sin scripts `.cmd` temporales ni procesos ocultos.
- **Metadatos y captura del Launcher (`BakuganLauncher.exe`)**: incorpora su propio `VERSIONINFO` (`OriginalFilename: BakuganLauncher.exe`), sustituye `GetAsyncKeyState` por `GetKeyState` limitado a la ventana enfocada y deja de enlazar librerías de FidelityFX sin uso.
- **Runtime y AMD FidelityFX FSR 3.1 (`BakuganRecomp.exe`)**: separa AMD FidelityFX FSR 3.1 en sus DLLs dedicadas (`ffx_*.dll`) y usa el runtime dinámico oficial de Microsoft (`/MD`, incluyendo las DLLs firmadas por Microsoft: `msvcp140.dll`, `vcruntime140.dll`, `vcruntime140_1.dll`).
- **Icono compacto (`bakugan.ico`)**: regenerado como DIB estándar sin compresión (16/24/32/48 px, 17.5 KB) en lugar de capas PNG de alta entropía.

### Verificación de seguridad (VirusTotal)

Todos los archivos de esta versión están compilados directamente desde este repositorio público y verificados con **0 detecciones**:

| Archivo | Resultado VirusTotal | SHA-256 | Reporte |
| :--- | :--- | :--- | :--- |
| `Bakugan-Battle-Brawlers-Recomp-1.3.6-win64.zip` | **0 / 65** (Limpio) | `5b9f26c2340069cd8283288c1f148128f003d4651cd2831f35b3852711057d4a` | [Ver reporte](https://www.virustotal.com/gui/file/5b9f26c2340069cd8283288c1f148128f003d4651cd2831f35b3852711057d4a) |
| `BakuganSetup.exe` | **0 / 70** (Limpio) | `02bc5681e579c92b68744c73a7d469733164f7e1790dc4caff1c473ac6cd77d1` | [Ver reporte](https://www.virustotal.com/gui/file/02bc5681e579c92b68744c73a7d469733164f7e1790dc4caff1c473ac6cd77d1) |
| `BakuganRecomp.exe` | **0 detecciones** (Limpio) | `ca30896a101bce94deed75bcbaa470cf171324342d1b28402ac1cb33858797b3` | [Ver reporte](https://www.virustotal.com/gui/file/ca30896a101bce94deed75bcbaa470cf171324342d1b28402ac1cb33858797b3) |
| `BakuganLauncher.exe` | **0 detecciones** (Limpio) | `fb6c8fe2a86239ef988616ff82e882c3f4690d8b9ef6715223177ee717c0106b` | [Ver reporte](https://www.virustotal.com/gui/file/fb6c8fe2a86239ef988616ff82e882c3f4690d8b9ef6715223177ee717c0106b) |
| `Uninstall.exe` | **0 detecciones** (Limpio) | `bf641c5853e29d53e08bfbd76bab1704145c0fc9251ec980633284546de5ecf7` | [Ver reporte](https://www.virustotal.com/gui/file/bf641c5853e29d53e08bfbd76bab1704145c0fc9251ec980633284546de5ecf7) |

### Instalación / Actualización opcional
- **Instalación nueva**: Descomprime todo el archivo ZIP, ejecuta `BakuganSetup.exe` junto a la carpeta `payload` y selecciona tu copia/ISO europea de PS3 (`BLES00758`).
- **Actualizar desde v1.2.0 / v1.3.0 / v1.3.5**: Cierra el juego y el launcher, descomprime el ZIP, ejecuta `BakuganSetup.exe`, selecciona tu carpeta instalada y **deja la ISO en blanco** para reutilizar los datos del juego conservando partidas y `player.ini`.

---

### Checksum (SHA-256)
```text
5B9F26C2340069CD8283288C1F148128F003D4651CD2831F35B3852711057D4A  Bakugan-Battle-Brawlers-Recomp-1.3.6-win64.zip
```
