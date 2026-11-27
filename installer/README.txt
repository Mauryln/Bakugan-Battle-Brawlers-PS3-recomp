Bakugan: Battle Brawlers (BLES00758) - Port nativo para Windows x64
====================================================================

Versión 1.4.0 - Windows x64

QUÉ ES
------
Recompilación estática del juego de PS3 (PPU/SPU) a código nativo de Windows
x64 con runtime HLE sobre D3D12, WASAPI y SDL3. El juego en ejecución corre
100 % nativo sin emuladores; el jugador aporta su propia copia del juego.

REQUISITOS
----------
- Windows 10/11 x64 con Direct3D 12.
- Tu propia ISO o carpeta extraída del juego (BLES00758). Es un título sólo en disco.
- ~2 GB libres para los datos del juego y ~100 MB para el instalador.

INSTALACIÓN
-----------
1. Extrae todo el ZIP y ejecuta BakuganSetup.exe junto a payload.
2. Selecciona tu archivo .iso o tu carpeta extraída del juego (que contenga
   PS3_GAME o directamente PS3_GAME\).
3. Elige la carpeta de instalación.
4. Marca/desmarca el icono de escritorio y el acceso del menú Inicio.
5. Pulsa Instalar / Actualizar.

El instalador copia los datos de PS3_GAME y extrae EBOOT.elf nativamente
mediante SelfExtract en C# (sin incluir ningún emulador en el instalador ni
en la carpeta final del juego). En modo silencioso acepta además `--eboot <ruta>`
como respaldo opcional por línea de comandos.

El juego compila y guarda su caché de shaders en el primer uso. El paquete
incluye los shaders de FSR1 y la biblioteca SDL3.

Al terminar, abre "Bakugan: Battle Brawlers" desde el escritorio, el menú Inicio
o BakuganLauncher.exe de la carpeta instalada, y pulsa Jugar. El launcher se
cierra solo al iniciar el juego. Para usarlo en modo portable, abre
payload\BakuganLauncher.exe con la carpeta payload completa extraída.

El juego funciona a 30 FPS nativos. FSR 3.1 es opcional y puede presentar
hasta 60 FPS mediante interpolación en combates compatibles.

ACTUALIZAR DESDE 1.2.0
----------------------
Cierra el juego y el launcher. Abre el nuevo BakuganSetup.exe, selecciona
la carpeta donde ya está instalado el juego y deja la ISO en blanco.
Pulsa Instalar / Actualizar. El instalador reutiliza los datos del juego
y conserva las partidas y los ajustes. Para reparar los datos del juego,
selecciona también tu ISO.

Si el juego se cierra, envía `%LOCALAPPDATA%\BakuganRecomp\BakuganRecomp.log`.

PARTIDAS Y AJUSTES
------------------
Partidas: %LOCALAPPDATA%\BakuganRecomp\dev_hdd0\home\00000001\savedata
Ajustes:  %LOCALAPPDATA%\BakuganRecomp\player.ini

El desinstalador conserva esas partidas y ajustes.

DESINSTALAR
-----------
Configuración > Aplicaciones > Bakugan: Battle Brawlers > Desinstalar, o
"Uninstall Bakugan" en el menú Inicio. Los archivos del juego se borran; las
partidas se conservan.

ESTADO Y LÍMITES
----------------
- Se corrigió el oscurecimiento de los Bakugan en la pantalla de duelo.
- El cursor se mueve libremente al usar el mouse para Sixaxis.
- Se corrigió el retrato del jugador en Modo Historia en la primera sesión
  (fijado/LRU de render targets offscreen, alias EA y estampado NV309E).
- Multijugador local habilitado con hasta 7 puertos independientes (PS4,
  Nintendo Switch Pro, Xbox/SDL3 y teclado/ratón dedicado), también en inicio
  directo desde BakuganBrawlers.cmd o BakuganRecomp.exe.
- Corregido el giro completo de 360° de los sticks analógicos en minijuegos.
- Menús, tutorial de historia, arena de batalla, HUD, cartas, cinemáticas y audio
  funcionan en la build de referencia.
- Límite de FPS fijo a 30.
- 30 FPS estables en todas las escenas sigue pendiente; el cuello es trabajo
  serial de CPU, no la GPU.
- Si falla la entrada a una arena o aparecen problemas de HUD o texturas,
  envía el log indicado arriba.
- Probado con DualShock 4, Nintendo Switch Pro Controller y teclado/ratón;
  otros mandos compatibles con SDL3 o XInput siguen el mismo puente multipuerto.
- El sistema de guardado funciona, pero quedan casos límite por validar.
- No hay actualizador automático.

CRÉDITOS
--------
Gracias a ps3recomp (Ned Heller / sp00nznet) por el toolkit de recompilación
PPU/SPU y el runtime HLE: https://github.com/sp00nznet/ps3recomp (MIT).
SDL3, FSR1, FSR 3.1 y el resto de componentes mantienen sus licencias.
Bakugan: Battle Brawlers es propiedad de sus titulares. No se distribuye ningún
código ni dump del disco del juego.
