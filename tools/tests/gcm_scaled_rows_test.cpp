// Include the actual gcm_scaled_row_copy extracted from cellGcmSys.c.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using u8 = uint8_t;
using u32 = uint32_t;
using u64 = uint64_t;
static u8 actual[65536], expected[65536];
u8* vm_base = actual;
static int g_resv_store_active, locked, lock_count, notify_count, arm_on_lock;
static int reserved_mode, checks;
static u32 g_ww_lo, g_ww_hi;
static u64 s_scaled_rows, s_scaled_pixels;
static void require(bool ok, const char* why)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}
static void spu_lockline_lock()
{
    require(!locked, "no recursive lock"); locked = 1; ++lock_count;
    if (arm_on_lock) g_resv_store_active = 1;
}
static void spu_lockline_unlock() { require(locked, "balanced lock"); locked = 0; }
static int spu_coh_is_reserved(u32 line)
{
    ++checks;
    if (reserved_mode == 1) return checks > 1;
    if (reserved_mode == 2) return 0;
    return (line & 128u) != 0;
}
static void spu_coh_notify_write(u32) { require(locked, "notification under lock"); ++notify_count; }
#include "scaled_rows.inc"

static u32 rng = 0x12345678;
static u32 random_value() { rng = rng * 1664525u + 1013904223u; return rng; }
static void reset()
{
    for (u32 i = 0; i < sizeof(actual); ++i) actual[i] = (u8)(i * 73u + i / 257u);
    std::memcpy(expected, actual, sizeof(actual));
}
static void reference_row(u32 d, u32 s, u32 n, u32 step)
{
    for (u32 x = 0; x < n; ++x) {
        u32 pixel;
        std::memcpy(&pixel, expected + s + x * step, 4);
        std::memcpy(expected + d + x * 4, &pixel, 4);
    }
}
int main()
{
    for (u32 trial = 0; trial < 10000; ++trial) {
        reset();
        u32 n = 1 + random_value() % 512, step = (trial & 1) ? 4 : 8;
        u32 s = random_value() % 60000, d = random_value() % 60000;
        // Frequently overlap in both directions, including unaligned words.
        if (trial % 3 == 0) d = s + 1 + trial % 127;
        if (trial % 3 == 1 && s > 128) d = s - 1 - trial % 127;
        reference_row(d, s, n, step);
        require(gcm_scaled_row_copy(d, s, n, step), "eligible row selected");
        require(!std::memcmp(actual, expected, sizeof(actual)), "forward byte order and overlap");
        require(!locked, "row unlocks");
    }
    for (u32 trial = 0; trial < 1000; ++trial) {
        reset();
        u32 ox = random_value() % 8, oy = random_value() % 8;
        u32 w = 1 + random_value() % 32, h = 1 + random_value() % 32;
        u32 cx = random_value() % 16, cy = random_value() % 16;
        u32 ex = cx + 1 + random_value() % 32, ey = cy + 1 + random_value() % 32;
        u32 u = random_value() % 32, v = random_value() % 32;
        u32 ds = (trial & 1) ? 0x100000 : 0x200000;
        u32 dt = (trial & 2) ? 0x100000 : 0x200000;
        for (u32 y = 0; y < h; ++y) {
            u32 dy = oy + y;
            if (dy < cy || dy >= ey) continue;
            u32 sy = (u32)(((u64)v * 65536 + (u64)y * dt) >> 20);
            for (u32 x = 0; x < w; ++x) {
                u32 dx = ox + x;
                if (dx < cx || dx >= ex) continue;
                u32 sx = (u32)(((u64)u * 65536 + (u64)x * ds) >> 20);
                reference_row(32768 + dy * 256 + dx * 4, sy * 256 + sx * 4, 1, 4);
            }
            u32 xa = ox < cx ? cx - ox : 0, xb = w;
            if ((u64)ox + xb > ex) xb = ex > ox ? ex - ox : 0;
            if (xa >= xb) continue;
            u32 step = (ds >> 20) * 4;
            u64 sx = ((u64)u * 65536 >> 20) + (u64)xa * (step / 4);
            require(gcm_scaled_row_copy(32768 + (u64)dy * 256 + (ox + xa) * 4,
                                        (u64)sy * 256 + sx * 4, xb - xa, step), "clipped row");
        }
        require(!std::memcmp(actual, expected, sizeof(actual)), "clip and fractional origin preserve all bytes");
    }
    reset();
    int before = lock_count;
    require(!gcm_scaled_row_copy(0xFFFFFFFCull, 0, 2, 4), "destination wrap falls back");
    require(!gcm_scaled_row_copy(0, 0xFFFFFFFCull, 2, 8), "source wrap falls back");
    require(!gcm_scaled_row_copy(0, 0, 2049, 4), "long row falls back");
    require(!gcm_scaled_row_copy(0, 0, 10, 12), "unsupported step falls back");
    g_resv_store_active = 1;
    require(!gcm_scaled_row_copy(0, 128, 10, 4), "PPU reservations fall back");
    g_resv_store_active = 0; g_ww_hi = 1;
    require(!gcm_scaled_row_copy(0, 128, 10, 4), "write watch falls back");
    g_ww_hi = 0;
    require(lock_count == before, "ineligible rows never lock or write");
    arm_on_lock = 1;
    require(!gcm_scaled_row_copy(128, 0, 10, 4) && !locked, "reservation recheck unlocks");
    require(!std::memcmp(actual, expected, sizeof(actual)), "fallback paths leave memory unchanged");
    require(notify_count > 0, "reserved lines notified");
    arm_on_lock = 0; g_resv_store_active = 0; reserved_mode = 2;
    before = lock_count;
    require(gcm_scaled_row_copy(0, 128, 10, 4) && lock_count == before,
            "unreserved rows avoid global lock");
    reserved_mode = 1; checks = 0;
    int notifications = notify_count;
    require(gcm_scaled_row_copy(0, 128, 10, 4) && lock_count == before + 1 &&
            notify_count == notifications + 1 && !locked,
            "reservation appearing during copy is notified under lock");
    std::puts("PASS: 10000 overlapping rows, 1000 clipped blits, bounds and coherent fallbacks");
}
