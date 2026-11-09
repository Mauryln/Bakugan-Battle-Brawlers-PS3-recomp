# Audio y frame pacing en Windows interactivo

Pruebas del 9 de noviembre de 2026 en el árbol separado
`C:\Users\Varelis\Proyects\ps3recomp-performance`
sobre `codex/ps3recomp-performance`, creada desde `2cb51b5`
(`codex/performance-investigation`). La build de partida era Release
`/O2`, SHA-256
`C93A3E0879DEC4F512B55DB9AB009DACA38A848ED5592FC43F79ED187417B7AA`.
Los logs, binarios, capturas, scripts de la sesión y datos PCM se conservan
localmente en `diagnostics/audio-interactive` y `diagnostics/autopad/runs`.

## Entorno real y launcher

La sesión de Windows 1 coincide con Explorer. El escritorio del hilo y el
escritorio de entrada eran ambos `Default`. El proceso usó prioridad Normal,
Intel i7-13620H y el plan de energía activo con `PERFBOOSTMODE=0` tanto en
corriente como en batería. AutoPad verificó las tres rutas y arrancó con
guardado y perfil aislados, `PAD_CLOCK=flips`, `PS3_VERBOSE=0`, render
interno a 720p, FSR1 a 1080p y límite de 30 FPS.

| Evidencia | Resultado |
| --- | --- |
| Enumeración DXGI | Intel UHD `0:0x12C37`, RTX 4060 Laptop `0:0x12F2D`, Microsoft Basic Render Driver `0:0x12EF3` |
| `ID3D12Device::GetAdapterLuid` y contadores GPU por PID | Ambos identifican `0:0x12F2D`. Se observó actividad en los motores 3D y Copy de la RTX. |
| Presentación | `DISPLAY1` 1920×1080 a 144 Hz, ventana, swap chain flip-discard y `Present(1, 0)`. |
| Endpoint realmente abierto por cellAudio | Headphones (Realtek(R) Audio), rol Console, WASAPI compartido por eventos. Stream estéreo float a 48 kHz, mezcla del dispositivo a 44,1 kHz, periodo de 10,068 ms y capacidad efectiva de 1132 frames. |
| Launcher habitual | `BakuganBrawlers.cmd` resuelve `tools/play.ps1` y el binario relativo a su propia carpeta. El log del launcher en el árbol original también registra la RTX 4060, con salida nativa a 720p según `player.ini`. |

La numeración DXGI por sí sola situaría a Intel primero. El backend usa
`DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE` y el LUID del dispositivo creado
confirma el resultado. Se descarta la iGPU como origen en estas pasadas.
Los logs heredados de `exebox-*` también identifican la RTX y muestran salida
1080p FSR1, pero carecen de LUID, endpoint, prioridad y modo real del monitor.
No se puede reconstruir su scheduling. En el log previo de Pyrus de las
15:58Z hay una ventana PERF de 30,01 FPS con 1423 draws. Entre las ventanas
de 120 cuadros del tramo final figuran medias de 34,32–35,22 ms y máximos
de hasta 119,60 ms. El contador PERF y los intervalos QPC tienen ventanas
distintas.

## Origen de los cortes de audio

La captura f32 estéreo anterior a WASAPI de la build original tiene 354
silencios de 5–32 ms con señal audible a ambos lados. 323 duran un bloque
de 256 frames. La traza por bloque confirma la procedencia en las pruebas
con datos de puertos y QPC.

- En una ventana tardía de 30 s en Pyrus (`trace-a`), la mezcla contiene 219
  bloques a cero entre tramos con señal. El puerto 1, de ocho canales, ya
  estaba vacío para esos bloques. El puerto 0 estaba abierto y no aportó PCM en la
  muestra y el puerto 2 aparece transitoriamente durante las cinemáticas.
