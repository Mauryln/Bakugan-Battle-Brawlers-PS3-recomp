#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef int32_t s32;
typedef uint32_t u32;
typedef uint64_t u64;

#define CELL_OK 0
#define CELL_AUDIO_ERROR_NOT_INIT -1
#define CELL_AUDIO_ERROR_PARAM -2
#define CELL_AUDIO_ERROR_EVENT_QUEUE -3
#define CELL_AUDIO_MAX_NOTIFY_EVENT_QUEUES 8
#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "FAIL: %s (line %d)\n", #expr, __LINE__); exit(1); } } while (0)

typedef struct { int in_use; u64 key; } AudioNotifySlot;
static AudioNotifySlot s_notify_queues[CELL_AUDIO_MAX_NOTIFY_EVENT_QUEUES];
static int s_audio_initialized = 1;
static CRITICAL_SECTION s_audio_mutex;
static volatile LONG s_audio_mutex_held = 0;
static u64 created_key;
static u64 published_key;
static u32 published_id;
static unsigned posted;
static unsigned create_calls;

/* Real bounded FIFO simulation matching cellAudio's 8-slot queue behavior. */
static CRITICAL_SECTION s_q_lock;
static unsigned s_q_count;
static unsigned s_q_cap = 8;
static unsigned s_burst_pushed;
static unsigned s_burst_rejected;
static int s_use_bounded_fifo;

static void mutex_lock(CRITICAL_SECTION* lock)
{
    EnterCriticalSection(lock);
    InterlockedExchange(&s_audio_mutex_held, 1);
}
static void mutex_unlock(CRITICAL_SECTION* lock)
{
    InterlockedExchange(&s_audio_mutex_held, 0);
    LeaveCriticalSection(lock);
}
static void vm_write32(u32 address, u32 value) { CHECK(address == 4); published_id = value; }
static void vm_write64(u32 address, u64 value) { CHECK(address == 8); published_key = value; }
static u32 sys_event_queue_create_direct(u64 key, int size)
{
    CHECK(!InterlockedCompareExchange(&s_audio_mutex_held, 0, 0));
    CHECK(size == 8);
    created_key = key;
    ++create_calls;
    return 17 + create_calls - 1u;
}
static u32 sys_event_find_queue_by_key(u64 key) { return key == created_key ? 17u : 0u; }
static int sys_event_queue_push_by_id(u32 id, u64 key, u64 d1, u64 d2, u64 d3)
{
    CHECK(!InterlockedCompareExchange(&s_audio_mutex_held, 0, 0));
    CHECK(id == 17 && key == created_key && !(d1 | d2 | d3));
    if (s_use_bounded_fifo) {
        EnterCriticalSection(&s_q_lock);
        if (s_q_count >= s_q_cap) {
            ++s_burst_rejected;
            LeaveCriticalSection(&s_q_lock);
            return -1;
        }
        ++s_q_count;
        ++s_burst_pushed;
        LeaveCriticalSection(&s_q_lock);
        return 0;
    }
    ++posted;
    return 0;
}

#include "audio_notify.inc"

typedef struct {
    volatile LONG stop;
    unsigned drained;
} BurstConsumer;

static DWORD WINAPI slow_consumer_thread(LPVOID arg)
{
    BurstConsumer* c = (BurstConsumer*)arg;
    while (!InterlockedCompareExchange((LONG*)&c->stop, 0, 0)) {
        EnterCriticalSection(&s_q_lock);
        if (s_q_count > 0) {
            /* CSmfPlayer pops 1 notification per ~20ms tick (~1/4 mix rate). */
            --s_q_count;
            ++c->drained;
        }
        LeaveCriticalSection(&s_q_lock);
        Sleep(2);
    }
    return 0;
}

int main(void)
{
    InitializeCriticalSection(&s_audio_mutex);
    InitializeCriticalSection(&s_q_lock);

    CHECK(cellAudioCreateNotifyEventQueue((u32*)(uintptr_t)4, (u64*)(uintptr_t)8) == CELL_OK);
    CHECK(published_id == 17 && published_key == created_key);
    audio_notify_event_queues();
    CHECK(posted == 1);
    CHECK(cellAudioSetNotifyEventQueue(published_key) == CELL_OK);
    audio_notify_event_queues();
    CHECK(posted == 2);
    CHECK(cellAudioRemoveNotifyEventQueue(published_key) == CELL_OK);
    audio_notify_event_queues();
    CHECK(posted == 2);
    CHECK(cellAudioSetNotifyEventQueue(published_key) == CELL_OK);
    CHECK(cellAudioSetNotifyEventQueue(published_key) == CELL_OK);
    audio_notify_event_queues();
    CHECK(posted == 3);

    /* Saturate remaining notify slots and ensure overflow does not leak queues. */
    u64 first_key = created_key;
    u32 before_create = create_calls;
    for (int i = 1; i < CELL_AUDIO_MAX_NOTIFY_EVENT_QUEUES; ++i) {
        CHECK(cellAudioCreateNotifyEventQueue((u32*)(uintptr_t)4, (u64*)(uintptr_t)8) == CELL_OK);
    }
    CHECK(create_calls == before_create + (CELL_AUDIO_MAX_NOTIFY_EVENT_QUEUES - 1));
    CHECK(cellAudioCreateNotifyEventQueue((u32*)(uintptr_t)4, (u64*)(uintptr_t)8) == CELL_AUDIO_ERROR_PARAM);
    CHECK(create_calls == before_create + (CELL_AUDIO_MAX_NOTIFY_EVENT_QUEUES - 1));
    for (int i = 1; i < CELL_AUDIO_MAX_NOTIFY_EVENT_QUEUES; ++i) {
        s_notify_queues[i].in_use = 0;
    }
    created_key = first_key;

    /* Rapid SFX/mix burst saturating the 8-slot FIFO while a slow consumer drains. */
    s_use_bounded_fifo = 1;
    s_q_count = 0;
    s_burst_pushed = 0;
    s_burst_rejected = 0;

    for (unsigned i = 0; i < 64; ++i) {
        audio_notify_event_queues();
    }
    CHECK(s_q_count == 8);
    CHECK(s_burst_pushed == 8);
    CHECK(s_burst_rejected == 56);

    BurstConsumer consumer = {0, 0};
    HANDLE th = CreateThread(NULL, 0, slow_consumer_thread, &consumer, 0, NULL);
    CHECK(th != NULL);
    for (unsigned i = 0; i < 400; ++i) {
        audio_notify_event_queues();
        if ((i % 25) == 0) Sleep(1);
    }
    InterlockedExchange((LONG*)&consumer.stop, 1);
    CHECK(WaitForSingleObject(th, 2000) == WAIT_OBJECT_0);
    CloseHandle(th);

    CHECK(s_q_count <= s_q_cap);
    CHECK(consumer.drained > 0);
    CHECK(s_burst_pushed == 8 + consumer.drained + s_q_count - 8);
    CHECK(s_burst_rejected > 0);

    puts("PASS: create-only, create-plus-set, slot cap, and saturated 8-slot SFX burst never block");
    DeleteCriticalSection(&s_q_lock);
    DeleteCriticalSection(&s_audio_mutex);
    return 0;
}
