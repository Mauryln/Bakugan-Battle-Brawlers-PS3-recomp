#include "ps3emu/asset_io.h"
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <vector>

int main(int argc, char** argv)
{
    const bool eviction = argc > 1;
    const auto path = std::filesystem::temp_directory_path() / "bakugan_asset_io_bench.bin";
    std::vector<unsigned char> contents(4 * 1024 * 1024);
    for (size_t i = 0; i < contents.size(); ++i) contents[i] = (unsigned char)((i * 31 + i / 997) & 255);
    FILE* f = std::fopen(path.string().c_str(), "wb");
    if (!f || std::fwrite(contents.data(), 1, contents.size(), f) != contents.size()) return 1;
    std::fclose(f);
    f = std::fopen(path.string().c_str(), "rb");
    if (!f) return 1;
    ps3_asset_io_attach(f, "/dev_bdvd/PS3_GAME/USRDIR/D00.ARC", 0);
    std::array<unsigned char, 256> bytes;
    // Fully warm the hit workload. Eviction workload cycles 64 blocks in 16 slots.
    if (!eviction) for (size_t off = 0; off < contents.size(); off += 65536)
        if (ps3_asset_io_read_at(f, bytes.data(), bytes.size(), off) != bytes.size()) return 1;
    const unsigned iterations = eviction ? 8192 : 500000;
    uint64_t checksum = 0;
    auto start = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < iterations; ++i) {
        size_t off = eviction ? (i % 64) * 65536ull : (i * 7919ull) % (contents.size() - bytes.size());
        if (ps3_asset_io_read_at(f, bytes.data(), bytes.size(), off) != bytes.size()) return 1;
        if (bytes[0] != contents[off] || bytes[255] != contents[off + 255]) return 1;
        checksum += bytes[0];
    }
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::printf("%s,%.3f,%llu\n", eviction ? "eviction" : "hits", ms, (unsigned long long)checksum);
    ps3_asset_io_detach(f);
    std::fclose(f);
    std::filesystem::remove(path);
}
