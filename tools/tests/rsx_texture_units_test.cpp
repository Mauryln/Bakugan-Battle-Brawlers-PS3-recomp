#include <d3d12.h>
#include <d3dcompiler.h>
#include <cassert>
#include <cstdio>
#include "rsx_commands.h"

int main()
{
    ID3D12Device* device = nullptr;
    assert(SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))));
    // The actual backend descriptor table and sampler declarations.
    #include "texture_root_entry.inc"
    ID3DBlob *serialized = nullptr, *errors = nullptr;
    assert(SUCCEEDED(D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors)));
    ID3D12RootSignature* root = nullptr;
    assert(SUCCEEDED(device->CreateRootSignature(0, serialized->GetBufferPointer(),
        serialized->GetBufferSize(), IID_PPV_ARGS(&root))));
    const char* vs = "float4 main(uint i:SV_VertexID):SV_Position{return float4(i&1,i>>1,0,1);}";
    const char* ps = "Texture2D tex:register(t15);SamplerState samp:register(s15);"
                     "float4 main():SV_Target{return tex.Sample(samp,float2(0.5,0.5));}";
    ID3DBlob *vertex = nullptr, *pixel = nullptr;
    assert(SUCCEEDED(D3DCompile(vs, strlen(vs), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vertex, &errors)));
    assert(SUCCEEDED(D3DCompile(ps, strlen(ps), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &pixel, &errors)));
    D3D12_GRAPHICS_PIPELINE_STATE_DESC state = {};
    state.pRootSignature = root;
    state.VS = {vertex->GetBufferPointer(), vertex->GetBufferSize()};
    state.PS = {pixel->GetBufferPointer(), pixel->GetBufferSize()};
    state.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    state.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    state.RasterizerState.DepthClipEnable = TRUE;
    auto& blend = state.BlendState.RenderTarget[0];
    blend.SrcBlend = blend.SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.DestBlend = blend.DestBlendAlpha = D3D12_BLEND_ZERO;
    blend.BlendOp = blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    state.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    state.SampleMask = UINT_MAX;
    state.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    state.NumRenderTargets = 1;
    state.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    state.SampleDesc.Count = 1;
    ID3D12PipelineState* pipeline = nullptr;
    HRESULT result = device->CreateGraphicsPipelineState(&state, IID_PPV_ARGS(&pipeline));
    printf("texture unit 15 PSO: %08lX\n", result); fflush(stdout);
    assert(SUCCEEDED(result));
    pipeline->Release(); pixel->Release(); vertex->Release(); root->Release();
    serialized->Release(); if (errors) errors->Release(); device->Release();
    puts("PASS: actual RSX root signature supports the highest fragment texture and sampler unit");
}
