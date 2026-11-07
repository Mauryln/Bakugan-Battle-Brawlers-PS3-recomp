# Bakugan: Battle Brawlers — Port nativo Windows x64 — v1.0

**Estado:** build 1.0 de prueba, publicada para validación en PC distintas.
La versión del juego es BLES00758 (Europa, disco).

## Lo bueno

- Port nativo Windows x64 con PPU/SPU recompilados y HLE de `cellGcm`,
  `cellAudio`, `cellSpurs` y `cellSail`.
- Menús, tutorial de historia, arena de batalla, HUD, cartas, sombras, agua,
  efectos y cinemáticas funcionando en la build de referencia.
- Launcher con salida 720p/1080p/1440p/2160p, FSR automático, nitidez RCAS,
  anisotropía, ventana/sin bordes, idioma del juego y remapeo completo de mandos.
- Interfaz del launcher en español e inglés, icono propio, sin consola y con la
  barra de título limpia.
- Instalador con selección de ISO, carpeta de instalación, accesos de escritorio
  y menú Inicio, extracción automática de `EBOOT.elf` con RPCS3 portable y
  desinstalador que conserva partidas y ajustes.
- El launcher se cierra al pulsar Jugar; el juego queda abierto sin consola.
- Sistema de guardado con slots `...-NN`; el primer guardado reutiliza un slot
  incompleto y el slot `00` vacío se oculta del menú de carga.
- Correcciones de caché de vértices y de invalidación de páginas al cargar
  partidas, leer archivos o decodificar PNG.
- Se incluye una caché de shaders validada y el juego escribe un log en
  `%LOCALAPPDATA%\BakuganRecomp\BakuganRecomp.log` para diagnosticar cierres.

## Limitaciones conocidas

- **30 FPS estables todavía no.** El límite de presentación es fijo a 30, pero el
  cuello real es trabajo serial de CPU; algunas escenas bajan de 30.
- **Artefactos visuales aleatorios en investigación:** texturas que se vuelven
  negras, retrato del jugador que a veces no aparece y fondo gris al entrar al
  tutorial tras cargar una partida. Hay correcciones aplicadas, pero falta
  reproducirlo y validarlo en más equipos y partidas.
- **Cierre al entrar en algunas arenas de Battle Arena:** en investigación. Esta
  build oculta el slot 00 vacío del menú de carga, incluye una caché de shaders
  completa y escribe un log de ejecución; confirmar si desaparece. Si vuelve a
  cerrarse, adjuntar `%LOCALAPPDATA%\BakuganRecomp\BakuganRecomp.log`.
- Cobertura de hardware incompleta: DS4 probado; DualSense y Switch Pro sin
  validación física.
- Sistema de guardado con casos límite pendientes (escritura, slots antiguos y
  partidas con personajes personalizados).
- El instalador no tiene actualizador automático ni firma de código; Windows
  SmartScreen puede avisar.
- Requiere una ISO/dump propio. No se distribuye ningún dato, código, clave ni
  lift del juego.
- RPCS3 se incluye como herramienta GPLv2 para `--decrypt`; no se incluye
  firmware ni claves del proyecto.

## Qué falta por corregir

1. Reproducir y cerrar los artefactos visuales aleatorios con capturas y logs de
   varias PC/partidas.
2. Estabilizar 30 FPS en todas las escenas y validar el rendimiento en hardware
   distinto al de desarrollo.
3. Validar DualSense, Switch Pro y reconexión en caliente.
4. Revisar casos límite de guardado y carga con personajes personalizados.
5. Añadir actualizador/firma y pulir el instalador.

## Instalación

1. Descomprime el ZIP.
2. Ejecuta `BakuganSetup.exe` (deja la carpeta `payload` al lado).
3. Selecciona tu ISO, carpeta de instalación y accesos directos.
4. El instalador extrae `EBOOT.elf` con RPCS3; si fallara, permite indicar un
   `EBOOT.elf` de respaldo.
5. Juega desde el launcher instalado.

Partidas: `%LOCALAPPDATA%\BakuganRecomp\dev_hdd0\home\00000001\savedata`  
Ajustes: `%LOCALAPPDATA%\BakuganRecomp\player.ini`

## Verificación de esta entrega

- Build Release clang-cl.
- Instalación completa probada desde ISO retail, sin pasar `EBOOT.elf`.
- RPCS3 `--decrypt` produjo un ELF idéntico al de referencia (mismo SHA-256).
- Instalador y desinstalador probados; el desinstalador elimina archivos,
  accesos y registro, conservando partidas.
- Launcher instalado arranca el juego instalado, sin consola y con el título
  limpio; el launcher se cierra al iniciar el juego.
- Replays de historia y batalla usados para validar imagen y HUD.
- Caché de shaders validada incluida en el instalador; el juego escribe log en
  `%LOCALAPPDATA%\BakuganRecomp\BakuganRecomp.log`.

## Créditos

RPCS3 (GPLv2): <https://github.com/RPCS3/rpcs3>  
SDL3, FSR1 y el resto de componentes conservan sus licencias.  
Bakugan: Battle Brawlers es propiedad de sus titulares.
