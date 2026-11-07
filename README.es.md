<div align="center">

<img src="docs/img/title.jpg" alt="Bakugan: Battle Brawlers como ejecutable nativo de Windows" width="100%">

<h1>Bakugan: Battle Brawlers — recompilación nativa de la versión PS3 para Windows</h1>

<p><strong>La versión de PlayStation 3 (<code>BLES00758</code>), recompilada estáticamente a un ejecutable nativo de Windows x64.</strong><br>
</p>

<p>
<img src="https://img.shields.io/badge/plataforma-Windows%20x64-0078D6?style=flat-square" alt="Plataforma: Windows x64">
<img src="https://img.shields.io/badge/gr%C3%A1ficos-Direct3D%2012-107C10?style=flat-square" alt="Gráficos: Direct3D 12">
<img src="https://img.shields.io/badge/t%C3%ADtulo-BLES00758-c0392b?style=flat-square" alt="Título: BLES00758">
<img src="https://img.shields.io/badge/licencia-MIT-2ea44f?style=flat-square" alt="Licencia: MIT">
<img src="https://img.shields.io/badge/status-jugable%20de%20principio%20a%20fin-yellow?style=flat-square" alt="Jugable de principio a fin; rendimiento limitado por el motor">
</p>

<p><a href="README.md">English</a> · <a href="docs/PROJECT_GUIDE.md">Guía técnica</a> · <a href="docs/LAUNCHER.md">Launcher y controles</a></p>

</div>

> **07/11/2026 — menos trabajo serial del walker.** Dos trabajadores CPU preparan
> la subida de vértices mientras el dueño prepara materiales. En ocho ventanas
> balanceadas de arena, frame_clock bajó del **80,1% al 69,0% de un núcleo**;
> la media pasó de **28,71 a 29,30 FPS**, con mejor p95/p99. La CPU total del
> proceso fue similar. **Siguen pendientes los 30 FPS estables.** [Mediciones y límites](docs/SESSION_20261107_WALKER.md).

> Se conservan también reducciones pequeñas del filtro de depuración y copias de tiras; su aumento adicional combinado de FPS sigue sin medirse.

