<div align="center">

<img src="docs/img/title.jpg" alt="Bakugan: Battle Brawlers running as a native Windows executable" width="100%">

<h1>Bakugan: Battle Brawlers — native PS3 recompilation for Windows</h1>

<p><strong>The PlayStation 3 release (<code>BLES00758</code>), statically recompiled into a native Windows x64 executable.</strong><br>
</p>

<p>
<img src="https://img.shields.io/badge/platform-Windows%20x64-0078D6?style=flat-square" alt="Platform: Windows x64">
<img src="https://img.shields.io/badge/graphics-Direct3D%2012-107C10?style=flat-square" alt="Graphics: Direct3D 12">
<img src="https://img.shields.io/badge/title-BLES00758-c0392b?style=flat-square" alt="Title: BLES00758">
<img src="https://img.shields.io/badge/license-MIT-2ea44f?style=flat-square" alt="License: MIT">
<img src="https://img.shields.io/badge/status-Battle%20Arena%20gameplay%20fixed-green?style=flat-square" alt="Aquos reflections and shadows corrected; performance pending">
</p>

<p><a href="README.es.md">Español</a> · <a href="docs/STATUS.md">Status</a> · <a href="docs/VIDEO.md">Video pipeline</a> · <a href="docs/ARCHITECTURE.md">Architecture</a> · <a href="CONTRIBUTING.md">Contributing</a></p>

</div>

> **2026-11-01 — Aquos reflections and shadows corrected.** Falling water, rain,
> reflections and dark shadows are confirmed by the user. Performance is the
> current priority: Aquos gameplay is around 12–15 FPS, loading still stutters,
> with audio crackle under load. FIFO recycling was fixed; the user's latest
> Aquos test had no black flashes. The original 30 FPS cap is preserved;
> sustained 30 FPS has not been achieved.
> [Current status and next steps](docs/NEXT_SESSION.md) · [Measurements](docs/PERFORMANCE_2026-11-01.md).

