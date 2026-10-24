#include "movie_video.h"
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>

extern "C" void rsx_d3d12_backend_video_frame(const uint8_t*, uint32_t, uint32_t);

struct MovieVideo::Impl {
    using Clock = std::chrono::steady_clock;
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::thread thread;
    bool stop=false, ready=false, playing=false, paused=false, finished=false;
    int32_t error=0;
    uint32_t width=0, height=0;
    unsigned presented=0;
    Clock::time_point started, paused_at;

    void run(std::wstring path)
    {
        // Construct, use and release MF/COM on this single worker thread.
        VideoDecoder decoder;
        VideoFrame frame;
        bool eof=false;
        int32_t rc=decoder.open(path.c_str());
        if (!rc) rc=decoder.read(frame,eof);
        {
            std::lock_guard lock(mutex);
            error=rc ? rc : eof ? (int32_t)0x80610702u : 0;
            width=frame.width; height=frame.height;
            ready=true;
        }
        changed.notify_all();
        if (rc || eof) return;
        const int64_t first_pts=frame.pts_100ns;
        for (;;) {
            {
                std::unique_lock lock(mutex);
                changed.wait(lock,[&]{ return stop || (playing && !paused); });
                if (stop) return;
                // Recompute after every pause/resume, which shifts the clock.
                auto due=started+std::chrono::nanoseconds((frame.pts_100ns-first_pts)*100);
                if (Clock::now()<due) {
                    changed.wait_until(lock,due,[&]{ return stop || paused || !playing; });
                    if (stop) return;
                    if (paused || !playing) continue;
                }
                rsx_d3d12_backend_video_frame(frame.pixels.data(),frame.width,frame.height);
                ++presented;
            }
            rc=decoder.read(frame,eof);
            if (rc || eof) {
                std::lock_guard lock(mutex);
                if (!stop) {
                    error=rc; finished=true; playing=false;
                    fprintf(stderr,"[video] end frames=%u error=%08X (native clock)\n",presented,(uint32_t)rc);
                }
                return;
            }
        }
    }
};

MovieVideo::MovieVideo() : impl_(std::make_unique<Impl>()) {}
MovieVideo::~MovieVideo() { close(); }
int32_t MovieVideo::open(const std::wstring& path, VideoFrame& format)
{
    close();
    {
        std::lock_guard lock(impl_->mutex);
        impl_->stop=impl_->ready=impl_->playing=impl_->paused=impl_->finished=false;
        impl_->error=0; impl_->presented=0;
    }
    impl_->thread=std::thread([this,path]{ impl_->run(path); });
    std::unique_lock lock(impl_->mutex);
    impl_->changed.wait(lock,[&]{ return impl_->ready; });
    format={}; format.width=impl_->width; format.height=impl_->height;
    return impl_->error;
}
void MovieVideo::start()
{
    std::lock_guard lock(impl_->mutex);
    impl_->started=Impl::Clock::now();
    impl_->playing=true; impl_->paused=false;
    impl_->changed.notify_all();
}
void MovieVideo::pause(bool value)
{
    std::lock_guard lock(impl_->mutex);
    if (value && !impl_->paused) impl_->paused_at=Impl::Clock::now();
    if (!value && impl_->paused) impl_->started+=Impl::Clock::now()-impl_->paused_at;
    impl_->paused=value;
    impl_->changed.notify_all();
}
void MovieVideo::close()
{
    {
        std::lock_guard lock(impl_->mutex);
        impl_->stop=true;
        impl_->changed.notify_all();
    }
    if (impl_->thread.joinable()) impl_->thread.join();
}
bool MovieVideo::take_finished()
{
    std::lock_guard lock(impl_->mutex);
    bool result=impl_->finished;
    impl_->finished=false;
    return result;
}
unsigned MovieVideo::frames() const
{
    std::lock_guard lock(impl_->mutex);
    return impl_->presented;
}
