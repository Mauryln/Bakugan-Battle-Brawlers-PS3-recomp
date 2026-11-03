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

## Pantalla negra o blanca: qué no tocar

Tres veces en un día se rompió la imagen entera por el mismo tipo de error: un
camino nuevo que **deja de registrar o de rellenar draws** sin dar ningún error.
Antes de investigar nada, mira `[PERF] ... draws N`: en batalla son ~1750. Si el
número se hunde, el fallo es de registro, no de shaders.

1. **No partir una cadena `if/else if/…/else`.** Insertar código entre el último
   `else if` y el `else` de "primitiva no soportada" hizo que ese `else`
   (`return;`) se comiera **todos** los draws que no fueran a la ruta nueva:
   pantalla negra, geometría suelta y un FPS que no significaba nada (97 % de
   draws perdidos).
2. **Layout del staging de vértices**: `mask`/`slot_off`, `dr->vb_stride`,
   `gpu_input_mask → gpu_stride` y los offsets deben cambiar **los cuatro a la
   vez**. Si el fill escribe compacto y la subida lee canónico (o al revés), cada
   vértice se lee desplazado: negro total, sin error en el log.
3. **`GPU_VTX=1` no se usa todavía.** El path de decodificación en GPU
   (`gpu_vtx_*`) elimina el dispositivo D3D12 en el primer cuadro con draws
   elegibles: `[GPU_VTX] device removed`, y a partir de ahí todo PSO devuelve
   `0x887A0005` y la pantalla queda blanca o negra. Apagado por defecto; no
   activarlo hasta que `[GPUV] exec/present` diga que el dispositivo sobrevive.
4. **Punteros del caché de shaders**: nunca `free()` de lo que devuelve el
   contenedor; el juego se cierra en silencio (~1 s tras el primer draw, sin
   mensaje).

Un fallo del dispositivo no perdona: si `Present` o `ExecuteCommandLists`
devuelven `0x887A0005`, todo lo que se dibuje después es basura aunque el juego
siga corriendo y subiendo contadores. Por eso el backend registra el primer
error de ejecución/presente con la etiqueta `[GPUV]`.

**Para diagnosticar un fallo de GPU hace falta la capa de validación**, que no
viene con Windows: `D3D12SDKLayers.dll` solo existe tras instalar la
característica opcional *Graphics Tools* (`Add-WindowsCapability -Online -Name
"Tools.Graphics.DirectX~~~~0.0.1.0"`, requiere administrador). Sin ella,
`D3D12_DBG=1` imprime "Debug layer requested but unavailable" y no hay ningún
mensaje del validador. Con ella, añadir `D3D12_GPU_VALIDATION=1` para que el
driver señale la lectura del shader que tumba el dispositivo.

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

## Pantalla negra: la tabla del staging de vértices

Cuatro datos describen el mismo run y tienen que coincidir siempre:

| Dato | Dónde | Quién lo lee |
|---|---|---|
| `s_vertex_fetch_plan.mask` / `slot_off[]` | fill (`vp_fetch_vertex`) | escritura de cada atributo |
| `dr->vb_stride` | registro del draw | merge de batches |
| `dr->gpu_input_mask` → `dr->gpu_stride` | `vp_upload_compact_frame` | subida y VBV de D3D12 |
| offsets de `s_d3d.vp_vb_offset` | cada `upload_*` | siguiente draw |

Por eso no se deja un interruptor que cambie solo un lado del layout; el intento
`VP_STAGE=0` hizo justo eso. Si se retoma la Fase 1, cambiar los cuatro a la vez
y validar con `VPCHK=1`.

El trabajo de la Fase 1 quedó guardado fuera de main: `backups/` contiene
`fase1-compact-vertices-WIP-20261103.patch` y el fichero completo. Medido con
él: `canonical` 109 → 36,8 MB/cuadro y `vertex flush` 8,4 → 3,8 ms, con `VPCHK`
en 0 desviaciones; el A/B de FPS contra el mismo sitio no llegó a cerrarse.

## Evidencia local

Las capturas y logs de las sesiones del 03/11 se borraron junto con
`diagnostics/`. Lo que queda de aquel diagnóstico está en este documento, en
`docs/img/` y en los commits `a9743b0`..`9e522bd` del toolkit.
