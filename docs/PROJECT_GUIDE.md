# Bakugan: Battle Brawlers (BLES00758) — Guía técnica unificada

> Documento de referencia para agentes de desarrollo. Consolidado a partir de `ARCHITECTURE.md`, `GUARDRAILS.md`, `IO_PIPELINE.md`, `PERFORMANCE.md`, `SESSION_20261104.md` y `STATUS.md`. **No es un informe del estado actual**: rutas, ramas, flags y cifras reflejan el código y las mediciones documentados en noviembre de 2026; verificar contra el repositorio antes de actuar.

## 1. Propósito y límites

HUD/cartas y cinemáticas corregidos al cierre del 8 de noviembre de 2026;
causa y pruebas en [punto de continuación](CONTINUAR_HUD.md). Primero terminar
la aceptación del código, después launcher e instalador. Los 60 FPS se aplazan.

Port nativo para Windows del juego de PS3 mediante recompilación estática de PPU/SPU a C/C++ y emulación de alto nivel (HLE) de librerías PS3 (`cellGcm`, `cellAudio`, `cellSpurs`, `cellSail`, etc.) sobre D3D12, audio y threading del host. El proyecto consta del port y de un checkout separado del toolkit PS3Recomp. El objetivo de optimización es **mejorar rendimiento sin perder fidelidad funcional ni gráfica**. No se acepta una subida de FPS si desaparecen geometría, HUD, efectos, audio o escenas.

## 2. Estructura del proyecto

```text
raíz del repositorio (ps3recomp)/
├── bakugan-recomp/
│   ├── main.cpp                 Runner: VM guest, ELF, VFS, módulos, hilos y arranque/cierre
│   ├── stubs.cpp                Hooks y overrides específicos del título
│   ├── CMakeLists.txt           Build y enlace con el toolkit
│   ├── recompiled/              PPU liftado GENERADO: no editar manualmente
│   ├── spu/                     SPU liftado GENERADO: no editar manualmente
│   ├── movie_*.cpp              Vídeo/audio de películas
│   ├── sail_video.cpp
│   ├── video_decoder.cpp
│   ├── config.toml              Documentación; la build no lo lee
│   └── build/                   Salida Release, no versionada
├── ps3recomp-src/               Checkout del toolkit (runtime/HLE), ignorado por git del port
│   ├── runtime/ppu/             Incluye ppu_fs.cpp
│   ├── runtime/spu/
│   ├── runtime/syscalls/        Incluye sys_fs.c
│   ├── runtime/io/              asset_io.cpp experimental, si existe
│   ├── include/ps3emu/         asset_io.h experimental, si existe
│   └── libs/
│       ├── video/               cellGcmSys, RSX, backend D3D12, vertex fetch, FP decompiler
│       ├── audio/
│       ├── spurs/
│       ├── sail/
│       ├── input/
│       └── filesystem/
├── patches/                     Parches del toolkit; no reaplicar en bloque
├── tools/                       run.ps1, tests/, herramientas de diagnóstico
├── game/                        Dump del juego, no versionado
├── shadercache/                 Caché de shaders, no versionada
├── backups/                     Checkpoints locales, no versionados
├── diagnostics/                 Logs, capturas y experimentos, no versionados
└── analysis/                    Datos de análisis, no versionados
```

Las rutas son las descritas por los archivos originales; comprobar existencia y nombres efectivos antes de editar. El port y `ps3recomp-src` tienen historiales distintos: **no resetear, cambiar de rama ni aplicar una serie de parches por inercia**. El toolkit puede contener modificaciones locales que no están en el Git del port.

## 3. Arranque y ciclo de vida

En `bakugan-recomp/main.cpp`:

1. Reservar memoria virtual del guest (`alloc_guest_vm`), cargar ELF y establecer raíz VFS.
2. Leer `PARAM.SFO` mediante `cellGame_init_from_paramsfo` y reconocer `BLES00758`.
3. Registrar funciones PPU liftadas (`ppu_recomp_register`), PRX, HLE por NID (`ppu_hle_init`), sysprx, filesystem y syscalls lv2.
4. Instalar hooks de vídeo (`bakugan_install_video`).
5. Iniciar `frame_clock`, workers de volcado de texturas y entrada del juego.

Configuración documentada que el runner asignaba si no estaba definida: `SPU_INTERP_UNLIFTED=1`, `RSX_ACCUM_FRAME=1`, `GCM_FULL_FRAMES=1`, y `timeBeginPeriod(1)` para granularidad de temporizador. Verificar su vigencia en el código.

### Cierre correcto: regla obligatoria para agentes

