# Repository Guidelines

## What this is

Native Windows x64 port of **Bakugan: Battle Brawlers** (PS3 `BLES00758`): the
game's PPU/SPU code is statically recompiled to C++ and runs against an HLE
runtime that implements cellGcm/cellAudio/cellSpurs/cellSail with D3D12, WASAPI
and host threads. The game is playable end to end; battle performance, save
writing and real controller mapping are incomplete.

Read `docs/PROJECT_GUIDE.md` first; the docs are in Spanish by design.
`docs/HUD_START_BUG.md` and `docs/DEFECTOS_Y_INSTALADOR.md` track the current
SPURS fixes and their validation. Use the current replay referenced there;
`docs/INPUT_REPLAY.md` was deleted and must not be treated as an available guide.

## Layout and ownership

- `bakugan-recomp/`: the port itself. `main.cpp` is the game-agnostic runner,
  `stubs.cpp` holds title hooks, `movie_*`/`video_decoder`/`sail_video` are the
  video path, and `CMakeLists.txt` is the build.
- `bakugan-recomp/recompiled/` and `bakugan-recomp/spu/`: generated lifts.
  Regenerate them; never edit them.
- `ps3recomp-src/`: disposable upstream toolkit checkout, ignored by git. It is
  on branch `test` with local commits and an untracked `runtime/io/` +
  `include/ps3emu/asset_io.h` experiment. **Do not reset, rebase, change branch
  or reapply the patch series.**
- `patches/`: toolkit changes kept for reproducibility. `0049-asset-io-bounded-cache.patch`
  is an unapproved experiment; do not apply it to the healthy reference.
- `tools/`: `play.ps1`, `run.ps1`, `pad_autopilot.ps1`, `autopad.ps1` and
  `tests/`. `autopad.ps1` is the route launcher (`list`/`play`/`record`/`verify`)
  that drives the game to `pyrus` or `cards` with a recorded pad.
  `tools/build.py` and `tools/pipeline.py` were
  deleted; stale references to them in `README.md`, `CONTRIBUTING.md`,
  `bakugan-recomp/README.md` and `tools/run.ps1` error messages are obsolete.
- `game/`, `iso/`, `analysis/`, `diagnostics/`, `backups/`, `shadercache/`:
  local inputs/outputs, never committed.

## Build and run

The Python venv already contains CMake and Ninja; a Visual Studio install with
the **clang-cl** component (`Microsoft.VisualStudio.Component.VC.Llvm.Clang`) is
required. `cl` will not work (the code needs `__int128`, `__atomic_*` and
`__builtin_bswap*`).

```powershell
# Incremental build (usual case)
.\.venv\Scripts\ninja.exe -C bakugan-recomp\build

# Launch; needs game/PS3_GAME/USRDIR/EBOOT.elf
.\tools\run.ps1
```

If the build needs configuration, use CMake in an x64 Visual Studio developer
environment with the Windows SDK and clang-cl, Ninja generator and Release.
See the current commands in README.md. `tools/configure.ps1` was deleted.
Compare performance only between Release `/O2` builds.
`BakuganBrawlers.cmd` is the player entry point; `run.ps1` supports diagnostics.

Close the game with Alt+F4 or `CloseMainWindow`. Never use
`Stop-Process -Force`/`taskkill /F` without explicit user approval: audio is not
released and the process can block the next link.

## Tests and verification

- There is no aggregate runner for `tools/tests/`. Each `*_test.c[pp]` is a
  standalone `main()` that includes toolkit headers/`.inc` files and must be
  compiled manually against `ps3recomp-src/`.
- Focused local checks: `tools/test_spurs_taskset_workload.py`,
  `tools/test_spu_user_event_ack.py`, `tools/test_player_boot.py`. The native
  fixtures require clang-cl (`--compiler <path>`); workload tests also require
  the current built runtime library.
- Toolkit unit tests only: from `ps3recomp-src/`,
  `cmake -B build-tests -DPS3RECOMP_BUILD_TESTS=ON`, then
  `cmake --build build-tests`, then
  `ctest --test-dir build-tests --output-on-failure`.