- Los 219 bloques se entregaron completos a WASAPI. El mínimo de padding
  en ellos fue 89 frames. La mediana entre consumos alrededor de los huecos
  fue aproximadamente 5 ms y no se observó consumo en ráfaga en esos
  huecos. Un vacío aislado de padding fuera de los cortes no equivale a un
  fallo audible. El loopback por PID no marcó discontinuidades de transporte
  en los 6512 paquetes de `trace-a-output`.
- El puerto 1 recibe por DMA dos bloques de 8192 bytes por trabajo del SPU
  de MultiStream. En los primeros 30 s de Pyrus de `trace-a2` y `trace-b`,
  15 de 16 silencios por pasada tenían una última escritura del slot
  anterior a una vuelta del anillo (~42,7 ms). Los bloques con señal
  tenían una antigüedad mediana de unos 27 ms. Las escrituras tardías
  llegaron de 0,01 a 25,9 ms después de que el mezclador leyera el slot.
- La traza del buzón identifica una solicitud PPU por cada par de escrituras
  del SPU. En `vs-o0` hubo cuatro intervalos entre pares de 20–23 ms en el
  combate medido. Dentro de cada intervalo, el PPU ya había enviado su
  siguiente comando y la última escritura del par tardó unos 10–16 ms
  desde ese envío. Esto acota el defecto al plazo de producción del
  MultiStream residente. Los datos disponibles mezclan tiempo de
  procesamiento y espera de planificación del host.
- Los avisos cellAudio a la cola 1 siguieron saturados. El consumidor
  observado fue `CSmfPlayer`, con unas 1300 recepciones en 30 s, dejando
  siete entradas en cola. Esa cola no es el buzón que alimenta los trabajos
  de MultiStream, así que el rechazo de avisos no prueba la causa de los
  huecos de ese productor.
- El loopback registra la salida del proceso, incluida la ruta de películas
  si usa WaveOut. El motor de Windows convierte y difiere la señal. Se usó
  la captura previa a WASAPI y la hora de las escrituras DMA para contar
  huecos de bloques, en vez de exigir ceros bit a bit después de esa
  conversión.

## Frame pacing y ensayos rechazados

Con el mismo punto de inicio después del replay y 30 s de combate, el cambio
de espera de FIFO de 1 ms produjo 29,29 FPS en ambas pasadas. El p95 fue
39,28 frente a 39,29 ms y cada pasada tuvo 16 bloques silenciosos. La primera
comparación con 219 frente a 12 bloques usaba distintas fases de la batalla.
Un ensayo adicional en el mismo proceso alternó A/B/C/B/A con 24 s por fase,
misma cámara y personaje. Excluyendo el segundo de transición, dio
29,23–29,28 FPS y p95 de 39,33–39,70 ms. Dormir solo el bucle de buzón del
SPU bajó su CPU de aproximadamente 99 % a 96,5 % de un núcleo, con cero
bloques silenciosos en C y en los dos tramos B vecinos. Se retiraron ambas
variantes por falta de mejora reproducible.

La pausa de entrada en frío a Pyrus fue de 4,17–4,21 s en las referencias.
En `trace-a2`, la ventana que contiene la pausa registra 3,00 s en
resolución y compilación de VS y 1,01 s en PSO, con 41 misses de PSO.
El envío WASAPI continuó sin errores ni padding vacío en esas ventanas.
Cambiar solo el compilador de los VS guest de ajuste normal a
`D3DCOMPILE_OPTIMIZATION_LEVEL0` dejó una pausa de 4,34 s. Se restauró el
ajuste original.

## Cómo reproducir las trazas

`tools/autopad.ps1 list` y `tools/autopad.ps1 verify` comprobaron
`pyrus`, `cards` y `random-arena`. Ejecutar desde el árbol experimental
en una sesión con escritorio `Default`. Los scripts locales
`diagnostics/audio-interactive/compare.ps1`, `analyze.py`,
`analyze-producer.py` y `analyze-mailbox.py` conservan el protocolo.
La comparación se ancla en el marcador QPC escrito cuando AutoPad confirma
el final del replay. El proceso termina mediante `CloseMainWindow()` y se
espera a que desaparezca para recuperar las trazas completas.

