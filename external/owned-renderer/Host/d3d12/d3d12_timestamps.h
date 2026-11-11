#pragma once
// GPU timestamps per plan pass (plan 2.5): every pass gets a begin/end pair
// in one timestamp query heap, resolved at frame end into the frame slot's
// readback buffer and read when the slot comes round again (its fence has
// completed), so the F11-style table keeps one name per plan pass.
//
// Each frame slot sizes itself by demand: a frame with more passes than its
// slot holds times the ones that fit, counts the rest as untimed (shown, not
// hidden) and the slot is recreated larger the next time it is used.
#include "d3d12_core.h"

#include <cstdint>
#include <string>
#include <vector>

namespace d3d12 {

struct PassTiming {
    std::string name;
    double ms = 0;
};

class Timestamps {
public:
    bool create(ID3D12Device* device, ID3D12CommandQueue* queue, std::string& reason, std::uint32_t initial_passes = 64);
    // After Core::begin_frame (the slot's previous frame has finished): reads
    // that frame's results into last(), regrows the slot if it overflowed.
    bool begin_frame(Core& core, std::string& reason);
    // External FG lists use the same profiler after their own queue fence completes.
    bool begin_frame(unsigned slot, std::string& reason);
    // Returns the pass's index for end(); passes nest or follow freely.
    std::uint32_t begin(ID3D12GraphicsCommandList* list, const char* name);
    void end(ID3D12GraphicsCommandList* list, std::uint32_t pass) noexcept;
    // Before Core::end_frame: resolves the slot's queries.
    void resolve(ID3D12GraphicsCommandList* list) noexcept;

    // The newest completed frame's passes (kFramesInFlight frames old) and
    // how many of its passes did not fit the slot.
    const std::vector<PassTiming>& last() const noexcept { return last_; }
    std::uint32_t last_untimed() const noexcept { return last_untimed_; }

private:
    struct Slot {
        ComPtr<ID3D12QueryHeap> heap;
        ComPtr<ID3D12Resource> readback;
        std::uint32_t capacity = 0;  // passes
        std::uint32_t wanted = 0;    // passes the last frame asked for
        std::vector<std::string> names;
        bool pending = false;
    };
    bool create_slot(Slot& slot, std::uint32_t passes, std::string& reason);

    ID3D12Device* device_ = nullptr;
    double ticks_per_ms_ = 1;
    Slot slots_[Core::kFramesInFlight];
    Slot* current_ = nullptr;
    std::vector<PassTiming> last_;
    std::uint32_t last_untimed_ = 0;
};

} // namespace d3d12
