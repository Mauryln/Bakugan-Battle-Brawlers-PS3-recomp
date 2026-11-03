#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include "spu_context.h"

// Only unrelated scheduling, DMA and tracing are stubbed. The channel queues,
// readiness predicate, Windows CV wait, rdch and rchcnt come from runtime code.
uint8_t* vm_base = nullptr;
#define PS3_FT_ON() 0
#define SPU_DECREMENTER_HZ 79800000ull
#define MFC_QUEUE_DEPTH 16
static uint64_t ps3_ft_now() { return 0; }
static void spu_ft_wait_span(spu_context*, uint32_t, uint64_t) {}
static void yz_lockstep_block_begin(spu_context*) {}
static void yz_lockstep_block_end(spu_context*) {}
static int taskset_shutdown_checkpoint(spu_context*) { return 0; }
static int channel_is_mfc(uint32_t) { return 0; }
struct MfcStub { uint32_t queue_count = 0; };
static MfcStub dma;
static MfcStub* mfc_for(spu_context*) { return &dma; }
static uint32_t mfc_channel_read(MfcStub*, spu_context*, uint32_t) { return 0; }
static uint64_t spu_host_ns() { return GetTickCount64() * 1000000ull; }
extern "C" void spu_halt(spu_context*) { abort(); }
uint32_t spu_rchcnt(spu_context*, uint32_t);

#include "spu_blocking_read.inc"

static uint32_t read_word(spu_context* ctx)
{
    u128 value = spu_rdch(ctx, SPU_RdInMbox);
    return spu_preferred_u32(&value);
}

static void require(bool condition, const char* message)
{
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}

static spu_context group_worker, scheduler_job, other_worker;

static void verify_delayed_message(spu_context* ctx, const char* label)
{
    // A drained queue retains its old head value. Empty rdch must not invent
    // either zero or that stale word while the producer has not sent anything.
    spu_channel_write(&ctx->ch_in_mbox, 0xDEADBEEF);
    require(spu_channel_read(&ctx->ch_in_mbox) == 0xDEADBEEF, "seed must drain");
    HANDLE entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE completed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    require(entered && completed, "test events must be created");
    uint32_t word = 0;
    std::thread reader([&] {
        SetEvent(entered);
        word = read_word(ctx);
        SetEvent(completed);
    });
    require(WaitForSingleObject(entered, 1000) == WAIT_OBJECT_0, "reader must start");
    bool returned_early = WaitForSingleObject(completed, 20) == WAIT_OBJECT_0;
    spu_channel_write(&ctx->ch_in_mbox, 0x39);
    spu_ch_wake(ctx);
    DWORD finished = WaitForSingleObject(completed, 1000);
    if (finished != WAIT_OBJECT_0) abort();
    reader.join();
    CloseHandle(entered); CloseHandle(completed);
    if (returned_early)
        fprintf(stderr, "%s: empty rdch returned 0x%08X before producer delivery\n", label, word);
    require(!returned_early, "real blocking read must wait until the delayed mailbox producer delivers data");
    require(word == 0x39 && ctx->ch_in_mbox.count == 0,
        "rdch must consume the delivered command exactly once");
    require(ctx->status == SPU_STATUS_RUNNING, "woken worker must return to running state");
}

int main()
{
    spu_context_init(&group_worker, 0x2001);
    group_worker.spu_group_id = 0x1001;
    verify_delayed_message(&group_worker, "LV2 group worker");

    // Valid zero-valued commands are data, and adjacent messages keep FIFO order.
    spu_channel_write(&group_worker.ch_in_mbox, 0x00786F00);
    spu_channel_write(&group_worker.ch_in_mbox, 0);
    require(spu_rchcnt(&group_worker, SPU_RdInMbox) == 2, "rchcnt must report queued messages without consuming them");
    require(read_word(&group_worker) == 0x00786F00,
        "first queued command must remain first");
    require(read_word(&group_worker) == 0,
        "a real zero-valued command must be delivered");
    require(spu_rchcnt(&group_worker, SPU_RdInMbox) == 0,
        "empty rchcnt must remain a nonblocking readiness probe");

    spu_context_init(&scheduler_job, 0);
    require(read_word(&scheduler_job) == 0,
        "group-zero scheduler contexts must retain legacy no-producer behavior");
    require(scheduler_job.status != SPU_STATUS_WAITING_CHANNEL,
        "scheduler job must not be parked without a producer");
    g_spu_force_ch_block = 1;
    verify_delayed_message(&scheduler_job, "explicitly forced scheduler job");
    g_spu_force_ch_block = 0;

    spu_context_init(&other_worker, 0x2002);
    other_worker.spu_group_id = 0x1002;
    spu_channel_write(&other_worker.ch_in_mbox, 0xABCDEF);
    require(spu_rchcnt(&group_worker, SPU_RdInMbox) == 0,
        "another SPU's input must not make this worker readable");
    require(read_word(&other_worker) == 0xABCDEF,
        "independent worker must consume only its own input");
    puts("PASS: LV2 group rdch waits for real input; FIFO, zero data, nonblocking rchcnt and scheduler scope remain correct");
}
