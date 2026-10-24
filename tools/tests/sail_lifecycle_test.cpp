#include "cellSail.h"
#include "ps3emu/guest_call.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

static uint8_t memory[16*1024*1024];
extern "C" {
uint8_t* vm_base=memory;
uint32_t g_ww_lo=0,g_ww_hi=0;
int g_resv_store_active=0;
void ps3_ww_report_inline(uint32_t,uint64_t,int) {}
int spu_coh_is_reserved(uint32_t) { return 0; }
void spu_lockline_lock() {}
void spu_coh_notify_write(uint32_t) {}
void spu_lockline_unlock() {}
void ppu_resv_break_store(uint64_t) {}
uint8_t vm_read8(uint64_t a) { return memory[a]; }
uint32_t vm_read32(uint64_t a) { uint32_t v;std::memcpy(&v,memory+a,4);return __builtin_bswap32(v); }
void vm_write32(uint64_t a,uint32_t v) { v=__builtin_bswap32(v);std::memcpy(memory+a,&v,4); }
ps3_guest_caller_fn g_ps3_guest_caller;
}
static unsigned completed;
static void callback(uint32_t opd,uint64_t arg,uint64_t event,uint64_t result,uint64_t token,
                     uint64_t,uint64_t,uint64_t,uint64_t)
{
    if (opd!=0x4000 || arg!=0x5678) std::abort();
    if (event==((uint64_t(2)<<32)|1)) {
        ++completed;
        cellSailFutureSet((uint32_t)token,(int32_t)result);
        // Bakugan changes its event callback inside the boot completion.
        cellSailPlayerReplaceEventHandler(0x1000,0x4000,0x5678,0,0);
    }
}
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while (0)
int main()
{
    std::memset(memory,0xCD,sizeof(memory));
    CHECK(cellSailFutureInitialize(0x2000)==0);
    CHECK(vm_read32(0x2018)==0xCDCDCDCD); // Exactly 24 bytes, preserve adjacent data.
    vm_write32(0x3000,0x12345678);
    CHECK(cellSailFutureGet(0x2000,0,0x3000)<0);
    CHECK(vm_read32(0x3000)==0x12345678);
    CHECK(cellSailFutureGet(0,0,0x3000)<0);
    CHECK(cellSailFutureSet(0x2000,-123)==0);
    CHECK(cellSailFutureGet(0x2000,0,0x3000)==0);
    CHECK((int32_t)vm_read32(0x3000)==-123);
    CHECK(cellSailFutureGet(0x2000,0,0)<0);
    g_ps3_guest_caller=callback;
    for (unsigned i=0;i<10;++i) {
        CHECK(cellSailFutureInitialize(0x2000)==0);
        CHECK(cellSailPlayerInitialize2(0x1000,0,0x4000,0x5678,0,0)==0);
        CHECK(cellSailPlayerBoot(0x1000,0x2000)==0);
        CHECK(cellSailFutureGet(0x2000,0,0x3000)==0 && vm_read32(0x3000)==0);
        CHECK(cellSailPlayerReplaceEventHandler(0x1000,0x4000,0x5678,0x3010,0x3014)==0);
        CHECK(vm_read32(0x3010)==0x4000 && vm_read32(0x3014)==0x5678);
        CHECK(cellSailPlayerFinalize(0x1000)==0);
        CHECK(cellSailPlayerBoot(0x1000,0)<0);
    }
    CHECK(completed==10);
    for (unsigned i=0;i<4;++i) CHECK(cellSailPlayerInitialize2(0x4000+i*0x400,0,0,0,0,0)==0);
    CHECK(cellSailPlayerInitialize2(0x6000,0,0,0,0,0)<0);
    for (unsigned i=0;i<4;++i) CHECK(cellSailPlayerFinalize(0x4000+i*0x400)==0);
    std::strcpy((char*)memory+0x7000,"x-cell-fs:///dev_bdvd/movie/test.mp4");
    CHECK(cellSailPlayerCreateDescriptor(0x1000,1,nullptr,(char*)0x7000,(uint32_t*)0x7100)==0);
    uint32_t desc=vm_read32(0x7100);
    CHECK(cellSailDescriptorGetUri(desc,(char*)0x7200,0)<0);
    CHECK(cellSailDescriptorGetUri(desc,(char*)0x7200,3)==0);
    CHECK(memory[0x7200]=='x' && memory[0x7201]=='-' && memory[0x7202]==0);
    CHECK(memory[0x7203]==0xCD);
    CHECK(cellSailPlayerDestroyDescriptor(0x1000,desc)==0);
    std::puts("PASS: SAIL futures, callback replacement, finalization, 10 player lifecycles");
}
