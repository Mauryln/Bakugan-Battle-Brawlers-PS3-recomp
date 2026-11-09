# Contributing

Thanks for looking. This project is a static recompilation, so most contributions
are smaller than they look.

## Read this first

- **Never commit game data or lifted code.** `game/`, `iso/`, `analysis/`,
  `bakugan-recomp/recompiled/`, `bakugan-recomp/spu/` and `ps3recomp-src/` are
  generated from a copyrighted disc and stay out of git. `.gitignore` already
  covers them; check `git status` before you push.
- **Preserve the local toolkit checkout.** It has independent history and local
  changes. Do not reset, rebase, switch branches or reapply the patch series.
  Preserve focused fixes as patches after validation; see [AGENTS.md](AGENTS.md).
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

Checks that need no disc or toolkit checkout:

```powershell
python -m compileall -q tools
python tools/test_repository_contract.py
```

Local runtime regression checks require the prepared Release tree and clang-cl:
`tools/test_spurs_taskset_workload.py`, `tools/test_spu_user_event_ack.py` and
`tools/test_player_boot.py`. See the [technical guide](docs/PROJECT_GUIDE.md).

Validate rendering with actual game-window screenshots, audio and gameplay.
RSX captures and draw manifests help diagnose backend behavior; they do not
replace visual verification of HUD, cards and both card cinematics.
