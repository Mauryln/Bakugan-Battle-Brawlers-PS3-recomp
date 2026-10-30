<div align="center">

<img src="docs/img/title.jpg" alt="Bakugan: Battle Brawlers como ejecutable nativo de Windows" width="100%">

<h1>Bakugan: Battle Brawlers — recompilación nativa de la versión PS3 para Windows</h1>

<p><strong>La versión de PlayStation 3 (<code>BLES00758</code>), recompilada estáticamente a un ejecutable nativo de Windows x64.</strong><br>
Sin emulador. Sin código de Xbox 360. Sin Linux.</p>

<p>
<img src="https://img.shields.io/badge/plataforma-Windows%20x64-0078D6?style=flat-square" alt="Plataforma: Windows x64">
<img src="https://img.shields.io/badge/gr%C3%A1ficos-Direct3D%2012-107C10?style=flat-square" alt="Gráficos: Direct3D 12">
<img src="https://img.shields.io/badge/t%C3%ADtulo-BLES00758-c0392b?style=flat-square" alt="Título: BLES00758">
<img src="https://img.shields.io/badge/licencia-MIT-2ea44f?style=flat-square" alt="Licencia: MIT">
<img src="https://img.shields.io/badge/estado-men%C3%BAs%20y%20FMV%20ok%2C%20gameplay%20en%20curso-orange?style=flat-square" alt="Estado: menús y FMV ok, gameplay en curso">
</p>

<p><a href="README.md">English</a> · <a href="docs/STATUS.md">Estado</a> · <a href="docs/VIDEO.md">Vídeo</a> · <a href="docs/ARCHITECTURE.md">Arquitectura</a></p>

</div>

[Bakugan: Battle Brawlers](https://es.wikipedia.org/wiki/Bakugan:_Battle_Brawlers) (2009) nunca salió en PC. Este proyecto levanta el código **PPU (CPU) y SPU (DSP)** del juego a C++ de forma estática con [ps3recomp](https://github.com/sp00nznet/ps3recomp) y lo enlaza contra un runtime HLE pequeño: el resultado es un `.exe` normal que dibuja por Direct3D 12 a 30 FPS.

Aquí no hay emulación: el código máquina se traduce una vez y después se compila como cualquier otro programa.

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
| Build — clang-cl + Ninja + Direct3D 12, 30 FPS | ✅ |
| Assets — 607 trabajos de inflate descomprimidos en los SPU | ✅ |
| Logos de distribuidora — Activision, Nowpro, Premium Agency, Nelvana | ✅ |
| Apertura e intro narrativa — H.264/AAC con Media Foundation | ✅ |
| Pantalla de título, todos los menús, selector de modo, texturas 2D | ✅ |
| Teclado del juego (nombre) y pad de teclado/XInput | ✅ |
| Música — partituras PCM `SNDP` mezcladas en el SPU, más el AAC de los vídeos | ✅ |
| Battle Arena — tarda en cargar, luego inicia el combate | ⚠️ |
| *Defecto conocido:* el "gate" del selector de modo — las capas 2D tras el retrato se solapan | ⚠️ |
| **Escena 3D** — las cinemáticas se ven (texturas aún granuladas); el turno del jugador se va a negro | ⚠️ |
| **Gameplay** — tutorial, combates de la historia | ❌ |
| **Efectos de sonido** — ATRAC3 en `SOUND/*.PAST` | ❌ |

En una línea: **el juego arranca, reproduce sus vídeos (con música), muestra todos los menús, inicia un duelo de Battle Arena y la escena 3D se ve en las cinemáticas — pero el turno del jugador sigue en negro.**

## Se busca ayuda

1. **La escena 3D.** Battle Arena envía 438–600 dibujos por frame al target MSAA de 1120×584 (`0xEAB0000`) y el target sí recibe contenido. El fix de strip-restart (`patches/0009`) quitó la deformación, y el rebind del render target + caché de shaders (`patches/0010`) arregló el negro de las cinemáticas y los congelamientos de compilación. Falta el **negro del turno del jugador** (el composite muestrea el target de la escena pero no deja píxeles), más las texturas granuladas y las caras invisibles. Ver [`docs/NEXT_SESSION.md`](docs/NEXT_SESSION.md) para la prueba exacta.
2. **Lo que queda de audio.** Las partituras de menú y el mezclador SPU funcionan; los efectos `.PAST` son ATRAC3 y siguen en silencio, y quedan cortes por revisar. `libs/audio/`.
3. **Testers.** ¿Arranca en tu GPU/driver? Con un `run.log` y una frase basta para abrir un issue.

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
| `docs/` | Estado, vídeo, arquitectura |
| `ps3recomp-src/`, `game/`, `analysis/`, `bakugan-recomp/recompiled/` | Generado o ajeno — **no** versionado |

## Legal

Bakugan: Battle Brawlers es © de sus titulares. Este repositorio **no** distribuye código del juego, assets, claves de cifrado, código levantado ni binarios. Necesitas el disco original. El código propio del port es MIT; el toolkit es MIT de Ned Heller / sp00nznet — ver [CREDITS.md](CREDITS.md) y [LICENSE](LICENSE).