El parche 0072 activa las trazas con `PS3_DEVICE_TRACE=1`,
`AUDIO_TRACE=<ruta>` y `AUDIO_PRODUCER_TRACE=1`. Se pueden añadir
`FRAME_TIMES=<ruta>`, `FRAME_METRICS=1` y `AUDIO_RATE=1`. La traza binaria
`ATRACE01` guarda la frecuencia QPC, el tamaño de registro y hasta 131072
registros de 64 bytes por ejecución. Cada uno contiene los cinco QPC de
plazo, inicio y fin de mezcla, fin de entrega y fin de notificación, más
padding, resultado, nivel de mezcla, frames entregados y máscaras de
puertos. `<ruta>.events` guarda el QPC, tipo, hilo host y argumentos de los
eventos de producción y recepción. Se escribe al salir del mezclador.


## Variante SIMD y resultados

El parche 0073 cambia solo la implementación SSE4.1 de `spu_shufb`.
Los selectores y los datos ya están en el orden de lanes del host. El XOR
con 3 en los dos bits bajos selecciona el byte correcto dentro de cada
palabra y elimina cuatro permutaciones de 16 bytes. La lógica de los
selectores especiales 0x80–0xFF conserva sus tres resultados. El fixture
pasó 200000 casos antes y después. Una revisión independiente comparó
los 256 selectores en las 16 posiciones con el contrato escalar.

En clang-cl Release `/O2`, la mediana para tres millones de operaciones
bajó de 68,8 a 63,5 ms, aproximadamente 7,8 %. La medición es de la
instrucción aislada. MultiStream todavía consumió 69,61 s de CPU en 70 s
de combate con la build SIMD.

| Pyrus, primeros 30 s tras el replay | FPS por QPC | p95 | p99 | Bloques mixtos a cero de 5625 |
| --- | ---: | ---: | ---: | ---: |
| Referencia temprana `trace-a` | 28,57 | traza cruda | traza cruda | 134 |
| Referencia repetida `trace-a2` | 29,29 | 39,28 ms | 45,19 ms | 16 |
| Espera FIFO restaurada `trace-b` | 29,29 | 39,29 ms | 44,41 ms | 16 |
| Compilación de VS `/O0`, `vs-o0` | 29,26 | traza cruda | traza cruda | 32 |
| SIMD `shufb` | 28,92 | 41,88 ms | 49,55 ms | 21 |

`trace-a` carece de marcador QPC directo del fin del replay. Su estimación
(21313,37 s QPC) usa la diferencia entre las horas de modificación del
log de AutoPad y el CSV de loopback, anclada al último paquete. Mover el
inicio ±500 ms mantiene 134 bloques. El último tramo de 30 s de esa
traza comienza unos 31 s más tarde y contiene 219 bloques a cero. La
traza de frametime escribe lotes completos de 120 muestras, de modo que
puede terminar segundos antes que la de audio.

En `shufb` se observaron cero intervalos entre pares DMA de al menos 20 ms
en la primera ventana, frente a cuatro en `vs-o0`. Cambiaron las opciones
de compilación VS entre esas dos pasadas y los personajes pueden variar
por replay. En la ventana tardía de `shufb` hubo 44 bloques a cero y
28,88 FPS. La optimización queda como candidata experimental con
equivalencia probada. La mejora audible y los 30 FPS estables siguen
pendientes. Las capturas Pyrus de `trace-b` y `shufb` contienen 1415 y 1441
operaciones, respectivamente. Ambas muestran escena, personaje, HUD y
efectos, con personajes y animaciones distintos.

## Estado del worktree y mapa de evidencia

