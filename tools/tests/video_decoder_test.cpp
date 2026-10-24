#include "video_decoder.h"
#include <cstdio>
#include <cstdint>
#include <set>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int wmain(int argc, wchar_t** argv)
{
    CHECK(argc == 2);
    VideoDecoder decoder;
    VideoFrame frame;
    bool eof = false;
    CHECK(decoder.read(frame, eof) < 0);
    CHECK(decoder.open(L"Z:\\__missing_bakugan_video__.mp4") < 0);
    CHECK(decoder.open(argv[1]) == 0);
    unsigned count = 0;
    int64_t previous_pts = -1;
    std::set<uint64_t> hashes;
    while (!eof && count < 36000) {
        CHECK(decoder.read(frame, eof) == 0);
        if (eof) break;
        CHECK(frame.width > 0 && frame.height > 0);
        CHECK(frame.pixels.size() == size_t(frame.width) * frame.height * 4);
        CHECK(frame.pts_100ns >= previous_pts);
        previous_pts = frame.pts_100ns;
        uint64_t hash = 14695981039346656037ull;
        for (uint8_t value : frame.pixels) hash = (hash ^ value) * 1099511628211ull;
        hashes.insert(hash);
        ++count;
    }
    CHECK(eof && count > 2 && hashes.size() > 1);
    CHECK(decoder.read(frame, eof) == 0 && eof);
    decoder.close();
    decoder.close();
    CHECK(decoder.read(frame, eof) < 0);
    CHECK(decoder.open(argv[1]) == 0);
    CHECK(decoder.read(frame, eof) == 0 && !eof);
    std::printf("PASS: %u frames, %zu distinct frames, %ux%u, last PTS %lld\n",
                count, hashes.size(), frame.width, frame.height, (long long)previous_pts);
}
