# Contributing

Thanks for looking. This project is a static recompilation, so most contributions
are smaller than they look.

## Read this first

- **Never commit game data or lifted code.** `game/`, `iso/`, `analysis/`,
  `bakugan-recomp/recompiled/`, `bakugan-recomp/spu/` and `ps3recomp-src/` are
  generated from a copyrighted disc and stay out of git. `.gitignore` already
  covers them; check `git status` before you push.
- **Toolkit changes go in `patches/`, not in `ps3recomp-src/`.** The checkout is
  disposable: edit it to test, then regenerate the patch with
  `git -C ps3recomp-src diff > patches/0001-....patch` and reset the checkout.
  `tools/pipeline.py --only patch` reapplies the series; it is idempotent and
  recognises overlapping patches.
- **Measurements beat guesses.** Every fix here started with an env knob
  (`RSX_CAP`, `VDEFDBG`, `VTXALLF`, `RTT_STAT`, `MTDBG`, ...). If you think you
  found a bug, capture the evidence first — it usually turns out to be elsewhere.

## Building

See [README.md](README.md#build-it). You need your own `BLES00758` disc dump and
Visual Studio 2022 Build Tools with clang-cl.

## What is useful right now

| Area | Where |
|---|---|
| RSX 3D geometry (nothing reaches the MSAA target) | `libs/video/` |
| Sound effects (ATRAC3 in `SOUND/*.PAST`) | `libs/audio/` |
| Boot reports from other GPUs and drivers | open an issue with `run.log` |

Open an issue before starting anything large, so two people do not solve the same
problem twice.

## Style

- C17 for toolkit C, C++20 for port C++. Four-space indent, LF, no trailing
  whitespace.
- `snake_case` for functions and variables, `UPPER_CASE` for constants, PS3 SDK
  names for HLE exports (`cellAudioInit`).
- Comment the *why* the code cannot show. If a limit is known and accepted, mark
  it: `shortcut: <the limit>, <when to upgrade>`.

## Tests

`tools/test_*.py` are standalone checks. Most need the toolkit checkout, a
compiler and sometimes a local MP4; `test_pipeline_patches.py` runs anywhere and
is what CI exercises.

```powershell
.\.venv\Scripts\python.exe tools\test_pipeline_patches.py
```

For anything that changes rendering, attach a before/after frame and the draw
manifest (`RSX_CAP`) instead of a screenshot alone.
