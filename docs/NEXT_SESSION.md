# Continuar desde aquí — 03/11/2026

## Handoff vigente

Leer [HUD_CINEMATICS.md](HUD_CINEMATICS.md) y
[AUDIO_MENU_GUARDRAILS.md](AUDIO_MENU_GUARDRAILS.md).

**Siguiente paso acordado:** evaluar PS3Recomp **v0.18.0** en un worktree aparte,
sin tocar `test`. El prompt completo y el estado de partida están en
[UPSTREAM_V018_EVAL.md](UPSTREAM_V018_EVAL.md). El toolkit va 380 commits por
detrás y upstream ya trae captura de RenderDoc integrada, caché de vértices por
draw y el arreglo de los SRV que pisaban el heap.

**WIP sin commit:** espera de canales SPU solo para grupos LV2 y admisión de
cuatro palabras en el buzón. Las pruebas de regresión pasan. No usar
`SPU_CH_BLOCK=1` global: bloquea trabajos SPURS.

**Compilar hoy:** `tools/build.py` y `tools/pipeline.py` se retiraron. La build de
Release se configura una vez y luego basta ninja:

```powershell
$env:PATH = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin;$env:PATH"
.\.venv\Lib\site-packages\cmake\data\bin\cmake.exe -S bakugan-recomp -B bakugan-recomp\build -G Ninja `
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM=.venv\Scripts\ninja.exe `
  -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl
.\.venv\Scripts\ninja.exe -C bakugan-recomp\build
.\tools\run.ps1
```

Las flags de link de Release apuntaban el `.map` a `diagnostics/performance/...`,
que ya no existe: cualquier enlace fallaba con "cannot open ... release.map". Se
devolvieron a `/INCREMENTAL:NO`.

**Rendimiento: cerrado por el usuario el 03/11, no reabrirlo con las mismas
ideas.** Medido en batalla: 15,8 → 17 FPS tras el revert del stream compacto,
con caídas que coinciden con el ruido de audio. Su conclusión, tras probar todas
las fases de este documento en una sesión anterior: **no es trabajo de render del
juego, es el manejo de recursos del guest y sus hilos**. Ya se probaron el stream
compacto (Fase 1; ver `backups/fase1-compact-vertices-WIP-20261103.patch`:
`canonical` 109 → 36,8 MB y `flush` 8,4 → 3,8 ms, sin mejora de FPS),
`PS3_KICKWAIT=0` (medido: no gana FPS y convierte la espera en spin de 73k
iteraciones/s), la caché de vértices, los blits NV3089 y la higiene de D3D12. No
volver a proponerlas como solución al ruido ni a las caídas.

**HUD, cartas y cinemáticas: resuelto el 03/11.** No era el render ni el SPU:
las tres capas desaparecían porque el juego se lanzaba con stderr redirigido,
lo que activa el log verboso y ahoga los hilos del guest. Con `PS3_VERBOSE=0`
la batalla muestra HUD, cartas y cinemáticas. Detalle y reglas de validación en
[HUD_CINEMATICS.md](HUD_CINEMATICS.md).

El resto de este archivo es el handoff anterior del 02/11 y queda como histórico.
Sus comandos incompatibles con las restricciones actuales no deben ejecutarse.

## Handoff histórico del 02/11/2026

> Leer primero [AUDIO_MENU_GUARDRAILS.md](AUDIO_MENU_GUARDRAILS.md): restricciones
> del usuario y hallazgos del 03/11/2026. Sustituyen los comandos incompatibles
> de este handoff, incluido `pipeline.py --only patch`. El bloqueo global SPU
> recuperó sonido y menú, pero dejó la carga esperando: no es la solución final.

## Estado vigente

Build principal: `bakugan-recomp/build/BakuganRecomp.exe`, Release Windows
D3D12, BLES00758. Arranque, menús, películas, teclado/XInput y Battle Arena
funcionan. Aquos tiene cascadas, lluvia, reflejos y sombras correctas
(confirmado por el usuario tras 0038/0039). 0043 dejó el menú destrabado, el
audio de menús sin ruido y sin pantallazos.

