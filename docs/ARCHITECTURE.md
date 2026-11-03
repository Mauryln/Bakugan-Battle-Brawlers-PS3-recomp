# Cómo funciona el código hasta aquí

Estado del 03/11/2026. Describe el port de **Bakugan: Battle Brawlers (BLES00758)**
sobre PS3Recomp: qué hace cada pieza, en qué orden, y dónde está cada cosa.
Es la referencia para quien retome el proyecto o lo porte a una base nueva.

## Qué es esto

Un port nativo Windows de un juego de PS3 por **recompilación estática** (el
código PPU/SPU del juego se traduce a C/C++ en tiempo de build) más **HLE** para
las librerías del sistema (cellGcm, cellAudio, cellSpurs, cellSail…), que no se
recompilan: se reimplementan con la API nativa (D3D12, WASAPI, hilos del host).

Dos repositorios:

| Repo | Contenido |
|---|---|
| raíz (`ps3recomp`) | el port: `bakugan-recomp/`, docs, herramientas, parches |
| `ps3recomp-src/` | checkout del toolkit upstream (ignorado por git), donde vive el runtime y los HLE |

## Mapa de directorios

```
bakugan-recomp/            El port (esto es lo nuestro)
  main.cpp                 Runner game-agnostic: VM, ELF, VFS, hilos, arranque
  stubs.cpp                Hooks del título (traza de movies) y overrides
  CMakeLists.txt           Build del ejecutable contra las fuentes del toolkit
  recompiled/              PPU liftado del juego (generado, no editar)
  spu/                     SPU liftado del juego (generado, no editar)
  build/                   Build Release (ignorado por git)
  movie_*.cpp, sail_video.cpp, video_decoder.cpp   Vídeo (cellSail/MP4)
  config.toml              Documentación del port; el build NO lo lee
ps3recomp-src/             Toolkit (runtime + libs HLE + tools)
  runtime/ppu/, runtime/spu/, runtime/syscalls/
  libs/video/              cellGcmSys, rsx_commands, rsx_d3d12_backend, rsx_vertex_fetch, decompiler FP
  libs/audio/, libs/spurs/, libs/sail/, libs/input/...
patches/                   Serie 0001–0048 aplicada al toolkit (reproducibilidad)
tools/                     run.ps1, tests/, renderdoc/, rpcs3/
game/                      Tu dump del disco (ignorado por git)
shadercache/               Caché de shaders (ignorado por git)
backups/                   Ejecutables y parches de referencia (ignorado por git)
analysis/, diagnostics/    Datos generados (ignorados por git)
```

## Arranque (`main.cpp`)

1. Reserva la memoria virtual del guest (`alloc_guest_vm`), carga el ELF y
   deriva la raíz VFS (los ficheros del juego).
2. `cellGame_init_from_paramsfo` da el title id real (BLES00758).
3. Registra: tabla de funciones liftadas (`ppu_recomp_register`), módulos PRX,
   HLE por NID (`ppu_hle_init`), sysprx (CRT), filesystem, syscalls lv2.
4. `bakugan_install_video()` engancha el vídeo del port.
5. Lanza el hilo **`frame_clock`** y dos hilos de volcado de texturas, y ejecuta
   el punto de entrada del juego.

Variables que el runner fija si no las pones tú, porque este título las necesita:
`SPU_INTERP_UNLIFTED=1` (ejecuta código SPU generado en runtime), `RSX_ACCUM_FRAME=1`
(acumula un cuadro hasta el clear de display), `GCM_FULL_FRAMES=1`, y
`timeBeginPeriod(1)` (el granularidad de 15,6 ms del scheduler arruina las esperas
cortas del guest).

## Hilos

| Hilo | Qué hace |
|---|---|
| PPU (N) | Un hilo del host por hilo del guest que crea el juego. La mayoría duerme en colas de eventos. |
| `frame_clock` | **El hilo crítico del render**: espera el kick del FIFO, lo recorre (`drain`), ejecuta los callbacks GCM, y en el clear de display graba y presenta el cuadro (`render_frame` + `Present`). |
| `spu_exec` (2) | Hilos SPU del guest (audio/mixer). Uno de ellos suele estar al ~100 % de un núcleo. |
| `spu_async` (N) | Workers SPURS. |
| `spurs_kernel`, `spurs_pm_worker` | Kernel del scheduler SPURS y su gestor de trabajos. |
| `audio_mix` | Mezclador cellAudio (187,5 bloques/s). |

