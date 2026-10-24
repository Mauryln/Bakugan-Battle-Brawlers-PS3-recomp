# Repository Guidelines

## Project Structure & Module Organization

This workspace builds a native Windows port of **Bakugan: Battle Brawlers**, PS3 European edition `BLES00758`, using static PPU/SPU recompilation.

- `bakugan-recomp/`: handwritten port entry point (`main.cpp`), HLE stubs (`stubs.cpp`), configuration, and CMake build definition.
- `ps3recomp-src/`: nested toolkit checkout; `runtime/` implements CPU support, `libs/` contains HLE modules, and `tools/` contains lifters and analysis utilities.
- `tools/`: project pipeline, build, launch, and diagnostic scripts.
- `patches/`: toolkit patches applied by the pipeline. Preserve reusable toolkit fixes here when needed for reproducibility.
- `docs/STATUS.md`: technical progress and known issues.
- `game/`, `analysis/`, `bakugan-recomp/recompiled/`, and `bakugan-recomp/spu/`: generated inputs and outputs. Change generators or handwritten sources rather than editing lifted code.

## Build, Test, and Development Commands

Run from the workspace root in PowerShell. Install Python dependencies as described in `README.md`; builds require VS 2022 C++ tools, clang-cl, CMake, and Ninja.

```powershell
.\.venv\Scripts\python.exe tools\pipeline.py
.\.venv\Scripts\python.exe tools\build.py
.\tools\run.ps1
```

These regenerate missing pipeline outputs, build `bakugan-recomp/build/BakuganRecomp.exe`, and launch with the extracted disc mounted. Use `pipeline.py --only lift` for a single stage, `--force` to regenerate outputs, or `build.py --debug` for debugging.

## Coding Style & Naming Conventions

Use four-space indentation, LF endings, and no trailing whitespace. Follow C17 for toolkit C and C++20 for port code. Use `snake_case` functions and variables, uppercase constants, and PS3 SDK names for HLE exports (for example, `cellAudioInit`). Python scripts use `snake_case`; add type hints where useful. Match surrounding code; no workspace-wide formatter or linter configuration was found.

## Testing Guidelines

Toolkit tests use CMake/CTest and standalone Python checks named `test_*.py`; native tests typically use `test_*.c` or `test_*.cpp`. From `ps3recomp-src/`, configure with `cmake -B build-tests -DPS3RECOMP_BUILD_TESTS=ON`, build with `cmake --build build-tests`, then run `ctest --test-dir build-tests --output-on-failure`. No coverage threshold is documented. For port changes, also launch and verify affected screens, input, or audio; record reproduction steps and relevant logs.

## Commit & Pull Request Guidelines

The workspace root has no Git history; the nested toolkit history available locally contains a merge commit, insufficient to establish a commit-message convention. Use concise imperative subjects. Keep PRs focused, describe behavior changes, link relevant issues, and report validation commands. Include before/after screenshots for rendering changes.

## Game Data & Secrets

Use your own disc dump. Never commit game binaries, assets, encryption keys, credentials, generated lifts, or build outputs. Respect `.gitignore` and inspect staged files before committing.
