# Capas visuales: HUD, cartas y cinemáticas

Escrito el 03/11/2026 tras recuperar las tres capas. Leer antes de concluir que
el render está roto.

## El síntoma que no hay que volver a diagnosticar desde cero

En batalla desaparecían a la vez:

- las cinemáticas de presentación (pantalla negra, el audio sí sonaba),
- los modelos 3D de las cartas,
- el HUD 2D (retratos, barras, joystick, "PRESS X").

El resto de la escena —arena, agua, sombras, partículas— se dibujaba bien. Por
eso los intentos de arreglarlo apuntaron al backend de vértices, al caché de
vértices, al SPU y al orden de las transferencias, y ninguno lo explicaba: los
tres draws se estaban enviando.

## Causa observada

**Lanzar el juego con stdout/stderr redirigidos.** `runtime/ps3_log.h` activa el
log verboso por defecto cuando stderr no es una consola, y ese log emite del
orden de 6 000 líneas/s desde todos los hilos del guest a través de un único
lock de `FILE`. Windows no garantiza equidad en ese lock: un hilo se queda sin
turno segundos y el título pierde su secuencia de inicialización y de carga de
assets. El propio fichero lo documenta con otro título (Tokyo Jungle: 4 cuadros
en 40 s con el log redirigido y 1 sin él).

Mismo ejecutable, mismo `shadercache`, misma batalla:

| Lanzamiento | Resultado |
|---|---|
| stderr redirigido (verbose implícito) | cinemáticas negras, cartas y HUD ausentes |
| `PS3_VERBOSE=0` | cinemáticas, cartas y HUD correctos |

Prueba: `docs/img/battle-arena-hud-cards-2026-11-03.png` (batalla con HUD,
retrato y joystick visibles, 1598 draws).

## Reglas

1. **Nunca juzgar lo visual desde una ejecución con logs redirigidos.** Para
   capturar logs sin alterar el juego: `PS3_VERBOSE=0` siempre. Si hace falta
   detalle, subirlo después de que la escena se vea bien.
2. `PS3_VERBOSE=1` solo para leer trazas, nunca para validar imagen ni FPS.
3. **No reactivar `SRV_SHADOW`.** El backend lleva escrito que con esa opción
   "cinematics and the 2D HUD stopped loading in battle": reproduce exactamente
   este síntoma. Sigue desactivada por defecto; no es la causa actual.
4. `DRAW_KEEP_TEX`, `DRAW_LAST_TEX`, `DRAW_KEEP_BLEND`, `TEX_MARKEMPTY`,
   `EMPTYTEX`, `DEPTH_OFF`, `TEX_RESOLVE_AUTO` siguen prohibidas (ver
   [AUDIO_MENU_GUARDRAILS.md](AUDIO_MENU_GUARDRAILS.md)).
5. Antes de tocar el camino de vértices, comprobar con esta lista en orden:
   aviso de guardado audible, menú, primera cinemática con imagen y audio,
   HUD/cartas/sombras/efectos en batalla. Solo entonces mirar FPS.

## Caché de shaders

Un solo fichero: `shadercache/fp_v2.cache`, registros `[u64 hash del HLSL][u32
tamaño][DXBC]`, cabecera de 12 bytes escrita campo a campo (un `struct` lleva
relleno de alineación). Al cargar se comprueban límites y la cabecera `DXBC`; si
el último registro quedó cortado se descarta y se reescribe. El guardado va a
`.tmp` y sustituye con `MoveFileEx`, así un cierre abrupto no deja el caché a
medias. Los ficheros viejos `shadercache/fp_*.dxbc` se importan y se borran en
el primer arranque.

**Nunca `free()` del puntero del caché.** El bytecode ya no es un `malloc`: es
un puntero dentro del contenedor. Liberarlo corrompe el heap y el juego se cierra
en silencio (~1 s después del primer draw, sin mensaje). Fue exactamente el
fallo al revertir la Fase 1.

## Pantalla negra: qué no tocar

El negro total de hoy no vino del render ni del SPU: vino de **desalinear el
layout del staging de vértices**. Cuatro cosas describen el mismo run y tienen
que coincidir siempre:

| Dato | Dónde | Quién lo lee |
|---|---|---|
| `s_vertex_fetch_plan.mask` / `slot_off[]` | fill (`vp_fetch_vertex`) | escritura de cada atributo |
| `dr->vb_stride` | registro del draw | merge de batches |
| `dr->gpu_input_mask` → `dr->gpu_stride` | `vp_upload_compact_frame` | subida y VBV de D3D12 |
| offsets de `s_d3d.vp_vb_offset` | cada `upload_*` | siguiente draw |

Si el fill escribe compacto y la subida lee con el stride canónico (o al revés),
cada vértice se decodifica desplazado: **toda la pantalla en negro**, sin error
en el log. Por eso no se deja un interruptor que cambie solo un lado del layout;
el intento `VP_STAGE=0` hizo justo eso. Si se retoma la Fase 1, cambiar los
cuatro a la vez y validar con `VPCHK=1`.

El trabajo de la Fase 1 quedó guardado fuera de main: `backups/` contiene
`fase1-compact-vertices-WIP-20261103.patch` y el fichero completo. Medido con
él: `canonical` 109 → 36,8 MB/cuadro y `vertex flush` 8,4 → 3,8 ms, con `VPCHK`
en 0 desviaciones; el A/B de FPS contra el mismo sitio no llegó a cerrarse.

También negro: cualquier camino que deje los draws sin PSO. Un bytecode de
caché inválido daba PSO nulo en silencio; por eso el caché ahora valida.

## Evidencia local

Las capturas y logs de las sesiones del 03/11 se borraron junto con
`diagnostics/`. Lo que queda de aquel diagnóstico está en este documento, en
`docs/img/` y en los commits `a9743b0`..`9e522bd` del toolkit.