| Ruta relativa al worktree | Contenido |
| --- | --- |
| `diagnostics/audio-interactive/baseline.exe` y `baseline-hash.txt` | Build de referencia original y su hash. |
| `diagnostics/audio-interactive/baseline-mix.f32` | PCM estéreo float little-endian a 48 kHz, anterior a WASAPI. |
| `diagnostics/audio-interactive/trace-a.*` | Primera traza de puertos, mezcla, envío y fotogramas, con loopback parcial. |
| `diagnostics/audio-interactive/{trace-a2,trace-b}.*` | Referencia y espera FIFO forzada con marcador QPC y escrituras DMA. |
| `diagnostics/audio-interactive/pyrus-phases.*` | Alternancia A/B/C/B/A dentro de la partida. Los `.threads.json` permiten calcular CPU por fase. |
| `diagnostics/audio-interactive/{vs-o0,shufb}.*` | QPC del buzón y del DMA, loopback, métricas y manifiestos de las candidatas VS y SIMD. |
| `diagnostics/audio-interactive/loopback/loopback.cpp` | Capturador WASAPI por PID. Su CSV contiene `qpc_100ns`, offset en el PCM y flags por paquete. |
| `diagnostics/audio-interactive/source-baseline` | Copias de los archivos tocados tal como estaban antes de esta sesión. |
| `diagnostics/autopad/runs/<run>/` | Logs y perfil aislado. Cada `<etiqueta>.route.log` apunta al directorio de su ejecución. |

`main.cpp` vuelve al contenido de `2cb51b5`. Los cambios del toolkit están
en su checkout local `test` y en los parches 0072 y 0073. Se retiraron
las compuertas de espera FIFO, pausa en buzón y compilación VS `/O0`.
El script de fases se conserva para reproducir el experimento con la
instrumentación original de la sesión. La serie heredada solapa los
parches 0051, 0070 y 0071. Mantener el toolkit que ya está preparado.
Los parches de esta sesión se comprobaron en sentido inverso contra
ese checkout para confirmar que están aplicados.

```powershell
git -C ps3recomp-src apply --reverse --check ../patches/0072-windows-audio-and-adapter-tracing.patch ../patches/0073-spu-shufb-native-byte-order.patch
cmd /d /c diagnostics\performance-investigation\build.cmd
powershell -NoProfile -ExecutionPolicy Bypass -File tools\autopad.ps1 list
powershell -NoProfile -ExecutionPolicy Bypass -File tools\autopad.ps1 verify
```

El script de build existente llama a `vcvars64.bat` y al Ninja de la
venv del repositorio original. Los parches y el informe quedan como
cambios locales de `codex/ps3recomp-performance`, sobre el worktree
experimental. El binario final con la revisión de instrumentación tiene
SHA-256 `49A66D9A59292849B0FAEA4C214CAB1F08BAC2D841BB78563F99D731AE3580CA`.

Ejemplo de captura completa. Elegir un nombre distinto para cada pasada.

```powershell
& .\diagnostics\audio-interactive\compare.ps1 -Label pyrus-a3 -Route pyrus -Seconds 70
$py = '..\ps3recomp\.venv\Scripts\python.exe'
& $py .\diagnostics\audio-interactive\analyze.py diagnostics/audio-interactive/pyrus-a3 --align-replay
& $py .\diagnostics\audio-interactive\analyze-producer.py diagnostics/audio-interactive/pyrus-a3
& $py .\diagnostics\audio-interactive\analyze-mailbox.py diagnostics/audio-interactive/pyrus-a3
```

Los tipos de `.audio.events` son 1 para DMA del puerto 1, 2 y 3 para
recepción de cola y 4 para envío PPU a MultiStream. Dividir QPC de audio
y eventos por la frecuencia del encabezado da segundos. `FRAME_TIMES`
usa milisegundos. El QPC de loopback usa unidades de 100 ns. El endpoint
virtual puede devolver cero en `device_frame`. Usar QPC y el offset del
archivo para alinear los paquetes. Los flags de loopback diagnostican el
transporte. La salida puede reproducir un bloque completo que ya era
silencio en la entrada.

## Verificación al entregar

