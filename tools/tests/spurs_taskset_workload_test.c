#include "libs/spurs/cellSpurs.c"
#include <assert.h>

uint8_t* vm_base;
extern uint32_t ppu_vm_size;
void ps3_guest_stamp(uint32_t ea, uint32_t n) { (void)ea; (void)n; }
void* function_table[1];
uint32_t function_table_count;
uint32_t g_guest_gen = 1;
uint32_t g_guest_page_gen[1u << 20];
extern int spu_taskset_wait_signal(uint32_t, uint32_t);
extern int spu_taskset_consume_wake(uint32_t);
extern void spu_taskset_latch_wake(uint32_t);
extern void spu_taskset_signal_task(uint32_t, uint32_t);
static DWORD WINAPI wait_for_signal(LPVOID ignored)
{
    (void)ignored;
    return (DWORD)spu_taskset_wait_signal(0x8000, 0);
}
static HANDLE second_waiting;
static DWORD WINAPI wait_for_second(LPVOID ignored)
{
    (void)ignored;
    SetEvent(second_waiting);
    return (DWORD)spu_taskset_wait_signal(0xA000, 0);
}

int main(void)
{
    ppu_vm_size = 0x40000;
    vm_base = calloc(1, ppu_vm_size);
    assert(vm_base);
    /* A live, idle instance isolates publication from policy execution. */
    s_inst[0].ea = 0x1000;
    s_inst[0].kernel_live = 1;
    assert(cellSpursCreateTaskset((CellSpurs*)0x1000, (CellSpursTaskset*)0x8000,
                                0x12345678, NULL, 1) == CELL_OK);
    assert(cellSpursCreateTaskset((CellSpurs*)0x1000, (CellSpursTaskset*)0x8000,
                                0, NULL, 1) != CELL_OK);
    u32 wid = vm_read32(0x8000 + CSTS_WID);
    u64 data = vm_read64(0x1000 + SPURS_WKL_INFO1 + wid * SPURS_WKL_INFO_SZ + 8);
    if (data != 0x8000) {
        fprintf(stderr, "FAIL: SPU workload lookup returns 0x%llX instead of taskset 0x8000\n", data);
        return 1;
    }
    assert(cellSpursCreateTaskset((CellSpurs*)0x1000, (CellSpursTaskset*)0xA000,
                                0, NULL, 1) == CELL_OK);
    u32 second = vm_read32(0xA000 + CSTS_WID);
    assert(second != wid);
    assert(vm_read64(0x1000 + SPURS_WKL_INFO1 + second * SPURS_WKL_INFO_SZ + 8) == 0xA000);
    assert(cellSpursAddWorkload((CellSpurs*)0x1000, (u32*)0x20000, (void*)0x21000,
                               16, 0x5678, NULL, 1, 1) == CELL_OK);
    assert(vm_read32(0x20000) != wid && vm_read32(0x20000) != second);
    assert(cellSpursShutdownTaskset((CellSpursTaskset*)0x8000) == CELL_OK);
    assert(cellSpursJoinTaskset((CellSpursTaskset*)0x8000) == CELL_OK);
    assert(vm_read64(0x1000 + SPURS_WKL_INFO1 + wid * SPURS_WKL_INFO_SZ + 8) == 0);
    assert(cellSpursCreateTaskset((CellSpurs*)0x1000, (CellSpursTaskset*)0xC000,
                                0, NULL, 1) == CELL_OK);
    assert(cellSpursJoinTaskset((CellSpursTaskset*)0x8000) != CELL_OK);
    assert(s_workloads[wid].taskset_ea == 0xC000 && s_workloads[wid].in_use);
    vm_write8(0xC000 + CSTS_ENABLED, 0x80);
    assert(cellSpursRemoveWorkload((CellSpurs*)0x1000, wid) != CELL_OK);
    assert(cellSpursDestroyTaskset((CellSpursTaskset*)0xC000) != CELL_OK);
    vm_write8(0xC000 + CSTS_ENABLED, 0);
    assert(cellSpursDestroyTaskset((CellSpursTaskset*)0xC000) == CELL_OK);
    for (unsigned i = 0; i < 32; ++i) {
        assert(cellSpursCreateTaskset((CellSpurs*)0x1000, (CellSpursTaskset*)0xE000,
                                    0, NULL, 1) == CELL_OK);
        assert(cellSpursDestroyTaskset((CellSpursTaskset*)0xE000) == CELL_OK);
    }
    assert(cellSpursCreateTaskset((CellSpurs*)0x1000, (CellSpursTaskset*)0x8000,
                                0, NULL, 1) == CELL_OK);
    assert(vm_read32(0x8000 + CSTS_WID) == wid);
    assert(!spu_taskset_consume_wake(0x8000));
    /* Producer arrives after the syscall's pre-check, before the actual wait. */
    spu_taskset_latch_wake(0x8000);
    HANDLE waiter = CreateThread(NULL, 0, wait_for_signal, NULL, 0, NULL);
    DWORD result = WaitForSingleObject(waiter, 100);
    if (result != WAIT_OBJECT_0) {
        spu_taskset_signal_task(0x8000, 0);
        WaitForSingleObject(waiter, 2000);
        CloseHandle(waiter);
        fprintf(stderr, "FAIL: WAIT_SIGNAL lost the wake between pre-check and park\n");
        return 1;
    }
    CloseHandle(waiter);
    assert(!spu_taskset_consume_wake(0x8000));
    const u32 queue = 0x22000, input = 0x23000, buffer = 0x24000;
    assert(_cellSpursLFQueueInitialize(0x1001, queue, buffer, 32, 280, 3) == CELL_OK);
    vm_write16(queue, 254);
    vm_write16(queue + 6, 254);
    vm_write16(queue + 8, 254);
    vm_write16(queue + 14, 254);
    vm_write16(queue + 4, 2); /* two registered consumers, neither notified */
    vm_write16(queue + 0x32, (u16)(wid << 8));
    vm_write16(queue + 0x34, (u16)(second << 8));
    vm_write32(input, 0x12345678);
    second_waiting = CreateEvent(NULL, TRUE, FALSE, NULL);
    HANDLE second_worker = CreateThread(NULL, 0, wait_for_second, NULL, 0, NULL);
    assert(WaitForSingleObject(second_waiting, 1000) == WAIT_OBJECT_0);
    assert(_cellSpursLFQueuePushBody(queue, input) == CELL_OK);
    u32 pack = vm_read16(queue + 4);
    if ((pack & 31) != 2 || ((pack >> 5) & 31) != 0 || ((pack >> 10) & 31) != 1) {
        fprintf(stderr, "FAIL: ANY2ANY producer must notify one consumer and preserve acknowledgements (0x%04X)\n", pack);
        return 1;
    }
    assert(WaitForSingleObject(second_worker, 50) == WAIT_TIMEOUT);
    /* First consumer enters its wait only after notification: it must not lose it. */
    waiter = CreateThread(NULL, 0, wait_for_signal, NULL, 0, NULL);
    assert(WaitForSingleObject(waiter, 1000) == WAIT_OBJECT_0);
    CloseHandle(waiter);
    vm_write32(input, 0xABCDEF01);
    assert(_cellSpursLFQueuePushBody(queue, input) == CELL_OK);
    assert(WaitForSingleObject(second_worker, 1000) == WAIT_OBJECT_0);
    CloseHandle(second_worker);
    CloseHandle(second_waiting);
    assert(vm_read16(queue + 4) == (2 | (2 << 10)));
    assert(vm_read16(queue + 8) == 256 && vm_read16(queue + 14) == 256);
    assert(vm_read32(buffer + 254 * 32) == 0x12345678);
    assert(vm_read32(buffer + 255 * 32) == 0xABCDEF01);
    assert(cellSpursQueuePopBody(queue, 0x23800, 0) == CELL_OK);
    assert(vm_read32(0x23800) == 0x12345678);
    assert(cellSpursQueuePopBody(queue, 0x23800, 0) == CELL_OK);
    assert(vm_read32(0x23800) == 0xABCDEF01);
    assert(cellSpursQueuePopBody(queue, 0x23800, 0) == CELL_SPURS_TASK_ERROR_AGAIN);

    assert(_cellSpursLFQueueInitialize(0x1001, queue, buffer, 32, 280, 3) == CELL_OK);
    vm_write16(queue + 4, (u16)(1 | (29 << 5) | (29 << 10)));
    vm_write16(queue + 0x32 + 28, (u16)(wid << 8));
    vm_write16(queue + 0x32, (u16)(second << 8));
    assert(_cellSpursLFQueuePushBody(queue, input) == CELL_OK);
    assert(vm_read16(queue + 4) == (1 | (29 << 5)));
    assert(spurs_bitset_test(0x8000 + CSTS_SIGNALLED, 0));
    assert(!spurs_bitset_test(0xA000 + CSTS_SIGNALLED, 0));
    assert(spu_taskset_wait_signal(0x8000, 0) == 0);
    assert(_cellSpursLFQueuePushBody(queue, input) == CELL_OK);
    assert(vm_read16(queue + 4) == (1 | (29 << 5) | (1 << 10)));
    assert(spu_taskset_wait_signal(0xA000, 0) == 0);
    assert(_cellSpursLFQueuePushBody(queue, input) == CELL_OK);
    assert(!spurs_bitset_test(0x8000 + CSTS_SIGNALLED, 0));
    assert(!spurs_bitset_test(0xA000 + CSTS_SIGNALLED, 0));

    assert(_cellSpursLFQueueInitialize(0x1001, queue, buffer, 32, 280, 3) == CELL_OK);
    vm_write16(queue + 4, 1);
    vm_write16(queue + 0x32, 0xFFFF);
    assert(_cellSpursLFQueuePushBody(queue, input) == CELL_SPURS_TASK_ERROR_STAT);
    assert(vm_read16(queue + 8) == 0 && vm_read16(queue + 14) == 0);
    assert(vm_read16(queue + 4) == 1);
    puts("PASS: taskset workload publication, distinct IDs, retirement and reuse");
    free(vm_base);
    return 0;
}
