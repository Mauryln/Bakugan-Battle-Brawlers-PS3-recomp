# Rendimiento: qué se midió, qué no ayuda, qué podría ayudar

Actualizado el 03/11/2026. Todo lo que aparece como "no ayuda" está medido en
este juego, en batalla, con la misma cámara y `PS3_VERBOSE=0` (sin logs
redirigidos). No reabrir esas ideas sin un dato nuevo.

## Punto de partida, medido

16 FPS en batalla = **63 ms por cuadro**, repartidos así (ventanas de 20 cuadros):

| Tramo | ms | Qué es |
|---|---:|---|
| `submit` | 25 | decodificar vértices (15,6) + registrar el lote |
| walker del FIFO + blits NV3089 | 13 | recorrer comandos y copiar superficies |
| `render_frame` | 15 | prepass: *flush* 8,4 + *srv* 4 + *pso* 2 |
| espera antes del drain | 8 | `PS3_KICKWAIT`, 4 ms por defecto |
| **total** | **63** | 15,8 FPS |

Reparto de CPU por hilo (20 s): `spu_exec` ~100 % de un núcleo, `frame_clock`
~86 %, un tercer hilo ~62 %. **GPU 3D: 7,5 %** (pico 20,8 %). No hay contención
de núcleos: el cuadro lo limita el trabajo **serial** del `frame_clock`.

`[FRAME]` a 16 FPS: `drain 1,22 s` de 2 s, `pres 0,55 s`, `wait 0,25 s`.

## Lo que NO mejora el rendimiento (probado)

| Intento | Resultado medido |
|---|---|
| **Stream compacto de vértices** (escribir solo los slots de la máscara): `canonical` 109 → 36,8 MB, `flush` 8,4 → 3,8 ms | Sin cambio de FPS. Se revirtió; el parche está en `backups/fase1-compact-vertices-WIP-20261103.patch`. |
| **`PS3_KICKWAIT=0`** | No gana FPS y convierte la espera en un spin de ~73 000 iteraciones/s. Descartado. |
| **Caché de vértices por draw** (`VP_CACHE`) | ~20 % de aciertos (8 700 de 43 000 callbacks): ahorra ~2 M vértices/ventana, no mueve el FPS. |
| **Decodificación de vértices en GPU** (`GPU_VTX`, compute shader) | Imagen correcta y 315 draws en GPU, pero **64 MB/cuadro de rangos crudos** contra los 36 MB compactos que sustituye: `attr` −2 ms y FPS igual (15,1–15,8). Revertido, conservado en el commit `977186e` y en `docs/legacy/` de esta serie. |
| **SIMD a mano** en el lector de atributos, variante con `restrict` | Sin ganancia. |
| **Batch de 4 vértices** en el camino de strips | Sin ganancia apreciable; ya está aplicado. |
| **Deduplicar heaps/targets/PSO por clave** (`SRV_SHADOW`) | Rompe HUD y cinemáticas. Prohibido. |
| **Reloj del mixer** para el ruido de batalla | 187,5 bloques/s, cero descartes, huecos en el PCM: el ruido es síntoma del tiempo de cuadro, no causa. |
| **Banderas de optimización** del compilador | El backend y los TUs liftados ya van a `/O2`. |

## Lo que podría mejorar el rendimiento

Por orden de retorno esperado / riesgo, con la evidencia que hay hoy.

### 1. Sacar `submit` del hilo del reloj (productor/consumidor)

El `frame_clock` hace walker + decodificación + grabación en serie. Separarlos en
dos hilos (uno recorre el FIFO y encola draws; otro decodifica y graba) solapa
los 13 ms del walker con los 25 ms de `submit`. No cambia ni un byte del render y
es la vía más barata: **~13 ms menos de camino crítico** si el solape es bueno.
Riesgo: el orden de los draws y las fronteras de cuadro son estrictas.

### 2. Mover la decodificación de vértices a la GPU **con espejo de memoria**

Lo que se intentó (`GPU_VTX`) no gana porque copia rangos crudos (64 MB/cuadro).
La versión que sí puede ganar es un **espejo persistente de la memoria del guest
en VRAM, actualizado solo por las páginas escritas** (el contador de generación
por página de 4 KB ya existe en `a9743b0`): las mallas estáticas se suben una vez
y por cuadro viaja solo lo que cambió. El *gather* lee del espejo, sin copia por
draw. Techo estimado: −20 ms de CPU con la GPU al 7,5 %.

Si el juego reescribe los vértices cada cuadro (animación por SPU), esas páginas
se suben igual y el ahorro se limita a la geometría estática.

### 3. Especializar los blits NV3089 escalados

`cellGcmSys.c:1253` tiene camino rápido solo para pasos 1:1; el resto recorre
píxel a píxel con `vm_read32`/`vm_write32` (traducción, swaps, coherencia de
páginas). En batalla hay transferencias 512x1024 y 512x512 reales. Un camino para
pasos enteros 2x/1x y 2x/2x, con coherencia por rango y fallback para fracciones
y solapamientos, quita trabajo del walker (13 ms).

### 4. Actualizar la base a PS3Recomp v0.18.0

Estamos **380 commits por detrás** y upstream ya trae cosas que aquí hemos
perseguido: caché de vértices por draw (`#518`), *draw indexed triangle lists*
(`#530`), ring de subidas de texturas (`#579`), arreglo de los SRV que pisaban el
heap (`4eabd92`), captura de RenderDoc integrada (`#561`) y varios arreglos de
flip/present. El issue upstream `#345` (inFAMOUS 2, 7 → 26 FPS) describe además
tres cosas aplicables: loops dedicados por tipo de atributo (7,6 → 5,2 ms en su
caso), caché del lookup de programa por draw, y **un solo recorrido para hallar
el rango de índices** (2,2 ms/cuadro).

Esto no es una microoptimización: es comprobar si la base nueva rinde más sin
perder lo nuestro. El plan y las condiciones están en
[legacy/UPSTREAM_V018_EVAL.md](legacy/UPSTREAM_V018_EVAL.md).

## La conclusión del usuario (03/11)

Medido y aceptado: el problema **no es el trabajo de render del juego**, es el
**manejo de recursos del guest y sus hilos**. Un `spu_exec` al 100 % y el
`frame_clock` al 86 % con la GPU al 7,5 % describen un cuadro limitado por un
hilo que hace demasiado, no por falta de hardware. El ruido de audio aparece
justo en las caídas porque el juego no entrega bloques PCM a tiempo.

Todo lo de la tabla de arriba ya se probó en una sesión anterior con el mismo
resultado. Antes de proponer algo nuevo, compáralo con esta lista.
