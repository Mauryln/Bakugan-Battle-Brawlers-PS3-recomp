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

<p><a href="README.md">English</a> · <a href="docs/STATUS.md">Estado</a> · <a href="docs/ARCHITECTURE.md">Arquitectura</a> · <a href="docs/PERFORMANCE.md">Rendimiento</a></p>

</div>

> **03/11/2026 — el juego es jugable de principio a fin.** Historia y batallas se
> pueden jugar completas, con HUD, cartas, cinemáticas de personajes, sombras y
> reflejos. **Todavía no hay release:** falta cerrar el sistema de guardado y
> mapear el control de verdad — ahora el mando se simula, no se traduce desde la
> librería de input del juego. El problema que queda es el rendimiento: 16–17 FPS
> en batalla, con el ruido de audio siguiendo al tiempo de cuadro. Es una
> limitación del motor del port, no del juego.
> [Estado](docs/STATUS.md) · [Lo medido](docs/PERFORMANCE.md) ·
> [Lo que no hay que tocar](docs/GUARDRAILS.md).

[Bakugan: Battle Brawlers](https://es.wikipedia.org/wiki/Bakugan:_Battle_Brawlers) (2009) nunca salió en PC. Este proyecto levanta el código **PPU (CPU) y SPU (DSP)** del juego a C++ de forma estática con [ps3recomp](https://github.com/sp00nznet/ps3recomp) y lo enlaza contra un runtime HLE pequeño: el resultado es un `.exe` normal que dibuja por Direct3D 12. El juego completo es jugable; los menús van a 30 FPS y las batallas a 16–17 FPS, que es donde queda trabajo.

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
| **Rendimiento** — 16–17 FPS en batalla; el ruido de audio sigue al tiempo de cuadro | ⚠️ |
| **Guardado** — cargar funciona, escribir la partida está incompleto | ❌ |
| **Control** — el pad XInput se simula, no se traduce desde la librería del juego | ❌ |
| **Efectos de sonido** — ATRAC3 en `SOUND/*.PAST` | ❌ |

**El juego es jugable de principio a fin. No habrá release hasta cerrar el guardado y el mapeo real del control; el rendimiento es el tercer bloqueo.**

## Se busca ayuda

1. **Rendimiento.** Reducir tirones de carga y alcanzar 30 FPS sostenidos, conservando efectos y cuadros completos. Lo ya medido y lo descartado está en [`docs/PERFORMANCE.md`](docs/PERFORMANCE.md).
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

Requisitos: Windows 10/11 x64 con Direct3D 12, Python 3.10+ y **Visual Studio 2022 Build Tools** con *Desktop development with C++* y el componente *C++ Clang Compiler for Windows*. Unos 15 GB libres.

```powershell
# 1. Tu propio volcado del disco, en iso/
#    "Bakugan - Battle Brawlers (Europe).iso"

# 2. Entorno de Python
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r ps3recomp-src\requirements.txt
.\.venv\Scripts\python.exe -m pip install cmake ninja

# 3. El toolkit, fijado al commit al que aplican nuestros parches
git clone https://github.com/sp00nznet/ps3recomp.git ps3recomp-src
git -C ps3recomp-src checkout a6d76c8

# 4. Regenerar todo (ISO -> ELF -> análisis -> lifts -> tabla HLE)
.\.venv\Scripts\python.exe tools\pipeline.py

# 5. Compilar y ejecutar
.\.venv\Scripts\python.exe tools\build.py
.\tools\run.ps1
```

**Controles:** `X` / Espacio = Cross (confirmar) · `C` `Z` `S` = Circle / Square / Triangle · flechas = D-pad · `Enter` = Start · `Q` `E` = L1/R1 · `1` `2` = L2/R2.

## Estructura

| Ruta | Qué es |
|---|---|
| `bakugan-recomp/` | El port: punto de entrada, stubs HLE, vídeo D3D12, build CMake |
| `patches/` | Nuestros cambios al toolkit ps3recomp; los aplica el pipeline |
| `tools/` | Pipeline, build, lanzador y diagnóstico |
| `docs/` | Estado, arquitectura, guardarraíles y rendimiento; `docs/legacy/` conserva los handoffs anteriores |
| `ps3recomp-src/`, `game/`, `analysis/`, `bakugan-recomp/recompiled/` | Generado o ajeno — **no** versionado |

## Legal

Bakugan: Battle Brawlers es © de sus titulares. Este repositorio **no** distribuye código del juego, assets, claves de cifrado, código levantado ni binarios. Necesitas el disco original. El código propio del port es MIT; el toolkit es MIT de Ned Heller / sp00nznet — ver [CREDITS.md](CREDITS.md) y [LICENSE](LICENSE).