**No basta con cerrar la ventana:** históricamente el render se detenía, pero el proceso y algunos hilos permanecían vivos, consumían CPU y bloqueaban la sustitución del ejecutable. La corrección documentada en `main.cpp` hace que el evento de cierre llame a `cellAudioQuit`, vacíe los streams y termine el host/proceso. **Conservar ese comportamiento** y verificar que el PID desaparezca antes de recompilar o volver a lanzar.

El agente debe solicitar cierre normal mediante `CloseMainWindow()` o Alt+F4, esperar la salida y comprobar que el proceso realmente terminó. No usar `Stop-Process -Force` ni `taskkill /F` como procedimiento normal: pueden omitir la liberación de audio y ocultar errores de apagado. Si no sale, diagnosticar el bloqueo; un cierre forzado excepcional requiere autorización explícita. No introducir otra vía de salida que deje threads vivos o recursos de audio sin liberar.

## 4. Hilos y camino crítico

| Hilo | Función |
|---|---|
| PPU (varios) | Un hilo host por hilo guest; muchos esperan eventos |
| `frame_clock` | Espera kick, drena FIFO, ejecuta callbacks GCM, prepara/renderiza y presenta |
| `spu_exec` (2 observados) | SPU del guest, incluido mezclador; uno puede saturar un núcleo |
| `spu_async` | Workers SPURS |
| `spurs_kernel`, `spurs_pm_worker` | Scheduler SPURS |
| `audio_mix` | Mezclador `cellAudio`, 187,5 bloques/s |

Flujo gráfico:

```text
Guest escribe comandos GCM al FIFO
  -> frame_clock / walker en cellGcmSys.c
  -> callbacks RSX: texturas, atributos, draws, clears
  -> rsx_d3d12_backend.c registra D3D12DrawRecord en s_d3d.draws[]
  -> clear de color del display marca frontera de cuadro
  -> render_frame: texturas/SRV/PSO + subida compacta de vértices
  -> draw records (PSO/RT/VBV/DrawInstanced)
  -> Present (vsync)
```

**No confundir** un clear offscreen o sin bits de color con el final del cuadro: cortarlo ahí puede dejar compositing negro. `RT_REBIND` fue necesario para conservar coherencia del render target en lotes offscreen largos.

## 5. Subsistemas y contratos delicados

### Vértices y geometría

- `vp_prepare_vertex_fetch` resuelve por draw máscara de atributos, bases, strides, tipos y offsets de slots.
- `vp_fetch_vertex` convierte formatos RSX/big-endian y rellena los atributos usados; el staging canónico documentado ocupa 256 B por vértice.
- Los `upload_*` escriben a `s_d3d.vp_vb_mapped` (192 MB en la versión documentada) y avanzan `vp_vb_offset`.
- `vp_upload_compact_frame` empaqueta atributos con stride `popcount(mask)*16` y alterna mitades mediante `vp_parity` para no sobrescribir datos en uso por GPU.
- Los strips indexados contienen reinicios `0x7FFF` aproximadamente cada 13 índices; `upload_native_strip_vp_indexed` inserta puentes y corrige paridad. **No sustituirlo por concatenación ingenua.**
- La caché `VP_CACHE` usa identidad de draw y generaciones de páginas guest de 4 KB; no reutilizar datos si cambian las páginas leídas. Las escrituras masivas que no pasan por `vm_write*` (lecturas `cellFs`/`sys_fs`, carga de partidas, PNG) también deben sellar sus páginas; la caché de texturas reutiliza una textura solo si ninguna página de su origen cambió desde la subida (el checksum disperso no detecta cambios pequeños, como un retrato personalizado). `VP_CACHE=0` y `TEX_NOCACHE=1` quedan como rollback de diagnóstico.

**Invariante de layout:** cambiar coordinadamente `s_vertex_fetch_plan.mask/slot_off[]`, `dr->vb_stride`, `dr->gpu_input_mask/dr->gpu_stride` y `s_d3d.vp_vb_offset`. Si un lado escribe compacto y otro lee canónico, puede aparecer pantalla negra sin error. No insertar un `return` en `upload_*` que descarte draws ni romper cadenas `if/else if/else` de primitivas: una regresión documentada redujo ~1743 draws a 51.

### Shaders, PSO y caché

- Vertex program guest → HLSL mediante `vp_get_vs`, caché por hash.
- Fragment program → HLSL mediante `rsx_fp_decompiler.c`, compilación con `D3DCompile` y caché local `%LOCALAPPDATA%/BakuganRecomp/shadercache/fp_v4_<GPU-driver>.cache`. Se comprueban tamaño, firma DXBC y checksum; los archivos antiguos de la instalación no se importan. La persistencia usa snapshots y reemplazo atómico en un hilo independiente; el cierre normal espera hasta 5 s al escritor. Sin identidad de GPU/driver o ruta escribible, la caché funciona en memoria. El instalador no distribuye bytecode cacheado.
- `vp_get_fp_pso` combina VS/FP, blend, RT, máscara de color, cull, depth y alpha; el prepass prepara PSO.
- **No hacer `free()` de bytecode que pertenece al contenedor de caché**: se observó corrupción de heap y cierre del juego tras el primer draw.
- Borrar caché de shaders solo con propósito diagnóstico y tras respaldo: provoca recompilaciones y tirones en el primer uso.

