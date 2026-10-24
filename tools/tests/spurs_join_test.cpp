#include "cellSpurs.h"
extern "C" {
void spurs_taskset_wait_idle(uint32_t);
int spurs_taskset_is_shutting_down(uint32_t);
int spurs_taskset_request_shutdown(uint32_t);
void spurs_taskset_init(uint32_t,uint32_t,uint64_t,uint32_t,uint32_t,uint32_t,uint32_t);
int spu_taskset_signal_parked_obj(uint32_t,uint32_t) { return 0; }
}
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>
#include <cstdlib>
#include <cstring>
extern "C" {
static uint8_t memory[65536];
uint8_t* vm_base=memory;
uint32_t ppu_vm_size=sizeof(memory);
uint32_t g_ww_lo=0xFFFFFFFFu,g_ww_hi=0;
int g_resv_store_active=0;
static std::mutex line;
void spu_lockline_lock() { line.lock(); }
void spu_lockline_unlock() { line.unlock(); }
void spu_coh_notify_write(uint32_t) {}
int spu_coh_is_reserved(uint32_t) { return 0; }
void ppu_resv_break_store(uint64_t) {}
void ps3_ww_report_inline(uint32_t,uint64_t,int) {}
}
#define GUEST_PTR(p,T) ((T)((p)?vm_base+(uint32_t)(uintptr_t)(p):nullptr))
#include "join_entry.inc"
int main() {
    constexpr uint32_t taskset=0x1000,queue=0x3000;
    std::atomic<bool> release=false,joining=false,joined=false;
    memory[taskset+0x3F]=1; // Task 127: cover the entire enabled bitset.
    memory[queue]=42;
    std::thread worker([&]{
        while(!release) std::this_thread::yield();
        memory[queue]=99; // The final DMA must precede a successful join.
        std::lock_guard lock(line);memory[taskset+0x3F]=0;
    });
    std::thread waiter([&]{ joining=true;cellSpursJoinTaskset((CellSpursTaskset*)(uintptr_t)taskset);joined=true; });
    while(!joining) std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    bool premature=joined;
    release=true;worker.join();waiter.join();
    if(premature || memory[queue]!=99) {std::puts("FAIL: JoinTaskset returned while an SPU task could still write the queue");return 1;}
    memory[taskset+0x30]=0x80;
    std::thread parked([&]{
        while(!spurs_taskset_is_shutting_down(taskset)) std::this_thread::yield();
        std::lock_guard lock(line);
        memory[queue]=123;memory[taskset+0x30]=0;
    });
    if(cellSpursShutdownTaskset((CellSpursTaskset*)(uintptr_t)taskset)) std::abort();
    cellSpursJoinTaskset((CellSpursTaskset*)(uintptr_t)taskset);
    parked.join();
    if(memory[queue]!=123) std::abort();
    spurs_taskset_init(taskset,0,0,0,0x1900,0,0);
    if(spurs_taskset_is_shutting_down(taskset)) std::abort();
    std::puts("PASS: shutdown wakes cooperative tasks, join waits for final writes, taskset reuse resets shutdown");
}