> **Nuevo: salida FSR 1 a 1080p.** EASU + RCAS oficiales de AMD reescalan el
> cuadro completo de 720p a 1080p. La ventana se ajusta al monitor e incluye modo
> sin bordes y gestión de DPI. La arena medida promedió **28,02 FPS**:
> mejora la resolución de salida, **sin aumento demostrado de FPS**.
> [Uso y límites](docs/FSR.md) · [Resultados de sesión](docs/SESSION_20261106_FSR.md).
>
> **06/11/2026 — gran mejora de rendimiento en batalla.** Las escenas de arena
> medidas alcanzan aproximadamente **27–28 FPS**, frente a la referencia anterior
> de **16–17 FPS**. Las copias coherentes por filas de NV3089 reducen trabajo CPU
> y conservan escenario, agua y HUD en las capturas revisadas.
> **Todavía no hay release:** el launcher y el mapeo están implementados; los 30 FPS
> sostenidos siguen pendientes. La última optimización aún no se validó en un
> duelo completo posterior al lanzamiento ni en todas las arenas.
> [Mejora medida](#mejoras-recientes-de-rendimiento) · [Estado](docs/PROJECT_GUIDE.md).

[Bakugan: Battle Brawlers](https://es.wikipedia.org/wiki/Bakugan:_Battle_Brawlers) (2009) nunca salió en PC. Este proyecto levanta el código **PPU (CPU) y SPU (DSP)** del juego a C++ de forma estática con [ps3recomp](https://github.com/sp00nznet/ps3recomp) y lo enlaza contra un runtime HLE pequeño: el resultado es un `.exe` normal que dibuja por Direct3D 12. El juego completo es jugable; los menús van a 30 FPS y la comparación más reciente de arena fija promedió 29,30 FPS con subida paralela de vértices. Queda alcanzar 30 FPS sostenidos y reducir los tirones.

El código levantado se compila por adelantado; los trabajos SPU dinámicos sin imagen recompilada pueden usar el fallback del runtime.

| | |
|---|---|
| ![Menús de arranque](docs/img/newgame.jpg) | ![Intro narrativa](docs/img/intro.jpg) |
| Flujo de título, aviso de guardado, carga, nueva partida / continuar | Intro narrativa — H.264/AAC con Media Foundation |

## Estado

| Hito | |
|---|---|
| Análisis del disco — 12.834 funciones OPD, 195 imports, 14 librerías | ✅ |
| Lift PPU — 15.682 funciones / 17 unidades de traducción | ✅ |
| Lift SPU — 5 imágenes, 1.331 funciones | ✅ |
| Tabla HLE — 1.204 handlers | ✅ |
| Build — clang-cl + Ninja + Direct3D 12 | ✅ |
| Assets — 607 trabajos de inflate descomprimidos en los SPU | ✅ |
| Logos de distribuidora — Activision, Nowpro, Premium Agency, Nelvana | ✅ |
| Apertura e intro narrativa — H.264/AAC con Media Foundation | ✅ |
| Pantalla de título, todos los menús, selector de modo, texturas 2D | ✅ |
| Teclado del juego (nombre) y pad de teclado/XInput | ✅ |
| Música — partituras PCM `SNDP` mezcladas en el SPU, más el AAC de los vídeos | ✅ |
| Battle Arena — turnos 1P/CPU, campo y cartas corregidos | ✅ |
| Historia y batallas jugables de principio a fin | ✅ |
| HUD, cartas, cinemáticas de personajes, sombras y reflejos | ✅ |
| *Defecto conocido:* el "gate" del selector de modo — las capas 2D tras el retrato se solapan | ⚠️ |
| **Rendimiento** — 29,30 FPS en la última comparación de pack en arena fija, frente a ~16–17 FPS históricos; 30 FPS sostenidos pendientes | ⚠️ |
| **Guardado** — cargar funciona, escribir la partida está incompleto | ❌ |
| **Control** — remapeo PS3 persistente, mandos SDL3, Sixaxis real o palanca R; DS4 probado | ✅ |
| **Efectos de sonido** — ATRAC3 en `SOUND/*.PAST` | ❌ |

**El juego es jugable de principio a fin. El launcher y el mapeo están implementados; faltan ampliar las pruebas de hardware y validar 30 FPS sostenidos para el lanzamiento.**

## Launcher y controles

Ejecuta `tools/run.ps1` sin argumentos o abre `bakugan-recomp/build/BakuganLauncher.exe`.
El launcher con estética Bakugan carga el fondo y la imagen del título del dump local; no distribuye assets.
El usuario completó una partida Bakugan con esta build. El launcher guarda resolución de salida, FSR 1, nitidez, filtrado anisotrópico, límite de FPS,
modo de ventana, idioma EU y mapeo de botones y palancas PS3 en
`%LOCALAPPDATA%/BakuganRecomp/player.ini`. El idioma predeterminado sigue Windows.
SDL3 detecta mandos y sensores reales; sin sensores se utiliza la palanca derecha.
Se probaron mapeo y sensores de DS4; DualSense y Switch Pro requieren validación física.
Los parámetros explícitos del script o `-Direct` conservan el arranque de desarrollo.
[Controles, compilación y validación](docs/LAUNCHER.md).

## Mejoras recientes de rendimiento

La build actual conserva estas mejoras juntas:

| Cambio | Comportamiento actual | Evidencia |
|---|---|---|
| Copias por filas NV3089 | Menos trabajo CPU serial en blits compatibles | Comparación controlada más abajo |
| Subida paralela de vértices | Dos trabajadores preparan el staging cerrado mientras el dueño prepara materiales | CPU del walker **80,1% → 69,0% de un núcleo**, FPS **28,71 → 29,30**, p95 **41,51 → 40,41 ms**, p99 **46,23 → 44,97 ms** |
| Caché del filtro de depuración | Evita consultas repetidas a `getenv` cuando el filtro está ausente o es negativo | 32 casos de opciones/modos; aumento adicional combinado de FPS sin medir |
| Copia diferida de `last` en tiras | Recupera el bloque ya emitido sólo cuando un corte lo necesita | 172032 paquetes con bytes idénticos; aumento adicional combinado de FPS sin medir |
| Salida FSR 1 | Cuadro completo de 720p reescalado a 1080p y ventana ajustada al monitor | Mejora de resolución; sin aumento demostrado de FPS |

Las cifras de subida corresponden a ocho ventanas balanceadas de 30 segundos en
el mismo proceso, anteriores a las dos adiciones pequeñas. La combinación final
está compilada y conservada; **su ganancia adicional no está medida y siguen
pendientes los 30 FPS estables**.
[Resultados completos, pruebas y opciones de rollback](docs/SESSION_20261107_WALKER.md).

La referencia histórica de 16–17 FPS y las observaciones anteriores de 27–28 FPS
proceden de ejecuciones diferentes: muestran avance, no un porcentaje A/B.
La comparación aislada de NV3089 mantuvo **escena, personaje, cámara y proceso**,
alternando optimizado/original/optimizado:

| Camino | FPS | Tiempo medio por cuadro |
|---|---:|---:|
| Original | 23,09 | 43,31 ms |
| Optimizado, primera ventana | 27,27 | 36,67 ms |
| Optimizado, repetición | 27,05 | 36,97 ms |

Las ventanas optimizadas promedian **27,16 FPS / 36,82 ms**: aproximadamente
**15% menos tiempo por cuadro y 17,6% más FPS** en esa comparación. El trabajo
CPU de NV3089 bajó de **6,03 a 1,42 ms por flip**. Se conservan recorte, orden
hacia adelante en solapamientos y notificaciones de reservas SPU; los casos no
soportados mantienen el camino original. No se eliminaron draws, shaders ni
efectos deliberadamente para subir los FPS.

Las capturas conservaron la firma habitual de batalla (~1750 operaciones), con
geometría, personajes, agua y HUD visibles; el audio fue confirmado normal en
las pruebas revisadas. La validación es acotada: no demuestra equivalencia en
todas las arenas, cartas/efectos ni un duelo completo. **Aún no hay 30 FPS estables.**

Código reproducible: [parche 0051](patches/0051-nv3089-scaled-rows.patch).
`NV3089_SCALED_ROWS=0` antes de ejecutar restaura el camino original.
El test independiente cubre 10000 filas solapadas, 1000 blits recortados,
límites de direcciones y fallbacks de coherencia. No requiere toolkit ni datos del juego:

```powershell
.\tools\test_gcm_scaled_rows.ps1 -FromPatch
```

## Se busca ayuda

1. **Rendimiento.** Reducir tirones de carga y alcanzar 30 FPS sostenidos, conservando efectos y cuadros completos. Lo ya medido y lo descartado está en [`docs/SESSION_20261107_WALKER.md`](docs/SESSION_20261107_WALKER.md).
2. **Guardado.** Cargar una partida funciona; escribirla está incompleto.
3. **Control.** El pad se simula desde el camino del teclado en vez de traducirse desde la librería de input del juego.
4. **Lo que queda de audio.** Las partituras de menú y el mezclador SPU funcionan; los efectos `.PAST` son ATRAC3 y siguen en silencio, y quedan cortes por revisar. `libs/audio/`.
5. **Testers.** ¿Arranca en tu GPU/driver? Con un `run.log` y una frase basta para abrir un issue.

Se aceptan intentos a medias, hipótesis equivocadas y "esto lo hace RPCS3 así". Para algo grande, abre primero un issue.


## Cómo funciona

```
iso/*.iso  ──extract_iso──▶  game/  ──descifrado EBOOT──▶  EBOOT.elf
                                                          │
                             analysis/  ◀──ppu_loader─────┘
                                  │
                    ┌─────────────┴────────────┐
              lift PPU (17 TUs)           lift SPU (5 imágenes)
                    └─────────────┬────────────┘
                          bakugan-recomp/ + runtime HLE (toolkit)
                                  ▼
                      BakuganRecomp.exe  ──D3D12──▶  tu monitor
```

**Todo lo generado se reconstruye desde tu propio volcado del disco.** Este repositorio no contiene código del juego, ni assets, ni claves, ni código levantado.

## Compilar

Requisitos: Windows 10/11 x64 con Direct3D 12, CMake/Ninja y Visual Studio Build
Tools con **clang-cl** y el Windows SDK x64. `cl` no está soportado. Necesitas tu
propio dump, `EBOOT.elf` descifrado, lifts generados y un checkout compatible del
toolkit; estos archivos no se distribuyen aquí.

Con una build Release ya configurada:

```powershell
.\.venv\Scripts\ninja.exe -C bakugan-recomp\build
.\tools\run.ps1
```

Para configurar inicialmente un árbol de fuentes ya preparado, usa un entorno
de desarrollo x64 de Visual Studio con clang-cl, CMake y Ninja en PATH:

```powershell
cmake -S bakugan-recomp -B bakugan-recomp/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl
cmake --build bakugan-recomp/build
```

`tools/pipeline.py` y `tools/build.py` ya no se distribuyen; actualmente no hay un
pipeline de ISO a build en un solo comando. No apliques todos los parches del
toolkit indiscriminadamente: el experimento de I/O 0049 no forma parte de la
referencia sana aceptada.

**Controles:** `X` / Espacio = Cruz (confirmar) · `C` `Z` `S` = Círculo / Cuadrado / Triángulo · flechas = D-pad · `Enter` = Start · `Q` `E` = L1/R1 · `1` `2` = L2/R2.

## Estructura

| Ruta | Qué es |
|---|---|
| `bakugan-recomp/` | El port: punto de entrada, stubs HLE, vídeo D3D12, build CMake |
| `patches/` | Cambios archivados del toolkit; revisar individualmente los aceptados y sus requisitos |
| `tools/` | Lanzador, pruebas de regresión y herramientas de diagnóstico |
| `docs/` | Estado, arquitectura, guardarraíles y rendimiento; `docs/legacy/` conserva los handoffs anteriores |
| `ps3recomp-src/`, `game/`, `analysis/`, `bakugan-recomp/recompiled/` | Generado o ajeno — **no** versionado |

## Legal

Bakugan: Battle Brawlers es © de sus titulares. Este repositorio **no** distribuye código del juego, assets, claves de cifrado, código levantado ni binarios. Necesitas el disco original. El código propio del port es MIT; el toolkit es MIT de Ned Heller / sp00nznet — ver [CREDITS.md](CREDITS.md) y [LICENSE](LICENSE).