[Bakugan: Battle Brawlers](https://en.wikipedia.org/wiki/Bakugan:_Battle_Brawlers) (2009) was never ported to PC. This project lifts the game's **PPU (CPU) and SPU (DSP) code to C++ ahead of time** with [ps3recomp](https://github.com/sp00nznet/ps3recomp) and links it against a small HLE runtime, producing an ordinary `.exe` that renders through Direct3D 12. Menus reach 30 FPS; detailed Aquos gameplay currently runs around 12–15 FPS, with further performance work pending.

Lifted code is compiled ahead of time; dynamic SPU jobs without a matching recompiled image can use the runtime fallback.

| | |
|---|---|
| ![Boot menus](docs/img/newgame.jpg) | ![Narrative intro](docs/img/intro.jpg) |
| Title flow, save-data warning, loading, new game / load game | Narrative intro — H.264/AAC decoded through Media Foundation |

## Status

| Milestone | |
|---|---|
| Disc analysis — 12,834 OPD functions, 195 imports, 14 libraries | ✅ |
| PPU lift — 15,682 functions / 17 translation units | ✅ |
| SPU lift — 5 images, 1,331 functions | ✅ |
| HLE table — 1,204 handlers | ✅ |
| Build — clang-cl + Ninja + Direct3D 12 | ✅ |
| Asset pipeline — 607 inflate jobs decompressed on the SPUs | ✅ |
| Publisher FMVs — Activision, Nowpro, Premium Agency, Nelvana | ✅ |
| Opening + narrative intro — H.264/AAC through Media Foundation | ✅ |
| Title screen, all menus, mode select, 2D textures | ✅ |
| In-game keyboard (name entry) and keyboard/XInput pad | ✅ |
| Music — `SNDP` PCM scores mixed by the SPU, plus FMV AAC | ✅ |
| Battle Arena — player/CPU turns, field and card textures fixed | ✅ |
| *Known defect:* mode-select "gate" — 2D layers behind the portrait overlap | ⚠️ |
| **3D scene** — Aquos reflections and shadows corrected; other arenas need validation | ⚠️ |
| **Gameplay** — tutorial and story require validation | ⚠️ |
| **Sound effects** — ATRAC3 in `SOUND/*.PAST` | ❌ |

**Battle Arena is playable with corrected field and card textures. Performance, audio under load, prolonged stability, and other-arena validation remain.**

## Help wanted

These are the walls we are stuck on. Each one is a self-contained problem with a measurement already done — the details are in [`docs/STATUS.md`](docs/STATUS.md) and [`docs/NEXT_SESSION.md`](docs/NEXT_SESSION.md).

1. **Performance.** Reduce loading stalls and reach sustained 30 FPS while preserving effects and complete frames. See [`docs/NEXT_SESSION.md`](docs/NEXT_SESSION.md).
2. **Audio leftovers.** Menu scores and the SPU mixer work; the `.PAST` effects are ATRAC3 and still silent, and a few cuts are under review. `libs/audio/`.
3. **Testers.** Does it boot on your GPU/driver? A `run.log` and one sentence is enough to open an issue.

Partial work, wrong guesses and "this is how RPCS3 does it" are all welcome — open an issue first if you plan something large.


## How it works

```
iso/*.iso  ──extract_iso──▶  game/  ──EBOOT decrypt──▶  EBOOT.elf
                                                        │
                             analysis/  ◀──ppu_loader────┘
                                 │
                    ┌────────────┴─────────────┐
              lift PPU (17 TUs)          lift SPU (5 images)
                    │                          │
                    └────────────┬─────────────┘
                          bakugan-recomp/
                                 │  + HLE runtime (ps3recomp toolkit)
                                 ▼
                     BakuganRecomp.exe  ──D3D12──▶  your monitor
```

**Everything generated is rebuilt from your own disc dump.** This repository contains no game code, no assets, no keys and no lifted sources — see [Legal](#legal).

## Build it

Requirements: Windows 10/11 x64 with Direct3D 12, Python 3.10+, and **Visual Studio 2022 Build Tools** with *Desktop development with C++* and the *C++ Clang Compiler for Windows* component. About 15 GB free.

```powershell
# 1. Your own disc dump
#    put "Bakugan - Battle Brawlers (Europe).iso" in iso/

# 2. Python environment
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r ps3recomp-src\requirements.txt
.\.venv\Scripts\python.exe -m pip install cmake ninja

# 3. The toolkit, pinned to the commit our patches apply to
git clone https://github.com/sp00nznet/ps3recomp.git ps3recomp-src
git -C ps3recomp-src checkout a6d76c8

# 4. Regenerate everything (ISO -> ELF -> analysis -> lifts -> HLE table)
.\.venv\Scripts\python.exe tools\pipeline.py

# 5. Compile and run
.\.venv\Scripts\python.exe tools\build.py
.\tools\run.ps1
```

`pipeline.py` also applies [`patches/`](patches) to the toolkit checkout, so step 3 is enough to reproduce our runtime changes. Use `--only <step>` for a single stage or `--force` to redo everything; the step list prints on `--help`.

**Controls:** `X` / Space = Cross (confirm) · `C` `Z` `S` = Circle / Square / Triangle · arrows = D-pad · `Enter` = Start · `Q` `E` = L1/R1 · `1` `2` = L2/R2.

## Repository layout

| Path | What it is |
|---|---|
| `bakugan-recomp/` | The port: entry point, HLE stubs, D3D12 movie path, CMake build |
| `patches/` | Our changes to the ps3recomp toolkit, applied by the pipeline |
| `tools/` | Pipeline, build, launch and diagnostic scripts |
| `docs/` | Status, video pipeline, architecture |
| `ps3recomp-src/`, `game/`, `analysis/`, `bakugan-recomp/recompiled/` | Generated or upstream — **not** versioned |

## Diagnostics

Every fix in this project started as a measurement, and the knobs are still there: `RSX_CAP` (frame capture with a draw manifest), `RTT_STAT`, `VDEFDBG`, `VTXALLF`/`VTXGATE` (per-draw bounds), `VPSEL`, `FLIPPC`, `NODEPTH`, `MTDBG`. They are environment variables, off by default, documented where they are defined.

## Legal

Bakugan: Battle Brawlers is © its rights holders. This repository ships **no game code, no assets, no encryption keys, no lifted sources and no binaries**. You must own the disc. The port's own code is MIT licensed; the toolkit is MIT by Ned Heller / sp00nznet — see [CREDITS.md](CREDITS.md) and [LICENSE](LICENSE).