**Lo único que falta para cerrar el proyecto es rendimiento: 30 FPS
sostenidos.** Persisten también carga lenta, audio adelantado a la imagen en
las dos primeras presentaciones y ruido de audio en batalla; el ruido es
síntoma de los FPS, no causa (ver abajo). No dar por cerrados Pyrus, Haos,
Subterra, Ventus, tutorial ni historia: faltan pruebas completas.

El desglose medido y las cifras están en
[PERFORMANCE_2026-11-02.md](PERFORMANCE_2026-11-02.md). Resumen: 67 ms/frame,
de los cuales 26,7 ms son triangle strips indexados, 14,5 ms el `prepass` de
PSO/SRV y 8,5 ms la subida a memoria write-combined. 1704 draws y 488k
vértices por frame, sin batches repetidos. Referencia del emulador en la misma
batalla: 29,9 FPS (el juego está bloqueado a 30). Es un problema de bytes
movidos, no de ALU.

Antes de tocar el bucle de vértices hace falta `[VPCHK]` (comparación byte a
byte); dos intentos sin esa garantía rompieron la geometría.

## Plan de rendimiento, en orden

Cada fase se valida antes de pasar a la siguiente y se descarta si no mejora
una cámara fija comparable. No mezclar fases en el mismo commit.

### Fase 0 — Medir el "resto" (~1 sesión, sin código)

El walker FIFO no lo mide nadie hoy: `guest` = `frame − render_frame`, así que
esconde present + drain + walker + guest juntos. La instrumentación ya existe:

- `PS3_FTRACE=<inicio_s>:<segundos>:<fichero>` → los spans `PS3_FT_DRAIN` ya se
  emiten en `cellGcmSys.c:1808`. Drain por pasada; los huecos son present+guest.
- `drain − [PERF submit]` = walker puro.
- `GCM_WORD_STATS=1` → palabras por cuadro que recorre el FIFO.
- `[PERF vertex] canonical=X upload=Y` (cada 120 cuadros) → ratio real de
  compactación, es decir cuántos slots usa de verdad el VS de este título.
- `[PERF primitive]` (0048) ya está y es el que destapó prim=6.

```powershell
$env:PERF = "1"
$env:GCM_WORD_STATS = "1"
$env:PS3_FTRACE = "60:30:diagnostics/performance/ftrace-0211.txt"
.\tools\run.ps1
```

Si `drain − submit` sale > 5 ms, el walker se convierte en fase propia. Si sale
~0, no se toca `cellGcmSys.c` y el techo lo fija la Fase 3.

### Fase 1 — Stream compacto escrito en el submit (estimado −13 ms) ⭐

Hoy se escribe 256 B por vértice y `render_frame` empaqueta después. En su
lugar, escribir solo los slots que el VS usa, directamente al recoger el draw,
y al final del frame un único `memcpy` al upload heap.

La máscara **ya se conoce en el submit**: `dr->vs_idx = vp_get_vs(...)`
(`rsx_d3d12_backend.c:7399`) deja `vp_vs[vs].input_mask` disponible, que es
exactamente lo que `vp_upload_compact_frame` recalcula más tarde (4539-4540).
`vp_compact_input_layout` (435) y `vp_pack_vertex_stream` (420) ya trabajan por
máscara; la pieza que falta es que el fetch escriba con stride variable.

- Toca: `vp_fetch_vertex`/`read_vp_vertex` (reciben máscara y stride), los seis
  `upload_*_vp*`, `vp_upload_compact_frame` (pasa a copiar, no a empaquetar),
  `vp_reserve_vertices` (stride variable).
- Conservar la disciplina de write-combine: escribir en RAM cacheada y copiar
  en bloque al `vp_vb_upload`, como ahora.
- **No romper:** `VTX_DUMP`, `RSX_CAP`, `TT` y los dumps leen el stream a
  stride 256 (líneas 1867, 5433). Con cualquiera de esas variables activa hay
  que usar `mask=0xFFFF`. Una rama, no un segundo camino.

### Fase 2 — Sacar el resolver del bucle (estimado −6 a −10 ms)

