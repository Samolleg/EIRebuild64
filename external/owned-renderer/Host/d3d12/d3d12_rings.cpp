// Upload and readback rings; see d3d12_rings.h.
#include "d3d12_rings.h"
#include "d3d12_memory.h"

#include <chrono>
#include <cstring>

namespace remote { void host_log(const char* format, ...); }

namespace d3d12 {

namespace {

ComPtr<ID3D12Resource> create_buffer(ID3D12Device* device, D3D12_HEAP_TYPE heap, std::uint64_t bytes,
                                     D3D12_RESOURCE_STATES state, const wchar_t* name, std::string& reason) {
    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = heap;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> buffer;
    const HRESULT hr = device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                                                       IID_PPV_ARGS(&buffer));
    if (FAILED(hr)) {
        reason = hresult_text(heap == D3D12_HEAP_TYPE_UPLOAD ? "CreateCommittedResource (upload ring)"
                                                             : "CreateCommittedResource (readback ring)", hr);
        return nullptr;
    }
    buffer->SetName(name);
    track_memory(device, buffer.Get(), MemoryKind::upload);  // or readback, by its heap
    return buffer;
}

std::uint64_t align_up(std::uint64_t value, std::uint64_t alignment) noexcept {
    return (value + alignment - 1) & ~(alignment - 1);
}

// Size of a replacement buffer that holds size bytes at alignment.
std::uint64_t grown(std::uint64_t capacity, std::uint64_t size, std::uint64_t alignment) noexcept {
    std::uint64_t bytes = capacity ? capacity * 2 : 1u << 20;
    while (bytes < size + alignment) bytes *= 2;
    return bytes;
}

} // namespace

// ---- UploadRing --------------------------------------------------------------------

UploadRing::~UploadRing() { stop(); }

void UploadRing::stop() noexcept {
    {
        std::lock_guard<std::mutex> guard(lock_);
        stop_ = true;
    }
    wake_.notify_all();
    if (spares_thread_.joinable()) spares_thread_.join();
    std::lock_guard<std::mutex> guard(lock_);
    spares_.clear();
}

bool UploadRing::create(ID3D12Device* device, std::string& reason, const wchar_t* name) {
    stop();
    device_ = device;
    name_ = name;
    blocks_.clear();
    current_ = 0;
    head_ = 0;
    wanted_ = 0;
    stop_ = false;
    if (!add_block(reason)) return false;
    frame_stats_ = last_ = {};
    try {
        spares_thread_ = std::thread([this] { spares_loop(); });
    } catch (...) {
        // No background spares: every new block is created here.
    }
    return true;
}

bool UploadRing::make_block(Block& block, std::string& reason) const {
    block.buffer = create_buffer(device_, D3D12_HEAP_TYPE_UPLOAD, kBlockBytes, D3D12_RESOURCE_STATE_GENERIC_READ, name_, reason);
    if (!block.buffer) return false;
    const D3D12_RANGE none{0, 0};  // the CPU never reads it
    void* mapped = nullptr;
    const HRESULT hr = block.buffer->Map(0, &none, &mapped);
    if (FAILED(hr)) { reason = hresult_text("Map (upload block)", hr); block.buffer.Reset(); return false; }
    block.cpu = static_cast<std::uint8_t*>(mapped);
    block.gpu = block.buffer->GetGPUVirtualAddress();
    return true;
}

void UploadRing::spares_loop() noexcept {
    for (;;) {
        {
            std::unique_lock<std::mutex> guard(lock_);
            wake_.wait(guard, [this] { return stop_ || spares_.size() < wanted_; });
            if (stop_) return;
        }
        Block block;
        std::string reason;
        if (!make_block(block, reason)) {  // the render thread creates its own and names a failure there
            std::lock_guard<std::mutex> guard(lock_);
            wanted_ = 0;
            continue;
        }
        try {
            std::lock_guard<std::mutex> guard(lock_);
            if (stop_) return;
            spares_.push_back(std::move(block));
        } catch (...) {
        }
    }
}

bool UploadRing::add_block(std::string& reason) {
    Block block;
    bool spare = false;
    {
        std::lock_guard<std::mutex> guard(lock_);
        if (!spares_.empty()) {
            block = std::move(spares_.back());
            spares_.pop_back();
            spare = true;
        }
        // More are likely to follow: keep spares ready while blocks are wanted.
        wanted_ = spares_thread_.joinable() ? kSpares : 0;
    }
    wake_.notify_one();
    if (!spare) {
        const auto begin = std::chrono::steady_clock::now();
        if (!make_block(block, reason)) return false;
        ++frame_stats_.created;
        frame_stats_.create_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    }
    block.used_frame = frame_;
    blocks_.push_back(std::move(block));
    ++frame_stats_.added;
    added_frame_ = frame_;
    return true;
}

bool UploadRing::transient(Core& core, std::uint64_t size, UploadAllocation& out, std::string& reason) {
    ComPtr<ID3D12Resource> buffer = create_buffer(device_, D3D12_HEAP_TYPE_UPLOAD, size, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                  L"eid3d11 dedicated upload", reason);
    if (!buffer) return false;
    const D3D12_RANGE none{0, 0};  // the CPU never reads it
    void* mapped = nullptr;
    const HRESULT hr = buffer->Map(0, &none, &mapped);
    if (FAILED(hr)) { reason = hresult_text("Map (dedicated upload)", hr); return false; }
    out.cpu = static_cast<std::uint8_t*>(mapped);
    out.gpu = buffer->GetGPUVirtualAddress();
    out.resource = buffer.Get();
    out.offset = 0;  // a resource start meets every placement alignment
    core.release_later(buffer);  // kept until the recording frame finished
    ++frame_stats_.dedicated;
    frame_stats_.dedicated_bytes += size;
    return true;
}

