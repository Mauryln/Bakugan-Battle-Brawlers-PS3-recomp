Bakugan: Battle Brawlers (BLES00758) - Port nativo para Windows x64
====================================================================

Versión 1.1.0 - Windows x64

QUÉ ES
------
Recompilación estática del juego de PS3 (PPU/SPU) con un runtime HLE que usa
D3D12, WASAPI y SDL3. El jugador aporta su propia ISO del disco.

REQUISITOS
----------
- Windows 10/11 x64 con Direct3D 12.
- Tu propia ISO o dump del juego (BLES00758). Es un título sólo en disco.
- ~2 GB libres para los datos del juego y ~175 MB para el instalador.

INSTALACIÓN
-----------
1. Extrae todo el ZIP y ejecuta BakuganSetup.exe junto a la carpeta payload.
2. Selecciona tu ISO.
3. Elige la carpeta de instalación.
4. Marca/desmarca el icono de escritorio y el acceso del menú Inicio.
5. Pulsa Instalar.

El instalador copia los datos de PS3_GAME y extrae y descifra EBOOT.elf
automáticamente con el RPCS3 portable incluido.

El juego compila y guarda su caché de shaders en el primer uso. El paquete
incluye los shaders de FSR1 y la biblioteca SDL3.

Al terminar, abre "Bakugan: Battle Brawlers" desde el escritorio, el menú Inicio
o BakuganLauncher.exe de la carpeta instalada, y pulsa Jugar. El launcher se
cierra solo al iniciar el juego.

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
- Menús, tutorial de historia, arena de batalla, HUD, cartas, cinemáticas y audio
  funcionan en la build de referencia.
- Límite de FPS fijo a 30.
- 30 FPS estables en todas las escenas sigue pendiente; el cuello es trabajo
  serial de CPU, no la GPU.
- Hay artefactos visuales aleatorios en investigación: texturas negras, retrato
  del jugador que a veces no carga y fondo gris tras cargar una partida. Esta
  build incluye correcciones de caché/vértices y de invalidación al cargar
  partidas, pero falta validación amplia en más PC y partidas.
- Si falla la entrada a una arena o aparecen problemas de HUD o texturas,
  envía el log indicado arriba.
- Cobertura de mandos limitada: DS4 probado; DualSense/Switch Pro pendientes.
- El sistema de guardado funciona, pero quedan casos límite por validar.
- No hay actualizador automático.

CRÉDITOS
--------
Gracias a ps3recomp (Ned Heller / sp00nznet) por el toolkit de recompilación
PPU/SPU y el runtime HLE: https://github.com/sp00nznet/ps3recomp (MIT).
RPCS3 portable (GPLv2) incluido únicamente para `rpcs3.exe --decrypt`.
Fuente: https://github.com/RPCS3/rpcs3
SDL3, FSR1 y el resto de componentes mantienen sus licencias.
Bakugan: Battle Brawlers es propiedad de sus titulares. No se distribuye ningún
asset, código, clave ni lift del juego.
