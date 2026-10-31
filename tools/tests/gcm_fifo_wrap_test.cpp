#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <map>
using u32 = unsigned;
using namespace std;
static map<u32, u32> memory;
static u32 vm_read32(u32 ea) { return memory[ea]; }
static void vm_write32(u32 ea, u32 value) { memory[ea] = value; }
static const u32 GCM_CONTROL_GUEST_ADDR = 0x100;
static const u32 GCM_FIFO_CALLBACK_SENTINEL_EA = 0x1234;
static const u32 GCM_FLIP_MARKER = 0xFEAD0000;
static const u32 CELL_GCM_MAX_DISPLAY_BUFFER_NUM = 8;
static const u32 GCM_RECYCLE_SLACK = 0x1000;
static const u32 GCM_FLIP_WRAP_BYTES = 0x40000;
static const u32 GCM_RECYCLE_MARGIN = 0x20000;
static int s_display_buffer_set[8] = {1};
static u32 s_fifo_getoff, s_fifo_back_jumps, g_gcm_fifo_drained_ea;
static int s_fifo_recycle_waiting;
static u32 s_fifo_flips_queued;
static u32 gcm_ea2io(u32 ea) { return ea - 0x100000; }
static unsigned long GetCurrentThreadId() { return 1; }
#define PS3_LOG_FIRST(n, limit) ((n)++ < (limit))
static int waits;
static void require(bool ok, const char* message)
{
    if (!ok) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}
static void Sleep(int)
{
    ++waits;
    require(vm_read32(0x208) == 0x1C0004, "producer retains tail until the consumer takes the jump");
    require(vm_read32(0x1C0004) == 0x20000000, "tail links back to the ring start");
    require(vm_read32(GCM_CONTROL_GUEST_ADDR) == 0, "wrapped tail is submitted before waiting");
    s_fifo_getoff = 0;
    ++s_fifo_back_jumps;
}
#include "fifo_wrap.inc"
int main()
{
    memory[0x200] = 0x100000;
    memory[0x204] = 0x200000;
    memory[0x208] = 0x1C0000;
    memory[0x20C] = 0x300;
    memory[0x300] = GCM_FIFO_CALLBACK_SENTINEL_EA;
    s_fifo_getoff = 0x70000;
    require(gcm_flip_into_fifo(0x200, 0) == 1, "flip queues successfully");
    require(waits == 0, "queuing a flip does not execute it before its completion request is armed");
    require(vm_read32(0x208) == 0x1C0004, "flip retains the write head until the buffer-full callback owns the wrap");
    require(vm_read32(0x1C0000) == GCM_FLIP_MARKER, "queued flip remains ahead of the wrap jump");
    require(s_fifo_flips_queued == 1, "wrap preserves the queued flip");
    cellGcm_fifo_recycle(0x200);
    require(waits > 0, "buffer-full callback waits for the outstanding tail");
    require(vm_read32(0x208) == 0x100000, "head becomes writable after the jump is consumed");
    puts("PASS: flip wrap applies FIFO backpressure before reusing commands");
}
