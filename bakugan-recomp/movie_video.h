#pragma once
#include "video_decoder.h"
#include <memory>
#include <string>

// Native movie clock and decoding, independent of guest render polling.
class MovieVideo {
public:
    MovieVideo();
    ~MovieVideo();
    int32_t open(const std::wstring& path, VideoFrame& format);
    void start();
    void pause(bool value);
    void close();
    bool take_finished();
    unsigned frames() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