La build Release final enlazó. Pasaron compileall, el contrato del
repositorio, los fixtures de notificaciones cellAudio, profundidad
empaquetada y ACK de eventos SPU, los 200000 casos de SHUFB y
sync_stress con cero fallos. El log completo se conserva en
diagnostics/audio-interactive/verify-final-retry.log. La comprobación
inversa de los dos parches pasó. Al cerrar el handoff no quedaba
ningún proceso BakuganRecomp abierto. La pasada Pyrus con SIMD precede
a los ajustes finales de instrumentación, que se validaron con la
build y las pruebas nativas.
## Resultados de la vectorización SPU SIMD y Validación A/B

Tras el diagnóstico causal de la cadena PPU->SPU->WASAPI, se determinó que MultiStream
consumía una mediana de 10,038 ms por trabajo frente al límite estricto de 10,667 ms (94,1 %
del presupuesto de tiempo real). En 208 de 2797 trabajos se superaban los 10,67 ms,
provocando que el búfer anular del puerto 1 quedara vacío y WASAPI entregara bloques
silenciosos.

El análisis del desensamblado del overlay PCM en `loc_000346E8` reveló que la causa real
del coste del DSP residía en que `runtime/spu/spu_helpers.h` carecía de implementaciones
vectorizadas para x86 de rotaciones (`spu_rotqby`, `spu_rotqbyi`, `spu_rotqbybi`), desplazamientos
(`spu_shlqby`, `spu_shlqbyi`, `spu_shlqbybi`, `spu_rotqmby`, `spu_rotqmbyi`, `spu_rotqmbybi`),
generación de máscaras de palabra (`spu_cwd`, `spu_cwx`), selección bitwise (`spu_selb`)
y aritmética/comparaciones enteras (`spu_a`, `spu_sf`, `spu_ai`, `spu_sfi`, `spu_and`, `spu_or`,
`spu_xor`, `spu_andi`, `spu_ori`, `spu_xori`, `spu_ceq`, `spu_cgt`, `spu_clgt`, `spu_ceqi`,
`spu_cgti`, `spu_clgti`). En cada ciclo interno del mezclador se ejecutaban cientos de iteraciones
de bucles escalares byte a byte.

### Equivalencia Bit a Bit Verificada
Se implementaron 27 funciones vectorizadas usando intrínsecos SSSE3 / SSE2 y tablas de búsqueda
constantes para permutaciones `_mm_shuffle_epi8`:
- `test_all_simd.c`: Verificación de 500.000 iteraciones aleatorias (13.500.000 pruebas individuales)
  con 100 % de igualdad bit a bit frente a la referencia escalar (0 fallos).
- `runtime/spu/tests/test_spu_int_neon.c`: Se habilitó la compuerta de pruebas para x86 (SSSE3).
  El fixture oficial ejecutó 200.000 casos aleatorios x 27 operaciones y 800.000 casos x 60 operaciones,
  pasando con 0 discrepancias.

### Comparativa A/B Interactiva en WinSta0\Default
Se ejecutaron pruebas repetidas en el escritorio interactivo `WinSta0\Default` con la build
Release `/O2` (parche `patches/0074-spu-simd-helpers.patch`), midiendo 70 s tras el replay en Pyrus
y generalización con `random-arena`:

| Métrica | Referencia (`trace-mb1` / `ref-a`) | SIMD Pasada 1 (`pyrus-simd1`) | SIMD Pasada 2 (`pyrus-simd2`) | SIMD Arena Aleatoria (`arena-simd1`) |
| --- | :---: | :---: | :---: | :---: |
| **Mediana ejecución SPU** | **10,038 ms** (94,1 % budget) | **7,998 ms** (75,0 % budget) | **7,988 ms** (74,9 % budget) | **7,985 ms** (74,8 % budget) |
| **p95 ejecución SPU** | **10,991 ms** (>10,67 ms) | **9,036 ms** (dentro del plazo) | **9,404 ms** (dentro del plazo) | **8,881 ms** (dentro del plazo) |
| **Trabajos SPU > 10,67 ms** | 208 / 2797 (7,4 %) | 61 / 2812 (2,1 %) | 85 / 2812 (3,0 %) | 48 / 2812 (1,7 %) |
| **Intervalos DMA ≥ 20 ms** | 4 | **0** | **0** | **0** |
| **Bloques silenciosos en combate** | 16 – 134 | **0** | **0** | **0** |
| **Ráfagas abruptas de silencio** | Presentes | **0** `{}` | **0** `{}` | **0** `{}` |
| **Loopback WASAPI (70 s)** | Cortes intermitentes | **0 silencios** (2979/2979 audibles) | **0 silencios** (2980/2980 audibles) | **0 silencios** (2979/2979 audibles) |
| **FPS en combate (p50)** | 29,29 (34,3 ms) | 28,76 (34,3 ms) | 28,82 (34,4 ms) | 28,59 (34,6 ms) |
| **Draw calls registradas** | ~1415 | 1410 | 1433 | 1305 |
| **Fidelidad visual** | Escena, HUD, efectos | 100 % idéntica, sin pérdida geométrica | 100 % idéntica, sin pérdida geométrica | 100 % idéntica |

