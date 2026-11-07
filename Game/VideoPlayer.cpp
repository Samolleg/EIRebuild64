#include "StdAfx.h"
#include "VideoPlayer.h"
#include "VideoAudio.h"
#include "MiniaudioOneShotSfx.h"
#include "CMappedResource.h"
#include "EngineDiagnostics.h"
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <cmath>
#include <limits>

namespace Video {
struct Player::Impl {
    std::vector<uint8_t> bytes;
    int64_t position = 0;
    AVIOContext* io = nullptr;
    AVFormatContext* format = nullptr;
    AVCodecContext *video = nullptr, *audioCodec = nullptr;
    int videoIndex = -1, audioIndex = -1;
    SwsContext* scale = nullptr;
    Audio audio;
    bool hasAudio = false, loop = false;
    std::atomic<bool> cancel{false}, done{false}, failed{false};
    std::thread worker;
    std::mutex mutex;
    std::condition_variable space;
    std::deque<std::shared_ptr<const Frame>> frames;
    std::shared_ptr<const Frame> current;
    std::string error;
    State state = State::Closed;
    double origin = 0, nextVideo = 0, nextAudio = 0, clock = 0, end = 0;
    double tick = MiniaudioOneShotSfx::VideoTimeSeconds();
    ~Impl() {
        Halt();
        sws_freeContext(scale);
        avcodec_free_context(&video); avcodec_free_context(&audioCodec);
        avformat_close_input(&format);
        if (io) { av_freep(&io->buffer); avio_context_free(&io); }
    }
    void Halt() {
        cancel = true; space.notify_all(); audio.Cancel();
        if (worker.joinable()) worker.join();
        audio.Close();
    }
    void Fail(const char* operation, int code = 0) {
        char message[AV_ERROR_MAX_STRING_SIZE]{};
        if (code) av_strerror(code, message, sizeof(message));
        { std::lock_guard<std::mutex> lock(mutex); error = std::string(operation) + (code ? ": " : "") + message; }
        EngineDiagnostics::Event("Video error operation=%s detail=%s", operation, message);
        failed = true;
    }
    static int Read(void* opaque, uint8_t* dest, int size) {
        auto& p = *static_cast<Impl*>(opaque);
        if (p.cancel) return AVERROR_EXIT;
        const size_t n = (std::min)(size_t(size), p.bytes.size() - size_t(p.position));
        if (!n) return AVERROR_EOF;
        std::memcpy(dest, p.bytes.data() + p.position, n);
        p.position += int64_t(n);
        return int(n);
    }
    static int64_t Seek(void* opaque, int64_t offset, int whence) {
        auto& p = *static_cast<Impl*>(opaque);
        if (p.cancel) return AVERROR_EXIT;
        if (whence == AVSEEK_SIZE) return int64_t(p.bytes.size());
        whence &= ~AVSEEK_FORCE;
        const int64_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? p.position :
            whence == SEEK_END ? int64_t(p.bytes.size()) : -1;
        if (base < 0 || offset < -base || offset > int64_t(p.bytes.size()) - base) return AVERROR(EINVAL);
        return p.position = base + offset;
    }
    static int Interrupt(void* opaque) { return static_cast<Impl*>(opaque)->cancel ? 1 : 0; }
    bool Codec(int index, AVCodecContext*& out) {
        const auto* params = format->streams[index]->codecpar;
        const auto* codec = avcodec_find_decoder(params->codec_id);
        if (!codec) { Fail("decoder unavailable"); return false; }
        out = avcodec_alloc_context3(codec);
        if (!out) { Fail("decoder allocation"); return false; }
        int result = avcodec_parameters_to_context(out, params);
        if (result >= 0) result = avcodec_open2(out, codec, nullptr);
        if (result < 0) { Fail("decoder open", result); return false; }
        return true;
    }
    double Pts(const AVFrame& frame, int index, double fallback) const {
        return frame.best_effort_timestamp == AV_NOPTS_VALUE ? fallback :
            double(frame.best_effort_timestamp) * av_q2d(format->streams[index]->time_base) - origin;
    }
    bool VideoFrame(const AVFrame& decoded) {
        // Bound both queue length and individual allocations (up to 4K).
        if (decoded.width <= 0 || decoded.height <= 0 || decoded.width > 4096 || decoded.height > 4096) {
            Fail("invalid video dimensions"); return false;
        }
        auto f = std::make_shared<Frame>();
        f->width = decoded.width; f->height = decoded.height; f->stride = f->width * 4;
        f->pts = Pts(decoded, videoIndex, nextVideo);
        const auto* stream = format->streams[videoIndex];
        f->duration = decoded.duration > 0 ? decoded.duration * av_q2d(stream->time_base) :
            stream->avg_frame_rate.num > 0 ? av_q2d(av_inv_q(stream->avg_frame_rate)) : 1.0 / 30;
        if (!std::isfinite(f->pts) || !std::isfinite(f->duration) || f->duration <= 0 || f->pts + 0.001 < nextVideo - f->duration) {
            Fail("invalid video timestamps"); return false;
        }
        nextVideo = f->pts + f->duration;
        f->pixels.resize(size_t(f->stride) * f->height);
        scale = sws_getCachedContext(scale, decoded.width, decoded.height, AVPixelFormat(decoded.format),
            decoded.width, decoded.height, AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!scale) { Fail("video conversion allocation"); return false; }
        uint8_t* dest[] = { f->pixels.data(), nullptr, nullptr, nullptr };
        int strides[] = { int(f->stride), 0, 0, 0 };
        if (sws_scale(scale, decoded.data, decoded.linesize, 0, decoded.height, dest, strides) != decoded.height) {
            Fail("video conversion"); return false;
        }
        std::unique_lock<std::mutex> lock(mutex);
        space.wait(lock, [&] { return cancel || frames.size() < 4; });
        if (cancel) return false;
        frames.push_back(std::move(f));
        return true;
    }
    bool Drain(AVCodecContext* codec, int index, AVFrame* frame) {
        while (!cancel) {
            const int result = avcodec_receive_frame(codec, frame);
            if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) return true;
            if (result < 0) { Fail("decode frame", result); return false; }
            bool ok;
            if (index == videoIndex) ok = VideoFrame(*frame);
            else {
                const double pts = Pts(*frame, index, nextAudio);
                nextAudio = pts + double(frame->nb_samples) / frame->sample_rate;
                ok = audio.Decode(*frame, pts);
                if (!ok && !cancel) Fail("audio conversion or discontinuity");
            }
            av_frame_unref(frame);
            if (!ok) return false;
        }
        return false;
    }
    bool Send(AVCodecContext* codec, int index, AVPacket* packet, AVFrame* frame) {
        int result = avcodec_send_packet(codec, packet);
        if (result == AVERROR(EAGAIN)) {
            if (!Drain(codec, index, frame)) return false;
            result = avcodec_send_packet(codec, packet);
        }
        if (result < 0 && result != AVERROR_EOF) { Fail("send packet", result); return false; }
        return Drain(codec, index, frame);
    }
    void Run() {
        AVPacket* packet = av_packet_alloc();
        AVFrame* frame = av_frame_alloc();
        if (!packet || !frame) Fail("decode allocation");
        try {
            while (!cancel && !failed) {
                const int result = av_read_frame(format, packet);
                if (result == AVERROR_EOF) {
                    if (Send(video, videoIndex, nullptr, frame) && hasAudio &&
                        Send(audioCodec, audioIndex, nullptr, frame) && !audio.Finish() && !cancel)
                        Fail("audio drain");
                    break;
                }
                if (result < 0) { if (!cancel) Fail("read packet", result); break; }
                bool ok = true;
                if (packet->stream_index == videoIndex) ok = Send(video, videoIndex, packet, frame);
                else if (hasAudio && packet->stream_index == audioIndex) ok = Send(audioCodec, audioIndex, packet, frame);
                av_packet_unref(packet);
                if (!ok) break;
            }
        } catch (const std::exception& e) { Fail(e.what()); }
        av_frame_free(&frame); av_packet_free(&packet);
        done = true;
    }
};
Player::Player() : p(new Impl) {}
Player::~Player() = default;
bool Player::Open(std::vector<uint8_t> bytes, bool loop) {
    p.reset(new Impl);
    p->bytes = std::move(bytes); p->loop = loop;
    if (p->bytes.empty() || p->bytes.size() > size_t(INT64_MAX)) { p->Fail("empty/oversized resource"); p->state = State::Failed; return false; }
    auto* buffer = static_cast<uint8_t*>(av_malloc(32768));
    if (buffer) p->io = avio_alloc_context(buffer, 32768, 0, p.get(), Impl::Read, nullptr, Impl::Seek);
    if (!p->io) { av_free(buffer); p->Fail("AVIO allocation"); p->state = State::Failed; return false; }
    p->format = avformat_alloc_context();
    if (!p->format) { p->Fail("demux allocation"); p->state = State::Failed; return false; }
    p->format->pb = p->io;
    p->format->flags |= AVFMT_FLAG_CUSTOM_IO;
    p->format->interrupt_callback = { Impl::Interrupt, p.get() };
    const AVInputFormat* bink = av_find_input_format("bink");
    int result = bink ? avformat_open_input(&p->format, nullptr, bink, nullptr) : AVERROR_DEMUXER_NOT_FOUND;
    if (result < 0) { p->Fail("Bink resource open", result); p->state = State::Failed; return false; }
    // Bink read_header provides codec parameters; no probing decode is needed.
    for (unsigned i = 0; i < p->format->nb_streams; ++i) {
        const auto type = p->format->streams[i]->codecpar->codec_type;
        if (type == AVMEDIA_TYPE_VIDEO && p->videoIndex < 0) p->videoIndex = int(i);
        if (type == AVMEDIA_TYPE_AUDIO && p->audioIndex < 0) p->audioIndex = int(i);
    }
    if (p->videoIndex < 0 || !p->Codec(p->videoIndex, p->video)) {
        if (!p->failed) p->Fail("video stream missing");
        p->state = State::Failed; return false;
    }
    auto* stream = p->format->streams[p->videoIndex];
    p->origin = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time * av_q2d(stream->time_base);
    p->hasAudio = p->audioIndex >= 0 && MiniaudioOneShotSfx::VideoAudioEnabled();
    if (p->hasAudio && (!p->Codec(p->audioIndex, p->audioCodec) || !p->audio.Open())) {
        if (!p->failed) p->Fail("miniaudio device unavailable");
        p->state = State::Failed; return false;
    }
    p->state = State::Ready;
    try { p->worker = std::thread([impl = p.get()] { impl->Run(); }); }
    catch (const std::exception& e) { p->Fail(e.what()); p->state = State::Failed; return false; }
    return true;
}
bool Player::OpenResource(CMappedResource& resource, const char* name, bool loop) {
    DWORD size = 0;
    auto* data = resource.GetFile(name, &size);
    if (!data) { p.reset(new Impl); p->Fail("movie resource missing"); p->state = State::Failed; return false; }
    std::vector<uint8_t> bytes;
    try { bytes.assign(data, data + size); }
    catch (...) { resource.CloseFile(data); throw; }
    resource.CloseFile(data);
    return Open(std::move(bytes), loop);
}
void Player::Play() {
    if (p->state == State::Stopped || p->state == State::Ended) {
        const bool loop = p->loop;
        auto bytes = std::move(p->bytes);
        if (!Open(std::move(bytes), loop)) return;
    }
    if (p->state == State::Ready || p->state == State::Paused) {
        p->state = State::Playing; p->tick = MiniaudioOneShotSfx::VideoTimeSeconds();
        p->audio.Control(false);
    }
}
void Player::Pause() {
    if (p->state == State::Playing) { p->state = State::Paused; p->audio.Control(true); }
}
void Player::Stop() { p->Halt(); p->state = State::Stopped; p->current.reset(); }
void Player::Cancel() { p->Halt(); p->state = State::Cancelled; p->current.reset(); }
void Player::Skip() { p->Halt(); p->state = State::Skipped; p->current.reset(); }
void Player::SetLoop(bool value) { p->loop = value; }
AudioState Player::AudioSnapshot() const noexcept {return p->hasAudio?p->audio.Snapshot():AudioState{};}
State Player::GetState() const { return p->failed ? State::Failed : p->state; }
std::string Player::Error() const { std::lock_guard<std::mutex> lock(p->mutex); return p->error; }
bool Player::Update(FrameSink& sink) {
    if (p->failed) { p->Halt(); p->state = State::Failed; return false; }
    if (p->state != State::Playing && p->state != State::Paused) return true;
    const double now = MiniaudioOneShotSfx::VideoTimeSeconds();
    const double delta = now - p->tick; p->tick = now;
    const bool paused = p->state == State::Paused || MiniaudioOneShotSfx::VideoSuspended();
    p->audio.Control(paused);
    bool drained = !p->hasAudio || p->audio.Drained();
    if (!paused) {
        if (p->hasAudio && !drained) p->clock = p->audio.Clock();
        else p->clock = (std::max)(p->clock, p->hasAudio ? p->audio.Clock() : 0.0) + delta;
    }
    bool empty;
    {
        std::lock_guard<std::mutex> lock(p->mutex);
        while (!paused && !p->frames.empty() && p->frames.front()->pts <= p->clock) {
            p->current = p->frames.front(); p->frames.pop_front();
            p->end = p->current->pts + p->current->duration;
        }
        empty = p->frames.empty();
    }
    p->space.notify_one();
    if (p->current && !sink.Submit(p->current)) {
        p->Fail("renderer rejected video frame"); p->Halt(); p->state = State::Failed; return false;
    }
    if (!paused && p->done && empty && drained && p->clock >= p->end) {
        if (p->loop) {
            p->Halt();
            auto bytes = std::move(p->bytes);
            if (!Open(std::move(bytes), true)) return false;
            Play();
        } else { p->Halt(); p->state = State::Ended; }
    }
    return true;
}
}
