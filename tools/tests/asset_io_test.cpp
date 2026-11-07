#include "ps3emu/asset_io.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <thread>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

int main()
{
    const auto path = std::filesystem::temp_directory_path() / "bakugan_asset_io_test.bin";
    std::vector<unsigned char> expected(3 * 1024 * 1024 + 19);
    for (size_t i = 0; i < expected.size(); ++i) expected[i] = (unsigned char)((i * 31 + i / 997) & 255);
    FILE* writer = std::fopen(path.string().c_str(), "wb");
    CHECK(writer);
    CHECK(std::fwrite(expected.data(), 1, expected.size(), writer) == expected.size());
    std::fclose(writer);
    FILE* a = std::fopen(path.string().c_str(), "rb");
    FILE* b = std::fopen(path.string().c_str(), "rb");
    CHECK(a && b);
    ps3_asset_io_attach(a, "/dev_bdvd/PS3_GAME/USRDIR/D00.ARC", 0);
    ps3_asset_io_attach(b, "/app_home/D00.ARC", 0);
    std::array<unsigned char, 70000> data;
    CHECK(ps3_asset_io_read(a, data.data(), data.size()) == data.size());
    CHECK(!std::memcmp(data.data(), expected.data(), data.size()));
    CHECK(std::ftell(a) == (long)data.size());
    ps3_asset_io_stats before{}, after{};
    ps3_asset_io_get_stats(&before);
    CHECK(ps3_asset_io_read_at(b, data.data(), 1000, 123) == 1000);
    CHECK(!std::memcmp(data.data(), expected.data() + 123, 1000));
    CHECK(std::ftell(b) == 0);
    ps3_asset_io_get_stats(&after);
    if (std::getenv("PS3_IO_TIMING") && std::strcmp(std::getenv("PS3_IO_TIMING"), "0"))
        CHECK(after.demand_reads == before.demand_reads + 1);
    const bool enabled = std::getenv("PS3_IO_CACHE_MB") == nullptr || std::strcmp(std::getenv("PS3_IO_CACHE_MB"), "0");
    if (enabled) CHECK(after.cache_hits > before.cache_hits && after.host_reads == before.host_reads);
    // Different physical files with the same guest name, size and offset cannot alias.
    const auto other_path = path.string() + ".other";
    auto other_contents = expected;
    other_contents[123] ^= 0x80;
    writer = std::fopen(other_path.c_str(), "wb"); CHECK(writer);
    CHECK(std::fwrite(other_contents.data(), 1, other_contents.size(), writer) == other_contents.size());
    std::fclose(writer);
    FILE* other = std::fopen(other_path.c_str(), "rb"); CHECK(other);
    ps3_asset_io_attach(other, "/dev_bdvd/PS3_GAME/USRDIR/D00.ARC", 0);
    CHECK(ps3_asset_io_read_at(other, data.data(), 1, 123) == 1);
    CHECK(data[0] == other_contents[123]);
    ps3_asset_io_detach(other); std::fclose(other);
    std::filesystem::remove(other_path);
    // Reads spanning blocks, concurrent absolute reads, bounded eviction.
    std::vector<std::thread> threads;
    for (unsigned t = 0; t < 4; ++t) threads.emplace_back([&, t] {
        std::array<unsigned char, 1301> local;
        for (unsigned i = 0; i < 600; ++i) {
            size_t off = ((i * 7919ull + t * 97) % (expected.size() - local.size()));
            CHECK(ps3_asset_io_read_at(t & 1 ? a : b, local.data(), local.size(), off) == local.size());
            CHECK(!std::memcmp(local.data(), expected.data() + off, local.size()));
        }
    });
    for (auto& thread : threads) thread.join();
    CHECK(std::ftell(a) == (long)data.size() && std::ftell(b) == 0);
    ps3_asset_io_get_stats(&after);
    CHECK(after.resident_bytes <= after.budget_bytes);
    if (enabled && after.budget_bytes == 1024 * 1024) CHECK(after.evictions > 0);
    if (enabled) CHECK(after.buffer_allocations <= after.budget_bytes / 65536);
    CHECK(ps3_asset_io_read_at(a, data.data(), 100, expected.size() - 7) == 7);
    CHECK(!std::memcmp(data.data(), expected.data() + expected.size() - 7, 7));
    CHECK(ps3_asset_io_read_at(a, data.data(), 1, UINT64_MAX) == 0);
    CHECK(ps3_asset_io_read_at(a, data.data(), 1, (1ull << 32) + 123) == 0);
    CHECK(std::fseek(a, (long)expected.size() - 7, SEEK_SET) == 0);
    CHECK(ps3_asset_io_read(a, data.data(), 100) == 7 && std::feof(a));
    ps3_asset_io_detach(a); std::fclose(a);
    ps3_asset_io_detach(b); std::fclose(b);
    // Save-data / writable mounts always use the original stream.
    a = std::fopen(path.string().c_str(), "r+b"); CHECK(a);
    ps3_asset_io_attach(a, "/dev_hdd0/home/00000001/savedata/PLAYER.BIN", 2);
    CHECK(std::fputc(42, a) == 42); std::rewind(a);
    CHECK(ps3_asset_io_read(a, data.data(), 1) == 1 && data[0] == 42);
    ps3_asset_io_detach(a); std::fclose(a);
    // Reopening the same file after its generation changes must not reuse bytes.
    writer = std::fopen(path.string().c_str(), "ab"); CHECK(writer);
    CHECK(std::fputc(99, writer) == 99); std::fclose(writer);
    a = std::fopen(path.string().c_str(), "rb"); CHECK(a);
    ps3_asset_io_attach(a, "/dev_bdvd/PS3_GAME/USRDIR/D00.ARC", 0);
    CHECK(ps3_asset_io_read(a, data.data(), 1) == 1 && data[0] == 42);
    ps3_asset_io_detach(a); std::fclose(a);
    // Read-only savedata and writable disc descriptors must also bypass caching.
    const char* excluded[] = {"/dev_hdd0/home/save.bin", "/dev_bdvd_typo/data", "/dev_bdvd/data"};
    for (unsigned i = 0; i < 3; ++i) {
        a = std::fopen(path.string().c_str(), "rb"); CHECK(a);
        ps3_asset_io_get_stats(&before);
        ps3_asset_io_attach(a, excluded[i], i == 2 ? 2 : 0);
        CHECK(ps3_asset_io_read(a, data.data(), 1) == 1 && data[0] == 42);
        ps3_asset_io_get_stats(&after);
        CHECK(after.host_reads == before.host_reads && after.cache_hits == before.cache_hits);
        ps3_asset_io_detach(a); std::fclose(a);
    }
    std::filesystem::remove(path);
    std::printf("PASS asset I/O: hits=%llu host_reads=%llu evictions=%llu resident=%llu/%llu\n",
        (unsigned long long)after.cache_hits, (unsigned long long)after.host_reads,
        (unsigned long long)after.evictions, (unsigned long long)after.resident_bytes,
        (unsigned long long)after.budget_bytes);
}