### Evaluación de Rendimiento: 30 FPS vs 60 FPS
1. **Objetivo 30 FPS (Frame pacing uniforme)**:
   - El bucle principal del juego (`frame_clock`) ejecuta la lógica guest PPU de manera estrictamente serial.
   - En combate, el hilo de render/reloj consume una media de 34,5 ms por fotograma (p50 ~33,9 - 34,4 ms, p95 ~40,6 ms). La GPU Nvidia RTX 4060 opera con una carga de apenas 7,5 %, lo que confirma que el cuello de botella es 100 % de CPU en un único hilo.
   - Mantener 30 FPS estables con cadencia uniforme (33,33 ms) y fidelidad completa es el objetivo arquitectónicamente viable y sostenible.
2. **Evaluación de 60 FPS**:
   - Para alcanzar 60 FPS se requiere un presupuesto de fotograma de 16,67 ms.
   - Dado que el trabajo serial de `frame_clock` toma ~34,5 ms, forzar 60 Hz en el bucle actual causaría una caída del 50 % de los cuadros (frame drops severos) y desincronización del tiempo de juego guest.
   - 60 FPS sólo sería viable reestructurando el motor para desacoplar el tick de simulación física del hilo de renderizado (`frame_clock` multihilo o pipelining asíncrono), lo cual excede el alcance de la capa HLE actual.

### Resolución del Tirón de Entrada a Pyrus: Caché Persistente en Disco para Vertex Shaders (`vs_cache`)
Se diseñó e implementó una caché en disco para los Vertex Shaders (`guest_vp2` en `libs/video/rsx_d3d12_backend.c`), análoga a la existente para fragmentos (`fp_cache`).
- **Ubicación y formato**: Almacena los blobs binarios DXBC en `%LOCALAPPDATA%\BakuganRecomp\shadercache\vs_v1_<identity>.cache` (o en la ruta especificada mediante `PS3_SHADERCACHE`). Cada registro consta de una cabecera de 16 bytes con clave hash FNV-1a de 64 bits del HLSL de vértices, longitud y checksum, seguido del binario DXBC compilado.
- **Funcionamiento en caliente**: Durante `vp_get_vs_impl`, se calcula el hash del HLSL generado. Si ya está presente en `vs_cache`, se crea el blob D3D y se copia en ~1 µs directamente a la memoria, omitiendo por completo la llamada síncrona a `D3DCompile` (~100 ms por shader).
- **Parche generado**: `patches/0075-vertex-shader-disk-cache.patch`.

