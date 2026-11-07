#include "ps3emu/asset_io.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

int main(int argc, char** argv)
{
    CHECK(argc == 3);
    const bool protected_cache = !std::strcmp(argv[1], "protected");
    const bool race = !std::strcmp(argv[1], "race");
    FILE* f = std::fopen(argv[2], "rb"); CHECK(f);
    if (protected_cache) {
        ps3_asset_io_attach(f, "/app_home/warmup", 0);
        unsigned char byte;
        for (unsigned i = 0; i < 16; ++i) CHECK(ps3_asset_io_read_at(f, &byte, 1, i * 65536ull) == 1);
        ps3_asset_io_detach(f); std::fclose(f);
        f = std::fopen(argv[2], "rb"); CHECK(f);
    }
    ps3_asset_io_attach(f, "/dev_bdvd/PS3_GAME/USRDIR/D00.ARC", 0);
    if (!std::strcmp(argv[1], "shutdown")) {
        ps3_asset_io_detach(f); std::fclose(f);
        std::puts("PASS trace shutdown with pending work");
        return 0;
    }
    if (!race) ps3_asset_io_wait_idle();
    ps3_asset_io_stats before{}, after{};
    ps3_asset_io_get_stats(&before);
    const bool replay = !std::strcmp(argv[1], "replay");
    if (replay) CHECK(before.preloaded_blocks == 4 && before.host_reads == 4);
    else if (!std::strcmp(argv[1], "truncated")) CHECK(before.preloaded_blocks == 3 && before.host_reads == 3);
    else if (protected_cache) CHECK(before.preloaded_blocks == 0 && before.host_reads == 16 && before.evictions == 0);
    else if (!race) CHECK(before.preloaded_blocks == 0 && before.host_reads == 0);
    std::array<unsigned char, 131082> bytes;
    const uint64_t offsets[] = {17, 17 * 65536ull};
    const size_t sizes[] = {bytes.size(), 256};
    for (unsigned k = 0; k < 2; ++k) {
        CHECK(ps3_asset_io_read_at(f, bytes.data(), sizes[k], offsets[k]) == sizes[k]);
        for (size_t i = 0; i < sizes[k]; ++i) {
            uint64_t p = offsets[k] + i;
            CHECK(bytes[i] == (unsigned char)((p * 31 + p / 997) & 255));
        }
    }
    if (!std::strcmp(argv[1], "limit"))
        for (unsigned i = 0; i < 4100; ++i) CHECK(ps3_asset_io_read_at(f, bytes.data(), 1, 17) == 1);
    ps3_asset_io_wait_idle();
    CHECK(std::ftell(f) == 0);
    ps3_asset_io_get_stats(&after);
    if (replay) CHECK(after.host_reads == before.host_reads && after.preload_hits == 4);
    if (race) CHECK(after.host_reads == 4);
    CHECK(after.resident_bytes <= after.budget_bytes);
    ps3_asset_io_detach(f); std::fclose(f);
    std::printf("PASS trace %s: preloaded=%llu host_reads=%llu\n", argv[1],
        (unsigned long long)after.preloaded_blocks, (unsigned long long)after.host_reads);
}
