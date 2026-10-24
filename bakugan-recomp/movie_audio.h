#pragma once
#include <atomic>
#include <memory>
#include <string>
#include <thread>

// Host audio sink for MP4 AAC tracks. All COM/decoder work stays on its worker.
class MovieAudio {
public:
    MovieAudio();
    ~MovieAudio();
    void start(const std::wstring& path);
    void pause(bool value);
    void stop();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
