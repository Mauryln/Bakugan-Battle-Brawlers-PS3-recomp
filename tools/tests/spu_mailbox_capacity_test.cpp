#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>
#include "ps3emu/error_codes.h"
#include "spu_context.h"

struct ppu_context { uint64_t gpr[32] = {}; };
struct spu_thread_t {
    void* live_ctx = nullptr;
    spu_context* sctx = nullptr;
    uint32_t pending_inmbox = 0, tid = 0x2001, args_ea = 0, args_size = 0;
    void* fb_user = nullptr;
    int64_t exit_status = 0;
    int64_t (*fb_handler)(uint32_t, uint32_t, uint32_t, void*) = nullptr;
};
static spu_context worker;
static spu_thread_t thread_info;
static unsigned sleep_calls, wake_calls, consume_on_sleep;
static uint32_t consumed;

static spu_thread_t* spu_find_thread(uint32_t tid)
{
    return tid == thread_info.tid ? &thread_info : nullptr;
}

extern "C" void spu_ch_wake(spu_context*) { ++wake_calls; }

static void mailbox_sleep(unsigned)
{
    ++sleep_calls;
    if (consume_on_sleep && sleep_calls == consume_on_sleep)
        consumed = spu_channel_read(&worker.ch_in_mbox);
}
#define Sleep mailbox_sleep
#include "spu_mailbox_handler.inc"
#undef Sleep

static void require(bool condition, const char* message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s (queued=%u, sleeps=%u, wakes=%u)\n",
            message, worker.ch_in_mbox.count, sleep_calls, wake_calls);
        exit(1);
    }
}

static int64_t send(uint32_t word)
{
    ppu_context ppu;
    ppu.gpr[3] = thread_info.tid;
    ppu.gpr[4] = word;
    int64_t result = sys_spu_thread_write_spu_mb_handler(&ppu);
    require(ppu.gpr[3] == static_cast<uint64_t>(result),
        "syscall return register must agree with returned status");
    return result;
}

static void reset()
{
    spu_context_init(&worker, thread_info.tid);
    thread_info.live_ctx = &worker;
    sleep_calls = wake_calls = consume_on_sleep = consumed = 0;
}

int main()
{
    reset();
    const uint32_t words[] = {0x00786F00, 0x39, 0, 0xDEADBEEF};
    for (uint32_t word : words) {
        require(send(word) == CELL_OK,
            "four unread mailbox words must be accepted without waiting for the consumer");
        require(sleep_calls == 0, "an inbox with free hardware slots must not sleep");
    }
    require(worker.ch_in_mbox.count == 4, "all four accepted words must remain queued");
    require(send(0xBAD) == static_cast<int32_t>(CELL_EBUSY),
        "a full hardware inbox must reject a fifth unread word with bounded backpressure");
    require(sleep_calls > 0 && sleep_calls <= 250, "full-inbox waiting must be bounded");
    require(worker.ch_in_mbox.count == 4, "a rejected write must not destroy accepted words");
    require(spu_channel_read(&worker.ch_in_mbox) == words[0], "inbox must preserve its first word");
    sleep_calls = 0;
    require(send(0x1234) == CELL_OK && sleep_calls == 0,
        "one consumed word must immediately free capacity for the next command");
    for (unsigned i = 1; i < 4; ++i)
        require(spu_channel_read(&worker.ch_in_mbox) == words[i], "queued command order must be FIFO");
    require(spu_channel_read(&worker.ch_in_mbox) == 0x1234,
        "new command must follow the previously accepted commands");

    reset();
    for (uint32_t word : words) require(send(word) == CELL_OK, "refill must succeed");
    consume_on_sleep = 3;
    require(send(0x5555) == CELL_OK, "a waiting sender must resume when a consumer frees a slot");
    require(consumed == words[0] && worker.ch_in_mbox.count == 4,
        "resumed sender must enqueue exactly once after consumption");
    for (unsigned i = 1; i < 4; ++i)
        require(spu_channel_read(&worker.ch_in_mbox) == words[i], "waiting must not reorder older commands");
    require(spu_channel_read(&worker.ch_in_mbox) == 0x5555, "resumed command must be last");

#ifdef HAVE_CHANNEL_TRY_WRITE
    // Two producers must not both observe one free slot and overfill the inbox.
    spu_channel channel = {};
    std::atomic<bool> start = false;
    std::atomic<unsigned> accepted = 0;
    std::vector<std::thread> producers;
    for (unsigned i = 0; i < 16; ++i) producers.emplace_back([&, i] {
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        if (spu_channel_try_write(&channel, i, SPU_IN_MBOX_HW_DEPTH)) ++accepted;
    });
    start.store(true, std::memory_order_release);
    for (auto& producer : producers) producer.join();
    require(accepted == 4 && channel.count == 4,
        "concurrent senders must accept exactly the hardware capacity");
    unsigned seen = 0;
    while (spu_channel_has_data(&channel)) {
        unsigned word = spu_channel_read(&channel);
        require(word < 16 && !(seen & (1u << word)), "concurrent writes must not duplicate or overwrite commands");
        seen |= 1u << word;
    }
#endif
    puts("PASS: real SPU mailbox syscall preserves four-word capacity, FIFO and bounded backpressure");
}
