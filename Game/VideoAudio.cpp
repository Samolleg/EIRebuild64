#include "StdAfx.h"
#include "VideoAudio.h"
#include "MiniaudioOneShotSfx.h"
#pragma push_macro("f32")
#pragma push_macro("s32")
#pragma push_macro("s16")
#pragma push_macro("u32")
#pragma push_macro("u16")
#pragma push_macro("s8")
#pragma push_macro("u8")
#pragma push_macro("f64")
#undef f32
#undef s32
#undef s16
#undef u32
#undef u16
#undef s8
#undef u8
#undef f64
#define MA_NO_ENCODING
#define MA_NO_FLAC
#define MA_NO_VORBIS
#include "../third_party/miniaudio/miniaudio.h"
#pragma pop_macro("f64")
#pragma pop_macro("u8")
#pragma pop_macro("s8")
#pragma pop_macro("u16")
#pragma pop_macro("u32")
#pragma pop_macro("s16")
#pragma pop_macro("s32")
#pragma pop_macro("f32")
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <vector>

namespace Video {
struct Audio::Impl {
    struct Source { ma_data_source_base base{}; Impl* owner = nullptr; } source;
    ma_sound sound{};
    ma_engine* engine = nullptr;
    SwrContext* swr = nullptr;
    AVChannelLayout inputLayout{};
    int inputRate = 0, inputFormat = -1;
    uint32_t rate = 0, channels = 0;
    std::vector<float> ring;
    mutable std::mutex mutex;
    std::condition_variable space;
    size_t read = 0, count = 0;
    uint64_t consumed = 0, produced = 0;
    bool cancelled = false, eof = false, soundReady = false, sourceReady = false;

