#pragma once

#include <cstdint>
#include <memory>
#include <vector>

// Host-only decoder. Pixels are tightly packed BGRA, top row first.
struct VideoFrame {
    uint32_t width = 0, height = 0;
    int64_t pts_100ns = 0;
    std::vector<uint8_t> pixels;
};

class VideoDecoder {
public:
    VideoDecoder();
    ~VideoDecoder();
    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;
    // Zero means success; errors are HRESULT values. Keep calls on one thread.
    int32_t open(const wchar_t* path);
    int32_t read(VideoFrame& frame, bool& eof);
    void close();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