Con la Fase 1 hecha, `submit` deja de estar limitado por ancho de banda y lo
que queda es coste fijo por vértice:

- `rsx_fetch_attrib` llama `cellGcmResolveLocated` **por atributo y por
  vértice** (`rsx_vertex_fetch.c:71-74`) y es una llamada entre TUs. Para LOCAL
  es literalmente `localAddress + offset` (`cellGcmSys.c:3438`): resolver la
  base una vez por draw y avanzar con `stride`.
- `read_vp_vertex` (6500) llama `rsx_vtx_pos_dbg` en cada vértice con la guarda
  dentro de la función: subirla a bandera cacheada.
- Quitar el `memcpy` a pila de `vp_fetch_vertex`: precargar defaults en destino
  y sobrescribir solo los slots activos. 512 B → 256 B. El comentario que lo
  justifica con memoria write-combined es obsoleto desde 0046: `vp_vb_mapped`
  es un `malloc`.

`rsx_vertex_compact.c` ya tiene esta variante rápida
(`rsx_vertex_fetch_plan_init` / `rsx_vertex_fetch_one`, base hoisteada por
draw). La usan Metal y Vulkan. Reutilizarla en vez de escribir otra.

### Fase 3 — Higiene de grabación D3D12 (estimado −4 a −6 ms)

Los ~9 ms de `render_frame` que no son empaquetado son llamadas por draw:
`SetDescriptorHeaps`, `OMSetRenderTargets`, viewport/scissor, barreras. Se
deduplican contra lo ya grabado; cambian con el render target o el viewport,
no por draw. Contenido y de bajo riesgo.

**Validación:** las 42 suites de `tools/test_*.py` más captura de cuadro
comparada con la actual.

### Fase 4 — Solo si la Fase 0 lo justifica

Walker FIFO / `Present` / guest. Es el último tramo hasta 33 ms y solo existe
si la Fase 0 encontró tiempo ahí.

### Techo realista

`submit` + `render_frame` = 44 ms y ambos bajan con evidencia. Si quedan en
~10 ms, el cuadro aterriza en 26–32 ms (30–38 FPS). Si el "resto" es mayor de
lo estimado, el proyecto aterriza en 22–26 FPS. **30 FPS no está garantizado
hasta cerrar la Fase 0.**

## Lo que NO se reintenta

- SIMD a mano en el lector de atributos y la variante con `restrict`: probadas,
  sin ganancia, descartadas. Evidencia en `diagnostics/arena/vertex-decode`.
- Ampliar `codex/gpu-vertex-pull` (0049–0051): cubre 32 draws y 1116 vértices
  por cuadro, el coste está en otro sitio. Guardado, no mezclado.
- Tocar el reloj del mixer para el ruido de batalla: 187,5 bloques/s con cero
  descartes y huecos en el PCM. El ruido desaparece cuando suben los FPS.
- Cambiar banderas de optimización del compilador: el backend RSX y los TUs
  liftados ya se compilan a `/O2` (`bakugan-recomp/build/build.ninja`). El
  comentario de `bakugan-recomp/CMakeLists.txt` que dice `-O1` está obsoleto.

## Qué cambió (serie de parches)

| Parche | Resultado |
|---|---|
| 0036 | Samplers reales por draw: wrap/filtros/LOD/borde; cascadas y lluvia confirmadas |
| 0037 | Vértices en RAM cacheada; mejora parcial de recuperación de carga, mayor RAM |
| 0038 | SET_SHADER_WINDOW / WPOS: origen, centro, altura y 1/W; reflejos confirmados |
| 0039 | D24 DSV/SRV por superficie, clears ordenados, pases sin color y ZFUNC correcto |
| 0040 | GCM_FULL_FRAMES por defecto, notificación de flip consumida una vez y 8192 draws |
| 0041 | PERF separa VS lookup, snapshots de constantes, fence y copia de vértices |
| 0042 | Stream GPU compacto por inputs del VP, fallback completo y capacidad GPU independiente |
| 0043 | Dueño único del reciclaje FIFO: corrige las esperas de 2000 ms y los pantallazos |
| 0044 | Opciones de diagnóstico leídas una vez, sin escanear el entorno por bind/quad |
| 0045 | CFLTS sin escala vectorizado, con resultados exactos de referencia |
| 0046 | Staging CPU único: crecer copia solo el prefijo usado |
| 0047 | Caché de decodificación de índices por paquete, sin conservar entre submissions |
| 0048 | PERF separa tiempo, llamadas y vértices de entrada por primitiva y por arrays/indexed |

