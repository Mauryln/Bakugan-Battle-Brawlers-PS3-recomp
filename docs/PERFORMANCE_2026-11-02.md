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

## Cach� de v�rtices: medido y descartado

Hip�tesis (del usuario): reutilizar los v�rtices ya decodificados en el mismo
frame. Se midi� primero si la geometr�a se repite.

- Contador `dups` en `[PERF]`: **18 700 / 42 400 callbacks por intervalo**
  = **44 %** de los draws indexados repiten `(shader, dma, primitiva, first,
  count)` dentro del mismo frame. La hip�tesis era correcta.
- Implementado `VP_CACHE=1`: en un acierto se reutiliza el rango can�nico ya
  escrito, sin tocar el empaquetado posterior.
  - Ganancia real: `prim=6` 547 ? 485 ms (-11 %), FPS 14,5 ? 15,5 (-4 %).
    Los hits cubr�an s�lo el 31 % de las repeticiones porque la clave inclu�a
    el shader de fragmentos, que no interviene en la decodificaci�n.
  - Quitando el shader de la clave: FPS 16-18, pero **el render se rompe**:
    pelo del personaje mal y parpadeos en arena y personajes, reproducido en
    3 arenas distintas.
- **Causa**: el juego reescribe los buffers de v�rtice entre pasadas sin
  cambiar nada observable desde el estado RSX. La clave no puede detectarlo.

**Conclusi�n:** un cach� de v�rtices necesita una **se�al de escritura del
guest** (dirty tracking por p�gina o contador global de escrituras) que el
runtime hoy no expone. Sin eso, reutilizar entre pasadas dibuja datos viejos.
Descartado hasta que exista esa se�al. El c�digo queda apagado por defecto y
documentado como intento fallido, no borrado, para no repetirlo.

## Estado del rendimiento tras el d�a

- Baseline: **14,5-15 FPS**, gr�ficos correctos.
- �nico cambio aplicado y validado: `rsx_attrib_base` / `rsx_fetch_attrib_base`
  (resolver hoisteado, byte-id�ntico, +0,5 %).
- Lo que queda, por tama�o: decodificaci�n de 488k v�rtices (**33 ms**, 79 ns c/u),
  walker FIFO (~15 ms), subida a WC (8,4 ms), SRV (4 ms), PSO (2,5 ms).
- **No hay ninguna optimizaci�n pendiente que sea segura y grande.** El �nico
  margen real es el conversor vectorizado por atributo (79 ? ~25 ns, -10 ms) y
  el walker; ambos son trabajo de varias sesiones.

## Reparto final del fetch, con contadores nuevos (02/11, tarde)

`[PERF detail]` ahora imprime `vfetch` (fetch completo del strip),
`def` (copia de los 256 B de defaults) y `attr` (bucle de atributos),
muestreados 1/32 y 1/64 para no pagar el cronometro.

Valores por intervalo de 20 frames, escena de batalla quieta:

| Contador | s/intervalo | ms/frame | Que es |
|---|---|---|---|
| `vfetch` | 0,551 | **27,6** | todo el fetch del strip |
| `def` | 0,212 | **10,6** | copiar 256 B de defaults por vertice |
| `attr` | 0,373 | **18,7** | 2,65 M lecturas de atributo (7 ns cada una) |

`vfetch` = `def` + `attr`: el coste del camino de vertices esta
completamente explicado, no queda nada oculto. 531k vertices/frame.

## Tecnica: el instrumento era parte del problema

Tres veces medi mal antes de acertar, y conviene recordarlo:

1. `perf_now()` llamaba a `QueryPerformanceFrequency` en cada invocacion,
   dos veces por draw. Se probo muestreando 1 de 32: el numero no cambio, asi
   que no era el coste. Pero la duda habia que quitarla.
2. Las canarias de tamano (`PS3_CANARY_COPY64`) usaron un `memcpy` de
   tamano **variable**, que se convierte en llamada a libreria y tapa la
   ganancia que se queria medir. Un recorte de 3,7 % no significaba "las copias
   no cuestan", significaba "el instrumento se comio el ahorro".
3. `rsx_fetch_attrib_inl` se puso en el header como `static inline`, pero
   el backend seguia llamando a `rsx_fetch_attrib_base`, que es una **funcion
   externa**: el cuerpo estaba inlineado y la llamada no. Solo al llamar al
   inline directamente bajo `attr` de 0,435 a 0,373 s.

## Cambios aplicados y validados

| Cambio | Efecto | Verificacion |
|---|---|---|
| `rsx_attrib_base` hoisteado | resolver 1 vez por draw en vez de 2,4 M | VPCHK 0 desviaciones |
| `pshufb` (SSSE3) para 4 floats BE | -14 % en `attr` | VPCHK 0 desviaciones |
| llamada al inline desde el backend | `attr` 0,435 -> 0,373 s | VPCHK 0 desviaciones |

Neto del dia: **14,5 -> 14,8 FPS**. Pequeno, pero los tres son byte-identicos.

## Lo que queda, con numeros

1. **`def`: -10,6 ms** eliminando la copia de defaults. Es el unico trozo
   grande y seguro que queda. Receta completa en la seccion siguiente.
