# Rendimiento — medidas del 02/11/2026

Todo lo de esta página está medido en el juego, no deducido. Escena:
batalla, quieto, mirando al rival. Build Release D3D12, `PERF=1`.

## Referencia: el emulador

RPCS3 (`rpcs3-v0.0.43-20372`) en la **misma batalla y el mismo punto**:
**29,9 FPS** con ~2,5 núcleos. El juego está **bloqueado a 30 FPS**, así que
ese es el techo real; 60 FPS sólo se obtiene forzando el desbloqueo.

| | Recomp | RPCS3 |
|---|---|---|
| FPS | 14,8 | 29,9 |
| frame | 67 ms | 33 ms |

## Reparto del frame (67 ms)

| Bloque | ms/frame | Fuente |
|---|---|---|
| `indexed prim=6` (triangle strips) | **26,7** | `[PERF primitive] indexed prim=6` |
| `indexed prim=5` (triangles) | 5,9 | `[PERF primitive] indexed prim=5` |
| `prepass` PSO/SRV | **14,5** | `[PERF] prepass` |
| subida a memoria write-combined | 8,5 | `[PERF detail] vertex flush` |
| walker FIFO + resto | ~11 | derivado |
| GPU | 9 | PresentMon `msGPUActive` |

Volumen real por frame (contadores `draws`/`verts` añadidos a `[PERF]`):

- **1704 draws** por frame.
- **488 000 vértices** canónicos (256 B cada uno) = **126 MB/frame** de staging.
- La GPU sólo consume **37 MB** (`[PERF vertex] canonical=126 MB upload=37 MB`).
- **282 vértices por draw** → no hay batches repetidos; la geometría es la que el
  juego emite.

## Qué se demostró y qué se descartó

| Hipótesis | Resultado |
|---|---|
| `Present`/vsync bloquea | **Falso**: `msInPresentAPI` 0,45 ms |
| La GPU es el cuello | **Falso**: 9 ms ocupada de 67 |
| Los hilos guest PPU son el cuello | **Falso**: ~2 % de un núcleo |
| El walker del FIFO es el cuello | **Falso**: `frame_clock` hace el frame pero el 92 % es trabajo de dibujo |
| Suspender el hilo del 100 % sube FPS | **Falso**: 14,33 vs 14,27 |
| El resolver por atributo cuesta | **Falso**: hoisteado → 14,7 vs 14,8 FPS, `prim=6` 543→533 ms |
| El cuello está en ALU | **Falso**: 250 MB/frame de `memcpy` a 9,4 GB/s lo explican |

**Canaria** (`PS3_CANARY_NOVTX=1`, quita todo el trabajo de strip): 14,05 →
**26,76 FPS**. Es decir, incluso con coste cero en strips, el frame sigue en
38 ms: hacen falta varias fases, no una.

## Los dos intentos que rompieron la geometría

Documentado para no repetirlo. Los dos parecían "mismos bytes, otro sitio" y
los dos rompieron arena y personajes:

1. **Escribir sólo los atributos de la máscara del shader.** Supuesto: los slots
   que el VS no lee no se empaquetan. Rompió la geometría.
2. **Cache de punteros en vez de copias en el bucle de strip.** Supuesto: el
   contenido de `out` en cada emisión es idéntico. Rompió la geometría.

Ninguno se pudo justificar con una comparación byte a byte, y por eso no hay
que volver a intentarlos sin instrumentación.

## Instrumentación que falta (prerrequisito)

`[VPCHK]`: para los primeros ~200 vértices de cada draw, calcular el bloque
canónico por la vía actual y compararlo con la variante, escribiendo
`[VPCHK] OK v=…` o `[VPCHK] MISMATCH v=… attr=… off=…`. Sin esto, cualquier
cambio en el bucle de vértices es una apuesta.

## Orden de trabajo propuesto

1. `[VPCHK]` (arriba). No es una optimización, es el permiso para optimizar.
2. Quitar **una sola** de las tres copias por vértice (fetch→cache,
   cache→vertex, vertex→out), medir, y sólo entonces la siguiente.
3. `prepass`: cachear descriptores PSO/SRV entre frames (14,5 → ~3 ms).
   No puede tocar un byte de geometría. Es el avance con mejor relación
   riesgo/beneficio que queda.
4. Subida a memoria write-combined y walker del FIFO.

## Sondas existentes (no rompen nada, apagadas por defecto)

| Variable | Para qué |
|---|---|
| `PERF=1` | línea `[PERF]` cada 20 frames con reparto, `draws` y `verts` |
| `PS3_CANARY_NOVTX=1` | techo sin trabajo de strip (imagen rota a propósito) |
| `GCM_RATE=1` | drains/s del FIFO |
| `PS3_POLLTOP=<s>` | sitios de sondeo de `usleep` |

## Estado del árbol

- `ps3recomp-src` es un repo **anidado** con ~2000 líneas sin commitear: nunca
  hacer `git checkout` ahí.
- Cambios de hoy aplicados y validados: `rsx_attrib_base` /
  `rsx_fetch_attrib_base` (resolver hoisteado, byte-idéntico) y los contadores
  `draws`/`verts` en `[PERF]`.
- Los dos intentos que rompieron geometría están **revertidos**.
- Docs sin commitear.
