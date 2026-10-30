#include <cstdio>
#include <cstdlib>

// Stand-ins needed only when extracting the old catch-up loop as the RED baseline.
static void cellGcmTickVBlank() {}
static void cellGcmTickFlip() {}
static int cellGcm_take_flip_pending() { return 0; }
static void present_guest_frame() {}
static unsigned cellGcm_flip_request_count() { return 0; }
static void cellGcm_rsx_process_fifo() {}
#include "frame_clock.inc"

static void expect(unsigned long long now, unsigned long long* next,
                   unsigned expected_ticks, unsigned long long expected_next)
{
    unsigned ticks = frame_clock_due_ticks(now, next);
    if (ticks != expected_ticks || *next != expected_next) {
        fprintf(stderr, "FAIL: now=%llu ticks=%u next=%llu expected ticks=%u next=%llu\n",
                now, ticks, *next, expected_ticks, expected_next);
        exit(1);
    }
}

int main()
{
    unsigned long long next = 0;
    expect(0, &next, 1, 16);
    expect(15, &next, 0, 16);
    expect(16, &next, 1, 32);
    expect(63, &next, 2, 64);
    expect(64, &next, 1, 80);
    expect(128, &next, 4, 144); // the full bounded catch-up window
    next = 80;
    expect(144, &next, 4, 160); // five ticks overdue: cap and resync
    next = 100;
    expect(610, &next, 4, 626); // half-second compilation stall
    expect(610, &next, 0, 626); // no accumulated debt on the next iteration
    expect(625, &next, 0, 626);
    expect(626, &next, 1, 642);
    next = 0;
    expect(5000, &next, 4, 5016); // never replay 240 FIFO-draining ticks
    expect(5000, &next, 0, 5016);
    expect(5016, &next, 1, 5032);
    puts("PASS: frame clock caps replay at four ticks and discards long-stall debt");
}
