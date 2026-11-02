#include <d3d11.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "rsx_vp_decompiler.h"
using Microsoft::WRL::ComPtr;
#include "compact_stream.inc"
static void require(bool ok,const char* message){if(!ok){fprintf(stderr,"FAIL: %s\n",message);exit(1);}}
static ComPtr<ID3DBlob> compile(const char* s,const char* profile){ComPtr<ID3DBlob>b,e;HRESULT hr=D3DCompile(s,strlen(s),nullptr,nullptr,nullptr,"main",profile,0,0,&b,&e);if(FAILED(hr)&&e)fprintf(stderr,"%s\n",(char*)e->GetBufferPointer());require(SUCCEEDED(hr),"compile actual decompiled VP");return b;}
static void mov(u32* code,u32 input,u32 output,bool end){u32 src=2|(1<<12)|(2<<10)|(3<<8);code[0]=(1u<<30)|(63u<<15);code[1]=(1u<<22)|(input<<8)|(src>>9);code[2]=(src&511u)<<23;code[3]=(15u<<13)|(output<<2)|unsigned(end);}
int main(){
    float vertices[3][16][4];
    for(u32 v=0;v<3;v++)for(u32 a=0;a<16;a++)for(u32 c=0;c<4;c++)vertices[v][a][c]=float(v*100+a*4+c)*.001f;
    for(u32 mask=0;mask<65536;mask++){
        u32 stride=vp_vertex_stride(mask);std::vector<u8> packed(stride*3+16,0xA5);vp_pack_vertex_stream(packed.data(),vertices,3,mask);
        for(u32 v=0;v<3;v++){u32 offset=0;for(u32 a=0;a<16;a++)if(mask&(1u<<a)){require(!memcmp(packed.data()+v*stride+offset,vertices[v][a],16),"all masks preserve selected attributes bit for bit");offset+=16;}}
        for(u32 n=stride*3;n<packed.size();n++)require(packed[n]==0xA5,"packing stays in its reserved prefix");
    }
    ComPtr<ID3D11Device>d;ComPtr<ID3D11DeviceContext>c;require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,nullptr,&c)),"WARP device");
    auto psb=compile("float4 main(float4 p:SV_Position,float4 col:COLOR0):SV_Target{return col;}","ps_5_0");ComPtr<ID3D11PixelShader>ps;d->CreatePixelShader(psb->GetBufferPointer(),psb->GetBufferSize(),nullptr,&ps);c->PSSetShader(ps.Get(),nullptr,0);
    D3D11_TEXTURE2D_DESC td={};td.Width=td.Height=16;td.ArraySize=td.MipLevels=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.SampleDesc.Count=1;td.BindFlags=D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D>rt,read;ComPtr<ID3D11RenderTargetView>rtv;require(SUCCEEDED(d->CreateTexture2D(&td,nullptr,&rt)),"target");d->CreateRenderTargetView(rt.Get(),nullptr,&rtv);td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;d->CreateTexture2D(&td,nullptr,&read);
    auto* view=rtv.Get();c->OMSetRenderTargets(1,&view,nullptr);D3D11_VIEWPORT viewport={0,0,16,16,0,1};c->RSSetViewports(1,&viewport);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    D3D11_RASTERIZER_DESC rs={};rs.FillMode=D3D11_FILL_SOLID;rs.CullMode=D3D11_CULL_NONE;rs.DepthClipEnable=TRUE;ComPtr<ID3D11RasterizerState>raster;d->CreateRasterizerState(&rs,&raster);c->RSSetState(raster.Get());
    float constants[514*4]={};constants[512*4]=constants[512*4+1]=constants[512*4+2]=1;
    D3D11_BUFFER_DESC bd={};bd.ByteWidth=sizeof(constants);bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;D3D11_SUBRESOURCE_DATA data={constants,0,0};ComPtr<ID3D11Buffer>cb;require(SUCCEEDED(d->CreateBuffer(&bd,&data,&cb)),"VP constants");auto* cbp=cb.Get();c->VSSetConstantBuffers(0,1,&cbp);
    for(u32 color:{3u,8u,15u}){
        u32 code[8];mov(code,0,0,false);mov(code+4,color,1,true);rsx_vp_input_analysis analysis={};require(rsx_vp_analyze_inputs((u8*)code,sizeof(code),&analysis)==2&&analysis.exact&&analysis.input_mask==((1u<<color)|1u),"exact sparse input analysis");
        const float pos[3][4]={{-1,-1,.5f,1},{-1,1,.5f,1},{1,-1,.5f,1}};for(u32 v=0;v<3;v++){memcpy(vertices[v][0],pos[v],16);float col[4]={.2f,.4f,.6f,1};memcpy(vertices[v][color],col,16);}
        std::vector<u8> reference;
        for(int compact=0;compact<2;compact++){
            u32 mask=compact?analysis.input_mask:65535;u32 stride=vp_vertex_stride(mask);UINT offset=compact?272u:16u;std::vector<u8> packed(offset+stride*3,0xA5);vp_pack_vertex_stream(packed.data()+offset,vertices,3,mask);
            static char hlsl[262144];int result=compact?rsx_vp_decompile_compact_ex((u8*)code,sizeof(code),0,mask,hlsl,sizeof(hlsl)):rsx_vp_decompile((u8*)code,sizeof(code),hlsl,sizeof(hlsl));require(result==2,"decompile both input paths");auto vsb=compile(hlsl,"vs_5_0");ComPtr<ID3D11VertexShader>vs;d->CreateVertexShader(vsb->GetBufferPointer(),vsb->GetBufferSize(),nullptr,&vs);c->VSSetShader(vs.Get(),nullptr,0);
            D3D12_INPUT_ELEMENT_DESC il12[16];u32 elements=vp_compact_input_layout(mask,il12);D3D11_INPUT_ELEMENT_DESC il[16]={};for(u32 i=0;i<elements;i++){il[i].SemanticName=il12[i].SemanticName;il[i].SemanticIndex=il12[i].SemanticIndex;il[i].Format=il12[i].Format;il[i].AlignedByteOffset=il12[i].AlignedByteOffset;}
            ComPtr<ID3D11InputLayout>layout;require(SUCCEEDED(d->CreateInputLayout(il,elements,vsb->GetBufferPointer(),vsb->GetBufferSize(),&layout)),"original ATTR semantics match compact layout");c->IASetInputLayout(layout.Get());
            bd.ByteWidth=(UINT)packed.size();bd.BindFlags=D3D11_BIND_VERTEX_BUFFER;data.pSysMem=packed.data();ComPtr<ID3D11Buffer>vb;d->CreateBuffer(&bd,&data,&vb);auto* vbp=vb.Get();c->IASetVertexBuffers(0,1,&vbp,&stride,&offset);float clear[4]={};c->ClearRenderTargetView(rtv.Get(),clear);c->Draw(3,0);c->CopyResource(read.Get(),rt.Get());D3D11_MAPPED_SUBRESOURCE map={};require(SUCCEEDED(c->Map(read.Get(),0,D3D11_MAP_READ,0,&map)),"read rasterized vertices");std::vector<u8> pixels(16*16*4);for(u32 y=0;y<16;y++)memcpy(pixels.data()+y*64,(u8*)map.pData+y*map.RowPitch,64);c->Unmap(read.Get(),0);
            if(!compact){reference=pixels;bool visible=false;for(u8 x:pixels)visible|=x!=0;require(visible,"reference triangle rasterizes");}else require(pixels==reference,"compact VS and upload rasterize identically");
        }
    }
    c->ClearState();puts("PASS: all 65536 attribute masks, bounded writes and sparse ATTR3/8/15 GPU equivalence");
}
