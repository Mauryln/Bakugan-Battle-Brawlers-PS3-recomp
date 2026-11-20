#define A_GPU 1
#define A_HLSL 1
#include "ffx_a.h"
#define FSR_EASU_F 1
#define FSR_RCAS_F 1

Texture2D<float4> input_color : register(t0);
Texture2D<float4> aux_texture : register(t1);
SamplerState clamp_sampler : register(s0);
SamplerState point_sampler : register(s1);
cbuffer FsrConstants : register(b0) {
    uint4 easu0, easu1, easu2, easu3;
    uint4 rcas;
    uint4 viewport_sizes; // xy = output, zw = input
};

AF4 FsrEasuRF(AF2 p) { return input_color.GatherRed(clamp_sampler, p); }
AF4 FsrEasuGF(AF2 p) { return input_color.GatherGreen(clamp_sampler, p); }
AF4 FsrEasuBF(AF2 p) { return input_color.GatherBlue(clamp_sampler, p); }
AF4 FsrRcasLoadF(ASU2 p) {
    return input_color.Load(int3(clamp(p, int2(0, 0), int2(viewport_sizes.xy) - 1), 0));
}
void FsrRcasInputF(inout AF1 r, inout AF1 g, inout AF1 b) {}
#include "ffx_fsr1.h"

float smaa_luma(int2 p, int2 sz) {
    int2 c = clamp(p, int2(0, 0), sz - 1);
    return dot(input_color.Load(int3(c, 0)).rgb, float3(0.2126, 0.7152, 0.0722));
}

float2 smaa_edge_load(int2 p, int2 sz) {
    if (p.x < 0 || p.y < 0 || p.x >= sz.x || p.y >= sz.y)
        return float2(0.0, 0.0);
    return input_color.Load(int3(p, 0)).rg;
}

float smaa_horiz_weight(int2 edge_px, int2 sz, bool below_edge) {
    if (smaa_edge_load(edge_px, sz).g < 0.5)
        return 0.0;
    int d_l = 0, d_r = 0;
    [unroll] for (int i = 1; i <= 7; ++i) {
        if (smaa_edge_load(edge_px + int2(-i, 0), sz).g > 0.5) d_l = i; else break;
    }
    [unroll] for (int j = 1; j <= 7; ++j) {
        if (smaa_edge_load(edge_px + int2(j, 0), sz).g > 0.5) d_r = j; else break;
    }
    int lx = edge_px.x - d_l;
    int rx = edge_px.x + d_r + 1;
    bool up_l = smaa_edge_load(int2(lx, edge_px.y - 1), sz).r > 0.5;
    bool dn_l = smaa_edge_load(int2(lx, edge_px.y), sz).r > 0.5;
    bool up_r = smaa_edge_load(int2(rx, edge_px.y - 1), sz).r > 0.5;
    bool dn_r = smaa_edge_load(int2(rx, edge_px.y), sz).r > 0.5;
    float len = float(d_l + d_r + 1);
    if (d_l < d_r) {
        bool want = below_edge ? (dn_l && !up_l) : (up_l && !dn_l);
        return want ? 0.5 * max(0.0, 1.0 - (2.0 * float(d_l) + 1.0) / len) : 0.0;
    }
    if (d_r < d_l) {
        bool want = below_edge ? (dn_r && !up_r) : (up_r && !dn_r);
        return want ? 0.5 * max(0.0, 1.0 - (2.0 * float(d_r) + 1.0) / len) : 0.0;
    }
    if (len == 1.0 && ((up_l && dn_r && !dn_l && !up_r) || (dn_l && up_r && !up_l && !dn_r)))
        return 0.25;
    return 0.0;
}

