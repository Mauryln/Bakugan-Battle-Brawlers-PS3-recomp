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
imports. Lifted functions run as compiled code. Dynamic SPU jobs without a
matching lifted image can use the runtime interpreter fallback; this port
also enables it for generated mixer kernels.

## The RSX path (where the bugs are)

```
cellGcmSys.c      owns the command buffer ring, tiles, display buffers and the
                  NV3062/NV3089 2D engine; the guest puts its FIFO here. The
                  FIFO walk itself runs on the port's frame_clock thread.
      │
rsx_commands.c    walks the NV4097 method stream and keeps rsx_state
                  (blend, depth, vertex format, textures, viewport...).
      │
rsx_vertex_fetch.c  decodes attribute arrays (formats, endianness, frequency).
                  rsx_fetch_attrib resolves each attribute per vertex.
      │
rsx_vp_decompiler.c  guest VP ucode -> HLSL, plus rsx_vp_analyze_inputs, which
                  yields the attribute mask the compact stream is built from.
      │
rsx_d3d12_backend.c  PSO cache keyed on the state that matters, per-draw
                  vertex buffers, present, and the readback used by diagnostics.
```

`rsx_vertex_compact.c` is the faster fetch path (`rsx_vertex_fetch_plan_init` /
`rsx_vertex_fetch_one`, base resolution hoisted per draw) with a compact layout
and index dedup. Metal and Vulkan use it; the D3D12 backend still calls
`rsx_fetch_attrib` directly. `rsx_draw_engine.c` is the register-file draw
engine (dedup + compact layout behind a backend interface) that only Metal and
Vulkan register — D3D12 never calls `rsx_draw_engine_set_backend`.

Where the frame time goes, and the evidence for it: `docs/STATUS.md` and the
newest section of `docs/PERFORMANCE_2026-11-01.md`.

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
| Main menu backdrop black, 3D scene missing | The 1120×584 2× MSAA target `0xEAB0000` was cleared and resolved every frame but received **zero** draws. Not re-observed after the 0036–0043 series (real samplers per draw, D24 depth surfaces, FIFO recycle owner), and it is no longer in the status list — treat it as unverified rather than open, and close it with a compared capture before citing it again. | `rsx_commands.c` (method walk), `cellGcmSys.c` (tiles/resolve), `rsx_vertex_fetch.c` |
| Sound effects missing | `.PAST` containers are ATRAC3 and unimplemented. Music PCM / the SPU mixer and the FMV audio path (Media Foundation) do work, so this is narrower than "no audio". | `libs/audio/` |
| Battle audio noise at low FPS | Correlates with frame rate and disappears in light views. The mixer holds 187.5 blocks/s with zero output drops; the mixed PCM has gaps. Do not fix by changing the mixer clock. | `libs/audio/`, `PERFORMANCE_2026-11-01.md` |

## Diagnostics contract

Every knob is an environment variable, off by default, printed where it is
defined, and cheap enough to leave in the tree:

`RSX_CAP=<start>:<count>:<stride>` + `RSX_CAP_DIR` → frame BMPs plus a
`manifest.txt` listing every op (clear/draw/resolve) with texture offsets, blend
key and vertex counts. `VDEFDBG`, `VTXALLF`, `VTXGATE`, `VPSEL`, `RTT_STAT`,
`RTOFF`, `FLIPPC`, `MTDBG`, `BLENDDBG`, `NODEPTH`.

Measurement knobs, for the performance work: `PERF=1` prints the per-block frame
table plus `[PERF submit]`, `[PERF detail]`, `[PERF index]`, `[PERF vertex]` and
`[PERF primitive]` (per RSX primitive, arrays vs indexed). `GCM_WORD_STATS=1`
counts FIFO words per frame. `PS3_FTRACE=<start_s>:<seconds>:<file>` writes a
span timeline (`include/ps3emu/ps3_ftrace.h`); `PS3_FT_DRAIN` is the FIFO walk,
so `drain − submit` is the method walker. `VP_COMPACT=0`, `VP_INDEX_CACHE=0` and
`RSX_NATIVE_STRIPS=0` revert the compact/index/native-strip paths for A/B.

Add one rather than adding a breakpoint.