2. **`attr`: 7 ns por atributo** esta cerca del suelo escalar; bajar mas exige
   leer menos atributos, no leerlos mas rapido.
3. **Walker FIFO (~15 ms) y subida a WC (8,4 ms)**: sin explorar.

## Techo medido: 26,76 FPS

La canaria `PS3_CANARY_NOVTX=1` (elimina TODO el trabajo de strip) da
**26,76 FPS**. Es decir: **30 FPS es imposible optimizando solo los vertices**,
por buenos que sean esos cambios. Hacen falta el walker y la subida a WC.

## Receta para eliminar `def` (intentado 2 veces, roto 2 veces)

Los dos fallos fueron por decidir la mascara en dos sitios distintos: el
relleno usaba una y `vp_upload_compact_frame` recalculaba otra.

1. `vp_prepare_vertex_fetch(state, mask)`: el plan guarda `mask` y la lista
   de atributos de la mascara, no de los arrays habilitados.
2. `vp_fetch_vertex`: sin `memcpy` de defaults; un
   `rsx_fetch_attrib_inl` por atributo de la mascara. Si el array esta
   deshabilitado esa funcion ya escribe el registro constante, asi que el
   resultado es identico.
3. La mascara se decide **una vez, en el drain**, y
   `vp_upload_compact_frame` usa `dr->gpu_input_mask` en vez de recalcular.
4. Si `!dr->fp_addr` en el drain -> `mask = 0xFFFF` (el pipeline base lee
   16 slots). Si al final `s_frame_psos[i] == NULL` con mascara compacta,
   saltar el draw y loguear `[VPMASK]`. Medido: no ocurre (el ratio
   upload/canonical 0,2945 lo demuestra).
5. Verificar con `VPCHK` **extendido a todo el stream**: contar emisiones y
   comparar cada bloque, no 200 vertices por draw.

Ganancia esperada: 67 -> 57 ms (~17,5 FPS).

## Relleno compacto: aplicado y SIN efecto en FPS

Implementado el paso 1-5 de la receta (mascara decidida una sola vez, guardia
`[VPMASK]`, empaquetado con la mascara registrada):

- `def` 0,212 s -> **0,000** (los 10,6 ms desaparecen)
- `VPCHK`: **0 desviaciones**
- `[VPMASK]`: **0 avisos**
- graficos correctos
- **FPS: 14,8 -> 14,6. Sin mejora.**

## El hallazgo que cambia el problema

Es la cuarta vez que se elimina un bloque grande medido (resolver, copias,
llamadas, `def`) y el FPS no se mueve. Por tanto:

**el frame NO es la suma de lo que mide `[PERF]`.**

Desglose actual: `submit` ~34 ms + `render_frame` ~16 ms = 50 ms, pero el
frame real es 68,5 ms. **Hay ~18 ms fuera de toda medida**, en el cubo
`guest`: walker del FIFO, `rsx_backend_pump` y lo que sea que espere el
hilo `frame_clock`. Mientras no se sepa que son esos 18 ms, optimizar el
camino de dibujo no puede mover el marcador: se esta quitando agua de un cubo
que no es el que desborda.

Siguiente paso obligado (no es dibujo): instrumentar el bucle de `frame_clock`
por fases -- drain, pump, espera -- y los 18 ms del cubo `guest`.

## Fase de carga: medida, no es I/O, no es render, es compute de SPU

Sintomas reportados: la pantalla de carga cae a 5-9 FPS y se congela segundos;
el audio de la primera cinematica empieza ~1 s antes que los modelos.

Medido con contadores y muestreo de hilos por CPU:

| Comprobacion | Resultado |
|---|---|
| Lecturas de fichero (`PS3_FSSTAT`) | **menos de 64** en toda la carga |
| Ejecuciones SPU via `spu_run_with_halt` | 0 |
| Vertices / draws durante la carga | 0k / ~32 |
| Hilo con mas CPU | **`spu_exec` al 99 %** (un solo nucleo) |
| Hilos `spurs_pm_worker` / `spurs_kernel` | ~1-2 %: duermen bien |
| `audio_mix` | ~1 % |

Conclusion: la carga la ejecuta **un hilo SPU liftado al 99 % de un nucleo**,
en serie. No hay espera ni bloqueo (una espera acotada en `spu_rdch` no cambio
nada), es computo real. El PPU avanza a trompicones porque compite con ese
nucleo, y de ahi el audio adelantado y los 5 FPS.

Para acelerarlo hay que optimizar la ejecucion del codigo SPU liftado, no el
render ni el VFS. Es una linea de trabajo distinta de la del gameplay.

## Instrumentacion de hilos del port

`[PORTTID] nombre=tid` se imprime al arrancar cada hilo del port:
`spurs_pm_worker`, `spurs_kernel`, `audio_mix`, `spu_async`,
`spu_exec`, `spu_fallback`, `pm_write_watch`, `timer_thread`,
`raw_spu`, `spurs_jc`. Sin eso no se puede correlacionar CPU con rol
(Windows oculta la direccion de inicio sin elevacion).
