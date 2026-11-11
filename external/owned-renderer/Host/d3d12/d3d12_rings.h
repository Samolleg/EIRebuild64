#pragma once
// Upload and readback rings of the D3D12 backend (plan 2.5).
//
// UploadRing: one persistently mapped UPLOAD buffer serving parameter
// blocks, per-draw constants, instance data and texture/buffer update
// sources. Space is recycled by frame fence value; there is no DISCARD.
// Demand-sized: an allocation that does not fit replaces the buffer with
// one twice as large (or large enough), the old one released by fence.
//
// ReadbackRing: a persistently mapped READBACK buffer for screenshots,
// captures and profiler data. An allocation returns a ticket naming the
// fence value of the frame that copies into it; the data is readable once
// that value completed and the space returns when the ticket is released.
#include "d3d12_core.h"

#include <cstdint>
#include <deque>
#include <vector>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <string>

namespace d3d12 {

struct UploadAllocation {
    std::uint8_t* cpu = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS gpu = 0;
    ID3D12Resource* resource = nullptr;  // valid for the recording frame
    std::uint64_t offset = 0;            // in resource
};

// The upload memory as fixed blocks rather than one buffer that doubles:
// allocations run linearly through the current block; a full block is left
// to its frames and the next free block (all its frames finished) or a new
// one takes over. Blocks no frame used for kIdleFrames are released, so
// steady play keeps one or two. New blocks come from a background thread
// that keeps spares ready while blocks are in demand (device creation is
// free-threaded), so a load frame writing hundreds of MB rarely creates one
// on the render thread.
class UploadRing {
public:
    static constexpr std::uint64_t kBlockBytes = 16u << 20;
    // An allocation this large gets its own upload buffer, released once the
    // recording frame finished (it would waste most of a block).
    static constexpr std::uint64_t kDedicatedBytes = 4u << 20;
    static constexpr std::uint64_t kIdleFrames = 3600;  // about a minute of frames
    static constexpr unsigned kSpares = 2;              // ready blocks while in demand
    UploadRing() = default;
    UploadRing(const UploadRing&) = delete;
    UploadRing& operator=(const UploadRing&) = delete;
    ~UploadRing();
    // name: the blocks' debug name.
    bool create(ID3D12Device* device, std::string& reason, const wchar_t* name = L"eid3d11 upload block");
    // size bytes at alignment (a power of two: 256 for constants, 512 for
    // texture rows) for the frame being recorded; false only when the device
    // cannot create a block.
    bool allocate(Core& core, std::uint64_t size, std::uint64_t alignment, UploadAllocation& out, std::string& reason);
    // Copies data into a fresh allocation (convenience for constants).
    bool push(Core& core, const void* data, std::uint64_t size, std::uint64_t alignment, UploadAllocation& out,
              std::string& reason);
    void end_frame(std::uint64_t fence_value);
    void retire(std::uint64_t completed) noexcept;
    std::uint64_t capacity() const noexcept { return blocks_.size() * kBlockBytes; }
    std::size_t blocks() const noexcept { return blocks_.size(); }
    // What the frame just ended took: blocks added (from the background
    // spares, and created here with their creation time), bytes allocated,
    // dedicated buffers and their bytes.
    struct FrameStats {
        unsigned added = 0, created = 0;
        double create_ms = 0;
        std::uint64_t bytes = 0;
        unsigned dedicated = 0;
        std::uint64_t dedicated_bytes = 0;
    };
    const FrameStats& last_frame() const noexcept { return last_; }

private:
    struct Block {
        ComPtr<ID3D12Resource> buffer;
        std::uint8_t* cpu = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS gpu = 0;
        std::uint64_t fence = 0;       // the last frame that used it (kRecording: the frame being recorded)
        std::uint64_t used_frame = 0;  // frame_ when last used (idle release)
    };
    static constexpr std::uint64_t kRecording = ~0ull;
    bool make_block(Block& block, std::string& reason) const;
    bool add_block(std::string& reason);
    bool transient(Core& core, std::uint64_t size, UploadAllocation& out, std::string& reason);
    void stop() noexcept;
    void spares_loop() noexcept;

    ID3D12Device* device_ = nullptr;
    const wchar_t* name_ = L"eid3d11 upload block";
    std::vector<Block> blocks_;
    std::size_t current_ = 0;
    std::uint64_t head_ = 0, completed_ = 0, frame_ = 0, added_frame_ = 0;
    FrameStats frame_stats_{}, last_{};
    // The background spares (lock_): the thread fills spares_ up to wanted_.
    std::mutex lock_;
    std::condition_variable wake_;
    std::vector<Block> spares_;
    unsigned wanted_ = 0;
    bool stop_ = false;
    std::thread spares_thread_;
};

struct ReadbackTicket {
    ComPtr<ID3D12Resource> buffer;  // keeps the memory (and its mapping) alive
    const std::uint8_t* cpu = nullptr;
    std::uint64_t offset = 0, size = 0;
    std::uint64_t fence_value = 0;  // the copying frame's
    std::uint64_t id = 0;
    std::uint32_t generation = 0;
};

class ReadbackRing {
public:
    bool create(ID3D12Device* device, std::uint64_t initial_bytes, std::string& reason);
    // Space for a copy recorded in the frame being recorded (fence value
    // core.recording_value()). Grows on demand like the upload ring.
    bool allocate(Core& core, std::uint64_t size, std::uint64_t alignment, ReadbackTicket& out, std::string& reason);
    static bool ready(const ReadbackTicket& ticket, std::uint64_t completed) noexcept {
        return ticket.buffer && completed >= ticket.fence_value;
    }
    // Returns the ticket's space; the ticket is empty afterwards.
    void release(ReadbackTicket& ticket) noexcept;

private:
    bool replace(std::uint64_t bytes, std::string& reason);

    struct Block {
        std::uint64_t id, bytes;  // bytes include alignment padding and a skipped ring end
        bool released;
    };
    ID3D12Device* device_ = nullptr;
    ComPtr<ID3D12Resource> buffer_;
    const std::uint8_t* cpu_ = nullptr;
    std::uint64_t capacity_ = 0, head_ = 0, used_ = 0, next_id_ = 1;
    std::uint32_t generation_ = 0;
    std::deque<Block> blocks_;
};

} // namespace d3d12
