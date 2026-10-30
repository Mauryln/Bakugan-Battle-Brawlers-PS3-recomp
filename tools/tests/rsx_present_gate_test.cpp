#include <cstdio>
#include <cstdlib>
typedef unsigned u32;
struct D3D12DrawRecord { int is_clear; u32 rt_off; int is_vp; u32 cmask; };
#include "present_gate.inc"
static void check(bool ok) { if(!ok) { puts("FAIL: colour-disabled shadow pass is not a display draw"); exit(1); } }
int main() {
 D3D12DrawRecord shadow={0,0,1,0}, geometry={0,0xEAB0000,1,15}, hud={0,0,1,15}, clear={1,0,1,15}, basic={0,0,0,0};
 check(!rsx_draw_has_display_color(&shadow));
 check(!rsx_draw_has_display_color(&geometry));
 check(rsx_draw_has_display_color(&hud));
 check(!rsx_draw_has_display_color(&clear));
 check(rsx_draw_has_display_color(&basic));
 puts("PASS: display gate excludes clears, offscreen geometry and depth-only shadows");
}