Main tiene 47 parches en la serie 0001–0048: 0023 sigue retirado. No editar
lifts. Evitar resets generales dentro de `ps3recomp-src`.

## Experimento GPU guardado, fuera de main

Rama remota `codex/gpu-vertex-pull`, commit `8ce9793`, parches 0049–0051.
Release y 30 pruebas pasaron; el usuario no observó mejora (~13–14 FPS) porque
solo recibía ~32 draws / 1116 vértices / 64 KiB por cuadro.

```powershell
git switch codex/gpu-vertex-pull
git merge main
.\.venv\Scripts\python.exe tools\pipeline.py --only patch
.\.venv\Scripts\python.exe tools\build.py
$env:PERF = "1"
$env:RSX_VERTEX_PULL = "1"
.\tools\run.ps1
```

Antes de volver a main, revertir SOLO los tres parches experimentales, de atrás
hacia delante, usando los archivos de esa rama; después cambiar rama:

```powershell
git -C ps3recomp-src apply --reverse ../patches/0051-d3d12-experimental-quad-vertex-pulling.patch
git -C ps3recomp-src apply --reverse ../patches/0050-rsx-immutable-vertex-pull-snapshots.patch
git -C ps3recomp-src apply --reverse ../patches/0049-rsx-gpu-vertex-decode-contract.patch
git switch main
```

No resetear el toolkit entero: conserva diagnósticos locales históricos.

## Reproducir / medir

```powershell
.\.venv\Scripts\python.exe tools\pipeline.py --only patch
.\.venv\Scripts\python.exe tools\build.py
$env:PERF = "1"
.\tools\run.ps1
```

PERF no debe combinarse con capturas ni con volcados masivos para comparar
FPS. Los scripts de RenderDoc y análisis están en
[EFFECTS_COMPARE_2026-11-01.md](EFFECTS_COMPARE_2026-11-01.md).

## Evidencia local útil

- `diagnostics/performance/index-reader-cache`, `strip-merge`: los logs con el
  desglose de 0047/0048 que sostiene las cifras de STATUS.
- `diagnostics/performance/native-strip`: harness que extrae las funciones de
  upload del backend y compara triángulos/píxeles en WARP. Reutilizarlo para
  cualquier cambio en el camino de vértices.
- `diagnostics/arena/aquos-gpu-pull-20261101`: menú ~30 FPS, vista ~13,3–13,8.
- `aquos-perf-20261101`: perfil sin capturas, CPU y fases de carga/gameplay.
- `aquos-shadow-compare-20261101`, `aquos-full-frames-20261101/frame_118..141`:
  sombras y fronteras de cuadro.
- `compact-prototype-20261101`: prototipo histórico; la versión integrada es 0042.
- `battle-arena-picture/bugs`: capturas aportadas por el usuario.

Rutas relativas a `diagnostics/arena/` salvo las de `diagnostics/performance/`
y la última. Dumps, símbolos, capturas y assets son locales e ignorados.

## Invariantes

Conservar límite original 30 FPS, cuadros completos, strips nativos, estado
por draw, todas las texturas/partículas, transparencia, profundidad y audio.
No activar NODEPTH/DEPTH_OFF, no simular sombras, no saltar trabajo SPU, no
alterar el reloj para inflar contadores. Conservar backpressure del FIFO y
comparación de alfa UNORM. Mantener parches reproducibles y pruebas GPU.

Los antiguos handoffs de 26–30/10 se retiraron por estar sustituidos. Sus
versiones siguen en Git; una copia local queda en
`diagnostics/arena/docs-before-close-20261101/`. Los informes de hitos con
evidencia útil se conservan, marcados como históricos.
