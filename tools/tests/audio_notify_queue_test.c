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
#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "FAIL: %s\n", #expr); exit(1); } } while (0)

typedef struct { int in_use; u64 key; } AudioNotifySlot;
static AudioNotifySlot s_notify_queues[CELL_AUDIO_MAX_NOTIFY_EVENT_QUEUES];
static int s_audio_initialized = 1;
static int s_audio_mutex;
static u64 created_key;
static u64 published_key;
static u32 published_id;
static unsigned posted;

static void mutex_lock(int* lock) { CHECK(!*lock); *lock = 1; }
static void mutex_unlock(int* lock) { CHECK(*lock); *lock = 0; }
static void vm_write32(u32 address, u32 value) { CHECK(address == 4); published_id = value; }
static void vm_write64(u32 address, u64 value) { CHECK(address == 8); published_key = value; }
static u32 sys_event_queue_create_direct(u64 key, int size)
{
    CHECK(size == 8);
    created_key = key;
    return 17;
}
static u32 sys_event_find_queue_by_key(u64 key) { return key == created_key ? 17u : 0u; }
static int sys_event_queue_push_by_id(u32 id, u64 key, u64 d1, u64 d2, u64 d3)
{
    CHECK(!s_audio_mutex);
    CHECK(id == 17 && key == created_key && !(d1 | d2 | d3));
    ++posted;
    return 0;
}

#include "audio_notify.inc"

int main(void)
{
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
    puts("PASS: create-only and create-plus-set post one event per audio block");
    return 0;
}
