#include "video_decoder.h"
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wrl/client.h>
#include <cstring>
#include <limits>

using Microsoft::WRL::ComPtr;

struct VideoDecoder::Impl {
    ComPtr<IMFSourceReader> reader;
    bool com_started = false, mf_started = false, eof = false;
};

VideoDecoder::VideoDecoder() : impl_(std::make_unique<Impl>()) {}
VideoDecoder::~VideoDecoder() { close(); }

void VideoDecoder::close()
{
    impl_->reader.Reset();
    if (impl_->mf_started) MFShutdown();
    if (impl_->com_started) CoUninitialize();
    impl_->mf_started = impl_->com_started = impl_->eof = false;
}

int32_t VideoDecoder::open(const wchar_t* path)
{
    close();
    if (!path || !*path) return E_INVALIDARG;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    // A host thread may already belong to an STA; Source Reader supports that.
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return hr;
    impl_->com_started = SUCCEEDED(hr);
    hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    if (FAILED(hr)) { close(); return hr; }
    impl_->mf_started = true;
    ComPtr<IMFAttributes> attributes;
    hr = MFCreateAttributes(&attributes, 1);
    if (SUCCEEDED(hr)) hr = attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    if (SUCCEEDED(hr)) hr = MFCreateSourceReaderFromURL(path, attributes.Get(), &impl_->reader);
    if (SUCCEEDED(hr)) hr = impl_->reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
    if (SUCCEEDED(hr)) hr = impl_->reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
    ComPtr<IMFMediaType> type;
    if (SUCCEEDED(hr)) hr = MFCreateMediaType(&type);
    if (SUCCEEDED(hr)) hr = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (SUCCEEDED(hr)) hr = type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    if (SUCCEEDED(hr)) hr = impl_->reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, type.Get());
    if (FAILED(hr)) close();
    return hr;
}

int32_t VideoDecoder::read(VideoFrame& frame, bool& eof)
{
    frame = {};
    eof = false;
    if (!impl_->reader) return MF_E_NOT_INITIALIZED;
    if (impl_->eof) { eof = true; return S_OK; }
    for (;;) {
        ComPtr<IMFSample> sample;
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        HRESULT hr = impl_->reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                              0, nullptr, &flags, &timestamp, &sample);
        if (FAILED(hr)) return hr;
        if (flags & MF_SOURCE_READERF_ERROR) return E_FAIL;
        if (!sample) {
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
                impl_->eof = eof = true;
                return S_OK;
            }
            continue; // Stream ticks are not frames.
        }
        ComPtr<IMFMediaType> type;
        hr = impl_->reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &type);
        UINT32 width = 0, height = 0, raw_stride = 0;
        if (SUCCEEDED(hr)) hr = MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &width, &height);
        if (FAILED(hr)) return hr;
        if (!width || !height || width > 8192 || height > 8192) return MF_E_INVALIDMEDIATYPE;
        LONG stride = 0;
        if (SUCCEEDED(type->GetUINT32(MF_MT_DEFAULT_STRIDE, &raw_stride))) stride = (LONG)raw_stride;
        else {
            hr = MFGetStrideForBitmapInfoHeader(MFVideoFormat_RGB32.Data1, width, &stride);
            if (FAILED(hr)) return hr;
        }
        ComPtr<IMFMediaBuffer> buffer;
        hr = sample->ConvertToContiguousBuffer(&buffer);
        if (FAILED(hr)) return hr;
        BYTE* data = nullptr;
        DWORD length = 0;
        hr = buffer->Lock(&data, nullptr, &length);
        if (FAILED(hr)) return hr;
        const uint64_t row_size = uint64_t(width) * 4;
        const uint64_t abs_stride = stride < 0 ? -int64_t(stride) : stride;
        if (abs_stride < row_size || abs_stride * (height - 1) + row_size > length) {
            buffer->Unlock();
            return MF_E_BUFFERTOOSMALL;
        }
        frame.width = width;
        frame.height = height;
        frame.pts_100ns = timestamp;
        frame.pixels.resize(size_t(row_size) * height);
        const BYTE* top = stride < 0 ? data + abs_stride * (height - 1) : data;
        for (uint32_t y = 0; y < height; ++y) {
            BYTE* row = frame.pixels.data() + size_t(y) * row_size;
            std::memcpy(row, top + int64_t(y) * stride, size_t(row_size));
            for (uint32_t x = 0; x < width; ++x) row[x * 4 + 3] = 255;
        }
        hr = buffer->Unlock();
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) impl_->eof = true;
        return hr;
    }
}
