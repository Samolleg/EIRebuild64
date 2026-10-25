#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
class CMappedResource;

namespace Video {
// Immutable CPU frame. A renderer may retain the shared snapshot or copy it
// synchronously. No AVFrame/decoder pointers cross this boundary (P03/P24).
struct Frame {
    enum class Format { BGRA8 };
    Format format = Format::BGRA8;
    uint32_t width = 0, height = 0, stride = 0;
    double pts = 0, duration = 0;
    std::vector<uint8_t> pixels;
};
class FrameSink {
public:
    virtual ~FrameSink() = default;
    virtual bool Submit(std::shared_ptr<const Frame>) = 0;
};
enum class State { Closed, Ready, Playing, Paused, Ended, Stopped, Cancelled, Skipped, Failed };
class Player {
public:
    Player();
    ~Player();
    // Resource selection stays with the host. Bytes are owned until worker join.
    bool Open(std::vector<uint8_t> bytes, bool loop = false);
    bool OpenResource(CMappedResource&, const char* name, bool loop = false);
    void Play();
    void Pause();
    void Stop();
    void Cancel();
    void Skip();
    void SetLoop(bool);
    // Host/render thread only, including Open/control/destruction. Call each tick,
    // also while paused. Rendering is serialized by the caller's render gate.
    bool Update(FrameSink&);
    State GetState() const;
    std::string Error() const;
private:
    struct Impl;
    std::unique_ptr<Impl> p;
};
}