### Texturas, SRV y depth

`d3d12_bind_texture` conserva textura por unidad y draw. El prepass escribe una ventana de **16 SRV por draw** (`DRAW_SRV_BASE + draw*16`). La deduplicación `SRV_SHADOW` rompió HUD y cinemáticas: **no activarla sin demostrar equivalencia visual**. Las superficies de color se distinguen por offset; el depth por offset de zeta (D24S8 en la implementación descrita), con posibilidad de muestreo. No sustituir por un único depth compartido sin preservar las superficies offscreen y sus lecturas.

Errores D3D12 relevantes: `Present` con `0x887A0005` indica dispositivo eliminado; `0x887A0001` puede indicar llamada inválida. Crear UAV sin `ALLOW_UNORDERED_ACCESS` o efectuar `ByteAddressBuffer.Load` en offset no alineado a 4 puede invalidar el dispositivo. Diagnosticar con `D3D12_DBG=1` y `D3D12_GPU_VALIDATION=1` (requiere *Graphics Tools*), no tapar fallos eliminando draws.

### SPU, audio y vídeo

`SPU_INTERP_UNLIFTED=1` permite interpretar kernels SPU generados en runtime y volver al camino liftado. `spu_channels.c` implementa canales, mailboxes, señales y eventos MFC; los bloqueos deben ser **selectivos**, no globales. `SPU_CH_BLOCK=1` global llegó a bloquear la carga de Battle Arena. `PS3_NOVSYNC=1` causó arranques mudos/bloqueos en menú y no es una opción válida para comparar FPS.

El mezclador trabaja a 187,5 bloques/s. El sonido «brrr» observado durante caídas de FPS se relacionó con huecos en el PCM entregado por el guest, **no** con un reloj de mezcla defectuoso. `movie_video.cpp` y `video_decoder.cpp` decodifican vídeo; `movie_audio.cpp` sirve audio. Las presentaciones de personajes son render del juego, no archivos MP4.

### Filesystem e I/O

Los caminos originales documentados incluyen `runtime/ppu/ppu_fs.cpp` y `runtime/syscalls/sys_fs.c`; el runner registra el scaffold PPU que atiende `cellFs`. Existe un experimento separado de `runtime/io/asset_io.cpp` y `include/ps3emu/asset_io.h`, preservado mediante `patches/0049-asset-io-bounded-cache.patch`. **No asumir que está integrado o habilitado.**

El experimento implementó caché LRU de bloques de 64 KB (64 MB por defecto), identidad física de archivos, lecturas absolutas (`ReOpenFile`/`pread`), agrupación de fallos concurrentes, bypass de lecturas grandes, captura de rangos y precarga limitada. Solo elegía aperturas de solo lectura en `/dev_bdvd/` y `/app_home/`; no debía cachear guardados o archivos temporales. Los tests host verificaron bytes, offsets, cursores, concurrencia y trazas, pero **no demostraron equivalencia visual del juego**.

En pruebas de integración aparecieron estructuras ausentes y, en otra variante, tinte rojo/parpadeos. `PS3_IO_CACHE_MB=0` no restauró por sí solo el comportamiento original mientras los wrappers experimentales seguían activos. Retirar la integración de wrappers e instrumentación recuperó la imagen en la prueba documentada, **sin aislar una causa única**. No reaplicar automáticamente el parche 0049 ni activar `PS3_IO_TIMING`, `PS3_IO_TRACE_RECORD`, `PS3_IO_PRELOAD` o el monitor experimental en una referencia de fidelidad. Separar instrumentación, wrappers y caché al investigar.

### Save data

