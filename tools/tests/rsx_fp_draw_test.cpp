#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "rsx_fp_decompiler.h"
#include "fp_draw.inc"

static void require(bool condition, const char* message)
{
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}

static void put_word(u8* p, u32 word)
{
    u32 be = (word << 16) | (word >> 16);
    p[0] = (u8)(be >> 24); p[1] = (u8)(be >> 16);
    p[2] = (u8)(be >> 8); p[3] = (u8)be;
}

static void put_float(u8* p, float value)
{
    u32 bits; memcpy(&bits, &value, 4); put_word(p, bits);
}

int main()
{
    constexpr u32 swizzle = (1u << 11) | (2u << 13) | (3u << 15);
    constexpr u32 execute = (7u << 18) | (1u << 23) | (2u << 25) | (3u << 27);
    u8 prog[32] = {};
    put_word(prog, (1u << 24) | (15u << 9) | 1u); // MOV r0, CONST, END
    put_word(prog + 4, 2u | swizzle | execute);
    for (unsigned i = 0; i < 4; ++i) put_float(prog + 16 + i * 4, 0.25f * i);
    static char first[RSX_FP_HLSL_BUFFER_SIZE], second[RSX_FP_HLSL_BUFFER_SIZE];
    require(fp_decompile_for_draw(prog, sizeof(prog), 0x40, 0, first, sizeof(first)) > 0,
            "first constant program translates");
    put_float(prog + 16, 0.875f);
    require(fp_decompile_for_draw(prog, sizeof(prog), 0x40, 0, second, sizeof(second)) > 0,
            "updated constant program translates");
    require(!strcmp(first, second), "changing inline constants must preserve compiled HLSL");
    require(strstr(first, "fp_constants[0]"), "shader reads per-draw constant bank");

    require(fp_override_rgb(first, sizeof(first)) == 1, "unlit diagnostic wraps the real return without dropping alpha");
    require(strstr(first, "_forced.rgb = float3(1,0,1)"), "unlit diagnostic preserves the fourth lane");
    ID3DBlob *shader = nullptr, *errors = nullptr;
    HRESULT hr = D3DCompile(first, strlen(first), nullptr, nullptr, nullptr,
        "main", "ps_5_0", 0, 0, &shader, &errors);
    if (FAILED(hr) && errors) fprintf(stderr, "%s\n", (const char*)errors->GetBufferPointer());
    require(SUCCEEDED(hr), "buffered shader compiles");
    ID3D11ShaderReflection* reflection = nullptr;
    require(SUCCEEDED(D3DReflect(shader->GetBufferPointer(), shader->GetBufferSize(),
        __uuidof(ID3D11ShaderReflection), (void**)&reflection)), "reflect compiled constant layout");
    auto* constants = reflection->GetConstantBufferByName("PSConstants");
    D3D11_SHADER_VARIABLE_DESC variable = {};
    require(SUCCEEDED(constants->GetVariableByName("fp_constants")->GetDesc(&variable)),
            "compiled shader exposes inline constant bank");
    require(variable.StartOffset == 20 * sizeof(float), "fp_constants begins at legacy b1 byte80");
    require(variable.Size == 64 * 4 * sizeof(float), "fp_constants bank contains 64 float4 slots");
    reflection->Release(); shader->Release(); if (errors) errors->Release();

    u8 tex[16] = {};
    put_word(tex, (0x17u << 24) | (15u << 9) | (4u << 13) | (3u << 17) | 1u);
    put_word(tex + 4, 1u | swizzle | execute);
    require(rsx_fp_texture_units(tex, sizeof(tex)) == (1u << 3), "only the referenced texture unit is active");
    require(rsx_fp_texture_units(prog, sizeof(prog)) == 0, "constant-only shaders consume no texture slots");
    require(fp_decompile_for_draw(tex, sizeof(tex), 0x40, 1u << 3, first, sizeof(first)) > 0,
            "cube program translates");
    require(strstr(first, "TextureCube rsx_tex3"), "unit3 is declared as a cube");
    require(strstr(first, "rsx_tex3.Sample(rsx_samp[3], ((input.tc0).xyzw).xyz)"),
            "cube sampling uses three direction components");
    char mrt[2048] =
        "struct PSOutput { float4 t0:SV_TARGET0; float4 t1:SV_TARGET1; };\n"
        "PSOutput main() { PSOutput result; result.t0=float4(0.2,0.4,0.6,0.3); result.t1=(float4)0.7;\n"
        "    return result;\n}\n";
    char original[2048]; strcpy(original, mrt);
    require(fp_override_rgb(mrt, (u32)strlen(mrt) + 1) == 0 && !strcmp(mrt, original),
            "diagnostic rejects insufficient capacity without mutation");
    require(fp_override_rgb(mrt, sizeof(mrt)) && strstr(mrt, "_forced.t0.rgb"),
            "MRT diagnostic changes only the first target RGB");
    shader = nullptr; errors = nullptr;
    hr = D3DCompile(mrt, strlen(mrt), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &shader, &errors);
    if (FAILED(hr) && errors) fprintf(stderr, "%s\n", (const char*)errors->GetBufferPointer());
    require(SUCCEEDED(hr), "MRT RGB override compiles");
    shader->Release(); if (errors) errors->Release();
    u8 oversized[65 * 32] = {};
    for (unsigned i = 0; i < 65; ++i) {
        auto* instruction = oversized + i * 32;
        put_word(instruction, (1u << 24) | (15u << 9) | (i == 64 ? 1u : 0u));
        put_word(instruction + 4, 2u | swizzle | execute);
        for (unsigned c = 0; c < 4; ++c) put_float(instruction + 16 + c * 4, 1.0f);
    }
    require(fp_decompile_for_draw(oversized, sizeof(oversized), 0x40, 0, first, sizeof(first)) < 0,
            "reject shaders exceeding the bound 64-slot constant bank");
    puts("PASS: dynamic fragment constants retain source identity, b1 layout, and cube sampling");
}