Medido en batalla (20 s): `frame_clock` ~86 % de un núcleo, un `spu_exec` ~100 %,
un tercer hilo ~62 %. La GPU, **7,5 %**. El cuadro lo limita el trabajo serial del
`frame_clock`, no la GPU ni la falta de núcleos.

## El cuadro, paso a paso

```
guest escribe comandos en el FIFO (GCM)
      │
      ▼
frame_clock: espera kick → walker del FIFO (cellGcmSys.c)
      │            cada comando llama al backend:
      │            bind_texture / set_vertex_attribs / draw_arrays / draw_indexed / clear
      ▼
rsx_d3d12_backend.c: registra un D3D12DrawRecord por draw en s_d3d.draws[]
      │            (estado por draw: shaders, texturas, RT, blend, depth, viewport)
      ▼
clear de display  →  "frame completo"  →  render_frame()
      │ pre pass:  sube texturas, escribe ventanas SRV, prepara PSO por draw
      │ vp_upload_compact_frame: empaqueta los vértices al heap de subida de la GPU
      │ draws:     por registro, fija PSO/RT/VBV y DrawInstanced
      ▼
Present  (vsync)
```

Tiempos medidos a 16 FPS (63 ms/cuadro): `submit` (decodificar + registrar) 25 ms,
walker/blits 13 ms, `render_frame` 15 ms (de ellos *flush* 8 y *srv* 4), espera
8 ms. El `frame_clock` los hace **en serie**.

## Camino de vértices (el 40 % del cuadro)

1. **Plan por draw** (`vp_prepare_vertex_fetch`): a partir de la máscara de
   atributos que el vertex program lee (`mask`), resuelve una vez por draw la
   base de cada array (`rsx_attrib_base`), su stride/tipo/tamaño, y el slot
   compacto de cada atributo (`slot_off`).
2. **Relleno** (`vp_fetch_vertex`): escribe **solo los slots de la máscara**,
   contiguos, dentro de un bloque canónico de 256 B por vértice. Convierte de
   big-endian y del tipo RSX (float, half, u8, s16). Las componentes que el array
   no aporta las toma del registro de constante del atributo.
3. **Staging CPU** (`s_d3d.vp_vb_mapped`, 192 MB): los uploads de cada primitiva
   (quads, triángulos, strips, fans, indexados) copian ahí; `vp_vb_offset` avanza
   en bloques de 256 B.
4. **Empaquetado y subida** (`vp_upload_compact_frame`): al final del cuadro,
   cada draw se copia al heap de subida de la GPU con stride compacto
   (`popcount(mask) * 16`), en dos mitades alternas (`vp_parity`) para no pisar
   lo que la GPU aún lee.
5. **Draw**: el VBV apunta a esa región con ese stride.

Los strips siguen siendo tiras: el guest las **corta** con reinicios (`0x7FFF`
cada ~13 índices) y el camino CPU las vuelve a unir insertando vértices de puente
y corrigiendo la paridad (`upload_native_strip_vp_indexed`). Cualquier cambio aquí
tiene que reproducir ese autómata exacto o la tira se desplaza.

## Caché de vértices (`VP_CACHE`)

Reutiliza el run canónico de un draw idéntico dentro del mismo cuadro. La clave
incluye programa de vértices, máscara, DMA/offset de índices, primitiva, rango y
las direcciones resueltas, y cada entrada guarda las **páginas del guest** que
leyó: si alguna cambió (contador de generación por página de 4 KB), no se reusa.
Acierta ~20 % de los draws y ahorra ~2 M vértices por ventana de 20 cuadros.

## Shaders

- **Vertex program**: microcódigo del guest → HLSL por draw (`rsx_d3d12_backend.c`,
  `vp_get_vs`), cacheado por hash del código.
- **Fragment program**: `rsx_fp_decompiler.c` traduce el microcódigo a HLSL
  (texturas, alpha test, export, WPOS…), se compila con `D3DCompile` y se cachea
  a disco en **un solo fichero** `shadercache/fp_v2.cache` (registros con hash,
  tamaño y cabecera `DXBC` validada; ver GUARDRAILS).
- **PSO**: `vp_get_fp_pso` construye el pipeline por combinación (VS, FP, blend,
  formatos de RT, máscara de color, cull, depth, alpha) y lo cachea en memoria.
  El prepass las prepara antes de los draws.

## Texturas y SRVs

