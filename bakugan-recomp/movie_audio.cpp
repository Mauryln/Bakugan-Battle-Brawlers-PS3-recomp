#include "movie_audio.h"
#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <array>
#include <cstdio>
#include <mutex>
#include <vector>

using Microsoft::WRL::ComPtr;

struct MovieAudio::Impl {
    std::thread thread;
    HANDLE stopped=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    std::mutex mutex;
    HWAVEOUT output=nullptr;
    bool paused=false;

    void run(std::wstring path)
    {
        HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        if (FAILED(hr)) return;
        hr=MFStartup(MF_VERSION);
        if (FAILED(hr)) { CoUninitialize(); return; }
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
            HRESULT hr=reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM,0,nullptr,&flags,nullptr,&sample);
            if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR)) {
                fprintf(stderr,"[video-audio] decode failed: %08X\n",(unsigned)hr); break;
            }
            eof=(flags & MF_SOURCE_READERF_ENDOFSTREAM)!=0;
            if (!sample) continue;
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
            samples+=length/fmt.nBlockAlign;
            index=(index+1)%blocks.size();
        }
        if (eof) {
            for (auto& b:blocks) while (b.prepared && !(b.header.dwFlags & WHDR_DONE)) {
                if (WaitForMultipleObjects(2,events,FALSE,100)==WAIT_OBJECT_0) goto finished;
            }
        }
finished:
        {
            std::lock_guard lock(mutex);
            waveOutReset(output);
            for (auto& b:blocks) if (b.prepared) waveOutUnprepareHeader(output,&b.header,sizeof b.header);
            waveOutClose(output); output=nullptr;
        }
        CloseHandle(ready);
        fprintf(stderr,"[video-audio] decoded %u stereo samples at 48000 Hz\n",samples);
    }
};

MovieAudio::MovieAudio() : impl_(std::make_unique<Impl>()) {}
MovieAudio::~MovieAudio() { stop(); CloseHandle(impl_->stopped); }
void MovieAudio::start(const std::wstring& path)
{
    stop(); ResetEvent(impl_->stopped);
    impl_->paused=false;
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
