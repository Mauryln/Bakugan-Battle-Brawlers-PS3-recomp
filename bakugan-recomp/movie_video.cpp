#include "movie_video.h"
#include "movie_audio.h"
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string_view>
#include <thread>

extern "C" void rsx_d3d12_backend_video_frame(const uint8_t*, uint32_t, uint32_t);

namespace {
bool av_sync_debug()
{
    static int enabled=-1;
    if (enabled<0) {
        const char* e=std::getenv("BAKUGAN_AV_SYNC_DEBUG");
        enabled=(e && *e && std::string_view(e)!="0") ? 1 : 0;
    }
    return enabled!=0;
}
} // namespace

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
    const MovieAudio* audio_probe=nullptr;

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
        Clock::time_point last_sync_log{};
        bool logged_first=false;
        double last_v_pts_ms=0.0;
        for (;;) {
            if (presented == 0) {
                const MovieAudio* startup_probe=nullptr;
                {
                    std::unique_lock lock(mutex);
                    changed.wait(lock,[&]{ return stop || (playing && !paused); });
                    if (stop) return;
                    startup_probe=audio_probe;
                }
                if (startup_probe) {
                    const auto wait_deadline=Clock::now()+std::chrono::milliseconds(200);
                    for (;;) {
                        const MovieAudio::SyncStats a=startup_probe->sync_stats();
                        if (a.started_output || a.finished || Clock::now()>=wait_deadline)
                            break;
                        std::unique_lock lock(mutex);
                        changed.wait_for(lock,std::chrono::milliseconds(2),[&]{ return stop || paused || !playing; });
                        if (stop) return;
                        if (paused || !playing) {
                            changed.wait(lock,[&]{ return stop || (playing && !paused); });
                            if (stop) return;
                        }
                        startup_probe=audio_probe;
                        if (!startup_probe) break;
                    }
                }
            } else {
                const double v_pts_sec=double(frame.pts_100ns-first_pts)*1e-7;
                for (;;) {
                    const MovieAudio* sync_probe=nullptr;
                    Clock::time_point base_started{};
                    {
                        std::unique_lock lock(mutex);
                        changed.wait(lock,[&]{ return stop || (playing && !paused); });
                        if (stop) return;
                        sync_probe=audio_probe;
                        base_started=started;
                    }
                    MovieAudio::SyncStats a{};
                    if (sync_probe) a=sync_probe->sync_stats();
                    if (sync_probe && a.output_open && a.played_sec>0.0) {
                        const double a_offset_sec=(a.first_pts_ms>=0.0)
                            ? (a.first_pts_ms*1e-3-double(first_pts)*1e-7)
                            : 0.0;
                        const double audio_sec=a.played_sec+a_offset_sec;
                        {
                            std::lock_guard lock(mutex);
                            if (!paused && playing) {
                                started=Clock::now()-std::chrono::duration_cast<Clock::duration>(
                                    std::chrono::duration<double>(audio_sec));
                            }
                        }
                        const double remain_sec=v_pts_sec-audio_sec;
                        if (remain_sec<=0.002) break;
                        const double slice_sec=remain_sec<0.010 ? remain_sec : 0.010;
                        const auto wait_dur=std::chrono::duration_cast<Clock::duration>(
                            std::chrono::duration<double>(slice_sec));
                        std::unique_lock lock(mutex);
                        changed.wait_for(lock,wait_dur,[&]{ return stop || paused || !playing; });
                        if (stop) return;
                    } else {
                        // Fall back to steady_clock when audio is absent, paused, or finished.
                        const auto due=base_started+std::chrono::nanoseconds((frame.pts_100ns-first_pts)*100);
                        const auto now=Clock::now();
                        if (now>=due) break;
                        const bool wait_audio=(sync_probe!=nullptr && !a.finished);
                        const auto slice_end=now+std::chrono::milliseconds(10);
                        const auto target=(wait_audio && slice_end<due) ? slice_end : due;
                        std::unique_lock lock(mutex);
                        changed.wait_until(lock,target,[&]{ return stop || paused || !playing; });
                        if (stop) return;
                    }
                }
            }
            bool do_log=false;
            unsigned log_frame=0;
            double log_wall_ms=0.0, log_v_pts_ms=0.0;
            const MovieAudio* probe=nullptr;
            {
                std::unique_lock lock(mutex);
                changed.wait(lock,[&]{ return stop || (playing && !paused); });
                if (stop) return;
                if (presented==0) started=Clock::now();
                rsx_d3d12_backend_video_frame(frame.pixels.data(),frame.width,frame.height);
                ++presented;
                if (av_sync_debug()) {
                    const auto present_now=Clock::now();
                    last_v_pts_ms=double(frame.pts_100ns-first_pts)/10000.0;
                    if (!logged_first || present_now-last_sync_log>=std::chrono::seconds(1)) {
                        logged_first=true;
                        last_sync_log=present_now;
                        do_log=true;
                        log_frame=presented;
                        log_wall_ms=std::chrono::duration<double,std::milli>(present_now-started).count();
                        log_v_pts_ms=last_v_pts_ms;
                        probe=audio_probe;
                    }
                }
            }
            if (do_log) {
                MovieAudio::SyncStats a{};
                if (probe) a=probe->sync_stats();
                const double a_play_ms=a.played_sec*1000.0;
                const double a_sub_ms=a.submitted_sec*1000.0;
                fprintf(stderr,
                    "[AVSYNC][MOVIE] frame=%u wall_ms=%.1f v_pts_ms=%.1f v_late_ms=%.1f a_open=%d a_play_ms=%.1f a_sub_ms=%.1f drift_a_minus_v_ms=%.1f\n",
                    log_frame,log_wall_ms,log_v_pts_ms,log_wall_ms-log_v_pts_ms,
                    a.output_open?1:0,a_play_ms,a_sub_ms,a_play_ms-log_v_pts_ms);
            }
            rc=decoder.read(frame,eof);
            if (rc || eof) {
                const MovieAudio* end_probe=nullptr;
                unsigned total=0;
                double end_wall_ms=0.0;
                bool emit_end=false;
                {
                    std::lock_guard lock(mutex);
                    if (!stop) {
                        error=rc; finished=true; playing=false;
                        total=presented;
                        end_probe=audio_probe;
                        end_wall_ms=std::chrono::duration<double,std::milli>(Clock::now()-started).count();
                        emit_end=av_sync_debug();
                        fprintf(stderr,"[video] end frames=%u error=%08X (native clock)\n",presented,(uint32_t)rc);
                    }
                }
                if (emit_end) {
                    MovieAudio::SyncStats a{};
                    if (end_probe) a=end_probe->sync_stats();
                    const double a_play_ms=a.played_sec*1000.0;
                    const double a_sub_ms=a.submitted_sec*1000.0;
                    fprintf(stderr,
                        "[AVSYNC][MOVIE] eof frame=%u wall_ms=%.1f v_pts_ms=%.1f v_late_ms=%.1f a_open=%d a_play_ms=%.1f a_sub_ms=%.1f drift_a_minus_v_ms=%.1f\n",
                        total,end_wall_ms,last_v_pts_ms,end_wall_ms-last_v_pts_ms,
                        a.output_open?1:0,a_play_ms,a_sub_ms,a_play_ms-last_v_pts_ms);
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
void MovieVideo::start(const MovieAudio* audio_probe)
{
    std::lock_guard lock(impl_->mutex);
    impl_->started=Impl::Clock::now();
    impl_->audio_probe=audio_probe;
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
    impl_->audio_probe=nullptr;
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
