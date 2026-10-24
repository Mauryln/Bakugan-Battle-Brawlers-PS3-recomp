#define main lifecycle_main
#include "sail_lifecycle_test.cpp"
#undef main
#include "ppu_context.h"
#include "movie_audio.h"
#include <unordered_map>
#include <atomic>
#include <thread>
#include <chrono>
#include <filesystem>

struct MovieAudio::Impl {};
MovieAudio::MovieAudio():impl_(std::make_unique<Impl>()) {}
MovieAudio::~MovieAudio()=default;
void MovieAudio::start(const std::wstring&) {}
void MovieAudio::pause(bool) {}
void MovieAudio::stop() {}
static std::unordered_map<uint32_t,void(*)(ppu_context*)> hooks;
static std::atomic<unsigned> frames_presented;
static unsigned allocations,frees;
static std::atomic<bool> plane_active;
static bool stream_finished;
extern "C" {
void vm_write8(uint64_t a,uint8_t v) { memory[a]=v; }
void vm_write16(uint64_t a,uint16_t v) { v=__builtin_bswap16(v);std::memcpy(memory+a,&v,2); }
void vm_write64(uint64_t a,uint64_t v) { v=__builtin_bswap64(v);std::memcpy(memory+a,&v,8); }
const char* ppu_vfs_root;
void ps3_hle_register_ctx(uint32_t nid,const char*,void(*fn)(ppu_context*)) { hooks[nid]=fn; }
void rsx_d3d12_backend_video_frame(const uint8_t* pixels,uint32_t w,uint32_t h) {
    plane_active=pixels!=nullptr;
    if(pixels) { if(w!=1280 || h!=720) std::abort(); ++frames_presented; }
}
uint64_t ppu_guest_call(uint32_t opd,uint64_t a,uint64_t b,uint64_t c,uint64_t d,
                        uint64_t,uint64_t,uint64_t,uint64_t) {
    if(opd==0x8000) return 0; // makeup
    if(opd==0x8004) return 0; // cleanup
    if(opd==0x8008) return 0; // format changed
    if(opd==0x800C) { // allocator's ppFrame is a guest out-param
        if(b!=1280*720*4 || c!=1) std::abort();
        vm_write32(d,0x100000); ++allocations; return 0;
    }
    if(opd==0x8010) { if(b!=1 || vm_read32(c)!=0x100000) std::abort(); ++frees; return 0; }
    if(opd==0x9000 && b==(uint64_t(3)<<32) && c==4) stream_finished=true;
    return 0; // player events
}
}
void bakugan_install_video();
static uint32_t invoke(uint32_t nid,uint64_t a,uint64_t b=0,uint64_t c=0,uint64_t d=0,uint64_t e=0) {
    ppu_context ctx{};ctx.gpr[1]=0xF000;
    ctx.gpr[3]=a;ctx.gpr[4]=b;ctx.gpr[5]=c;ctx.gpr[6]=d;ctx.gpr[7]=e;
    hooks.at(nid)(&ctx);return (uint32_t)ctx.gpr[3];
}
int main(int argc,char** argv)
{
    CHECK(argc==2);ppu_vfs_root=argv[1];bakugan_install_video();
    CHECK(invoke(0x23654375,0x1000,0,0x9000)==0);
    for(unsigned i=0;i<5;++i) vm_write32(0x5000+i*4,0x8000+i*4);
    CHECK(invoke(0x1C983864,0x4000,0x5000,0x5678)==0);
    CHECK(invoke(0x18BCD21B,0x1000,0,0x4000)==0);
    std::strcpy((char*)memory+0x6000,"x-cell-fs:///dev_bdvd/PS3_GAME/USRDIR/movie/client_16_9.mp4");
    CHECK(invoke(0xD7938B8D,0x1000,1,0,0x6000,0x6100)==0);
    CHECK(invoke(0x752F8585,0x1000)==1);
    CHECK(invoke(0x34ECC1B9,0x1000,0)==0 && allocations==0);
    stream_finished=false;
    CHECK(invoke(0xE535B0D3,0x1000,0)==0);
    // Asset loading can prevent guest GetFrame calls entirely. Native video
    // must continue progressing while the guest is busy, not merely catch up
    // after its next poll.
    unsigned before_stall=frames_presented.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    CHECK(frames_presented.load()>before_stall+2);
    CHECK(invoke(0xD1D55A90,0x1000,1)==0);
    unsigned paused_frames=frames_presented.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    CHECK(frames_presented.load()==paused_frames);
    CHECK(invoke(0xD1D55A90,0x1000,0)==0);
    unsigned delivered=0; bool eof=false;
    bool delayed=false;
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    while(std::chrono::steady_clock::now()<deadline) {
        uint32_t status=invoke(0x0247C69E,0x4000,0x6200);
        CHECK(status==0 && vm_read32(0x6200)==0); // Native plane owns the frame.
        CHECK(invoke(0xFFD58AA4,0x4000,0x6300)==0);
        CHECK(vm_read32(0x6304)==((1280u<<16)|720)); // Including the final frame.
        delivered=frames_presented-1;
        if(delivered && !delayed) { std::this_thread::sleep_for(std::chrono::milliseconds(250)); delayed=true; }
        if(stream_finished) { eof=true;break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(eof && delivered>200 && delivered<=311); // Guest stalls no longer stop native presentation.
    CHECK(invoke(0x0247C69E,0x4000,0x6200)==0);
    CHECK(invoke(0x85BEFFCC,0x1000)==0 && !plane_active && frees==0);
    CHECK((int32_t)invoke(0xFFD58AA4,0x4000,0x6300)<0);
    // Reopening and stopping during pause must wake and join the decoder.
    CHECK(invoke(0x34ECC1B9,0x1000,0)==0);
    CHECK(invoke(0xE535B0D3,0x1000,0)==0);
    unsigned reopened=frames_presented.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    CHECK(frames_presented.load()>reopened);
    CHECK(invoke(0xD1D55A90,0x1000,1)==0);
    CHECK(invoke(0x85BEFFCC,0x1000)==0 && !plane_active);
    CHECK(invoke(0x18B4629D,0x1000)==0 && frees==0);
    CHECK(invoke(0x23654375,0x1000)==0); // Same guest EA can be reused.
    CHECK(invoke(0x18B4629D,0x1000)==0);
    std::puts("PASS: independent movie clock, pause/resume, reopen and close while paused, EOF dimensions and plane cleanup");
}