float smaa_vert_weight(int2 edge_px, int2 sz, bool right_of_edge) {
    if (smaa_edge_load(edge_px, sz).r < 0.5)
        return 0.0;
    int d_u = 0, d_d = 0;
    [unroll] for (int i = 1; i <= 7; ++i) {
        if (smaa_edge_load(edge_px + int2(0, -i), sz).r > 0.5) d_u = i; else break;
    }
    [unroll] for (int j = 1; j <= 7; ++j) {
        if (smaa_edge_load(edge_px + int2(0, j), sz).r > 0.5) d_d = j; else break;
    }
    int ty = edge_px.y - d_u;
    int by = edge_px.y + d_d + 1;
    bool lf_t = smaa_edge_load(int2(edge_px.x - 1, ty), sz).g > 0.5;
    bool rt_t = smaa_edge_load(int2(edge_px.x, ty), sz).g > 0.5;
    bool lf_b = smaa_edge_load(int2(edge_px.x - 1, by), sz).g > 0.5;
    bool rt_b = smaa_edge_load(int2(edge_px.x, by), sz).g > 0.5;
    float len = float(d_u + d_d + 1);
    if (d_u < d_d) {
        bool want = right_of_edge ? (rt_t && !lf_t) : (lf_t && !rt_t);
        return want ? 0.5 * max(0.0, 1.0 - (2.0 * float(d_u) + 1.0) / len) : 0.0;
    }
    if (d_d < d_u) {
        bool want = right_of_edge ? (rt_b && !lf_b) : (lf_b && !rt_b);
        return want ? 0.5 * max(0.0, 1.0 - (2.0 * float(d_d) + 1.0) / len) : 0.0;
    }
    if (len == 1.0 && ((lf_t && rt_b && !rt_t && !lf_b) || (rt_t && lf_b && !lf_t && !rt_b)))
        return 0.25;
    return 0.0;
}

float4 vs_main(uint id : SV_VertexID) : SV_Position {
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p.x * 2 - 1, 1 - p.y * 2, 0, 1);
}
float4 smaa_edge_main(float4 p : SV_Position) : SV_Target {
    int2 ip = int2(p.xy);
    int2 sz = int2(viewport_sizes.zw);
    float L = smaa_luma(ip, sz);
    float L_left = smaa_luma(ip + int2(-1, 0), sz);
    float L_top = smaa_luma(ip + int2(0, -1), sz);
    float2 delta = float2(abs(L - L_left), abs(L - L_top));
    float2 edges = step(0.10, delta);
    if (dot(edges, float2(1.0, 1.0)) == 0.0)
        return float4(0.0, 0.0, 0.0, 1.0);
    float L_right = smaa_luma(ip + int2(1, 0), sz);
    float L_bottom = smaa_luma(ip + int2(0, 1), sz);
    float L_left2 = smaa_luma(ip + int2(-2, 0), sz);
    float L_top2 = smaa_luma(ip + int2(0, -2), sz);
    float max_delta = max(max(delta.x, delta.y), max(abs(L - L_right), abs(L - L_bottom)));
    max_delta = max(max_delta, max(abs(L_left - L_left2), abs(L_top - L_top2)));
    edges *= step(max_delta, 2.0 * delta);
    return float4(edges, 0.0, 1.0);
}
float4 smaa_weight_main(float4 p : SV_Position) : SV_Target {
    int2 ip = int2(p.xy);
    int2 sz = int2(viewport_sizes.zw);
    float w_top = smaa_horiz_weight(ip, sz, true);
    float w_bot = smaa_horiz_weight(ip + int2(0, 1), sz, false);
    float w_left = smaa_vert_weight(ip, sz, true);
    float w_right = smaa_vert_weight(ip + int2(1, 0), sz, false);
    return float4(w_top, w_bot, w_left, w_right);
}
float4 smaa_blend_main(float4 p : SV_Position) : SV_Target {
    int2 ip = int2(p.xy);
    float4 w = aux_texture.Load(int3(ip, 0));
    if (dot(w, float4(1.0, 1.0, 1.0, 1.0)) < 1e-5)
        return float4(input_color.Load(int3(ip, 0)).rgb, 1.0);
    float2 texel = 1.0 / float2(viewport_sizes.zw);
    float2 uv = p.xy * texel;
    float2 offset = float2(0.0, 0.0);
    if (max(w.r, w.g) > max(w.b, w.a))
        offset.y = (w.r > w.g ? -w.r : w.g) * texel.y;
    else
        offset.x = (w.b > w.a ? -w.b : w.a) * texel.x;
    return float4(input_color.SampleLevel(clamp_sampler, uv + offset, 0).rgb, 1.0);
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
