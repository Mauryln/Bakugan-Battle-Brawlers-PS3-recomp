# Rendimiento â€” cierre 01/11/2026

Objetivo pendiente: 30 FPS sostenidos (33,3 ms/cuadro), manteniendo efectos,
cuadros completos, audio y temporizaciÃ³n. La build actual NO alcanza ese
objetivo: el usuario reporta gameplay <=15 FPS y el perfil de Aquos ronda
12 FPS en la vista detallada. No se optimizÃ³ ni se validÃ³ todavÃ­a el flujo
compacto de vÃ©rtices en el juego.

## ReproducciÃ³n y mediciones

Corrida sin capturas ni FP_DUMP: `diagnostics/arena/aquos-perf-20261101`.
PERF=1; el usuario recorriÃ³ carga, presentaciones y turno mirando al rival /
zona despejada. Los intervalos agrupan 20 cuadros; categorÃ­as anidadas no
se suman. `guest` es el tiempo restante fuera de render_frame, no PPU puro.

| Tramo | Evidencia | InterpretaciÃ³n |
|---|---|---|
| Loading inicial | 5,03 / 4,64 / 4,57 FPS; render_frame ~0,01 s por intervalo, guest 3,97 / 4,30 / 4,37 s | El atasco inicial estÃ¡ fuera del renderizador; medir PPU/SPU, carga y esperas antes de atribuirlo a memoria GPU |
| Primera geometrÃ­a | VBO crece a 384, 768 y 1536 MiB; intervalo 3,05 FPS, submit/carga altos | Sigue el crecimiento masivo de CPU + UPLOAD; separar esta fase de la carga inicial |
| Gameplay detallado | ~12,0 FPS; intervalo ~1,66 s | ~83 ms/cuadro, frente al objetivo de 33,3 ms |
| PreparaciÃ³n de draws | submit ~0,79â€“0,82 s (~48â€“49%) | Principal bloque medido en el hilo de render |
| render_frame | ~0,39â€“0,41 s (~24â€“25%) | Incluye preparaciÃ³n y copia de vÃ©rtices |
| Copia de vÃ©rtices | ~0,225â€“0,232 s / 20 cuadros | ~11â€“12 ms/cuadro en la copia CPU â†’ UPLOAD |
| VS lookup / constantes | ~0,051 / 0,032 s | ~3% / 2%; no son el primer objetivo |
| Fence GPU | 0â€“0,023 s | No domina estos intervalos |
| PSO FP | 0 MISS con cachÃ© caliente; ~0,05 s | No atribuir todo el atasco sostenido a compilar shaders |

La etiqueta `vtx` (~0,14â€“0,15 s) solo cronometra los uploads de TRIANGLES;
no cubre toda la conversiÃ³n de strips/quads. No interpretarla como el coste
total de geometrÃ­a. En cuadros capturados hay ~474kâ€“500k vÃ©rtices fÃ­sicos,
unos 116â€“122 MiB por cuadro a 256 bytes/vÃ©rtice.

El usuario volviÃ³ a reproducir audio adelantado a la imagen en las dos
primeras presentaciones de personajes, bajada en la presentaciÃ³n del campo
y gameplay lento. La mejora parcial de `0037` no cierra esos sÃ­ntomas.

## Muestreo nativo de CPU

Muestras de 8 segundos del proceso, sin cambiar el ejecutable en ejecuciÃ³n:
`cpu-raw.txt`, `modules.txt`, `cpu-samples.txt` en la corrida de perfil. El
mapa de sÃ­mbolos se generÃ³ desde los mismos objetos. Se verificaron
idÃ©nticos los 18861 rangos de funciones .pdata antes de usar sus direcciones.
No son stacks completos ni un benchmark exacto. WPR no pudo habilitar el
perfil de sistema; se usÃ³ un muestreador local del proceso.

- Hilo de render: ~7,1 s de CPU / 8 s. ~16,7% de muestras en rsx_fetch_attrib,
  ~4,9% read_vp_vertex, ~6,5% nv3089_blit, ~3,3% vp_get_vs. Gran parte restante
  cae en bibliotecas del sistema/runtime; no adjudicarles funciones exactas
  usando solo el sÃ­mbolo pÃºblico mÃ¡s cercano.
- El mezclador/audio SPU ocupa otro nÃºcleo (~8 s CPU). Esto no demuestra que
  limite los FPS y no autoriza omitir trabajo de audio/SPU.
- Fuentes locales del muestreador y sÃ­mbolos: `diagnostics/arena/window/`.
  No se suben binarios, sÃ­mbolos ni datos del juego a Git.

## PrÃ³ximo trabajo, en orden

1. Mantener una cÃ¡mara / escena de comparaciÃ³n y medir con PERF=1, sin
   capturas, RenderDoc ni volcados masivos durante la mediciÃ³n.
2. Integrar de forma incremental un stream GPU compacto por atributos
   realmente usados por el VP. El toolkit ya ofrece rsx_vp_analyze_inputs y
   rsx_vp_decompile_compact_ex. Ante anÃ¡lisis incierto, conservar los 16.
3. Separar capacidad CPU de capacidad del buffer GPU: no asignar 1536 MiB de
   UPLOAD por crecer el staging CPU. Medir crecimiento y bytes enviados.
