#include "movie_audio.h"
#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string_view>
#include <vector>

using Microsoft::WRL::ComPtr;

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

double query_waveout_sec(HWAVEOUT output, unsigned block_align, unsigned sample_rate)
{
    if (!output) return 0.0;
    MMTIME mm{};
    mm.wType=TIME_SAMPLES;
    if (waveOutGetPosition(output,&mm,sizeof mm)==MMSYSERR_NOERROR) {
        if (mm.wType==TIME_SAMPLES) return (double)mm.u.sample/(double)sample_rate;
        if (mm.wType==TIME_BYTES) return (double)mm.u.cb/(double)(block_align*sample_rate);
    }
    mm={};
    mm.wType=TIME_BYTES;
    if (waveOutGetPosition(output,&mm,sizeof mm)==MMSYSERR_NOERROR && mm.wType==TIME_BYTES)
        return (double)mm.u.cb/(double)(block_align*sample_rate);
    return 0.0;
}
} // namespace

struct MovieAudio::Impl {
    using Clock = std::chrono::steady_clock;
    std::thread thread;
    HANDLE stopped=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    mutable std::mutex mutex;
    HWAVEOUT output=nullptr;
    bool paused=false;
    Clock::time_point start_tp{};
    std::atomic<uint64_t> submitted_samples{0};
    std::atomic<bool> output_open{false};
    std::atomic<bool> audio_started{false};
    std::atomic<bool> audio_finished{false};
    std::atomic<int64_t> first_pts_100ns{-1};
    std::atomic<int64_t> start_delay_us{-1};
    double last_played_sec=0.0;

    void run(std::wstring path)
    {
        HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        if (FAILED(hr)) {
            audio_finished.store(true,std::memory_order_release);
            return;
        }
        hr=MFStartup(MF_VERSION);
        if (FAILED(hr)) {
            CoUninitialize();
            audio_finished.store(true,std::memory_order_release);
            return;
        }
        {
            ComPtr<IMFSourceReader> reader;
            ComPtr<IMFMediaType> type;
            hr=MFCreateSourceReaderFromURL(path.c_str(),nullptr,&reader);
            if (SUCCEEDED(hr)) hr=reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS,FALSE);
            if (SUCCEEDED(hr)) hr=reader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM,TRUE);
            if (SUCCEEDED(hr)) hr=MFCreateMediaType(&type);
            if (SUCCEEDED(hr)) hr=type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Audio);
            if (SUCCEEDED(hr)) hr=type->SetGUID(MF_MT_SUBTYPE,MFAudioFormat_Float);
            if (SUCCEEDED(hr)) hr=type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,2);
            if (SUCCEEDED(hr)) hr=type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,48000);
            if (SUCCEEDED(hr)) hr=reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM,nullptr,type.Get());
            if (SUCCEEDED(hr)) play(reader.Get());
            else fprintf(stderr,"[video-audio] no decodable audio: %08X\n",(unsigned)hr);
        }
        MFShutdown();
        CoUninitialize();
        audio_finished.store(true,std::memory_order_release);
    }

    void play(IMFSourceReader* reader)
    {
        HANDLE ready=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if (!ready) return;
        WAVEFORMATEX fmt{};
        fmt.wFormatTag=WAVE_FORMAT_IEEE_FLOAT; fmt.nChannels=2;
        fmt.nSamplesPerSec=48000; fmt.wBitsPerSample=32;
        fmt.nBlockAlign=8; fmt.nAvgBytesPerSec=384000;
        {
            std::lock_guard lock(mutex);
            if (waveOutOpen(&output,WAVE_MAPPER,&fmt,(DWORD_PTR)ready,0,CALLBACK_EVENT)!=MMSYSERR_NOERROR) {
                output=nullptr; CloseHandle(ready); return;
            }
            output_open.store(true,std::memory_order_relaxed);
            last_played_sec=0.0;
            if (paused) waveOutPause(output);
        }
        struct Block { WAVEHDR header{}; std::vector<BYTE> bytes; bool prepared=false; };
        std::array<Block,3> blocks;
        HANDLE events[]={stopped,ready};
        unsigned index=0, samples=0;
        bool eof=false, failed=false;
        while (!eof && !failed && WaitForSingleObject(stopped,0)!=WAIT_OBJECT_0) {
            Block& b=blocks[index];
            while (b.prepared && !(b.header.dwFlags & WHDR_DONE)) {
                if (WaitForMultipleObjects(2,events,FALSE,100)==WAIT_OBJECT_0) goto finished;
            }
            if (b.prepared) {
                waveOutUnprepareHeader(output,&b.header,sizeof b.header);
                b.prepared=false;
            }
            ComPtr<IMFSample> sample;
            DWORD flags=0;
            LONGLONG sample_pts=0;
            HRESULT hr=reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM,0,nullptr,&flags,&sample_pts,&sample);
            if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR)) {
                fprintf(stderr,"[video-audio] decode failed: %08X\n",(unsigned)hr); break;
            }
            eof=(flags & MF_SOURCE_READERF_ENDOFSTREAM)!=0;
            if (!sample) continue;
            if (first_pts_100ns.load(std::memory_order_relaxed)<0)
                first_pts_100ns.store(sample_pts,std::memory_order_relaxed);
            ComPtr<IMFMediaBuffer> buffer;
            hr=sample->ConvertToContiguousBuffer(&buffer);
            if (FAILED(hr)) break;
            BYTE* bytes=nullptr; DWORD length=0;
            hr=buffer->Lock(&bytes,nullptr,&length);
            if (FAILED(hr)) break;
            b.bytes.assign(bytes,bytes+length);
            buffer->Unlock();
            if (!length || length % fmt.nBlockAlign) { failed=true; break; }
            b.header={}; b.header.lpData=(LPSTR)b.bytes.data(); b.header.dwBufferLength=length;
            if (waveOutPrepareHeader(output,&b.header,sizeof b.header)!=MMSYSERR_NOERROR) break;
            b.prepared=true;
            if (waveOutWrite(output,&b.header,sizeof b.header)!=MMSYSERR_NOERROR) break;
            audio_started.store(true,std::memory_order_release);
            const unsigned block_samples=length/fmt.nBlockAlign;
            samples+=block_samples;
            submitted_samples.fetch_add(block_samples,std::memory_order_relaxed);
            if (start_delay_us.load(std::memory_order_relaxed)<0) {
                const auto delay=std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-start_tp).count();
                start_delay_us.store(delay,std::memory_order_relaxed);
                if (av_sync_debug()) {
                    fprintf(stderr,"[AVSYNC][MOVIE-A] start_delay_ms=%.1f first_pts_ms=%.1f\n",
                            delay/1000.0,first_pts_100ns.load(std::memory_order_relaxed)/10000.0);
                }
            }
            index=(index+1)%blocks.size();
        }
        if (eof) {
            for (auto& b:blocks) while (b.prepared && !(b.header.dwFlags & WHDR_DONE)) {
                if (WaitForMultipleObjects(2,events,FALSE,100)==WAIT_OBJECT_0) goto finished;
            }
        }
