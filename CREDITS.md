# Credits

## Toolkit

[ps3recomp](https://github.com/sp00nznet/ps3recomp) by Ned Heller / sp00nznet
(MIT). Pinned at commit `a6d76c8` (v0.17.0 merge). It is not vendored: our
changes live in `patches/` and are applied by `tools/pipeline.py --only patch`.

## This port

PPU/SPU lifting, HLE table, D3D12 backend wiring, Media Foundation video path,
SPURS LF-queue fix, keyboard input and everything else in `bakugan-recomp/`,
`tools/` and `patches/`.

## Third-party

- `tools/fetch_rpcs3.py` downloads the portable [RPCS3](https://rpcs3.net)
  build, used exactly once to decrypt the retail `EBOOT.BIN`. The game is never
  run in RPCS3 for this project.
- Media Foundation (Windows) decodes the H.264/AAC videos.

## Game

Bakugan: Battle Brawlers (PlayStation 3, 2009, `BLES00758`) is the property of
its rights holders. No game code, assets, keys or lifted sources are distributed
here; everything is regenerated from the user's own disc dump.