`libs/system/cellSaveData.c` implementa el applet de guardado. La raíz por defecto es `%LOCALAPPDATA%\BakuganRecomp\dev_hdd0\home\00000001\savedata`; `PS3_GAMEDATA_ROOT` la sobrescribe y la primera ejecución copia una vez `gamedata/` (marcador `.gamedata-migrated`). `cellSaveDataListSave2` crea un slot `...-NN` por guardado manual, con el nombre libre que calcula el `funcList` del juego. Si un slot `-NN` sólo contiene `PARAM.SFO` (primer guardado abortado), el siguiente guardado lo reutiliza en lugar de crear el número siguiente; así el slot no queda huérfano. `cellSaveDataFixedSave2` es el checkpoint y sobrescribe el directorio que pide el título (`PS3_SAVEDATA_NEW_SLOT_FIXED=1` fuerza slot nuevo). `ListAutoSave` mantiene `BLES00758-SYSTEM-DATA` fijo. `cellSaveDataListLoad2` y los borrados usan el selector dentro del juego (`bakugan-recomp/savedata_overlay.cpp`): un canvas GDI+ con `PIC1.PNG` del disco y el `ICON0.PNG` de cada slot, subido como textura D3D12 y dibujado sobre el cuadro final. Se controla con teclado, ratón o XInput; `PS3_SAVEDATA_UI=0` lo desactiva. `PS3_SAVEDATA_CHOICE=new|first|last|<n>|<dir>|cancel` automatiza la elección. El selector es entrada de host: `PAD_FILE`/`PAD_REPLAY` no lo alimentan, así que un replay que llega a "Load Game" se queda en esa pantalla salvo que se fije `PS3_SAVEDATA_CHOICE`. Cancelar devuelve `CELL_CANCEL` (1), no `CELL_SAVEDATA_ERROR_CBRESULT`; un título puede tratar ese error como fallo fatal y salir con `exit(-1)` (`0xffffffff` en PowerShell).

El instalador conserva las partidas sustituidas por `--save` bajo
`savedata/.backups/`, fuera de la lista de slots. El runtime omite nombres de
directorio de 32 bytes o más: no caben en `CellSaveDataDirList.dirName` y
truncarlos provoca que una selección existente se cargue como `new=1`.
`tools/test_savedata_names.py --compiler <clang-cl.exe>` verifica este filtro;
`tools/tests/installer_savedata_test.cs` verifica que una importación conserve
el contenido anterior sin ofrecer el respaldo como partida.

### Checkpoints de escena

`tools/scene.ps1` guarda y reproduce checkpoints reproducibles para testers y
agentes: línea de tiempo de `cellPad` desde el arranque + copia del savedata +
frame objetivo. No es un snapshot de memoria (los hilos nativos y los handles
D3D12/WASAPI no son portables entre procesos). `start` graba en una sesión
aislada; F5 guarda, F9 recarga el último checkpoint y `play` relanza el juego con
una copia limpia del savedata. Detalles y límites en `docs/SCENE_CHECKPOINTS.md`.

## 6. Guardrails: cambios y opciones de alto riesgo

| Evitar | Razón demostrada o riesgo |
|---|---|
| Editar `recompiled/` o `spu/` | Son fuentes generadas; regenerar desde origen |
| Resetear toolkit, cambiar rama o reaplicar parches masivamente | Puede destruir o duplicar modificaciones locales |
| Forzar terminación del proceso | Puede omitir limpieza de audio y enmascarar hilos colgados |
| `PS3_NOVSYNC=1` | Bloqueos/ausencia de audio observados |
| `SPU_CH_BLOCK=1` global | Espera SPURS sin productor durante carga |
| `SRV_SHADOW=1` | HUD/cinemáticas ausentes |
| `DRAW_KEEP_TEX`, `DRAW_LAST_TEX`, `DRAW_KEEP_BLEND`, `TEX_MARKEMPTY`, `EMPTYTEX`, `DEPTH_OFF`, `TEX_RESOLVE_AUTO` | Modifican contenido o fuentes del cuadro; solo diagnóstico controlado |
| Logs masivos con stderr redirigido sin `PS3_VERBOSE=0` | Se observaron ~6000 líneas/s y contención de `FILE`, con pérdida de inicialización/assets |
| `PS3_IO_CACHE_MB=0` como supuesto rollback | Los wrappers nuevos seguían causando regresión |
| Reintroducir `patches/0049-asset-io-bounded-cache.patch` sin aislamiento | Integración no validada visualmente |
| Nuevos reportes síncronos en `frame_clock` | Pueden perturbar scheduling y tiempos del guest |
| Concluir que más FPS equivale a optimización | Pueden faltar cientos de draws y geometría |

Otras señales de regresión: atributos half/s16 con `be16lo`/`be16hi` invertidos alteran componentes, a veces solo en cinemáticas; un triángulo transitorio puede deberse a datos del cuadro anterior. No cambiar texturas, SPU o shaders para compensar un fallo de I/O sin evidencia.

## 7. Rendimiento: evidencia histórica y límites de inferencia

**Estas cifras son muestras históricas, no un benchmark del HEAD actual.** En una batalla de referencia con imagen completa se midieron aproximadamente **16 FPS (63 ms/cuadro)**:

| Componente | Tiempo aproximado |
|---|---:|
| `submit` (decodificación de vértices + registro) | 25 ms (15,6 ms de vértices) |
| Walker FIFO + blits NV3089 | 13 ms |
| `render_frame` | 15 ms (flush ~8,4; SRV ~4; PSO ~2) |
| Espera antes del drain | 8 ms |

