// Bakugan's SAIL lifecycle with native video/audio presentation on Windows.
#include "ppu_recomp.h"
#include "movie_video.h"
#include "movie_audio.h"
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

extern "C" {
void rsx_d3d12_backend_video_frame(const uint8_t*, uint32_t, uint32_t);
void ps3_hle_register_ctx(uint32_t, const char*, void (*)(ppu_context*));
uint64_t ppu_guest_call(uint32_t, uint64_t, uint64_t, uint64_t, uint64_t,
                        uint64_t, uint64_t, uint64_t, uint64_t);
extern const char* ppu_vfs_root;
int32_t cellSailPlayerInitialize2(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t cellSailPlayerFinalize(uint32_t);
int32_t cellSailPlayerReplaceEventHandler(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t cellSailGraphicsAdapterInitialize(uint32_t, uint32_t, uint32_t);
int32_t cellSailPlayerSetGraphicsAdapter(uint32_t, uint32_t, void*);
int32_t cellSailPlayerCreateDescriptor(uint32_t, int32_t, void*, char*, uint32_t*);
int32_t cellSailPlayerDestroyDescriptor(uint32_t, uint32_t);
}

namespace {
constexpr uint32_t invalid = 0x80610701, invalid_state = 0x80610702;
std::recursive_mutex sail_mutex;
struct Adapter {
    std::array<uint32_t, 3> callbacks{};
    uint32_t arg = 0;
};
struct Player {
    uint32_t callback = 0, arg = 0, graphics = 0, descriptor = ~0u;
    uint32_t session = 0;
    bool opened = false;
    bool running = false, paused = false, made_up = false;
    std::string uri;
    MovieVideo video;
    MovieAudio audio;
    VideoFrame frame;
};
std::unordered_map<uint32_t, std::unique_ptr<Player>> players;
std::unordered_map<uint32_t, Adapter> adapters;

Player* player(ppu_context* ctx)
{
    auto it = players.find((uint32_t)ctx->gpr[3]);
    return it == players.end() ? nullptr : it->second.get();
}

uint64_t call(uint32_t opd, uint64_t a=0, uint64_t b=0, uint64_t c=0, uint64_t d=0)
{
    return opd ? ppu_guest_call(opd, a, b, c, d, 0, 0, 0, 0) : 0;
}

void notify(Player& p, uint32_t major, uint32_t minor, uint64_t a=0, uint64_t b=0)
{
    call(p.callback, p.arg, (uint64_t(major) << 32) | minor, a, b);
}

void complete(Player& p, uint32_t command, uint32_t via, uint32_t to, uint64_t token)
{
    notify(p, 3, 0, via);
    notify(p, 2, command, 0, token);
    notify(p, 3, 0, to);
}

std::string guest_string(uint32_t ea)
{
    std::string result;
    for (unsigned i = 0; ea && i < 1024; ++i) {
        char ch = (char)vm_read8(ea + i);
        if (!ch) break;
        result += ch;
    }
    return result;
}

std::filesystem::path movie_path(const std::string& uri)
{
    constexpr char prefix[] = "x-cell-fs:///dev_bdvd/";
    if (uri.compare(0, sizeof(prefix)-1, prefix) != 0) return {};
    auto relative = std::filesystem::path(uri.substr(sizeof(prefix)-1));
    if (relative.is_absolute()) return {};
    for (const auto& part : relative) if (part == "..") return {};
    return std::filesystem::path(ppu_vfs_root) / relative;
}

void format(uint32_t ea, const VideoFrame& f)
{
    for (unsigned i=0; i<32; i+=4) vm_write32(ea+i, ~0u);
    vm_write8(ea, 1); // RGBA interleaved, as requested by the title.
    vm_write8(ea+1, 0); vm_write8(ea+2, 8); vm_write8(ea+3, 4); // progressive, 30 Hz
    vm_write16(ea+4, (uint16_t)f.width); vm_write16(ea+6, (uint16_t)f.height);
    vm_write32(ea+8, f.width*4); vm_write32(ea+12, 255);
}

void cleanup(Player& p)
{
    p.video.close();
    p.audio.stop();
    rsx_d3d12_backend_video_frame(nullptr,0,0);
    if (p.graphics && adapters.count(p.graphics)) {
        const Adapter a = adapters.at(p.graphics);
        if (p.made_up) call(a.callbacks[1], a.arg);
    }
    p.frame = {}; p.opened = false;
    p.running = p.made_up = false;
}

void initialize(ppu_context* c)
{
    uint32_t self=(uint32_t)c->gpr[3];
    int32_t rc=cellSailPlayerInitialize2(self,(uint32_t)c->gpr[4],(uint32_t)c->gpr[5],
        (uint32_t)c->gpr[6],(uint32_t)c->gpr[7],(uint32_t)c->gpr[8]);
    if (!rc) {
        auto p=std::make_unique<Player>(); p->callback=(uint32_t)c->gpr[5]; p->arg=(uint32_t)c->gpr[6];
        players[self]=std::move(p);
    }
    c->gpr[3]=(uint32_t)rc;
}

void replace_handler(ppu_context* c)
{
    Player* p=player(c);
    if (!p) { c->gpr[3]=invalid; return; }
    uint32_t cb=(uint32_t)c->gpr[4], arg=(uint32_t)c->gpr[5];
    c->gpr[3]=(uint32_t)cellSailPlayerReplaceEventHandler((uint32_t)c->gpr[3],cb,arg,
        (uint32_t)c->gpr[6],(uint32_t)c->gpr[7]);
    if (!c->gpr[3]) { p->callback=cb; p->arg=arg; }
}

void graphics_initialize(ppu_context* c)
{
    Adapter a; a.arg=(uint32_t)c->gpr[5];
    for (unsigned i=0;i<3;++i) a.callbacks[i]=vm_read32(c->gpr[4]+i*4);
    uint32_t self=(uint32_t)c->gpr[3];
    c->gpr[3]=(uint32_t)cellSailGraphicsAdapterInitialize(self,(uint32_t)c->gpr[4],a.arg);
    if (!c->gpr[3]) adapters[self]=a;
}

void set_graphics(ppu_context* c)
{
    Player* p=player(c);
    uint32_t adapter=(uint32_t)c->gpr[5];
    if (!p || !adapters.count(adapter)) { c->gpr[3]=invalid; return; }
    c->gpr[3]=(uint32_t)cellSailPlayerSetGraphicsAdapter((uint32_t)c->gpr[3],
        (uint32_t)c->gpr[4], (void*)(uintptr_t)adapter);
    if (!c->gpr[3]) p->graphics=adapter;
}

void create_descriptor(ppu_context* c)
{
    Player* p=player(c);
    if (!p) { c->gpr[3]=invalid; return; }
    std::string uri=guest_string((uint32_t)c->gpr[6]);
    uint32_t out=(uint32_t)c->gpr[7];
    c->gpr[3]=(uint32_t)cellSailPlayerCreateDescriptor((uint32_t)c->gpr[3],(int32_t)c->gpr[4],
        (void*)(uintptr_t)c->gpr[5], (char*)(uintptr_t)c->gpr[6], (uint32_t*)(uintptr_t)out);
    if (!c->gpr[3]) { p->uri=uri; p->descriptor=vm_read32(out); }
}

void descriptor_count(ppu_context* c)
{
    Player* p=player(c); c->gpr[3]=p ? (p->descriptor != ~0u) : invalid;
}

void destroy_descriptor(ppu_context* c)
{
    Player* p=player(c);
    if (!p) { c->gpr[3]=invalid; return; }
    c->gpr[3]=(uint32_t)cellSailPlayerDestroyDescriptor((uint32_t)c->gpr[3],(uint32_t)c->gpr[4]);
    if (!c->gpr[3]) { p->descriptor=~0u; p->uri.clear(); }
}

void open_stream(ppu_context* c)
{
    Player* p=player(c);
    if (!p || !p->graphics || p->descriptor==~0u) { c->gpr[3]=invalid_state; return; }
    auto path=movie_path(p->uri);
    int32_t rc=path.empty() ? (int32_t)invalid : p->video.open(path.wstring(),p->frame);
    if (rc) { fprintf(stderr,"[video] open failed %08X: %s\n",(uint32_t)rc,p->uri.c_str()); c->gpr[3]=invalid; return; }
    Adapter a=adapters.at(p->graphics);
    uint32_t scratch=(uint32_t)c->gpr[1]-0x100;
    rc=(int32_t)call(a.callbacks[0],a.arg);
    if (!rc) {
        p->made_up=true;
    }
    if (rc) { cleanup(*p); c->gpr[3]=invalid; return; }
    p->opened=true;
    ++p->session;
    format(scratch+16,p->frame);
    call(a.callbacks[2],a.arg,scratch+16,p->session);
    fprintf(stderr,"[video] open %s %ux%u (native plane)\n",p->uri.c_str(),p->frame.width,p->frame.height);
    complete(*p,2,3,4,c->gpr[5]); c->gpr[3]=0;
}

void start(ppu_context* c)
{
    Player* p=player(c);
    if (!p || !p->opened) { c->gpr[3]=invalid_state; return; }
    p->running=true; p->paused=false;
    p->video.start();
    p->audio.start(movie_path(p->uri).wstring());
    complete(*p,10,5,6,c->gpr[5]); c->gpr[3]=0;
}

void stop(ppu_context* c)
{
    Player* p=player(c);
    if (!p) { c->gpr[3]=invalid; return; }
    p->running=false; p->video.pause(true); p->audio.stop();
    fprintf(stderr,"[video] stop after %u frames: %s\n",p->video.frames(),p->uri.c_str());
    rsx_d3d12_backend_video_frame(nullptr,0,0);
    complete(*p,11,7,4,c->gpr[4]); c->gpr[3]=0;
}

Player* adapter_player(uint32_t adapter)
{
    for (auto& entry:players) if (entry.second->graphics==adapter) return entry.second.get();
    return nullptr;
}

void get_format(ppu_context* c)
{
    Player* p=adapter_player((uint32_t)c->gpr[3]);
    if (!p || !p->opened || !c->gpr[4]) { c->gpr[3]=invalid_state; return; }
    format((uint32_t)c->gpr[4],p->frame); c->gpr[3]=0;
}

void get_frame(ppu_context* c)
{
    Player* p=adapter_player((uint32_t)c->gpr[3]);
    uint32_t out=(uint32_t)c->gpr[4];
    if (!p || !out) { c->gpr[3]=invalid; return; }
    for (unsigned i=0;i<24;i+=4) vm_write32(out+i,0);
    if (!p->running || p->paused) { c->gpr[3]=0; return; }
    // Completion callbacks stay on a guest thread. Presentation and decoding
    // keep running even when asset loading prevents these guest polls.
    if (p->video.take_finished()) {
        p->running=false;
        notify(*p,3,0,7); notify(*p,3,0,4);
    }
    // The native plane owns presentation. Do not also submit guest textures:
    // the title's unresolved render-to-texture path remains independently usable.
    c->gpr[3]=0; // CELL_SAIL_SYNC_NO_FRAME for the guest renderer.
}

void close_stream(ppu_context* c)
{
    Player* p=player(c);
    if (!p) { c->gpr[3]=invalid; return; }
    cleanup(*p);
    complete(*p,3,8,2,c->gpr[4]); c->gpr[3]=0;
}

void finalize(ppu_context* c)
{
    uint32_t self=(uint32_t)c->gpr[3]; Player* p=player(c);
    if (p) cleanup(*p);
    if (p && p->descriptor!=~0u) cellSailPlayerDestroyDescriptor(self,p->descriptor);
    players.erase(self); c->gpr[3]=(uint32_t)cellSailPlayerFinalize(self);
}

void set_paused(ppu_context* c)
{
    Player* p=player(c);
    if (!p) { c->gpr[3]=invalid; return; }
    bool paused=c->gpr[4]!=0;
    p->video.pause(paused);
    p->paused=paused; notify(*p,7,0,paused); c->gpr[3]=0;
    p->audio.pause(paused);
}

// The title keeps its audio worker active; movie PCM is rendered by waveOut.
// Explicit empty records prevent the guest from interpreting stale stack data.
void sound_frame(ppu_context* c)
{
    if (!c->gpr[4]) { c->gpr[3]=invalid; return; }
    for (unsigned i=0;i<24;i+=4) vm_write32(c->gpr[4]+i,0);
    c->gpr[3]=0;
}
template<void (*Fn)(ppu_context*)>
void serialized(ppu_context* c)
{
    std::lock_guard lock(sail_mutex);
    Fn(c);
}
} // namespace

void bakugan_install_video()
{
    struct Hook { uint32_t nid; const char* name; void (*fn)(ppu_context*); };
    const Hook hooks[]={
        {0x23654375,"cellSailPlayerInitialize2",serialized<initialize>},
        {0x47632810,"cellSailPlayerReplaceEventHandler",serialized<replace_handler>},
        {0x1C983864,"cellSailGraphicsAdapterInitialize",serialized<graphics_initialize>},
        {0x18BCD21B,"cellSailPlayerSetGraphicsAdapter",serialized<set_graphics>},
        {0xD7938B8D,"cellSailPlayerCreateDescriptor",serialized<create_descriptor>},
        {0xFC839BD4,"cellSailPlayerDestroyDescriptor",serialized<destroy_descriptor>},
        {0x752F8585,"cellSailPlayerGetDescriptorCount",serialized<descriptor_count>},
        {0x34ECC1B9,"cellSailPlayerOpenStream",serialized<open_stream>},
        {0xE535B0D3,"cellSailPlayerStart",serialized<start>},
        {0xEBA8D4EC,"cellSailPlayerStop",serialized<stop>},
        {0x85BEFFCC,"cellSailPlayerCloseStream",serialized<close_stream>},
        {0x18B4629D,"cellSailPlayerFinalize",serialized<finalize>},
        {0xFFD58AA4,"cellSailGraphicsAdapterGetFormat",serialized<get_format>},
        {0x0247C69E,"cellSailGraphicsAdapterGetFrame",serialized<get_frame>},
        {0xD1D55A90,"cellSailPlayerSetPaused",serialized<set_paused>},
        {0x7EB8D6B5,"cellSailSoundAdapterGetFrame",serialized<sound_frame>},
    };
    for (const auto& h:hooks) ps3_hle_register_ctx(h.nid,h.name,h.fn);
}