- CI runs `python -m compileall -q tools` and
  `python tools/test_repository_contract.py`. It does not boot the proprietary
  game or replace the local native/visual validation.

For rendering changes, validate in this order: save-data warning audio, full
menu, first cinematic with image **and** audio, then battle HUD, cards, shadows
and effects. Only then measure FPS with `PERF=1` at a fixed camera. A higher FPS
count with missing geometry is not a valid improvement. For vertex-path changes,
`VPCHK=1` plus an `RSX_CAP` before/after capture is the real test.

## Port-specific gotchas

- **Always set `PS3_VERBOSE=0` when redirecting logs.** Without it, redirecting
  stderr enables ~6,000 verbose lines/s; guest threads starve and the title
  loses initialization/assets (HUD and cinematics disappear).
- In battle, `[PERF] ... draws` is ~1750. If it collapses, draws stopped being
  recorded — look for a new `return` in an `upload_*` path or a split
  `if/else if/.../else` chain, not a shader bug.
- Vertex staging has four coupled fields that must change together:
  `s_vertex_fetch_plan.mask`/`slot_off[]`, `dr->vb_stride`,
  `dr->gpu_input_mask` -> `dr->gpu_stride`, and `s_d3d.vp_vb_offset`.
- Do not enable `PS3_NOVSYNC=1`, global `SPU_CH_BLOCK=1`, `SRV_SHADOW=1`,
  `DRAW_KEEP_TEX`, `DRAW_LAST_TEX`, `DRAW_KEEP_BLEND`, `TEX_MARKEMPTY`,
  `EMPTYTEX`, `DEPTH_OFF` or `TEX_RESOLVE_AUTO`; each breaks image, audio or
  loading in documented ways.
- Keep `PS3_INTERNAL=720` (the default). The title's viewports, display buffer
  and final composite are 1280x720; a larger host raster leaves the image in a
  1280x720 corner, and faking `cellVideoOutGetResolution` makes it smaller.
  Raise the output with `PS3_OUTPUT` + FSR1 instead. `RSX_PACK_WORKERS=4` is
  slower here (p99 146 ms); leave it unset.
- Texture quality defaults: aniso 16x (`PS3_ANISO=1` to compare), `PS3_LOD_BIAS`
  unset (negative values sharpen but shimmer), RCAS 0.6 at 1080p. Mips are
  already uploaded as `level_count` subresources; do not replace them with a
  single level.
- `main.cpp` sets `SPU_INTERP_UNLIFTED=1`, `RSX_ACCUM_FRAME=1`,
  `GCM_FULL_FRAMES=1` and `timeBeginPeriod(1)` because this title needs them.
- A D3D12 device removal reports `0x887A0005` (or `0x887A0001` for an illegal
  call) and is logged as `[GPUV]`; everything drawn afterwards is invalid.
  GPU validation requires Windows "Graphics Tools" plus `D3D12_DBG=1` and
  `D3D12_GPU_VALIDATION=1`.
- Current bottleneck is serial `frame_clock` work, not the GPU (~7.5% used).
  Do not claim FPS gains from cache hit counters or incomplete draws. The 29–30
  FPS seen during the I/O experiment came with missing geometry and was rejected.

## Style and commits

Four-space indent, LF endings, no trailing whitespace. C17 for toolkit C, C++20
for port C++. `snake_case` functions/variables, `UPPER_CASE` constants, PS3 SDK
names for HLE exports (`cellAudioInit`). Comment the *why*; mark accepted limits
as `shortcut: <limit>, <when to upgrade>`.

Use concise imperative commit subjects. Official repository:
`https://github.com/Mauryln/Bakugan-Battle-Brawlers-PS3-recomp`. Keep changes
focused, report validation commands, and attach before/after frames plus the
draw manifest for rendering changes.

## Game data and secrets

Use your own disc dump. Never commit game binaries, assets, encryption keys,
credentials, generated lifts, logs, captures or build outputs. `.gitignore`
covers the generated trees; inspect `git status` before committing.
