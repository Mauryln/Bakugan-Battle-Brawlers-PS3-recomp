# AMD FidelityFX Super Resolution 1

Official FSR **v1.0.2** shader headers, distributed under the included MIT license.

Source: https://github.com/GPUOpen-Effects/FidelityFX-FSR/tree/v1.0.2/ffx-fsr

The port uses the FP32 EASU and RCAS implementations without modifying these
headers. `output_upscale.cpp` computes their constants; `output_upscale.hlsl`
runs the two output passes. CMake copies shaders, headers and license beside
the executable under `shaders/fsr1/`.

This is spatial upscaling. It does not perform temporal reconstruction or frame
generation. The input remains the title's complete 1280×720 frame, including
its HUD; the output is 1920×1080. Native mode bypasses both passes.
