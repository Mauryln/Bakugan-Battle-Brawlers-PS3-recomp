#include <d3d11.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include "rsx_fp_decompiler.h"
#include "alpha_draw.inc"

static void require(bool ok, const char* message)
{
    if (!ok) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}
static void put_word(u8* p, u32 value)
{
    value = (value << 16) | (value >> 16);
    p[0] = (u8)(value >> 24); p[1] = (u8)(value >> 16);
    p[2] = (u8)(value >> 8); p[3] = (u8)value;
}
static ID3DBlob* compile(const char* source, const char* profile)
{
    ID3DBlob *code = nullptr, *errors = nullptr;
    HRESULT hr = D3DCompile(source, strlen(source), nullptr, nullptr, nullptr,
        "main", profile, 0, 0, &code, &errors);
    if (FAILED(hr) && errors) fprintf(stderr, "%s\n", (char*)errors->GetBufferPointer());
    require(SUCCEEDED(hr), "generated shader compiles");
    if (errors) errors->Release();
    return code;
}
static void verify(ID3D11Device* device, ID3D11DeviceContext* context,
                   bool half, bool mrt, bool unorm, float alpha, bool passes)
{
    u8 program[32] = {};
    // MOV h0/r0, COL0, and optionally the second color export.
    for (u32 i = 0; i < (mrt ? 2u : 1u); ++i) {
        u32 reg = i ? (half ? 4u : 2u) : 0u;
        put_word(program + i * 16, (1u << 24) | (15u << 9) | (1u << 13) |
                 (half ? 1u << 7 : 0u) | (reg << 1) | (i == (mrt ? 1u : 0u) ? 1u : 0u));
        put_word(program + i * 16 + 4, 1u | (0xE4u << 9) | (7u << 18));
    }
    char hlsl[RSX_FP_HLSL_BUFFER_SIZE];
    require(rsx_fp_decompile(program, mrt ? 32 : 16, half ? 0 : 0x40,
                            hlsl, sizeof(hlsl)) > 0, "color export translates");
    require(fp_apply_alpha_test_for_draw(hlsl, sizeof(hlsl), 0x106FF,
        unorm ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_R32G32B32A32_FLOAT) >= 0,
        "alpha GEQUAL test is generated with target precision");
    auto* ps_code = compile(hlsl, "ps_5_0");
    ID3D11PixelShader* ps = nullptr;
    require(SUCCEEDED(device->CreatePixelShader(ps_code->GetBufferPointer(), ps_code->GetBufferSize(), nullptr, &ps)), "create pixel shader");
    char vs_source[1024];
    snprintf(vs_source, sizeof(vs_source),
        "struct Out {float4 pos:SV_Position; float4 col:COLOR0;};\n"
        "Out main(uint id:SV_VertexID) {Out o; float2 p=float2((id<<1)&2,id&2);"
        "o.pos=float4(p*float2(2,-2)+float2(-1,1),0,1);"
        "o.col=float4(1,0,0,%.9g); return o;}", (double)alpha);
    auto* vs_code = compile(vs_source, "vs_5_0");
    ID3D11VertexShader* vs = nullptr;
    require(SUCCEEDED(device->CreateVertexShader(vs_code->GetBufferPointer(), vs_code->GetBufferSize(), nullptr, &vs)), "create vertex shader");
    context->VSSetShader(vs, nullptr, 0); context->PSSetShader(ps, nullptr, 0);
    D3D11_TEXTURE2D_DESC desc = {}; desc.Width = desc.Height = 4;
    desc.MipLevels = desc.ArraySize = 1; desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1; desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    ID3D11Texture2D* target = nullptr;
    require(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &target)), "create color target");
    ID3D11RenderTargetView* rtv = nullptr;
    require(SUCCEEDED(device->CreateRenderTargetView(target, nullptr, &rtv)), "create target view");
    context->OMSetRenderTargets(1, &rtv, nullptr);
    float black[4] = {}; context->ClearRenderTargetView(rtv, black);
    D3D11_VIEWPORT vp = {0, 0, 4, 4, 0, 1}; context->RSSetViewports(1, &vp);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->Draw(3, 0);
    desc.BindFlags = 0; desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* readback = nullptr;
    require(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &readback)), "create readback");
    context->CopyResource(readback, target);
    D3D11_MAPPED_SUBRESOURCE map = {};
    require(SUCCEEDED(context->Map(readback, 0, D3D11_MAP_READ, 0, &map)), "read rendered pixels");
    for (u32 y = 0; y < 4; ++y) for (u32 x = 0; x < 4; ++x) {
        u8 red = ((u8*)map.pData)[y * map.RowPitch + x * 4];
        if (red != (passes ? 255 : 0))
            fprintf(stderr, "half=%d mrt=%d unorm=%d alpha=%.9g red=%u at %u,%u\n",
                    half, mrt, unorm, (double)alpha, red, x, y);
        require(red == (passes ? 255 : 0), "alpha test respects export precision for every pixel");
    }
    context->Unmap(readback, 0); context->ClearState();
    readback->Release(); rtv->Release(); target->Release(); vs->Release(); ps->Release();
    vs_code->Release(); ps_code->Release();
}
int main()
{
    ID3D11Device* device = nullptr; ID3D11DeviceContext* context = nullptr;
    require(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context)), "create WARP test device");
    for (bool mrt : {false, true}) for (bool half : {false, true}) {
        verify(device, context, half, mrt, true, 1.0f, true);
        verify(device, context, half, mrt, true, 0.99999988f, true);
        verify(device, context, half, mrt, true, 0.99f, false);
        verify(device, context, half, mrt, false, 0.99999988f, false);
        verify(device, context, half, mrt, false, 1.0f, true);
    }
    context->Release(); device->Release();
    puts("PASS: UNORM opaque edges survive alpha testing; cutouts and float targets retain their comparisons");
}