    static ma_result Read(ma_data_source* ds, void* output, ma_uint64 frames, ma_uint64* got) {
        auto& s = *reinterpret_cast<Source*>(ds)->owner;
        if (got) *got = frames;
        if (output) std::memset(output, 0, size_t(frames) * s.channels * sizeof(float));
        // The real-time device callback never waits for the decoder.
        std::unique_lock<std::mutex> lock(s.mutex, std::try_to_lock);
        if (!lock.owns_lock() || s.cancelled || MiniaudioOneShotSfx::VideoSuspended()) return MA_SUCCESS;
        const size_t n = (std::min)(size_t(frames), s.count);
        const size_t capacity = s.ring.size() / s.channels;
        for (size_t i = 0; i < n; ++i) {
            if (output) std::memcpy(static_cast<float*>(output) + i * s.channels,
                &s.ring[((s.read + i) % capacity) * s.channels], s.channels * sizeof(float));
        }
        s.read = (s.read + n) % capacity;
        s.count -= n;
        // The device clock includes underrun silence. Late decoded samples are
        // dropped by Push, so a short audio stream cannot deadlock the demuxer
        // behind a full video queue. At EOF the host times the remaining video.
        s.consumed += s.eof ? n : size_t(frames);
        lock.unlock();
        s.space.notify_one();
        return MA_SUCCESS;
    }
    static ma_result Format(ma_data_source* ds, ma_format* f, ma_uint32* c,
        ma_uint32* r, ma_channel* map, size_t capacity) {
        auto& s = *reinterpret_cast<Source*>(ds)->owner;
        if (f) *f = ma_format_f32;
        if (c) *c = s.channels;
        if (r) *r = s.rate;
        if (map) ma_channel_map_init_standard(ma_standard_channel_map_default, map, capacity, s.channels);
        return MA_SUCCESS;
    }
    bool Push(const float* data, size_t frames) {
        std::unique_lock<std::mutex> lock(mutex);
        const size_t capacity = ring.size() / channels;
        while (frames) {
            space.wait(lock, [&] { return cancelled || count < capacity; });
            if (cancelled) return false;
            const size_t late = size_t((std::min)(uint64_t(frames), consumed > produced ? consumed - produced : 0));
            frames -= late; produced += late;
            if (data) data += late * channels;
            if (!frames) break;
            const size_t n = (std::min)(frames, capacity - count);
            for (size_t i = 0; i < n; ++i) {
                float* dest = &ring[((read + count + i) % capacity) * channels];
                if (data) std::memcpy(dest, data + i * channels, channels * sizeof(float));
                else std::fill(dest, dest + channels, 0.f);
            }
            count += n; produced += n; frames -= n;
            if (data) data += n * channels;
        }
        return !cancelled;
    }
    bool Convert(const AVFrame* frame) {
        const int maxFrames = swr_get_out_samples(swr, frame ? frame->nb_samples : 0);
        if (maxFrames < 0 || maxFrames > int(rate) * 10) return false;
        std::vector<float> pcm(size_t(maxFrames) * channels);
        uint8_t* out = reinterpret_cast<uint8_t*>(pcm.data());
        const int n = swr_convert(swr, &out, maxFrames,
            frame ? const_cast<const uint8_t**>(frame->extended_data) : nullptr,
            frame ? frame->nb_samples : 0);
        return n >= 0 && Push(pcm.data(), size_t(n));
    }
};
Audio::Audio() : p(new Impl) {}
Audio::~Audio() { Close(); }
bool Audio::Open() {
    Close();
    p.reset(new Impl);
    p->engine = static_cast<ma_engine*>(MiniaudioOneShotSfx::AcquireVideoEngine());
    if (!p->engine) return false;
    p->rate = ma_engine_get_sample_rate(p->engine);
    p->channels = ma_engine_get_channels(p->engine);
    if (!p->rate || !p->channels || p->channels > 32) { Close(); return false; }
    p->ring.resize(size_t(p->rate) * 2 * p->channels);
    static const ma_data_source_vtable vtable = { Impl::Read, nullptr, Impl::Format, nullptr, nullptr, nullptr, 0 };
    auto config = ma_data_source_config_init();
    config.vtable = &vtable;
    p->source.owner = p.get();
    if (ma_data_source_init(&config, &p->source.base) != MA_SUCCESS) { Close(); return false; }
    p->sourceReady = true;
    if (ma_sound_init_from_data_source(p->engine, &p->source.base,
        MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_NO_PITCH, nullptr, &p->sound) != MA_SUCCESS) {
        Close(); return false;
    }
    p->soundReady = true;
    return true;
}
bool Audio::Decode(const AVFrame& frame, double pts) {
    if (!p->engine || frame.sample_rate <= 0 || frame.nb_samples < 0) return false;
    if (!p->swr) {
        AVChannelLayout output{};
        av_channel_layout_default(&output, int(p->channels));
        const int result = swr_alloc_set_opts2(&p->swr, &output, AV_SAMPLE_FMT_FLT,
            int(p->rate), &frame.ch_layout, AVSampleFormat(frame.format), frame.sample_rate, 0, nullptr);
        av_channel_layout_uninit(&output);
        if (result < 0 || !p->swr || swr_init(p->swr) < 0) return false;
        if (av_channel_layout_copy(&p->inputLayout, &frame.ch_layout) < 0) return false;
        p->inputRate = frame.sample_rate; p->inputFormat = frame.format;
        // Align the first audio sample to the common stream origin.
        if (!std::isfinite(pts) || pts > 10) return false;
        if (pts > 0 && !p->Push(nullptr, size_t(pts * p->rate))) return false;
    }
    if (p->inputRate != frame.sample_rate || p->inputFormat != frame.format ||
        av_channel_layout_compare(&p->inputLayout, &frame.ch_layout)) return false;
    // Bink PCM is continuous; reject discontinuities rather than silently drift.
    const double pending = double(swr_get_delay(p->swr, p->rate)) / p->rate;
    if (std::isfinite(pts) && std::abs(pts - (double(p->produced) / p->rate + pending)) > 0.25)
        return false;
    return p->Convert(&frame);
}
bool Audio::Finish() {
    if (p->swr && !p->Convert(nullptr)) return false;
    std::lock_guard<std::mutex> lock(p->mutex);
    p->eof = true;
    return !p->cancelled;
}
void Audio::Cancel() {
    { std::lock_guard<std::mutex> lock(p->mutex); p->cancelled = true; }
    p->space.notify_all();
}
void Audio::Close() {
    Cancel();
    if (p->soundReady) { ma_sound_uninit(&p->sound); p->soundReady = false; }
    if (p->sourceReady) { ma_data_source_uninit(&p->source.base); p->sourceReady = false; }
    swr_free(&p->swr);
    av_channel_layout_uninit(&p->inputLayout);
    if (p->engine) { p->engine = nullptr; MiniaudioOneShotSfx::ReleaseVideoEngine(); }
}
void Audio::Control(bool paused) {
    if (!p->soundReady) return;
    ma_sound_set_volume(&p->sound, MiniaudioOneShotSfx::VideoGain());
    if (paused || MiniaudioOneShotSfx::VideoSuspended()) ma_sound_stop(&p->sound);
    else ma_sound_start(&p->sound);
}
double Audio::Clock() const {
    std::lock_guard<std::mutex> lock(p->mutex);
    return p->rate ? double(p->consumed) / p->rate : 0;
}
bool Audio::Drained() const {
    std::lock_guard<std::mutex> lock(p->mutex);
    return p->eof && p->count == 0;
}
}
