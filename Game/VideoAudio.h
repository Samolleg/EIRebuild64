#pragma once
#include <cstdint>
#include <memory>

struct AVFrame;
namespace Video {
struct AudioState;
// Control/Close on the host thread, Decode/Finish on one decoder thread.
// Cancel is thread-safe; join the decoder before Close.
class Audio {
public:
    Audio();
    ~Audio();
    bool Open();
    bool Decode(const AVFrame&, double pts);
    bool Finish();
    void Cancel();
    void Close();
    void Control(bool paused);
    double Clock() const;
    bool Drained() const;
    AudioState Snapshot() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> p;
};
}
