<div align="center">


<h1>Bakugan: Battle Brawlers — native PS3 recompilation for Windows</h1>

<p><strong>The PlayStation 3 release (<code>BLES00758</code>), statically recompiled into a native Windows x64 executable.</strong><br>
</p>

<p>
<img src="https://img.shields.io/badge/platform-Windows%20x64-0078D6?style=flat-square" alt="Platform: Windows x64">
<img src="https://img.shields.io/badge/graphics-Direct3D%2012-107C10?style=flat-square" alt="Graphics: Direct3D 12">
<img src="https://img.shields.io/badge/title-BLES00758-c0392b?style=flat-square" alt="Title: BLES00758">
<img src="https://img.shields.io/badge/license-MIT-2ea44f?style=flat-square" alt="License: MIT">
<img src="https://img.shields.io/badge/status-playable%20end%20to%20end-yellow?style=flat-square" alt="Playable end to end, performance limited by the engine">
</p>

<p><a href="README.es.md">Español</a> · <a href="docs/PROJECT_GUIDE.md">Guía técnica</a> · <a href="docs/PROJECT_GUIDE.md">FSR y salida</a> · <a href="CONTRIBUTING.md">Contributing</a></p>

</div>

> **2026-11-07 — less serial walker work.** Two CPU workers now pack the closed
> vertex staging while the owner prepares materials. In eight balanced arena
> windows, frame_clock fell from **80.1% to 69.0% of one core**; average output
> changed from **28.71 to 29.30 FPS**, with better p95/p99. Total process CPU was
> similar. **Stable 30 FPS remains a target.** [Measurements and limits](docs/PROJECT_GUIDE.md).

> Small debug-filter lookup and strip-copy reductions are also retained; their additional combined FPS effect is not yet measured.

