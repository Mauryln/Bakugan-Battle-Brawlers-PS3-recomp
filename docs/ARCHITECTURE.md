# Architecture

One page, enough to know where to look.

## Layers

```
lifted guest code        bakugan-recomp/recompiled/*.cpp      (generated)
SPU workloads            bakugan-recomp/spu/*.c               (generated)
─────────────────────────────────────────────────────────────────────────
PPU runtime              ps3recomp-src/runtime/ppu/           threads, TLS,
                                                              OPDs, syscalls
HLE libraries            ps3recomp-src/libs/<domain>/         cellGcmSys,
                                                              cellAudio, cellPad,
                                                              cellSpurs, cellSail
Host backends            D3D12, Media Foundation, XInput, Win32 threads
─────────────────────────────────────────────────────────────────────────
port-specific glue       bakugan-recomp/main.cpp, stubs.cpp, movie_*.cpp
```

The guest executes lifted C++ and calls HLE functions where the original called
imports. There is no interpreter and no JIT: if a function was lifted, it runs
as compiled code.

## The RSX path (where the bugs are)

```
cellGcmSys.c      owns the command buffer ring, tiles, display buffers and the
                  NV3062/NV3089 2D engine; the guest puts its FIFO here.
      │
rsx_commands.c    walks the NV4097 method stream and keeps rsx_state
                  (blend, depth, vertex format, textures, viewport...).
      │
rsx_vertex_fetch.c  resolves attribute arrays and PROGRAMS (VP/FP) to host code.
      │
rsx_d3d12_backend.c  PSO cache keyed on the state that matters, per-draw
                  vertex buffers, present, and the readback used by diagnostics.
```

Anything visible-but-wrong is almost always one of three things:

1. **Guest state we ignore.** The depth-test bug that hid every 2D panel was
   this: the backend forced `DepthEnable=TRUE` instead of honouring
   `SET_DEPTH_TEST_ENABLE`. Fixed in `patches/0001` (and refined by `0003`).
2. **A program we decompile wrong.** Check with `VPSEL` / `VDEFDBG` whether the
   vertices and constants reach the shader as the guest intended.
3. **A draw that never arrives.** Count draws per surface (`g_draw_surf_eab`
   and friends) instead of trusting the picture: a black layer may simply never
   be drawn.

## Open problems

| Symptom | Known so far | Start here |
|---|---|---|
| Main menu backdrop black, 3D scene missing | The 1120×584 2× MSAA target `0xEAB0000` is cleared and resolved every frame but receives **zero** draws over 780 frames; the NV3089 resolve therefore copies zeros into `0xDF60000`. | `rsx_commands.c` (method walk), `cellGcmSys.c` (tiles/resolve), `rsx_vertex_fetch.c` |
| No sound effects | `.PAST` containers are ATRAC3; only the FMV audio path (Media Foundation) is implemented. | `libs/audio/` |
| Cutscene audio fine, game audio silent | Same audio gap. | `libs/audio/` |

## Diagnostics contract

Every knob is an environment variable, off by default, printed where it is
defined, and cheap enough to leave in the tree:

`RSX_CAP=<start>:<count>:<stride>` + `RSX_CAP_DIR` → frame BMPs plus a
`manifest.txt` listing every op (clear/draw/resolve) with texture offsets, blend
key and vertex counts. `VDEFDBG`, `VTXALLF`, `VTXGATE`, `VPSEL`, `RTT_STAT`,
`RTOFF`, `FLIPPC`, `MTDBG`, `BLENDDBG`, `NODEPTH`.

Add one rather than adding a breakpoint.