`d3d12_bind_texture` guarda por draw la textura de cada unidad; el prepass sube
las que cambiaron y escribe una **ventana de 16 descriptores por draw**
(`DRAW_SRV_BASE + draw*16`). Algunos títulos (este entre ellos) dependen de que
esa ventana se reescriba cada cuadro: deduplicarla rompió HUD y cinemáticas
(`SRV_SHADOW`).

## Render targets, depth y fronteras de cuadro

- Cada superficie de color del guest (`offset`) tiene su RT de D3D12; el depth
  es por offset de zeta (D24S8) y se puede muestrear como textura.
- Un **clear de display** (color en el RT de pantalla) es la frontera del cuadro:
  se presenta lo acumulado y se reinicia el lote. Un clear offscreen o sin bits
  de color **no** es frontera (solo se registra), o el compositing saldría negro.
- `RT_REBIND` está activo por defecto: el lote offscreen largo desincroniza el
  target rastreado del que D3D12 tiene enlazado.

## SPU

- Las imágenes SPU del juego se lanzan como hilos del host (`spu_exec`), con un
  token de ejecución compartido.
- El código que el título genera en runtime (kernels del mezclador de
  Multistream) no tiene lift: `SPU_INTERP_UNLIFTED=1` lo interpreta y vuelve al
  camino compilado al llegar a una dirección liftada.
- Canales y buzones en `spu_channels.c`: `rdch`/`wrch`, mailboxes, señales,
  eventos MFC. Lee de ahí la regla de bloqueo selectivo (GUARDARRAILS).

## Audio

`cellAudio` abre puertos y el juego mezcla con kernels SPU: 187,5 bloques/s. Si
el juego no entrega bloques a tiempo quedan **huecos en el PCM** y se oye el
"brrr" que aparece justo cuando caen los FPS; no es un problema del reloj del
mixer.

## Vídeo (cellSail / MP4)

`movie_video.cpp` + `video_decoder.cpp` decodifican y suben el frame a textura;
`movie_audio.cpp` entrega el audio. Las cinemáticas de presentación de personajes
son **render del juego**, no vídeo.

## Diagnóstico

| Variable | Qué da |
|---|---|
| `PS3_VERBOSE=0` | Silencia el log verboso. **Obligatorio** al capturar logs (si no, ahoga los hilos del guest). |
| `PERF=1` | Líneas `[PERF]`, `[PERF submit]`, `[PERF detail]`, `[PERF primitive]`, `[PERF vcache]`. |
| `GCM_WORD_STATS=1` | Palabras por cuadro que recorre el FIFO. |
| `PS3_FTRACE=inicio:seg:fichero` | Spans de drain/present/SPU busy-wait. |
| `VPCHK=1` | Compara cada vértice emitido contra una referencia lenta: la única prueba que convierte "parece igual" en "es igual". |
| `VBFULL=1`, `VTX_POS`, `VP_ATTRS`, `RSX_VTXDBG` | Volcados puntuales del camino de vértices. |
| `RSX_CAP`, `CELLMARK_DUMP` | Capturas de RT y de cuadros a BMP. |
| `D3D12_DBG=1`, `D3D12_GPU_VALIDATION=1` | Capa de validación y validación GPU (requiere la característica *Graphics Tools* de Windows). |

`[PERF]` por ventana de 20 cuadros: `fps`, `draws`, `verts`, `vpc` (aciertos de la
caché), `tex`, `vtx`, `render_frame`, `prepass` (con `srv`/`pso`), y `guest`
(= cuadro − `render_frame`, o sea present+drain+walker+guest juntos). `[FRAME]`
cada 2 s: `drain`, `wait`, `pres (n presents)`.

## Build y ejecución

```powershell
$env:PATH = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin;$env:PATH"
.\.venv\Lib\site-packages\cmake\data\bin\cmake.exe -S bakugan-recomp -B bakugan-recomp\build -G Ninja `
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM=<repo>\.venv\Scripts\ninja.exe `
  -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl
.\.venv\Scripts\ninja.exe -C bakugan-recomp\build
.\tools\run.ps1
```

`tools/build.py` y `tools/pipeline.py` se retiraron; la configuración de arriba es
la que funciona. Release usa `/O2`; no compares medidas con una build Debug
(`/Od`).

## Pruebas

`tools/tests/` (C++: buzón de canales SPU, lectura bloqueante) y `ctest` del
toolkit en `ps3recomp-src/` (`cmake -B build-tests -DPS3RECOMP_BUILD_TESTS=ON`).
Para cualquier cambio del camino de vértices, la prueba real es `VPCHK=1` más una
captura comparada.