bool UploadRing::allocate(Core& core, std::uint64_t size, std::uint64_t alignment, UploadAllocation& out, std::string& reason) {
    if (!size) size = 1;
    if (size >= kDedicatedBytes) return transient(core, size, out, reason);
    std::uint64_t start = align_up(head_, alignment);
    if (blocks_.empty() || start + size > kBlockBytes) {
        // The current block stays with its frames; the next is a block whose
        // frames all finished, else a new one.
        std::size_t next = blocks_.size();
        for (std::size_t index = 0; index < blocks_.size(); ++index)
            if (index != current_ && blocks_[index].fence <= completed_) { next = index; break; }
        if (next == blocks_.size() && !add_block(reason)) return false;
        current_ = next;
        start = head_ = 0;
    }
    Block& block = blocks_[current_];
    block.fence = kRecording;
    block.used_frame = frame_;
    head_ = start + size;
    frame_stats_.bytes += size;
    out.cpu = block.cpu + start;
    out.gpu = block.gpu + start;
    out.resource = block.buffer.Get();
    out.offset = start;
    return true;
}

bool UploadRing::push(Core& core, const void* data, std::uint64_t size, std::uint64_t alignment, UploadAllocation& out,
                      std::string& reason) {
    if (!allocate(core, size, alignment, out, reason)) return false;
    std::memcpy(out.cpu, data, static_cast<std::size_t>(size));
    return true;
}

void UploadRing::end_frame(std::uint64_t fence_value) {
    for (auto& block : blocks_)
        if (block.fence == kRecording) block.fence = fence_value;
    last_ = frame_stats_;
    frame_stats_ = {};
    ++frame_;
    // Blocks idle for kIdleFrames (all their frames finished) are released;
    // the current one stays. Spares go once no block was added for as long.
    for (std::size_t index = blocks_.size(); index-- > 0;) {
        const Block& block = blocks_[index];
        if (index == current_ || block.fence > completed_ || block.used_frame + kIdleFrames > frame_) continue;
        blocks_.erase(blocks_.begin() + std::ptrdiff_t(index));
        if (current_ > index) --current_;
    }
    if (added_frame_ + kIdleFrames <= frame_) {
        std::vector<Block> released;
        {
            std::lock_guard<std::mutex> guard(lock_);
            wanted_ = 0;
            released.swap(spares_);
        }
    }
}

void UploadRing::retire(std::uint64_t completed) noexcept {
    completed_ = completed;
}

// ---- ReadbackRing ------------------------------------------------------------------

bool ReadbackRing::create(ID3D12Device* device, std::uint64_t initial_bytes, std::string& reason) {
    device_ = device;
    return replace(initial_bytes, reason);
}

bool ReadbackRing::replace(std::uint64_t bytes, std::string& reason) {
    ComPtr<ID3D12Resource> buffer = create_buffer(device_, D3D12_HEAP_TYPE_READBACK, bytes, D3D12_RESOURCE_STATE_COPY_DEST,
                                                  L"eid3d11 readback ring", reason);
    if (!buffer) return false;
    void* mapped = nullptr;
    const HRESULT hr = buffer->Map(0, nullptr, &mapped);  // read after each ticket's fence
    if (FAILED(hr)) { reason = hresult_text("Map (readback ring)", hr); return false; }
    // Outstanding tickets hold the old buffer; its blocks are no longer tracked.
    buffer_ = buffer;
    cpu_ = static_cast<const std::uint8_t*>(mapped);
    capacity_ = bytes;
    head_ = used_ = 0;
    blocks_.clear();
    ++generation_;
    return true;
}

bool ReadbackRing::allocate(Core& core, std::uint64_t size, std::uint64_t alignment, ReadbackTicket& out, std::string& reason) {
    if (!size) size = 1;
    for (int attempt = 0; attempt < 2; ++attempt) {
        std::uint64_t start = align_up(head_, alignment), taken = start - head_ + size;
        if (start + size > capacity_) {
            taken = capacity_ - head_ + size;
            start = 0;
        }
        if (start + size <= capacity_ && used_ + taken <= capacity_) {
            used_ += taken;
            head_ = start + size;
            out.buffer = buffer_;
            out.cpu = cpu_ + start;
            out.offset = start;
            out.size = size;
            out.fence_value = core.recording_value();
            out.id = next_id_++;
            out.generation = generation_;
            blocks_.push_back({out.id, taken, false});
            return true;
        }
        if (attempt == 0 && !replace(grown(capacity_, size, alignment), reason)) return false;
    }
    reason = "readback ring allocation failed after growing";
    return false;
}

void ReadbackRing::release(ReadbackTicket& ticket) noexcept {
    if (ticket.buffer && ticket.generation == generation_) {
        for (Block& block : blocks_)
            if (block.id == ticket.id) { block.released = true; break; }
        while (!blocks_.empty() && blocks_.front().released) {
            used_ -= blocks_.front().bytes;
            blocks_.pop_front();
        }
    }
    ticket = ReadbackTicket{};
}

} // namespace d3d12
