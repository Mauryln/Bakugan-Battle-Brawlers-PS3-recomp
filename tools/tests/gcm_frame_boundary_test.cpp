#include <cstdlib>
#include <cstdio>
#include <cstdint>
using u32 = uint32_t;
#define GCM_FIFO_CATCHUP_BYTES 0x10000u
#include "frame_flip.inc"
static bool pending;
static unsigned presents;
static int cellGcm_take_flip_pending() { int p=pending;pending=false;return p; }
static void present_guest_frame() { ++presents; }
static void require(bool ok, const char* msg) {if(!ok){fprintf(stderr,"FAIL: %s\n",msg);exit(1);}}
int main(int argc,char** argv) {
    bool strict=argc>1 && argv[1][0]=='1';
    require(gcm_fifo_stop_at_flip(0),"small FIFO frame stops at flip");
    require(bool(gcm_fifo_stop_at_flip(0x80000))==strict,"backlogged full-frame mode must stop before next reflection pass");
    unsigned last_flip=0;
    for(unsigned fc=1;fc<=10;fc++) {
        pending=true; // FIFO just reached this frame's marker.
#include "ticker_flip.inc"
        // Next outer iteration's pre-drain check must not present it again.
        if(cellGcm_take_flip_pending()) present_guest_frame();
        require(presents==fc,"each consumed FIFO flip presents exactly once");
    }
    puts("PASS: backlog preserves full frames and ticker consumes each flip exactly once");
}