Las cifras están redondeadas y los tramos/esperas pueden solaparse en las métricas; no sumarlos como una contabilidad exacta. Durante 20 s se observaron `spu_exec` ~100 % de un núcleo, `frame_clock` ~86 % y otro hilo ~62 %; GPU 3D ~7,5 % (pico ~20,8 %). **Hipótesis principal sustentada:** límite por trabajo serial del `frame_clock` y/o sincronización guest/host, no por saturación GPU. La carga lenta y el SPU ocupado son fenómenos que requieren perfilado separado.

### Optimizaciones ensayadas sin mejora válida de FPS

| Ensayo | Resultado documentado |
|---|---|
| Compactar staging de vértices | Redujo tráfico (~109→36,8 MB) y flush (~8,4→3,8 ms), pero no elevó FPS; versión experimental revertida |
| `PS3_KICKWAIT=0` | Sin FPS extra; busy spin ~73 000 iteraciones/s |
| `VP_CACHE` | ~20 % hits y ~2 M vértices evitados por ventana, sin aumento medible de FPS |
| `GPU_VTX` compute sin espejo persistente | ~64 MB/cuadro de rangos crudos, `attr` ~2 ms menos, FPS similares (~15,1–15,8); revertido |
| SIMD manual / `restrict` | Sin ganancia relevante |
| Batch de 4 vértices para strips | Sin ganancia apreciable |
| `SRV_SHADOW` | Regresión visual, descartado |
| Ajustar reloj del mixer | No resuelve huecos PCM causados por el guest |
| Más flags del compilador | Ya se utilizaba Release `/O2` |
| Caché/precarga de I/O experimental | Microbenchmarks positivos, pero regresión visual; **ninguna mejora de FPS válida** |

En la prueba de I/O se vieron ~29–30 FPS **con gran parte de la arena ausente** (~594–658 draws frente a ~1750 en una batalla de referencia). **Estos FPS no son una mejora**; no compararlos con escenas completas. El microbenchmark del backend LRU reportó ~40,3 % menos tiempo en aciertos y ~16 % menos en expulsiones, pero mide un fixture sintético, no FPS ni tiempo de carga real.

### Hipótesis de mejora aún por validar

1. **Separar walker y `submit` en productor/consumidor.** Potencial de solapar ~13 ms de walker con ~25 ms de submit. Estimación teórica, no resultado probado; conservar orden de draws, memoria visible y fronteras de cuadro.
2. **Espejo persistente de memoria guest en VRAM**, actualizado por generaciones de páginas de 4 KB. Evitaría subir rangos crudos por draw; beneficio depende de cuánto reescribe el guest cada cuadro. La estimación histórica de hasta ~20 ms de CPU es especulativa.
3. **Especializar blits NV3089 escalados.** El camino general usa `vm_read32`/`vm_write32` píxel a píxel; evaluar escalas enteras 2x/1x y 2x/2x, coherencia por rangos y fallback seguro para fracciones/solapamientos.
4. **Evaluar upstream PS3Recomp v0.18.0 o posterior en worktree separado.** El documento histórico menciona mejoras de cachés, indexed draws, subida de texturas, SRV, RenderDoc y flip/present. No asumir compatibilidad ni que los 380 commits de diferencia sigan siendo actuales; compilar y comparar con la rama de referencia sin alterarla.
5. **Diagnosticar la carga lenta con I/O y SPU separados.** Medir tiempo real de lecturas, latencia de descompresión, esperas SPURS, page faults y CPU por hilo antes de elegir caché, AIO real o descompresor nativo. AIO verdaderamente asíncrono y sustitución del descompresor **no estaban implementados** en los documentos.

No prometer 30 FPS ni mejoras de carga sin A/B con imagen y comportamiento idénticos.

### Resolución, FSR y límite real (2026-11-07)

- El render interno del título es **1280x720** (viewports, buffer de display y
  composición final). Subir el backbuffer del host a 1080p deja la imagen en un
  rectángulo 1280x720 en la esquina; falsear `cellVideoOutGetResolution` la
  empequeñece. La ruta validada es 720p interno + FSR1 a la salida.
- El launcher se eliminó el 2026-11-08. `BakuganBrawlers.cmd` → `tools/play.ps1`
  es la entrada actual. La pérdida de HUD/cinemáticas se corrigió en SPURS/LFQueue;
  no era una obligación de redirigir stdio desde el padre. El puerto aplica
  Graphics, idioma y controles de player.ini, respetando el entorno explícito.
  Ver [estado actual](CONTINUAR_HUD.md). Las mediciones siguientes son históricas.
