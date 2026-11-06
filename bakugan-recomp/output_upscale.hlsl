#define A_GPU 1
#define A_HLSL 1
#include "ffx_a.h"
#define FSR_EASU_F 1
#define FSR_RCAS_F 1

Texture2D<float4> input_color : register(t0);
SamplerState clamp_sampler : register(s0);
cbuffer FsrConstants : register(b0) {
    uint4 easu0, easu1, easu2, easu3;
    uint4 rcas;
    uint4 output_size;
};

AF4 FsrEasuRF(AF2 p) { return input_color.GatherRed(clamp_sampler, p); }
AF4 FsrEasuGF(AF2 p) { return input_color.GatherGreen(clamp_sampler, p); }
AF4 FsrEasuBF(AF2 p) { return input_color.GatherBlue(clamp_sampler, p); }
AF4 FsrRcasLoadF(ASU2 p) {
    return input_color.Load(int3(clamp(p, int2(0, 0), int2(output_size.xy) - 1), 0));
}
void FsrRcasInputF(inout AF1 r, inout AF1 g, inout AF1 b) {}
#include "ffx_fsr1.h"

float4 vs_main(uint id : SV_VertexID) : SV_Position {
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p.x * 2 - 1, 1 - p.y * 2, 0, 1);
}
float4 easu_main(float4 p : SV_Position) : SV_Target {
    float3 color;
    FsrEasuF(color, uint2(p.xy), easu0, easu1, easu2, easu3);
    return float4(color, 1);
}
float4 rcas_main(float4 p : SV_Position) : SV_Target {
    float3 color;
    FsrRcasF(color.r, color.g, color.b, uint2(p.xy), rcas);
    return float4(saturate(color), 1);
}
