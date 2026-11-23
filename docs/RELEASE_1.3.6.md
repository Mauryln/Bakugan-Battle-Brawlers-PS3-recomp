# Bakugan Recomp v1.3.6

> **Actualización opcional:** No es obligatorio actualizar si ya estás jugando en la **v1.3.5**. Esta versión contiene exactamente el mismo juego y funciones que la `1.3.5`; únicamente limpia el empaquetado e instalador para eliminar los falsos positivos de antivirus en VirusTotal (dejando todos los binarios y el ZIP en **0 detecciones**).

## Cambios técnicos

- **Instalador nativo (`BakuganSetup.exe`)**: reemplaza PowerShell y `WScript.Shell` por APIs nativas de Windows (`virtdisk.dll` para montar/desmontar la ISO e `IShellLinkW` para accesos directos), añade metadatos `AssemblyInfo`, manifiesto explícito de Windows 10/11 (`asInvoker`), `HIGH_ENTROPY_VA` y checksum PE válido.
- **Desinstalador limpio (`Uninstall.exe`)**: elimina los archivos del juego, accesos directos y registro directamente desde C#, sin scripts `.cmd` temporales ni procesos ocultos.
- **Launcher (`BakuganLauncher.exe`)**: añade su propio `VERSIONINFO` (`OriginalFilename: BakuganLauncher.exe`), sustituye `GetAsyncKeyState` por `GetKeyState` limitado a la ventana enfocada y deja de enlazar librerías de FidelityFX sin uso.
- **Runtime y FSR 3.1 (`BakuganRecomp.exe`)**: separa AMD FidelityFX FSR 3.1 en sus DLLs dedicadas (`ffx_*.dll`) y usa el runtime dinámico oficial de Microsoft (`/MD`, incluyendo las DLLs firmadas por Microsoft en el paquete).
- **Icono optimizado (`bakugan.ico`)**: generado como DIB estándar compacto (16/24/32/48 px) en lugar de capas PNG de alta entropía.

## Verificación de seguridad (VirusTotal)

Todos los archivos de esta versión están compilados directamente desde este repositorio público y verificados con **0 detecciones**:

| Archivo | Resultado VirusTotal | SHA-256 | Enlace |
|---|---|---|---|
| `Bakugan-Battle-Brawlers-Recomp-1.3.6-win64.zip` | **0 / 65** (Limpio) | `5b9f26c2340069cd8283288c1f148128f003d4651cd2831f35b3852711057d4a` | [Ver reporte](https://www.virustotal.com/gui/file/5b9f26c2340069cd8283288c1f148128f003d4651cd2831f35b3852711057d4a) |
| `BakuganSetup.exe` | **0 / 70** (Limpio) | `02bc5681e579c92b68744c73a7d469733164f7e1790dc4caff1c473ac6cd77d1` | [Ver reporte](https://www.virustotal.com/gui/file/02bc5681e579c92b68744c73a7d469733164f7e1790dc4caff1c473ac6cd77d1) |
| `BakuganRecomp.exe` | **0 detecciones** (Limpio) | `ca30896a101bce94deed75bcbaa470cf171324342d1b28402ac1cb33858797b3` | [Ver reporte](https://www.virustotal.com/gui/file/ca30896a101bce94deed75bcbaa470cf171324342d1b28402ac1cb33858797b3) |
| `BakuganLauncher.exe` | **0 detecciones** (Limpio) | `fb6c8fe2a86239ef988616ff82e882c3f4690d8b9ef6715223177ee717c0106b` | [Ver reporte](https://www.virustotal.com/gui/file/fb6c8fe2a86239ef988616ff82e882c3f4690d8b9ef6715223177ee717c0106b) |
| `Uninstall.exe` | **0 detecciones** (Limpio) | `bf641c5853e29d53e08bfbd76bab1704145c0fc9251ec980633284546de5ecf7` | [Ver reporte](https://www.virustotal.com/gui/file/bf641c5853e29d53e08bfbd76bab1704145c0fc9251ec980633284546de5ecf7) |

## Cómo actualizar (opcional)

Cierra el juego y el launcher, extrae todo el ZIP y ejecuta `BakuganSetup.exe`. Selecciona tu carpeta instalada y deja la ISO en blanco para reutilizar los datos del juego y conservar tus partidas y ajustes.