- Medición con `tools/bench_arena.ps1`, arena fija, 720p->1080p FSR1, `PERF=0`:
  media **28,65 FPS / 34,9 ms**, p95 41,9 ms, 57 % de cuadros > 33,3 ms.
- `PS3_NOVSYNC=1` no cambió el resultado (28,29 FPS): el techo no es Present,
  es trabajo CPU serial. `RSX_PACK_WORKERS=4` empeoró la media (24,4 FPS,
  p99 146 ms) y queda apagado.
- 720p nativo sin FSR: 28,37 FPS (frente a 28,65 con FSR 1080p). El upscaler no
  es el cuello; la GPU tampoco.
- `PS3_KICKWAIT=0`: 29,00 FPS, pero p95 42,5 ms (peor que 41,9). Sin ganancia
  clara; se mantiene el valor por defecto 4.
- 60 FPS exige bajar el trabajo de ~35 ms a <16,6 ms por cuadro; no se logra con
  ajustes locales. Requiere solapar walker/submit/render o reducir el coste del
  guest, y debe medirse con la misma arena.
- Topes de presentación: `PS3_FPS_CAP=30|60|90|120|monitor|0`. Verificado 30.00
  exacto; 60/90 no elevan el FPS de batalla porque el renderer sigue limitado
  por CPU. `PS3_TICK_HZ=90/120` es experimental (acelera la lógica del guest).
- Resolución dinámica: `PS3_OUTPUT=auto` (default) elige 2160/1440/1080/nativo
  según el panel y recorta si el monitor es menor; nunca pide más que el panel.
  Validado en 1080p: `auto` y `2160p` caen a 1920x1080 sin romper la vista.
- Texturas: aniso 16x por defecto (`PS3_ANISO`), `PS3_LOD_BIAS` opcional, RCAS
  0.6 y salida 1080p por defecto. Arena con aniso 16: **28,95 FPS** (p95 41,1)
  frente a 28,65 sin aniso; sin regresión. Los mips ya se suben completos.


## 8. Diagnóstico y validación para agentes

| Opción | Uso documentado |
|---|---|
| `PS3_VERBOSE=0` | Evitar logs verbosos que interfieren con el guest; especialmente al redirigir stderr |
| `PS3_WINDOW_MONITOR=primary` | Abrir la ventana D3D12 en el monitor principal |
| `PS3_FPS_TITLE=1` | FPS de presentaciones correctas de D3D12 en la barra de título (modo ventana) |
| `PERF=1` | Métricas de FPS/draws/vértices/submit/render; solo tras validar imagen |
| `AUDIO_RATE=1` | Tasa de mezcla, bloques silenciosos y muestras descartadas; resumen cada 5 s |
| `GCM_WORD_STATS=1` | Palabras FIFO por cuadro |
| `PS3_FTRACE=inicio:seg:fichero` | Spans de drain/present/SPU busy-wait |
| `VPCHK=1` | Comparación de vértices contra referencia lenta |
| `VBFULL=1`, `VTX_POS`, `VP_ATTRS`, `RSX_VTXDBG` | Diagnóstico puntual de vértices |
| `RSX_CAP`, `CELLMARK_DUMP` | Capturas de targets/cuadros |
| `D3D12_DBG=1`, `D3D12_GPU_VALIDATION=1` | Validación D3D12/GPU, si está instalada |

Secuencia de aceptación antes de medir rendimiento:

1. Aviso de guardado con audio y arranque normal.
2. Menú completo y navegable.
3. Primera cinemática con imagen y sonido.
4. Escenario de batalla completo: geometría, personajes, HUD, cartas, sombras, agua, colores, efectos, sin parpadeos.
5. Misma arena, cámara, personajes y configuración de CPU; solo entonces activar métricas y comparar FPS/tiempos.
6. Cerrar normalmente y verificar que el PID desaparece antes de la siguiente build.

Usar una build Release comparable y cambiar **una sola variable o subsistema por prueba**. Mantener capturas, opciones efectivas, hashes y logs locales; no versionar binarios, assets o trazas. Los tests de bytes de un backend no sustituyen una prueba visual; los contadores de `PERF` son ventanas de cuadros, no percentiles p95/p99 de frametime. El tiempo entre lanzamiento y llegada a batalla incluye navegación manual: no etiquetarlo como tiempo puro de carga.

### Prueba con boost desactivado (2026-11-09)

La investigación posterior de Pyrus, con el replay manual y la corrección de
profundidad, está en [PYRUS_RENDER_AUDIO.md](PYRUS_RENDER_AUDIO.md).

En un i7-13620H, Windows reportó `PERFBOOSTMODE=0` en corriente y batería.
La prueba usó Release, render interno 720p, salida 1080p FSR1 y límite de
30 FPS. El selector se omitió con `PS3_SAVEDATA_CHOICE=BLES00758-PLAYER-DATA-00`
y `PS3_SAVEDATA_UI=0`, sobre una copia aislada del guardado. La ejecución final
se hizo sin replay. La primera prueba con replay entró en Historia y quedó
fuera de la medición. `PS3_WINDOW_MONITOR=primary` ubicó la ventana en DISPLAY1.

