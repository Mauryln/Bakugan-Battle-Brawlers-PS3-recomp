# Vídeos de Bakugan

## Uso

Windows 10/11 con Media Foundation instalada. No se añade FFmpeg ni paquetes
externos. Se necesitan los MP4 de la copia del usuario en `game/PS3_GAME/USRDIR/MOVIE/`.
Compilar y ejecutar como siempre:

```powershell
.\.venv\Scripts\python.exe tools\pipeline.py --only patch
.\.venv\Scripts\python.exe tools\pipeline.py --only hle-nids --force
.\.venv\Scripts\python.exe tools\build.py
.\tools\run.ps1
```

El juego selecciona las películas y permite saltarlas con su control normal.
`BAKUGAN_VIDEO_TRACE=1` registra las entradas del estado de película y los pasos
de la secuencia inicial. Las unidades recompiladas se regeneran sin editarlas.

## Implementación

- `video_decoder.cpp`: Source Reader de Media Foundation, H.264 → BGRA8,
  normalización del stride, marcas temporales de 100 ns y EOF.
- `movie_video.cpp`: decodificación y reloj de presentación en un hilo propio;
  continúa sin depender del polling SAIL del juego. Abre, usa y cierra COM/MF
  en ese mismo hilo; pausa y cierre sincronizan su finalización.
- `movie_audio.cpp`: AAC → float estéreo a 48 kHz; salida `waveOut` en un hilo
  propio. Pausa y cierre cancelan/descargan sus buffers antes de liberarlos.
- `sail_video.cpp`: adapta el ABI PPU mediante handlers de contexto. Conserva
  los tokens y eventos SAIL de boot/open/start/stop/close, las URI del disco,
  el cambio de callback durante boot y la reutilización del player.
- D3D12 presenta un plano de vídeo nativo, con copia propia protegida por lock.
  Al cerrar o saltar un clip se retira ese plano. El renderer invitado recibe
  `NO_FRAME`: no se crean texturas duplicadas en el heap del juego.

Se eligió presentación nativa porque la ruta invitada de composición seguía
mostrando negro incluso después de decodificar y entregar los frames. La composición del menú se corrige por separado mediante
`0003-menu-mask.patch`. El audio de película
sale por el dispositivo predeterminado de Windows; no pasa por el mezclador del
juego. El reloj de vídeo sigue avanzando cuando el bucle invitado deja de consultar
`GraphicsAdapterGetFrame`. Los eventos de EOF se entregan desde el hilo invitado
cuando vuelve a consultar, sin ejecutar callbacks PPU desde el decodificador.

Las correcciones reutilizables están en `patches/0002-sail-video.patch`, aplicado
después de `0001`. Incluyen Future initialize/set/get para completados síncronos,
finalización/reutilización de slots, cambio de event handler, copia segura de URI
a memoria invitada y la interfaz opcional del plano de vídeo. No es una
implementación general de todo SAIL ni de sus esperas asíncronas.

## Diagnóstico y pruebas

La hipótesis inicial de «cero llamadas SAIL» correspondía al tramo anterior al
aviso legal. Después de confirmarlo, el juego sí llegaba a `PlayerBoot`; faltaban
los imports Future y la espera leía un resultado sin inicializar. Además, los
slots del player no se liberaban entre intentos.

```powershell
.\.venv\Scripts\python.exe tools\test_video_decoder.py
.\.venv\Scripts\python.exe tools\test_sail_lifecycle.py
.\.venv\Scripts\python.exe tools\test_sail_video.py
```

Las pruebas verifican imágenes distintas y PTS ordenados, EOF, errores de apertura,
límites de Future, diez ciclos de player, capacidad agotada, URI truncada,
avance durante 250 ms sin consultas invitadas, pausa/reanudación, reapertura,
cierre durante pausa, dimensiones al alcanzar EOF
y retirada del plano sin asignaciones invitadas de vídeo.
Artefactos y logs se guardan en `diagnostics/video/` (ignorado por Git).

Comprobado en ejecución: Activision (311 frames), NOWPRO (180), Premium Agency
(175) y Nelvana (211), con decodificación de audio. Se capturó Nelvana visible a
30 fps. `OPENING_16_9.MP4` completó sus 1.080 frames en el juego. `INTRO_16_9_ENG.MP4` completó sus 977 frames dentro del juego y regresó al menú
sin saltarlo (validación 2026-10-24, `diagnostics/stability/narrative.*`).
El decodificador también verificó los 977 frames de la introducción narrativa
completa, con 963 imágenes distintas y PTS final de 40,708659 segundos.

El crash de heap también se reprodujo con SAIL desactivado. La causa confirmada
fue `cellSpursJoinTaskset` retornando antes de que las tareas SPU dejaran de
escribir una LFQueue ya liberada. `0004-taskset-shutdown-and-callbacks.patch`
añade cierre cooperativo en puntos seguros de canales SPU y una espera real de
Join. También aísla stacks/TLS de callbacks PPU concurrentes y anidados.

```powershell
.\.venv\Scripts\python.exe tools\test_guest_callbacks.py
.\.venv\Scripts\python.exe tools\test_spurs_join.py
.\.venv\Scripts\python.exe tools\test_pipeline_patches.py
```

Referencias: [Source Reader de Microsoft](https://learn.microsoft.com/en-us/windows/win32/medfound/processing-media-data-with-the-source-reader)
y [estructuras SAIL de RPCS3](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/Cell/Modules/cellSail.h).
