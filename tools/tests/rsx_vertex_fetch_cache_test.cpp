#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using u8=uint8_t;using u32=uint32_t;
struct rsx_state { struct { int enabled;u32 stride; } vertex_attribs[16];float vertex_data4f[16][4]; };
struct VPSlot {float v[4];};
struct VPVertexFetchPlan {const rsx_state* state;float defaults[16][4];u8 active[16];u32 count;};
static VPVertexFetchPlan s_vertex_fetch_plan;
static int calls;
void rsx_fetch_attrib(const rsx_state* st,int i,u32 vi,float* out){++calls;memcpy(out,st->vertex_data4f[i],16);if(st->vertex_attribs[i].enabled&&st->vertex_attribs[i].stride){for(int j=0;j<4;j++)out[j]=(float)(vi*100+i*4+j);}}
#include "vertex_fetch_cache.inc"
void check(bool c){if(!c){puts("FAIL: planned vertex fetch differs from canonical fetch");exit(1);}}
int main(){
 rsx_state st={};for(int i=0;i<16;i++)for(int j=0;j<4;j++)st.vertex_data4f[i][j]=(float)(i*4+j);
 st.vertex_attribs[0]={1,16};st.vertex_attribs[3]={1,4};st.vertex_attribs[8]={1,8};st.vertex_attribs[12]={1,0};
 vp_prepare_vertex_fetch(&st);
 for(u32 vi=0;vi<20;vi++){
  VPSlot a[16],b[16];for(int i=0;i<16;i++)rsx_fetch_attrib(&st,i,vi,a[i].v);
  calls=0;vp_fetch_vertex(&st,vi,b);check(calls==3);check(!memcmp(a,b,sizeof(a)));
 }
 st.vertex_data4f[7][3]=0.25f;st.vertex_attribs[3].enabled=0;vp_prepare_vertex_fetch(&st);
 VPSlot a[16];calls=0;vp_fetch_vertex(&st,5,a);check(calls==2&&a[7].v[3]==0.25f&&a[3].v[0]==12.0f);
 rsx_state second=st;second.vertex_data4f[7][3]=0.75f;vp_fetch_vertex(&second,5,a);check(a[7].v[3]==0.75f);
 puts("PASS: constant attributes copied once per draw, active fetches match all 16 canonical slots byte-for-byte");
}
