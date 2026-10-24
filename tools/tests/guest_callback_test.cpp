#include "ppu_recomp.h"
#include <windows.h>
#include <atomic>
#include <barrier>
#include <cstdio>
#include <cstdlib>
#include <thread>
extern "C" {
uint8_t* vm_base;
extern uint32_t ppu_vm_size;
void ppu_register_function(uint64_t,void(*)(ppu_context*));
uint64_t ppu_guest_call_ct(uint32_t,uint32_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
extern PPU_THREAD_LOCAL ppu_context* g_active_ctx;
}
// Link-only fixtures for loader services outside the callback path.
extern "C" {
const func_entry function_table[]={{0,nullptr,nullptr}};
const uint64_t function_table_count=0;
uint32_t ppu_hle_inject_base=0x3000000;
void (*g_ppu_thread_entry_trampoline)(ppu_context*);
uint32_t spu_registry_size() { return 0; }
int spu_registry_entry(uint32_t,void**,uint32_t*) { return 0; }
int spu_coh_is_reserved(uint32_t) { return 0; }
void spu_lockline_lock() {} void spu_lockline_unlock() {}
void spu_coh_notify_write(uint32_t) {}
int spu_raw_reg_load(uint32_t,uint32_t*) { return 0; }
void spu_raw_reg_store(uint32_t,uint32_t,unsigned) {}
void cellGcm_ref_on_poll() {}
uint32_t ps3_hle_count() { return 0; }
void ps3_msf(const char*,...) {}
void ppu_prof_stamp(void*,unsigned) {}
void sys_mmapper_allocate_address() { std::abort(); }
void lv2_try_syscall() { std::abort(); }
void ps3_mmapper_block_size() { std::abort(); }
void sys_mmapper_search_map_address() { std::abort(); }
void sys_memory_set_image_query() { std::abort(); }
uint32_t ppu_thread_callback_stack() {
    static std::atomic<uint32_t> next{0xD8000000u};
    return next.fetch_add(0x100000u)+0xFFF00u;
}
void ppu_thread_register_main() { std::abort(); }
}
static std::barrier rendezvous(2);
static std::atomic<unsigned> failures;
static void callback(ppu_context* ctx) {
    if(g_active_ctx!=ctx || (ctx->gpr[4] && ctx->gpr[13]!=ctx->gpr[4])) ++failures;
    auto slot=ctx->gpr[1]-64;
    vm_write32(slot,(uint32_t)ctx->gpr[3]);
    rendezvous.arrive_and_wait();
    if(vm_read32(slot)!=ctx->gpr[3]) ++failures;
    rendezvous.arrive_and_wait();
}
static void nested(ppu_context* ctx) {
    auto slot=ctx->gpr[1]-64;
    vm_write32(slot,0x12345678);
    ppu_guest_call_ct(0x1010,0,ctx->gpr[1],ctx->gpr[13],0,0,0,0,0,0);
    if(vm_read32(slot)!=0x12345678 || g_active_ctx!=ctx) ++failures;
}
static void inner(ppu_context* ctx) {
    if(ctx->gpr[1]>=ctx->gpr[3]-64 || ctx->gpr[13]!=ctx->gpr[4]) ++failures;
    vm_write32(ctx->gpr[1]-64,0x87654321);
}
int main() {
    setvbuf(stdout,nullptr,_IONBF,0);
    vm_base=(uint8_t*)VirtualAlloc(nullptr,0x100000000ull,MEM_RESERVE,PAGE_READWRITE);
    if(!vm_base) return 2;
    if(!VirtualAlloc(vm_base+0xCFE00000u,0x200000u,MEM_COMMIT,PAGE_READWRITE) ||
       !VirtualAlloc(vm_base+0xD8000000u,0x2000000u,MEM_COMMIT,PAGE_READWRITE)) return 2;
    ppu_register_function(0x1000,callback);
    auto run=[](unsigned id) { ppu_guest_call_ct(0x1000,0,id,0,0,0,0,0,0,0); };
    std::thread a(run,1),b(run,2);a.join();b.join();
    auto with_caller=[](unsigned id) {
        ppu_context caller{};
        caller.gpr[1]=0xD9000000u+id*0x100000u;
        caller.gpr[13]=0x120000u+id*0x10000u;
        caller.thread_id=id;
        g_active_ctx=&caller;
        ppu_guest_call_ct(0x1000,0,id,caller.gpr[13],0,0,0,0,0,0);
        if(g_active_ctx!=&caller) ++failures;
        g_active_ctx=nullptr;
    };
    std::thread c(with_caller,3),d(with_caller,4);c.join();d.join();
    ppu_register_function(0x1008,nested);
    ppu_register_function(0x1010,inner);

    ppu_guest_call_ct(0x1008,0,0,0,0,0,0,0,0,0);

    if(g_active_ctx!=nullptr) ++failures;
    if(failures) { std::puts("FAIL: concurrent guest callbacks overwrite each other's stack");return 1; }
    std::puts("PASS: concurrent/nested guest callbacks preserve stacks, caller TLS and active context");
}