> **New: FSR 1 output at 1080p.** Official AMD EASU + RCAS upscale the complete
> 720p frame to 1080p. Windowed output fits the monitor; borderless mode and
> per-monitor DPI handling are available. The checked arena averaged **28.02 FPS**;
> this improves output resolution, with **no demonstrated FPS increase**.
> [Usage and limits](docs/PROJECT_GUIDE.md) · [Session results](docs/PROJECT_GUIDE.md).
>
> **2026-11-06 — major battle performance improvement.** Measured arena scenes
> now run at roughly **27–28 FPS**, compared with the earlier **16–17 FPS** reference.
> Coherent NV3089 row copies reduce CPU work while preserving the visible scene,
> water and HUD in the checked captures.
> **No release yet:** controller mapping is now available through the launcher; broader hardware validation is pending,
> and sustained 30 FPS is still a target. The latest optimization has not yet
> been validated through a complete post-launch duel or across every arena.
> [Measured improvement](#recent-performance-improvements) · [Guía técnica](docs/PROJECT_GUIDE.md).

[Bakugan: Battle Brawlers](https://en.wikipedia.org/wiki/Bakugan:_Battle_Brawlers) (2009) was never ported to PC. This project lifts the game's **PPU (CPU) and SPU (DSP) code to C++ ahead of time** with [ps3recomp](https://github.com/sp00nznet/ps3recomp) and links it against a small HLE runtime, producing an ordinary `.exe` that renders through Direct3D 12. The whole game is playable end to end; menus hold 30 FPS and the latest fixed-arena comparison averaged 29.30 FPS with parallel vertex uploads. Sustained 30 FPS and frame-time consistency remain the performance goals.

Lifted code is compiled ahead of time; dynamic SPU jobs without a matching recompiled image can use the runtime fallback.

| | |
|---|---|
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
| Story and battles playable end to end | ✅ |
| HUD, cards, character cinematics, shadows and reflections | ✅ |
| *Known defect:* mode-select "gate" — 2D layers behind the portrait overlap | ⚠️ |
| **Performance** — 29.30 FPS in the latest fixed-arena pack comparison, versus the historical ~16–17 FPS; sustained 30 FPS pending | ⚠️ |
| **Save data** — new game, manual saves (one slot each), load picker, system data | ✅ |
| **Controller** — persistent PS3 remapping, SDL3 gamepads, real Sixaxis sensors or right-stick fallback; DS4 tested | ✅ |
| **Sound effects** — ATRAC3 in `SOUND/*.PAST` | ❌ |

**The game is playable end to end. Controller mapping is implemented; the launcher and installer are deferred; broader hardware coverage and sustained 30 FPS remain release validation work.**

Saves live in `%LOCALAPPDATA%\BakuganRecomp\dev_hdd0\home\00000001\savedata`
(set `PS3_GAMEDATA_ROOT` to override). The first launch copies the old
`./gamedata` tree there once. `cellSaveDataListSave2` gives every manual save
its own `...-NN` slot, `cellSaveDataFixedSave2` checkpoints update the current
slot, and the system data keeps its fixed directory. Loading draws an in-game
overlay over the finished frame (the game's `PIC1.PNG` backdrop and each slot's
`ICON0.PNG`, keyboard/pad/mouse); `PS3_SAVEDATA_UI=0` restores the headless
defaults. The list/fixed delete entry points share the same callback flow.

## How to play, and controls

Double-click `BakuganBrawlers.cmd`, or run `.\BakuganBrawlers.cmd` from the
repository root. Logs are written to `%LOCALAPPDATA%\BakuganRecomp`.
The old launcher and installer were removed; rebuilding them is deferred until
runtime validation is complete. See [current defects](docs/DEFECTOS_Y_INSTALADOR.md).

The runner reads graphics, language and custom input settings from
`%LOCALAPPDATA%\BakuganRecomp\player.ini`. Explicit environment settings take
precedence. Internal rendering stays at 1280x720; output scaling uses FSR1.
See [the technical guide](docs/PROJECT_GUIDE.md) and
[the SPURS investigation](docs/HUD_START_BUG.md).

## Recent performance improvements

The current build retains these changes together:

| Change | Current behavior | Evidence |
|---|---|---|
| NV3089 row copies | Less serial CPU work for supported blits | Controlled comparison below |
| Parallel vertex uploads | Two workers pack closed staging while the owner prepares materials | Walker CPU **80.1% → 69.0% of one core**, FPS **28.71 → 29.30**, p95 **41.51 → 40.41 ms**, p99 **46.23 → 44.97 ms** |
| Debug-filter lookup cache | Avoid repeated `getenv` calls for an absent/negative filter | 32 option/mode regression cases; additional combined FPS effect unmeasured |
| Deferred strip `last` copy | Recover the already-emitted block only when a cut consumes it | 172032 packet comparisons with identical bytes; additional combined FPS effect unmeasured |
| FSR 1 output | Complete 720p frame upscaled to 1080p, with monitor-fitting windows | Resolution improvement; no demonstrated FPS gain |

The upload figures come from eight balanced 30-second windows in one process,
before the two small follow-up changes. The final combination is built and retained;
**its extra gains have not been measured, and stable 30 FPS is still pending**.
[Full results, tests and rollback options](docs/PROJECT_GUIDE.md).

The historical 16–17 FPS reference and the earlier 27–28 FPS observations come
from different runs; they describe progress, not a controlled percentage gain.
The isolated NV3089 comparison used the **same scene, character, camera and
process**, switching optimized/original/optimized:

| Path | FPS | Mean frame time |
|---|---:|---:|
| Original | 23.09 | 43.31 ms |
| Optimized, first window | 27.27 | 36.67 ms |
| Optimized, repeat window | 27.05 | 36.97 ms |

The two optimized windows average **27.16 FPS / 36.82 ms**, about **15% less frame
time and 17.6% more FPS** than the original path in that comparison. NV3089 CPU
work fell from approximately **6.03 to 1.42 ms per flip**. Copies preserve clipping,
forward order for overlapping ranges and SPU reservation notifications; unsupported
cases retain the original path. No draws, shaders or effects were deliberately removed.

Captures retained the normal battle draw signature (roughly 1750 operations),
with scene geometry, characters, water and HUD visible; audio was confirmed normal
in the reviewed runs. This is bounded validation, not proof for all arenas,
every card/effect or a complete duel. **30 FPS is not yet stable.**

The implementation is archived in [patch 0051](patches/0051-nv3089-scaled-rows.patch).
Set `NV3089_SCALED_ROWS=0` before launching to restore the original copy path.
The standalone regression test covers 10000 overlapping rows, 1000 clipped blits,
address bounds and coherent fallbacks. Run it without a toolkit checkout or game data:

```powershell
.\tools\test_gcm_scaled_rows.ps1 -FromPatch
```

## Help wanted

These are the walls we are stuck on. Each one is a self-contained problem with a measurement already done - the details are in [`docs/PROJECT_GUIDE.md`](docs/PROJECT_GUIDE.md).

1. **Performance.** Reduce loading stalls and reach sustained 30 FPS while preserving effects and complete frames. What has already been measured, and what it ruled out, is in [`docs/PROJECT_GUIDE.md`](docs/PROJECT_GUIDE.md).
2. **Controller.** The pad is simulated from the keyboard path instead of being mapped from the guest's input library.
3. **Audio leftovers.** Menu scores and the SPU mixer work; the `.PAST` effects are ATRAC3 and still silent, and a few cuts are under review. `libs/audio/`.
4. **Testers.** Does it boot on your GPU/driver? A `run.log` and one sentence is enough to open an issue.

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

Requirements: Windows 10/11 x64 with Direct3D 12, CMake/Ninja, and Visual Studio
Build Tools with **clang-cl** and the x64 Windows SDK. `cl` is not supported.
You must provide your own disc dump, decrypted `EBOOT.elf`, generated lifts and
compatible local toolkit checkout; these inputs are not shipped here.

For an existing configured Release tree:

```powershell
.\.venv\Scripts\ninja.exe -C bakugan-recomp\build
.\BakuganBrawlers.cmd
```

Internal rendering stays at 1280x720. Set output resolution and FSR1 in `player.ini`; no 60 FPS guarantee is made.

For a prepared source tree that needs initial configuration, use an x64 Visual
Studio developer environment with `clang-cl`, CMake and Ninja on PATH:

```powershell
cmake -S bakugan-recomp -B bakugan-recomp/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl
cmake --build bakugan-recomp/build
```

The former `tools/pipeline.py` and `tools/build.py` scripts are no longer shipped;
there is currently no one-command ISO-to-build pipeline in this repository.
Do not apply every toolkit patch indiscriminately: experimental I/O patch 0049
is not an accepted part of the healthy reference.

**Controls:** `X` / Space = Cross (confirm) · `C` `Z` `S` = Circle / Square / Triangle · arrows = D-pad · `Enter` = Start · `Q` `E` = L1/R1 · `1` `2` = L2/R2.

## Repository layout

| Path | What it is |
|---|---|
| `bakugan-recomp/` | The port: entry point, HLE stubs, D3D12 movie path, CMake build |
| `patches/` | Archived toolkit changes; review accepted patches and their prerequisites individually |
| `tools/` | Launch, regression-test and diagnostic tools |
| `docs/` | Status, architecture, guardrails, performance; `docs/legacy/` keeps the earlier handoffs |
| `ps3recomp-src/`, `game/`, `analysis/`, `bakugan-recomp/recompiled/` | Generated or upstream — **not** versioned |

## Diagnostics

Every fix in this project started as a measurement, and the knobs are still there: `RSX_CAP` (frame capture with a draw manifest), `RTT_STAT`, `VDEFDBG`, `VTXALLF`/`VTXGATE` (per-draw bounds), `VPSEL`, `FLIPPC`, `NODEPTH`, `MTDBG`. They are environment variables, off by default, documented where they are defined.

## Legal

Bakugan: Battle Brawlers is © its rights holders. This repository ships **no game code, no assets, no encryption keys, no lifted sources and no binaries**. You must own the disc. The port's own code is MIT licensed; the toolkit is MIT by Ned Heller / sp00nznet — see [CREDITS.md](CREDITS.md) and [LICENSE](LICENSE).