En Ventus, con cámara de apuntado, HUD y cartas visibles, se midieron 713
intervalos en unos 25 s. La media fue **28,42 FPS**, mediana 34,77 ms,
p95 41,72 ms y p99 48,41 ms. Hubo 1008–1019 draws por ventana de `PERF` y cero
aserciones de edgezlib o errores GPUV en el log. El hilo `frame_clock` consumió
17,77 s de CPU. El decode/fetch de vértices medido sumó unos 12,3 ms por cuadro
(`vfetch`, mediana de ventanas de 20), y el prepass de SRV unos 5 ms por cuadro.
Es una referencia de este equipo bajo esas condiciones. Para cuantificar el
coste de quitar el boost falta una comparación con la misma escena y boost activo.

El usuario oyó ruido en el audio y observó cargas más lentas. La carga quedó
sin cronometrar. En una segunda ejecución con `AUDIO_RATE=1`, la arena Pyrus
mostró 187,4–187,6 bloques/s frente a 187,5 esperados y cero muestras descartadas
por falta de espacio. Hubo ventanas con bloques mezclados silenciosos. Las
cifras prueban el ritmo medio de mezcla y dejan por localizar el ruido audible.
El contador de descartes no mide subalimentación del dispositivo ni silencios
en un solo puerto. `AUDIO_WAV` guarda PCM previo a WASAPI para la siguiente
comparación con la salida. Mantener el ritmo del guest y los búferes hasta
tener evidencia del tramo que falla.

Evidencia local en `diagnostics/boost-off-20261109/run-080934797/`, con ajustes
de energía, hash, logs, frametimes.bin y ventus-sample.json. El contador de FPS
y la selección de monitor quedan en los parches 0067 y 0068 del toolkit.
Registro de la segunda prueba en `diagnostics/boost-off-20261109/run-081356102/stderr.log`.

### Ejecución directa de referencia con HUD 2D y logs

El ejecutable actual contiene la corrección del runtime descrita en
[HUD_START_BUG.md](HUD_START_BUG.md). Para reproducir con logs y guardado
aislado, usar el build actual del repositorio, no el binario antiguo guardado
en diagnostics/installed-fidelity. Abrir PowerShell nueva sin flags de otros
experimentos. El arranque normal para jugar es `BakuganBrawlers.cmd`.

Este bloque reproduce esa configuración, copia la baseline a un directorio nuevo
y separa stdout/stderr. Ejecutarlo sin otro proceso del juego abierto:

```powershell
Set-Location 'C:\Users\Varelis\Proyects\ps3recomp'
$repo = (Get-Location).Path
$recording = Join-Path $repo 'diagnostics/record-cards-20261108-111621'
$run = Join-Path $recording ('direct-hud-' + (Get-Date -Format yyyyMMdd-HHmmssfff))
$exe = Join-Path $repo 'bakugan-recomp/build/BakuganRecomp.exe'
$elf = Join-Path $repo 'game/PS3_GAME/USRDIR/EBOOT.elf'
foreach ($path in @($exe, $elf, "$recording/baseline", "$recording/cards.padrec")) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Falta la referencia: $path" }
}
if (Get-Process BakuganRecomp -ErrorAction SilentlyContinue) {
    throw 'Cierra normalmente el juego anterior y comprueba que termine.'
}
New-Item -ItemType Directory -Path $run | Out-Null
Copy-Item -LiteralPath "$recording/baseline" -Destination "$run/gamedata" -Recurse

$env:PS3_DEBUG = '1'
$env:PS3_VERBOSE = '0' # Obligatorio al redirigir logs: evita saturar al guest.
$env:PS3_VFS_ROOT = "$repo/game"
$env:PS3_GAMEDATA_ROOT = "$run/gamedata"
$env:PS3_SAVEDATA_CHOICE = 'BLES00758-PLAYER-DATA-00'
$env:PS3_INTERNAL = '720'
$env:PS3_OUTPUT = '720p'
$env:PS3_FSR = 'auto'
$env:PS3_FSR_SHARPNESS = '0.600000'
$env:PS3_FPS_CAP = '30'
$env:PS3_WINDOW_MODE = 'windowed'
$env:PS3_ANISO = '16'
$env:PS3_LOD_BIAS = '0'
$env:SPU_INTERP_UNLIFTED = '1'
$env:RSX_ACCUM_FRAME = '1'
$env:GCM_FULL_FRAMES = '1'
$env:RSX_PACK_WORKERS = '2'
foreach ($name in @('PS3_PLAYER_CONFIG', 'PS3_TICK_HZ', 'PS3_LANG',
    'PAD_RECORD', 'PAD_FILE', 'PAD_AUTOPRESS', 'PAD_SCRIPT', 'PAD_STICK',
    'PAD_SWEEP', 'PS3_SAVEDATA_UI', 'RSX_CAP', 'BAKUGAN_UI_IO_AUDIT')) {
    Remove-Item -LiteralPath "Env:$name" -ErrorAction SilentlyContinue
}
$env:PAD_REPLAY = "$recording/cards.padrec"
$env:PAD_CLOCK = 'flips'
$env:BAKUGAN_BOOT_REPORT = "$run/boot.txt"
$env:RSX_CAP_TRIGGER = "$run/capture.trigger"
$env:RSX_CAP_DIR = "$run/captures"

$game = Start-Process -FilePath $exe -ArgumentList "`"$elf`"" `
    -WorkingDirectory $repo -PassThru `
    -RedirectStandardOutput "$run/stdout.log" `
    -RedirectStandardError "$run/stderr.log"
