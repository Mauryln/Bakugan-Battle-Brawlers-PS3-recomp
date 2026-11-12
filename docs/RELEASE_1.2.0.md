# Bakugan Battle Brawlers Recomp v1.2.0

Windows 10/11 x64 with Direct3D 12. Requires your own European PlayStation 3 disc copy (`BLES00758`).

---

## English

### Highlights & Bug Fixes
- **Bug Fixed — Multiplayer Game Enabled**: Multi-controller support via SDL3 and `cellPad` across up to 7 independent ports (PS4 DualShock 4, Nintendo Switch Pro Controller, Xbox/XInput gamepads, generic SDL controllers, plus a dedicated Keyboard/Mouse player port). Works automatically both when launched through `BakuganLauncher.exe` and on direct launches (`BakuganBrawlers.cmd` / `BakuganRecomp.exe`), so 2–4 players can join multiplayer matches simultaneously.
- **Minor Bug Fixed — Story Mode Player Portrait**: Fixed the custom player avatar portrait appearing missing or glitched during the first Story Mode session. Sampled-only offscreen render targets (`200x200` portrait RTs) are now pinned across passes with LRU eviction and expanded pool slots, alias invalidation checks resolved guest EAs, `NV309E` blits stamp guest pages, texture upload generation is captured safely, and the depth pool limit was raised (`MAX_DEPTH_SURFACES = 32`).
- **360° Analog Stick Fix for Minigames**: Replaced independent per-axis axial snapping on analog sticks with a 2D radial deadzone and angle-preserving circular-to-square shaping (`shape_stick_2d`). L-stick and R-stick spins now register smooth 360° intermediate directions cleanly during timing/rotation minigames instead of snapping only to Up/Right/Down/Left.
- **Updated Launcher & Installer Packaging**: Rebuilt `BakuganLauncher.exe`, `BakuganRecomp.exe` (`v1.2.0.0`), `BakuganSetup.exe`, `Uninstall.exe`, `SDL3.dll`, FSR1 shaders, and RPCS3 portable extractor in one complete release payload.

### Install / Upgrade
Extract the entire ZIP archive and run `BakuganSetup.exe` alongside the `payload` folder, then select your own European PS3 ISO/disc dump (`BLES00758`). To upgrade an existing installation, select your current install folder. Save data and settings are preserved in `%LOCALAPPDATA%\BakuganRecomp`.

---

## Español

### Novedades y Correcciones de Bugs
- **Bug corregido — Multijugador habilitado**: Soporte multipuerto mediante SDL3 y `cellPad` para hasta 7 puertos independientes (PS4 DualShock 4, Nintendo Switch Pro Controller, Xbox/XInput, mandos SDL genéricos y un puerto dedicado para Teclado/Ratón). Funciona tanto abriendo desde `BakuganLauncher.exe` como en ejecuciones directas (`BakuganBrawlers.cmd` / `BakuganRecomp.exe`), permitiendo jugar con 2 o más jugadores a la vez.
- **Bug menor corregido — Retrato del jugador en Modo Historia**: Solucionado el fallo por el que el retrato personalizado del jugador faltaba o salía con glitches en la primera sesión del Modo Historia. Ahora los render targets offscreen muestreados (`200x200`) se fijan durante el frame con desalojo LRU y mayor capacidad, la invalidación de alias comprueba las EA reales en memoria guest, `NV309E` marca las páginas escritas, se evita la carrera de generación al subir texturas y se amplió `MAX_DEPTH_SURFACES` a 32.
- **Corregido giro completo de 360° en sticks para minijuegos**: Se reemplazó el recorte axial independiente por eje en mandos por una zona muerta radial 2D con proyección circular-a-cuadrado que conserva el ángulo (`shape_stick_2d`). Ahora los giros completos con L-stick y R-stick registran todas las direcciones intermedias en los minijuegos en lugar de quedar restringidos a Arriba/Derecha/Abajo/Izquierda.
- **Launcher e Instalador actualizados**: Incluye `BakuganLauncher.exe`, `BakuganRecomp.exe` (`v1.2.0.0`), `BakuganSetup.exe`, `Uninstall.exe`, `SDL3.dll`, shaders FSR1 y RPCS3 portable actualizados.

### Instalación / Actualización
Extrae todo el ZIP y abre `BakuganSetup.exe` junto a la carpeta `payload`. Selecciona tu copia/ISO europea de PS3 (`BLES00758`). Para actualizar una versión anterior, elige la misma carpeta donde ya lo tenías instalado. Las partidas guardadas y ajustes se conservan en `%LOCALAPPDATA%\BakuganRecomp`.
