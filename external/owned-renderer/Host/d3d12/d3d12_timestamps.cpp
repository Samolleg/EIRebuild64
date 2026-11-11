// Per-pass GPU timestamps; see d3d12_timestamps.h.
#include "d3d12_timestamps.h"

#include <algorithm>

namespace d3d12 {

bool Timestamps::create(ID3D12Device* device, ID3D12CommandQueue* queue, std::string& reason, std::uint32_t initial_passes) {
    device_ = device;
    UINT64 frequency = 0;
    const HRESULT hr = queue->GetTimestampFrequency(&frequency);
    if (FAILED(hr) || !frequency) { reason = hresult_text("ID3D12CommandQueue::GetTimestampFrequency", hr); return false; }
    ticks_per_ms_ = double(frequency) / 1000.0;
    for (Slot& slot : slots_)
        if (!create_slot(slot, initial_passes, reason)) return false;
    return true;
}

bool Timestamps::create_slot(Slot& slot, std::uint32_t passes, std::string& reason) {
    D3D12_QUERY_HEAP_DESC desc{};
    desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    desc.Count = passes * 2;
    ComPtr<ID3D12QueryHeap> heap;
    HRESULT hr = device_->CreateQueryHeap(&desc, IID_PPV_ARGS(&heap));
    if (FAILED(hr)) { reason = hresult_text("CreateQueryHeap (timestamps)", hr); return false; }
    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = std::uint64_t(passes) * 2 * sizeof(UINT64);
    buffer.Height = 1;
    buffer.DepthOrArraySize = 1;
    buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> readback;
    hr = device_->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                          IID_PPV_ARGS(&readback));
    if (FAILED(hr)) { reason = hresult_text("CreateCommittedResource (timestamp readback)", hr); return false; }
    slot.heap = heap;
    slot.readback = readback;
    slot.capacity = passes;
    slot.pending = false;
    return true;
}

bool Timestamps::begin_frame(Core& core, std::string& reason) {
    return begin_frame(core.frame_slot(), reason);
}

bool Timestamps::begin_frame(unsigned index, std::string& reason) {
    Slot& slot = slots_[index];
    if (slot.pending) {
        const std::uint32_t timed = std::uint32_t((std::min)(slot.names.size(), std::size_t(slot.capacity)));
        const D3D12_RANGE range{0, std::size_t(timed) * 2 * sizeof(UINT64)};
        void* mapped = nullptr;
        if (timed && SUCCEEDED(slot.readback->Map(0, &range, &mapped))) {
            const auto* ticks = static_cast<const UINT64*>(mapped);
            last_.resize(timed);
            for (std::uint32_t i = 0; i < timed; ++i) {
                last_[i].name = slot.names[i];
                last_[i].ms = ticks[2 * i + 1] >= ticks[2 * i] ? double(ticks[2 * i + 1] - ticks[2 * i]) / ticks_per_ms_ : 0.0;
            }
            const D3D12_RANGE none{0, 0};
            slot.readback->Unmap(0, &none);
        } else {
            last_.clear();
        }
        last_untimed_ = slot.wanted > slot.capacity ? slot.wanted - slot.capacity : 0;
        slot.pending = false;
    }
    // The slot's queries finished with its previous frame: safe to replace.
    if (slot.wanted > slot.capacity) {
        std::uint32_t passes = slot.capacity;
        while (passes < slot.wanted) passes *= 2;
        if (!create_slot(slot, passes, reason)) return false;
    }
    slot.names.clear();
    slot.wanted = 0;
    current_ = &slot;
    return true;
}

std::uint32_t Timestamps::begin(ID3D12GraphicsCommandList* list, const char* name) {
    Slot& slot = *current_;
    const std::uint32_t pass = slot.wanted++;
    if (pass >= slot.capacity) return pass;  // counted, untimed this frame
    slot.names.emplace_back(name);
    list->EndQuery(slot.heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, pass * 2);
    return pass;
}

void Timestamps::end(ID3D12GraphicsCommandList* list, std::uint32_t pass) noexcept {
    Slot& slot = *current_;
    if (pass < slot.capacity) list->EndQuery(slot.heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, pass * 2 + 1);
}

void Timestamps::resolve(ID3D12GraphicsCommandList* list) noexcept {
    Slot& slot = *current_;
    const std::uint32_t timed = (std::min)(slot.wanted, slot.capacity);
    if (timed) list->ResolveQueryData(slot.heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, timed * 2, slot.readback.Get(), 0);
    slot.pending = true;
}

} // namespace d3d12
