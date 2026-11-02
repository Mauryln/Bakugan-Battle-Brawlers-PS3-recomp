# Rendimiento — cierre 01/11/2026

Objetivo pendiente: 30 FPS sostenidos (33,3 ms/cuadro), manteniendo efectos,
cuadros completos, audio y temporización. La build actual NO alcanza ese
objetivo: el usuario reporta gameplay <=15 FPS y el perfil de Aquos ronda
12 FPS en la vista detallada. No se optimizó ni se validó todavía el flujo
compacto de vértices en el juego.

## Reproducción y mediciones

Corrida sin capturas ni FP_DUMP: `diagnostics/arena/aquos-perf-20261101`.
PERF=1; el usuario recorrió carga, presentaciones y turno mirando al rival /
zona despejada. Los intervalos agrupan 20 cuadros; categorías anidadas no
se suman. `guest` es el tiempo restante fuera de render_frame, no PPU puro.

| Tramo | Evidencia | Interpretación |
|---|---|---|
| Loading inicial | 5,03 / 4,64 / 4,57 FPS; render_frame ~0,01 s por intervalo, guest 3,97 / 4,30 / 4,37 s | El atasco inicial está fuera del renderizador; medir PPU/SPU, carga y esperas antes de atribuirlo a memoria GPU |
| Primera geometría | VBO crece a 384, 768 y 1536 MiB; intervalo 3,05 FPS, submit/carga altos | Sigue el crecimiento masivo de CPU + UPLOAD; separar esta fase de la carga inicial |
| Gameplay detallado | ~12,0 FPS; intervalo ~1,66 s | ~83 ms/cuadro, frente al objetivo de 33,3 ms |
| Preparación de draws | submit ~0,79–0,82 s (~48–49%) | Principal bloque medido en el hilo de render |
| render_frame | ~0,39–0,41 s (~24–25%) | Incluye preparación y copia de vértices |
| Copia de vértices | ~0,225–0,232 s / 20 cuadros | ~11–12 ms/cuadro en la copia CPU → UPLOAD |
| VS lookup / constantes | ~0,051 / 0,032 s | ~3% / 2%; no son el primer objetivo |
| Fence GPU | 0–0,023 s | No domina estos intervalos |
| PSO FP | 0 MISS con caché caliente; ~0,05 s | No atribuir todo el atasco sostenido a compilar shaders |

La etiqueta `vtx` (~0,14–0,15 s) solo cronometra los uploads de TRIANGLES;
no cubre toda la conversión de strips/quads. No interpretarla como el coste
total de geometría. En cuadros capturados hay ~474k–500k vértices físicos,
unos 116–122 MiB por cuadro a 256 bytes/vértice.

El usuario volvió a reproducir audio adelantado a la imagen en las dos
primeras presentaciones de personajes, bajada en la presentación del campo
y gameplay lento. La mejora parcial de `0037` no cierra esos síntomas.

## Muestreo nativo de CPU

Muestras de 8 segundos del proceso, sin cambiar el ejecutable en ejecución:
`cpu-raw.txt`, `modules.txt`, `cpu-samples.txt` en la corrida de perfil. El
mapa de símbolos se generó desde los mismos objetos. Se verificaron
idénticos los 18861 rangos de funciones .pdata antes de usar sus direcciones.
No son stacks completos ni un benchmark exacto. WPR no pudo habilitar el
perfil de sistema; se usó un muestreador local del proceso.

- Hilo de render: ~7,1 s de CPU / 8 s. ~16,7% de muestras en rsx_fetch_attrib,
  ~4,9% read_vp_vertex, ~6,5% nv3089_blit, ~3,3% vp_get_vs. Gran parte restante
  cae en bibliotecas del sistema/runtime; no adjudicarles funciones exactas
  usando solo el símbolo público más cercano.
- El mezclador/audio SPU ocupa otro núcleo (~8 s CPU). Esto no demuestra que
  limite los FPS y no autoriza omitir trabajo de audio/SPU.
- Fuentes locales del muestreador y símbolos: `diagnostics/arena/window/`.
  No se suben binarios, símbolos ni datos del juego a Git.

## Próximo trabajo, en orden

1. Mantener una cámara / escena de comparación y medir con PERF=1, sin
   capturas, RenderDoc ni volcados masivos durante la medición.
2. Integrar de forma incremental un stream GPU compacto por atributos
   realmente usados por el VP. El toolkit ya ofrece rsx_vp_analyze_inputs y
   rsx_vp_decompile_compact_ex. Ante análisis incierto, conservar los 16.
3. Separar capacidad CPU de capacidad del buffer GPU: no asignar 1536 MiB de
   UPLOAD por crecer el staging CPU. Medir crecimiento y bytes enviados.
4. Después optimizar decodificación de atributos / índices y consultas de
   diagnóstico dentro de bucles (VTX_DUMP aún consulta getenv por quad).
5. Perfilar la loading inicial por separado (PPU/SPU / carga / esperas). No
   reducirla artificialmente saltando trabajos, cuadros o sincronización.
6. Volver a comprobar el destello negro aislado tras sesiones largas.

## Prototipo conservado, FUERA de la build

`diagnostics/arena/compact-prototype-20261101/` guarda helpers de empaquetado y
las pruebas `test_rsx_compact_stream.py` / `rsx_compact_stream_test.cpp`.
La prueba aislada pasó para las 65536 máscaras de atributos y rasterizado
WARP idéntico con ATTR3, ATTR8 y ATTR15 dispersos, usando el decompilador real.
NO está integrada en el backend; no se afirma mejora de FPS. Se retiró de
los tests activos y del código final para no publicar un trabajo incompleto.

Integración pendiente: metadatos por VP, layouts de entrada por máscara,
stride/offset GPU por draw, crecimiento GPU independiente, conservación del
fallback completo y capturas canónicas. La CPU puede conservar inicialmente
los 16 slots para aislar el cambio de formato GPU. Después medir y decidir
si conviene compactar también la preparación CPU.