finished:
        double final_played_sec=0.0;
        {
            std::lock_guard lock(mutex);
            final_played_sec=query_waveout_sec(output,fmt.nBlockAlign,fmt.nSamplesPerSec);
            last_played_sec=final_played_sec;
            output_open.store(false,std::memory_order_relaxed);
            waveOutReset(output);
            for (auto& b:blocks) if (b.prepared) waveOutUnprepareHeader(output,&b.header,sizeof b.header);
            waveOutClose(output); output=nullptr;
        }
        CloseHandle(ready);
        if (av_sync_debug()) {
            fprintf(stderr,"[AVSYNC][MOVIE-A] end played_ms=%.1f sub_ms=%.1f\n",
                    final_played_sec*1000.0,(double)samples/48.0);
        }
        fprintf(stderr,"[video-audio] decoded %u stereo samples at 48000 Hz\n",samples);
    }
};

MovieAudio::MovieAudio() : impl_(std::make_unique<Impl>()) {}
MovieAudio::~MovieAudio() { stop(); CloseHandle(impl_->stopped); }
void MovieAudio::start(const std::wstring& path)
{
    stop(); ResetEvent(impl_->stopped);
    {
        std::lock_guard lock(impl_->mutex);
        impl_->paused=false;
        impl_->start_tp=Impl::Clock::now();
        impl_->last_played_sec=0.0;
    }
    impl_->submitted_samples.store(0,std::memory_order_relaxed);
    impl_->output_open.store(false,std::memory_order_relaxed);
    impl_->audio_started.store(false,std::memory_order_relaxed);
    impl_->audio_finished.store(false,std::memory_order_relaxed);
    impl_->first_pts_100ns.store(-1,std::memory_order_relaxed);
    impl_->start_delay_us.store(-1,std::memory_order_relaxed);
    impl_->thread=std::thread([this,path]{ impl_->run(path); });
}
void MovieAudio::pause(bool value)
{
    std::lock_guard lock(impl_->mutex);
    impl_->paused=value;
    if (impl_->output) { if (value) waveOutPause(impl_->output); else waveOutRestart(impl_->output); }
}
void MovieAudio::stop()
{
    SetEvent(impl_->stopped);
    if (impl_->thread.joinable()) impl_->thread.join();
}
MovieAudio::SyncStats MovieAudio::sync_stats() const
{
    SyncStats s{};
    {
        std::lock_guard lock(impl_->mutex);
        if (impl_->output) {
            impl_->last_played_sec=query_waveout_sec(impl_->output,8,48000);
            s.played_sec=impl_->last_played_sec;
            s.output_open=true;
        } else {
            s.played_sec=impl_->last_played_sec;
            s.output_open=impl_->output_open.load(std::memory_order_relaxed);
        }
    }
    s.submitted_sec=(double)impl_->submitted_samples.load(std::memory_order_relaxed)/48000.0;
    s.started_output=impl_->audio_started.load(std::memory_order_acquire);
    s.finished=impl_->audio_finished.load(std::memory_order_acquire);
    const int64_t pts=impl_->first_pts_100ns.load(std::memory_order_relaxed);
    if (pts>=0) s.first_pts_ms=pts/10000.0;
    const int64_t delay=impl_->start_delay_us.load(std::memory_order_relaxed);
    if (delay>=0) s.start_delay_ms=delay/1000.0;
    return s;
}
