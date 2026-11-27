#pragma once
#include <atomic>
#include <memory>
#include <string>
#include <thread>

// Host audio sink for MP4 AAC tracks. All COM/decoder work stays on its worker.
class MovieAudio {
public:
    struct SyncStats {
        double played_sec = 0.0;
        double submitted_sec = 0.0;
        double first_pts_ms = -1.0;
        double start_delay_ms = -1.0;
        bool output_open = false;
        bool started_output = false;
        bool finished = false;
    };

    MovieAudio();
    ~MovieAudio();
    void start(const std::wstring& path);
    void pause(bool value);
    void stop();
    SyncStats sync_stats() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