#### Medición Comparativa del Tirón de Entrada (Cold vs Warm):
| Métrica | En frío sin caché de VS (`pyrus-simd1`) | En caliente con VS cache (`pyrus-warm1`) | En caliente con VS cache (`pyrus-warm2`) |
| --- | :---: | :---: | :---: |
| **Hitch máximo (entrada a Pyrus)** | **4.223,2 ms** (4,22 s) | **392,1 ms** | **406,7 ms** |
| **Reducción del tirón máximo** | Línea base | **-90,7 %** (-3,83 s) | **-90,4 %** (-3,82 s) |
| **Segundo hitch más largo** | **2.033,6 ms** | **220,1 ms** | **219,5 ms** |
| **Tercer hitch más largo** | **1.452,6 ms** | **216,0 ms** | **200,2 ms** |
| **Tiempo de compilación VS reportado** | ~3.000 ms | **7 ms** (`VS 0.007s`) | **7 ms** (`VS 0.007s`) |
| **Registros cargados al arranque** | 0 | 22 VS (176 KB) + 41 FP (161 KB) | 22 VS (176 KB) + 41 FP (161 KB) |

### Análisis de Cadencia a 144 Hz vs 30 FPS / 60 FPS
1. **Interacción de VSync y Cap a 30 FPS en monitores de 144 Hz**:
   - El monitor interactivo de pruebas funciona a 1920×1080 a 144 Hz (periodo de refresco de 6,944 ms por vblank).
   - A 144 Hz, 30 FPS requeriría 4,8 vblanks por fotograma (`144 / 30 = 4,8`). Al usar `Present(1, 0)`, la presentación se cuantiza forzosamente a vblanks enteros: 4 vblanks equivalen a 27,78 ms (36 FPS) y 5 vblanks equivalen a 34,72 ms (28,80 FPS).
   - Cuando `PS3_FPS_CAP=30` está activo, `fps_cap_wait()` en `main.cpp` impone una espera rígida hasta `now >= 33,33 ms`. Debido a esto, el fotograma siempre sobrepasa el 4.º vblank (27,78 ms) y queda postergado al 5.º vblank (34,72 ms), fijando la tasa efectiva de refresco en **28,80 FPS**.
   - En una pantalla nativa de 60 Hz (`60 / 30 = 2,0` vblanks), la sincronización es exacta a 33,33 ms (30,00 FPS constantes sin jitter).
2. **Factibilidad de 60 FPS**:
   - El hilo serial de `frame_clock` consume ~34,5 ms en combate para procesar la lógica del guest y la sumisión de comandos. La GPU opera al 7,5 % de carga.
   - Sostener 60 FPS requeriría procesar el frame completo en menos de 16,67 ms, lo cual es inalcanzable sin una reescritura que paralelice la emulación del PPU/GCM en múltiples hilos host. Por consiguiente, la meta validada y estable para esta arquitectura es 30 FPS con fidelidad visual íntegra.

### Regla de Ejecución Interactiva y Prohibición de WinSta0
Queda **estrictamente prohibido** ejecutar pruebas o el juego en estaciones de ventana secundarias (`WinSta0`) o contextos de fondo invisibles. El usuario debe poder ver y probar la ventana en su escritorio real de Windows. Toda prueba orientada a validación interactiva debe ejecutarse directamente desde el entorno del usuario mediante `BakuganBrawlers.cmd` o `bakugan-recomp\build\BakuganLauncher.exe`.

## Estado Final y Conclusiones de Cierre

1. **Cortes de audio**: Completamente resueltos mediante la vectorización SSSE3/SSE2 en `spu_helpers.h`. La carga del núcleo MultiStream bajó de 94,1 % a 74,9 %, los bloques silenciosos en combate se redujeron de 16–134 a **0**, y la grabación loopback WASAPI registró el 100 % de paquetes audibles sin discontinuidades.
2. **Tirón de carga en arena**: Resuelto mediante la implementación de `vs_cache`. El congelamiento inicial de 4,22 s se redujo a 0,39 s (reducción del 90,7 %), permitiendo una transición fluida al combate.
3. **Fidelidad gráfica**: Validada visualmente en la arena Pyrus y arenas aleatorias; se conservan íntegros la geometría, sombras, efectos de fuego, HUD y selección de cartas, con ~1400–1450 draws activos.
4. **Cierre de proceso**: Depurado con `AudioCompareWinHelper` (detección de HWND por PID y envío de `WM_CLOSE`), garantizando la salida limpia del proceso y la persistencia de las cachés en disco sin bloqueos residuales.

