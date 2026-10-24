#include "cellAudio.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
typedef struct { long long QuadPart; } LARGE_INTEGER;
typedef unsigned long long ULONGLONG;
static long long now;
static volatile int s_mix_thread_running = 1;
static unsigned blocks_mixed;
static long long times[16];
static float s_mix_buffer[CELL_AUDIO_BLOCK_SAMPLES * 2];
static unsigned long long g_audio_dropped;
static void QueryPerformanceFrequency(LARGE_INTEGER* value) { value->QuadPart = 1000000; }
static void QueryPerformanceCounter(LARGE_INTEGER* value) { value->QuadPart = now; }
static void Sleep(unsigned ms) { now += ms * 1000; }
static ULONGLONG GetTickCount64(void) { return now / 1000; }
/* A device with room available must not cause a burst of guest audio ticks. */
static int audio_backend_room(void) { return 4096; }
static void audio_backend_wait(void) { Sleep(1); }
static void audio_backend_submit(const float* data, u32 count) { (void)data; (void)count; }
static void audio_notify_event_queues(void) {}
static void audio_mix_one_block(void)
{
    times[blocks_mixed++] = now;
    if (blocks_mixed == 16) s_mix_thread_running = 0;
}
#include "audio_clock_entry.inc"
int main(void)
{
    audio_mix_thread_func(NULL);
    assert(blocks_mixed == 16);
    for (unsigned i = 1; i < 16; i++) {
        assert(times[i] - times[i - 1] >= 5000);
        assert(times[i] - times[i - 1] <= 6000);
    }
    assert(times[15] >= 80000 && times[15] <= 81000);
    puts("PASS: guest audio stays at 256/48000 seconds per block even when WASAPI has spare capacity");
}
