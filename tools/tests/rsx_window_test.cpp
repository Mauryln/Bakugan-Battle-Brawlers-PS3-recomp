#include <d3d11.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include "rsx_fp_decompiler.h"
#include "rsx_commands.h"
#include "window.inc"
static void require(bool ok, const char* message) {
    if (!ok) { fprintf(stderr,"FAIL: %s\n",message); exit(1); }
}
static ID3DBlob* compile(const char* s, const char* profile) {
    ID3DBlob *b=nullptr,*e=nullptr;
    HRESULT hr=D3DCompile(s,strlen(s),nullptr,nullptr,nullptr,"main",profile,0,0,&b,&e);
    if (FAILED(hr)&&e) fprintf(stderr,"%s\n",(char*)e->GetBufferPointer());
    require(SUCCEEDED(hr),"compile actual translated shader");
    if(e)e->Release(); return b;
}
static void word(u8* p,u32 w) {
    w=(w<<16)|(w>>16); p[0]=w>>24;p[1]=w>>16;p[2]=w>>8;p[3]=w;
}
static int decode(rsx_state* state, u32 method, u32 data) {
#include "window_commands.inc"
    return -1;
}
int main() {
    u8 code[16]={};
    word(code,(1u<<24)|(15u<<9)|1u); // MOV r0, WPOS; END
    word(code+4,1u|(1u<<11)|(2u<<13)|(3u<<15)|(7u<<18)|(1u<<23)|(2u<<25)|(3u<<27));
    static char hlsl[RSX_FP_HLSL_BUFFER_SIZE];
    require(fp_decompile_for_draw(code,sizeof(code),0x40,0,hlsl,sizeof(hlsl),nullptr)>0,"translate WPOS");
    auto* psb=compile(hlsl,"ps_5_0");
    auto* vsb=compile("float4 main(uint id:SV_VertexID):SV_Position{float2 p=float2((id<<1)&2,id&2);return float4((p*float2(2,-2)+float2(-1,1))*2,1,2);}","vs_5_0");
    ID3D11Device* d=nullptr;ID3D11DeviceContext* c=nullptr;
    require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,nullptr,&c)),"WARP device");
    ID3D11VertexShader* vs=nullptr;ID3D11PixelShader* ps=nullptr;
    require(SUCCEEDED(d->CreateVertexShader(vsb->GetBufferPointer(),vsb->GetBufferSize(),nullptr,&vs)),"VS");
    require(SUCCEEDED(d->CreatePixelShader(psb->GetBufferPointer(),psb->GetBufferSize(),nullptr,&ps)),"PS");
    D3D11_TEXTURE2D_DESC td={};td.Width=td.Height=2;td.MipLevels=td.ArraySize=1;td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.SampleDesc.Count=1;td.BindFlags=D3D11_BIND_RENDER_TARGET;
    ID3D11Texture2D *rt=nullptr,*read=nullptr;ID3D11RenderTargetView* view=nullptr;
    require(SUCCEEDED(d->CreateTexture2D(&td,nullptr,&rt)),"target");require(SUCCEEDED(d->CreateRenderTargetView(rt,nullptr,&view)),"RTV");
    td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    require(SUCCEEDED(d->CreateTexture2D(&td,nullptr,&read)),"readback");
    D3D11_BUFFER_DESC bd={};bd.ByteWidth=1280;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;bd.Usage=D3D11_USAGE_DEFAULT;
    ID3D11Buffer* cb=nullptr;require(SUCCEEDED(d->CreateBuffer(&bd,nullptr,&cb)),"constants");
    c->VSSetShader(vs,nullptr,0);c->PSSetShader(ps,nullptr,0);c->PSSetConstantBuffers(1,1,&cb);c->OMSetRenderTargets(1,&view,nullptr);
    D3D11_VIEWPORT vp={0,0,2,2,0,1};c->RSSetViewports(1,&vp);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    for(unsigned origin=0;origin<2;origin++) for(unsigned integer=0;integer<2;integer++) {
        float k[320]={},transform[3];
        rsx_state state={};
        require(decode(&state,NV4097_SET_SHADER_WINDOW,584|(origin<<12)|(integer<<16))==0,"decode SET_SHADER_WINDOW");
        rsx_window_transform(state.shader_window,transform);
        k[2]=transform[0];k[3]=transform[1];k[6]=transform[2];
        c->UpdateSubresource(cb,0,nullptr,k,0,0);c->Draw(3,0);c->CopyResource(read,rt);
        D3D11_MAPPED_SUBRESOURCE m={};require(SUCCEEDED(c->Map(read,0,D3D11_MAP_READ,0,&m)),"map");
        for(unsigned y=0;y<2;y++)for(unsigned x=0;x<2;x++) {
            const float* p=(const float*)((const char*)m.pData+y*m.RowPitch)+x*4;
            float ey=(origin?584-(y+.5f):y+.5f)-integer*.5f;
            require(fabs(p[0]-(x+.5f-integer*.5f))<1e-5f && fabs(p[1]-ey)<1e-5f,"WPOS respects top/bottom origin, guest height, pixel center");
            require(fabs(p[2]-.5f)<1e-5f && fabs(p[3]-.5f)<1e-5f,"RSX WPOS exposes depth and reciprocal clip W");
        }
        c->Unmap(read,0);
    }
    // RSX shadow functions compare stored depth against the reference.
    word(code,(0x17u<<24)|(15u<<9)|(4u<<13)|1u); // TEX r0, TC0, t0
    rsx_fp_shadow_desc shadow[16]={};shadow[0].enabled=1;shadow[0].func=4;
    require(fp_decompile_for_draw(code,sizeof(code),0x40,0,hlsl,sizeof(hlsl),shadow)>0,"translate shadow comparison");
    auto* shadow_psb=compile(hlsl,"ps_5_0");
    auto* shadow_vsb=compile("struct O{float4 p:SV_Position;float4 c0:COLOR0;float4 c1:COLOR1;float4 fog:FOG;float4 tc:TEXCOORD0;};O main(uint id:SV_VertexID){float2 p=float2((id<<1)&2,id&2);O o;o.c0=o.c1=o.fog=0;o.p=float4(p*float2(2,-2)+float2(-1,1),0,1);o.tc=float4(p,.5,1);return o;}","vs_5_0");
    ID3D11VertexShader* shadow_vs=nullptr;ID3D11PixelShader* shadow_ps=nullptr;
    require(SUCCEEDED(d->CreateVertexShader(shadow_vsb->GetBufferPointer(),shadow_vsb->GetBufferSize(),nullptr,&shadow_vs)),"shadow VS");
    require(SUCCEEDED(d->CreatePixelShader(shadow_psb->GetBufferPointer(),shadow_psb->GetBufferSize(),nullptr,&shadow_ps)),"shadow PS");
    D3D11_TEXTURE2D_DESC dd={};dd.Width=dd.Height=2;dd.MipLevels=dd.ArraySize=1;dd.Format=DXGI_FORMAT_R32_FLOAT;dd.SampleDesc.Count=1;dd.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    float depths[]={.25f,.75f,.25f,.75f};D3D11_SUBRESOURCE_DATA data={depths,8,0};
    ID3D11Texture2D* depth=nullptr;ID3D11ShaderResourceView* srv=nullptr;
    require(SUCCEEDED(d->CreateTexture2D(&dd,&data,&depth)),"comparison depth texture");
    require(SUCCEEDED(d->CreateShaderResourceView(depth,nullptr,&srv)),"depth SRV");
    D3D11_SAMPLER_DESC sd={};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=1;
    ID3D11SamplerState* sampler=nullptr;require(SUCCEEDED(d->CreateSamplerState(&sd,&sampler)),"depth sampler");
    c->VSSetShader(shadow_vs,nullptr,0);c->PSSetShader(shadow_ps,nullptr,0);c->PSSetShaderResources(0,1,&srv);c->PSSetSamplers(0,1,&sampler);c->Draw(3,0);c->CopyResource(read,rt);
    D3D11_MAPPED_SUBRESOURCE m={};require(SUCCEEDED(c->Map(read,0,D3D11_MAP_READ,0,&m)),"read comparison");
    for(unsigned y=0;y<2;y++)for(unsigned x=0;x<2;x++) {
        const float* p=(const float*)((const char*)m.pData+y*m.RowPitch)+x*4;
        require(fabs(p[0]-float(x))<1e-5f,"GREATER shadow comparison is depth > reference, not reference > depth");
    }
    c->Unmap(read,0);c->ClearState();sampler->Release();srv->Release();depth->Release();shadow_ps->Release();shadow_vs->Release();shadow_psb->Release();shadow_vsb->Release();
    c->ClearState();cb->Release();read->Release();view->Release();rt->Release();ps->Release();vs->Release();psb->Release();vsb->Release();c->Release();d->Release();
    puts("PASS: actual WPOS shader respects origin/height/pixel center, depth and reciprocal W");
}
