#pragma once
// Frame generation pacing (DLSS-G, app paced; DLSS-FG Programming Guide
// section 7: the feature neither paces nor presents). While frame generation
// is on, the swap chain stays on the main direct queue and only this pacer's
// thread presents: per real frame it waits for the frame's GPU work, copies
// the generated image to the swap-chain buffer and presents it at once, then
// presents the real image half a real-frame interval later. Copies,
// renderer work and external overlay writes share the swap chain's queue.
// Player VSync and the output frame cap apply to both final presents.
#include "d3d12_core.h"
#include "frame_limiter.h"

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace d3d12 {

class Presenter;

class Pacer {
public:
    ~Pacer() { stop(); }
    // The shared queue reference, fence, copy lists and thread.
    bool start(Core& core, std::string& reason);
    // Drains, joins and releases its presentation resources.
    void stop() noexcept;
    bool running() const noexcept { return thread_.joinable(); }
    // One real frame whose main-queue work signals main_value: generated
    // (null: none this frame, the real image is presented when ready) then
    // real, both in COPY_SOURCE. Waits while the previous frame is still
    // being presented (that is the frame's back-pressure, as a blocking
    // Present). slot: the ring slot both images belong to.
    // game_start, host_taken: QPC times for the real Present's latency sample.
    void submit(Presenter& presenter, ID3D12Resource* generated, ID3D12Resource* real, std::uint64_t main_value, unsigned slot,
                std::int64_t game_start, std::int64_t host_taken, UINT sync_interval, int frame_limit) noexcept;
    // Waits until every submitted frame is presented and its copies finished.
    void drain() noexcept;
    // Waits until the pacer's copies out of ring slot `slot` have finished
    // (before the main queue writes that slot's images again).
    void wait_slot(unsigned slot) noexcept;
    // Presents so far (generated and real) and the latest smoothed interval
    // between real-frame GPU completions (ms).
    struct Stats { std::uint64_t generated = 0, real = 0; double real_interval_ms = 0; };
    Stats stats() const noexcept;
    static constexpr unsigned kSlots = 2;

private:
    struct Frame;
    void run() noexcept;
    bool copy_and_present(ID3D12Resource* source, std::uint64_t& signalled, const Frame& frame) noexcept;
    void wait_value(std::uint64_t value) noexcept;
    static void sleep_until(std::int64_t deadline, std::int64_t frequency, HANDLE timer) noexcept;

    Core* core_ = nullptr;
    Presenter* presenter_ = nullptr;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<ID3D12Fence> fence_;
    HANDLE event_ = nullptr, main_event_ = nullptr, timer_ = nullptr;
    static constexpr unsigned kLists = 4;
    ComPtr<ID3D12CommandAllocator> allocators_[kLists];
    std::uint64_t allocator_values_[kLists] = {};
    ComPtr<ID3D12GraphicsCommandList> list_;
    unsigned next_allocator_ = 0;
    std::uint64_t value_ = 0;                 // the pacer fence's last signalled value (pacer thread)
    std::uint64_t slot_values_[kSlots] = {};  // the value after the last copy out of each slot
    std::thread thread_;
    mutable std::mutex mutex_;
    std::condition_variable wake_, idle_;
    bool stop_ = false, pending_ = false, busy_ = false;
    struct Frame {
        ID3D12Resource* generated = nullptr; ID3D12Resource* real = nullptr; std::uint64_t main_value = 0; unsigned slot = 0;
        std::int64_t game_start = 0, host_taken = 0, submitted = 0;
        UINT sync_interval = 0; int frame_limit = 0;
    } frame_;
    owned::FrameLimiter limiter_;  // pacer thread: real and generated output
    // Latency stages of real frames (ms from the game's frame start; 5 s means).
    double stage_taken_sum_ = 0, stage_submitted_sum_ = 0, stage_done_sum_ = 0, stage_real_sum_ = 0;
    std::uint64_t stage_count_ = 0;
    std::int64_t last_done_ = 0;              // QPC of the previous real frame's GPU completion
    double interval_ms_ = 0;
    Stats stats_;
    // Pacing measurement (pacer thread; logged every 5 s).
    std::int64_t frequency_ = 1, last_present_ = 0, window_start_ = 0;
    double spacing_sum_ = 0, spacing_squares_ = 0, spacing_max_ = 0, hold_sum_ = 0;
    std::uint64_t spacing_count_ = 0, hold_count_ = 0, window_generated_ = 0, window_real_ = 0;
    double generated_after_sum_ = 0, real_call_sum_ = 0;  // held = generated_after + T/2 + real call
    std::uint64_t generated_after_count_ = 0, real_call_count_ = 0;
};

} // namespace d3d12
