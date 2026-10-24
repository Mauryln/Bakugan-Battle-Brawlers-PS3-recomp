#include "spu_context.h"
#include "sys_event.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/* Only the surrounding LV2 table is a fixture. The three tested handlers
 * and the actual channel STOP entrypoint are included verbatim by the runner. */
typedef struct {
    uint32_t tid;
    struct { uint32_t spuq, queue; } receive_bind[8];
    int receive_bind_n;
} spu_thread_t;
static spu_thread_t test_thread = { .tid = 0x2000 };
static SRWLOCK s_spu_port_lock = SRWLOCK_INIT;
static CONDITION_VARIABLE s_spu_port_bound = CONDITION_VARIABLE_INIT;
static spu_thread_t* spu_find_thread(uint32_t tid) {
    return tid == test_thread.tid ? &test_thread : NULL;
}
int (*g_spu_stop_hook)(spu_context*);
void spu_halt(spu_context* ctx) { (void)ctx; abort(); }
#include "receive_entry.inc"

uint8_t* vm_base;
uint32_t ppu_vm_size;
sys_event_queue_info g_sys_event_queues[SYS_EVENT_QUEUE_MAX];
static unsigned received;
int64_t sys_event_queue_receive(ppu_context* ctx)
{
    assert(ctx->gpr[3] == 17);
    received++;
    ctx->gpr[5] = 0x1122334455667788ull;
    ctx->gpr[6] = 0xAABBCCDD;
    ctx->gpr[7] = 0x12345678;
    return 0;
}

int main(void)
{
    spu_thread_t* thread = &test_thread;
    assert(thread);
    ppu_context call = {0};
    call.gpr[3] = thread->tid;
    call.gpr[4] = 17;
    call.gpr[5] = 0x1234ABCD; /* Inbound queue keys are arbitrary u32, not port IDs. */
    g_sys_event_queues[16].active = 1;
    g_sys_event_queues[16].type = SYS_SPU_QUEUE;
    assert(sys_spu_thread_bind_queue_handler(&call) == 0);
    assert(call.gpr[3] == 0); /* LV2 handlers must also publish the guest result. */
    static spu_context spu;
    spu.spu_id = thread->tid;
    spu.stop_code = 0x110;
    spu.status = SPU_STATUS_STOPPED_BY_STOP;
    spu_channel_write(&spu.ch_out_mbox, 0x1234ABCD);
    g_spu_stop_hook = spu_service_stop;
    spu_stop(&spu);
    assert(spu.status == SPU_STATUS_RUNNING);
    assert(received == 1 && spu.ch_out_mbox.count == 0);
    assert(spu.rcv_evt_n == 4 && spu.rcv_evt[0] == 0);
    assert(spu.rcv_evt[1] == 0x55667788);
    assert(spu.rcv_evt[2] == 0xAABBCCDD && spu.rcv_evt[3] == 0x12345678);
    call.gpr[3] = thread->tid;
    call.gpr[4] = 0x1234ABCD;
    assert(sys_spu_thread_unbind_queue_handler(&call) == 0);
    assert(call.gpr[3] == 0);
    spu.rcv_evt_n = 0;
    spu_channel_write(&spu.ch_out_mbox, 0x1234ABCD);
    assert(spu_service_stop(&spu));
    assert(spu.rcv_evt_n == 1 && spu.rcv_evt[0] == CELL_EINVAL);
    assert(received == 1);
    puts("PASS: bound SPU queue receives event payload, resumes, and unbind rejects later receives");
}