Write-Host "PID: $($game.Id) | Logs: $run"
```

Registrar el hash del binario: recompilar cambia la referencia. Mantener
PS3_VERBOSE=0 al redirigir. El destino de stdio no sustituye la corrección
del protocolo. El bloque usa un guardado de prueba y no toca el original.

Observar el resultado del replay: validar por separado las escenas sosteniendo
y lanzando cartas, las cartas sobre el campo y el HUD 2D. El replay usa flips;
si cambia el ritmo puede navegar mal. Un log sin errores no valida la imagen.
Para navegación manual, quitar `PAD_REPLAY` y `PAD_CLOCK` antes de lanzar.

Solo después de observar la escena estable, desde la misma PowerShell:

```powershell
Set-Content -LiteralPath "$run/capture.trigger" -Value 'capture'
# Al terminar, cierre normal y comprobación de salida:
$game.Refresh()
if (-not $game.HasExited) {
    $game.CloseMainWindow() | Out-Null
    if (-not $game.WaitForExit(10000)) { Write-Warning 'El proceso aún no terminó.' }
}
Get-Process BakuganRecomp -ErrorAction SilentlyContinue
```

No disparar capturas durante la carga ni terminar el proceso a la fuerza.
`boot.txt`, `stdout.log`, `stderr.log` y la captura tardía quedan en `$run`.
La auditoría adicional de lecturas 2D se activa con
`$env:BAKUGAN_UI_IO_AUDIT = '1'` **antes** de lanzar; para comparar, activarla
en ambos casos y conservar también una ejecución sin ella.

### Build de referencia documentada (Windows)

```powershell
$env:PATH = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin;$env:PATH"
.\.venv\Lib\site-packages\cmake\data\bin\cmake.exe -S bakugan-recomp -B bakugan-recomp\build -G Ninja `
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM=<ruta-del-repo>\.venv\Scripts\ninja.exe `
  -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl
.\.venv\Scripts\ninja.exe -C bakugan-recomp\build
.\tools\run.ps1
```

`<ruta-del-repo>` es un marcador que debe sustituirse. Para recompilar sin reconfigurar, si el build existe: ` .\.venv\Scripts\ninja.exe -C bakugan-recomp\build `. Los scripts `tools/build.py` y `tools/pipeline.py` se describieron como retirados. Comprobar toolchain y rutas antes de ejecutar. Tests documentados: `tools/tests/`, CTest del toolkit y `VPCHK=1` para cambios de geometría. El fixture experimental de I/O (`tools/test_asset_io.py`) solo es pertinente si se trabaja expresamente en ese experimento.

## 9. Política de trabajo segura

**Antes de modificar:** inspeccionar HEAD, diferencias sin commit, archivos reales, configuración activa y referencia visual. Respaldar el estado sano sin pisar cambios ajenos. **Durante:** formular hipótesis comprobable, instrumentar mínimamente fuera del camino crítico cuando sea posible, modificar una pieza y ejecutar pruebas. **Después:** verificar integridad gráfica/audio, medir con carga equivalente, documentar resultado y revertir si hay regresión. Nunca ocultar draws, alterar SRVs, saltar SPU ni borrar superficies para fabricar FPS. El agente opera compilación/logs/código; las pruebas que requieren navegación del juego se hacen con entradas manuales del usuario, salvo autorización expresa para otra metodología.

---

**Alcance:** consolidación de conocimiento técnico, invariantes y riesgos; se han excluido snapshots de estado, commits/hashes coyunturales, cronologías, listas de tareas de sesiones cerradas y métricas inválidas como objetivos. Las cifras conservadas son **evidencia histórica**, no declaraciones sobre la build presente.