4. DespuÃ©s optimizar decodificaciÃ³n de atributos / Ã­ndices y consultas de
   diagnÃ³stico dentro de bucles (VTX_DUMP aÃºn consulta getenv por quad).
5. Perfilar la loading inicial por separado (PPU/SPU / carga / esperas). No
   reducirla artificialmente saltando trabajos, cuadros o sincronizaciÃ³n.
6. Volver a comprobar el destello negro aislado tras sesiones largas.

## Prototipo histórico previo a la integración

`diagnostics/arena/compact-prototype-20261101/` guarda helpers de empaquetado y
las pruebas `test_rsx_compact_stream.py` / `rsx_compact_stream_test.cpp`.
La prueba aislada pasÃ³ para las 65536 mÃ¡scaras de atributos y rasterizado
WARP idÃ©ntico con ATTR3, ATTR8 y ATTR15 dispersos, usando el decompilador real.
NO estÃ¡ integrada en el backend; no se afirma mejora de FPS. Se retirÃ³ de
los tests activos y del cÃ³digo final para no publicar un trabajo incompleto.

IntegraciÃ³n pendiente: metadatos por VP, layouts de entrada por mÃ¡scara,
stride/offset GPU por draw, crecimiento GPU independiente, conservaciÃ³n del
fallback completo y capturas canÃ³nicas. La CPU puede conservar inicialmente
los 16 slots para aislar el cambio de formato GPU. DespuÃ©s medir y decidir
si conviene compactar tambiÃ©n la preparaciÃ³n CPU.

## Integración del prototipo compacto (0042)

El stream GPU compacto está activo por defecto en la build principal. El
análisis exacto del VP selecciona ATTRn sin renumerar sus semánticas; ante
control de flujo incierto conserva 16 atributos. Layout y stride se asocian
al shader elegido. Los pases que usan el pipeline de respaldo mantienen el
formato completo. `VP_COMPACT=0` permite comparar el camino de 16 atributos.

La capacidad GPU se calcula por los bytes del cuadro y crece después del
fence; crecer el staging CPU ya no crea otro UPLOAD del mismo tamaño. Las
capturas y los diagnósticos leen RAM canónica, no memoria write-combined.
La preparación CPU todavía conserva sus 16 slots, para poder usar el
pipeline de respaldo sin perder atributos.

Primera corrida: `diagnostics/arena/window/compact-run.err`. En la vista
detallada se registraron 119948288 bytes canónicos frente a 35454656 de
upload (~70% menos). GPU: 192 MiB por paridad, 384 MiB totales, frente al
crecimiento anterior de 1536 MiB. El empaquetado + copia ronda 0,16–0,18 s
por 20 cuadros, frente a ~0,23 s antes. No confundir la reducción de bytes
con una mejora proporcional de FPS: se observaron ~12,6–13,2 FPS y el usuario
confirmó que la mejora era pequeña. La cámara no fue una reproducción exacta
pixel a pixel del perfil anterior; estas cifras no son un benchmark A/B.

El usuario confirmó iluminación, sombras y reflejos correctos. Reportó ruido
de sonido y pantallazos negros al mover el personaje. La siguiente corrida
mostró también las esperas FIFO de 2000 ms del menú, investigadas en 0043.

Prueba activa: `tools/test_rsx_compact_stream.py`: las 65536 máscaras,
límites de escritura y equivalencia de píxeles WARP con ATTR3/8/15, offsets
de buffer distintos de cero y el decompilador real. La suite de render pasa
a 24 pruebas. El prototipo histórico de diagnostics se conserva como evidencia;
la implementación reproducible está en `patches/0042-d3d12-compact-vertex-upload.patch`.

## Reciclaje FIFO: causa reproducida y corrección 0043

En `aquos-compact-20261101` la pantalla principal presentó intervalos de
~5,3 FPS con `guest` ~3,77 s y render ~0,01 s por 20 cuadros. Coincidían con
`fifo recycle: drain stalled`, 2000 ms, y con el reciclador automático del
título. Ese mecanismo de recuperación también actuaba sobre nuestro callback
HLE: podía llevar GET al inicio sin consumir el JUMP que el productor esperaba.

0043 excluye del reciclador automático los contextos cuyo callback es el
sentinel HLE (directo o vía OPD). Mantiene la espera real de backpressure y
la recuperación para callbacks de títulos que no reciclan. Prueba ampliada:
`tools/test_gcm_fifo_wrap.py`.

En `diagnostics/arena/aquos-recycle-owner-20261101` los reciclajes observados
tardaron ~3–7 ms; no volvieron avisos de stalls ni de recuperación automática.
El usuario confirmó menú destrabado, audio de menús sin ruido y ausencia de
pantallazos en la prueba de Aquos. Persisten tirones desde loading, gameplay
reportado de 11 FPS y ruido de audio en batalla. No dar estos últimos por
resueltos ni la estabilidad prolongada por demostrada.

Se repitió muestreo CPU de gameplay, verificando idéntica .pdata en el mapa
auxiliar y el ejecutable. El hilo de render ocupa ~7,3 s de CPU/8 s; fetch de
atributos ~17% de muestras. Las pilas adicionales señalan también consultas
CRT de diagnóstico dentro de `d3d12_bind_texture`. El audio/SPU sigue ocupando
un núcleo; esto no demuestra que pueda omitirse ni que sea el único límite.
