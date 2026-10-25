#pragma once
// Per-draw constant ring (issue #162).
//
// Small per-draw constant blocks are appended into one dynamic constant
// buffer with MAP_WRITE_NO_OVERWRITE and bound by range with the D3D11.1
// *SetConstantBuffers1 calls (first constant and count in 16-constant steps).
// Only the first write of a frame or command list, and a wrap, discard, so
// the driver renames the buffer a handful of times per frame instead of once
// per draw (a single dynamic buffer discarded ~900 times a frame ran out of
// renamed copies and stalled the game thread).
//
// Why discard at each frame start rather than fenced frame slots (the
// GD3D11 ConstantBufferPool design): a discard per frame needs no fence, and
// nothing written since the last discard is ever overwritten.
//
// Ownership: the thread that owns the renderer's immediate context (the
// game thread, or the scene thread while render_thread is on); never a
// deferred context. Sized by demand: a ring that wrapped since the last
// restart is recreated at the next restart with room for the whole demand.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
#include "x64_remote_device.h"

namespace owned {

class ConstantRing {
public:
    // A written block: bind with *SetConstantBuffers1(slot, 1, &buffer, &first, &count).
    struct Range { ID3D11Buffer* buffer = nullptr; UINT first = 0; UINT count = 0; };
    static constexpr UINT kAlign = 256;              // 16 constants: the D3D11.1 offset and size step
    static constexpr UINT kInitialBytes = 64u * 1024u;

    HRESULT write(ID3D11Device* device, ID3D11DeviceContext* context, const void* data, UINT bytes, Range& out) noexcept {
        if (!device || !context || !data || !bytes) return E_INVALIDARG;
        const UINT size = (bytes + kAlign - 1u) & ~(kAlign - 1u);
        if (!buffer_ || size > capacity_) {
            UINT capacity = std::max(capacity_, kInitialBytes);
            while (capacity < size) capacity *= 2u;
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = capacity;
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            buffer_.Reset();
            const HRESULT hr = device->CreateBuffer(&desc, nullptr, &buffer_);
            if (FAILED(hr)) { capacity_ = 0; return hr; }
            capacity_ = capacity;
            offset_ = 0;
            discard_ = true;
            ++generation_;
        }
        if (offset_ + size > capacity_) { offset_ = 0; discard_ = true; wrapped_ = true; ++generation_; }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        x64remote::declare_map_range(context, buffer_.Get(), offset_, bytes);
        const HRESULT hr = context->Map(buffer_.Get(), 0, discard_ ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE, 0, &mapped);
        if (FAILED(hr)) return hr;
        std::memcpy(static_cast<std::uint8_t*>(mapped.pData) + offset_, data, bytes);
        context->Unmap(buffer_.Get(), 0);
        out.buffer = buffer_.Get();
        out.first = offset_ / 16u;
        out.count = size / 16u;
        offset_ += size;
        used_ += size;
        discard_ = false;
        return S_OK;
    }

    // Start of a frame or command list: the next write discards, and every
    // earlier range is stale. A ring that wrapped since the last restart
    // grows to that whole demand (recreated at the next write).
    void restart() noexcept {
        if (wrapped_) {
            UINT capacity = std::max(capacity_, kInitialBytes);
            while (capacity < used_ && capacity < 0x40000000u) capacity *= 2u;
            capacity_ = capacity;
            buffer_.Reset();
        }
        offset_ = used_ = 0;
        discard_ = true;
        wrapped_ = false;
        ++generation_;
    }

    // Device loss or teardown.
    void release() noexcept {
        buffer_.Reset();
        capacity_ = offset_ = used_ = 0;
        discard_ = true;
        wrapped_ = false;
        ++generation_;
    }

    // Changes whenever earlier ranges may no longer hold their bytes: a
    // restart, a wrap or a regrowth (a discard renames the whole buffer, so a
    // range still bound from before reads the new contents).
    std::uint32_t generation() const noexcept { return generation_; }
    UINT capacity() const noexcept { return capacity_; }

private:
    Microsoft::WRL::ComPtr<ID3D11Buffer> buffer_;
    UINT capacity_ = 0;
    UINT offset_ = 0;
    UINT used_ = 0;        // bytes written since the last restart, wraps included
    bool discard_ = true;
    bool wrapped_ = false;
    std::uint32_t generation_ = 1;
};

// A VS and PS constant-buffer slot saved with its D3D11.1 range, for state
// guards. A plain *GetConstantBuffers/*SetConstantBuffers pair returns only
// the buffer and rebinds it from its first constant, so a guard restoring a
// ring range that way hands the next draws the ring's first block (the F11
// capture's per-pass guard did exactly that between native batches).
struct SavedConstantSlot {
    ID3D11Buffer* vs = nullptr;
    ID3D11Buffer* ps = nullptr;
    UINT vs_first = 0, vs_count = 0, ps_first = 0, ps_count = 0;

    void save(ID3D11DeviceContext* context, UINT slot) noexcept {
        Microsoft::WRL::ComPtr<ID3D11DeviceContext1> ranged;
        if (SUCCEEDED(context->QueryInterface(IID_PPV_ARGS(&ranged)))) {
            ranged->VSGetConstantBuffers1(slot, 1, &vs, &vs_first, &vs_count);
            ranged->PSGetConstantBuffers1(slot, 1, &ps, &ps_first, &ps_count);
        } else {
            context->VSGetConstantBuffers(slot, 1, &vs);
            context->PSGetConstantBuffers(slot, 1, &ps);
        }
    }
    // A range starting at the first constant is rebound plainly (the shader
    // sees the same constants); any other through the 11.1 call.
    void restore(ID3D11DeviceContext* context, UINT slot) const noexcept {
        Microsoft::WRL::ComPtr<ID3D11DeviceContext1> ranged;
        if ((vs_first || ps_first) && SUCCEEDED(context->QueryInterface(IID_PPV_ARGS(&ranged)))) {
            ranged->VSSetConstantBuffers1(slot, 1, &vs, &vs_first, &vs_count);
            ranged->PSSetConstantBuffers1(slot, 1, &ps, &ps_first, &ps_count);
            return;
        }
        ID3D11Buffer* const vertex = vs;
        ID3D11Buffer* const pixel = ps;
        context->VSSetConstantBuffers(slot, 1, &vertex);
        context->PSSetConstantBuffers(slot, 1, &pixel);
    }
    void release() noexcept {
        if (vs) vs->Release();
        if (ps) ps->Release();
        vs = ps = nullptr;
    }
};

// Per-frame upload ring for dynamic structured buffers read as SRVs
// (instance updates, pose inputs; issue #162 follow-up).
//
// A DISCARD of such a buffer that was also mapped NO_OVERWRITE stalled 5-14
// ms (Suslanger route, rt0 and rt1, db87ce42): ConstantRing's frame-start
// discard does not carry over from constant buffers. This ring never
// discards a buffer the GPU may still read. Each frame appends with
// NO_OVERWRITE into one slot buffer; at the frame boundary the slot gets an
// event query, and a slot is written again only after its query reports the
// GPU done. When the next slot is still in flight a new slot is added
// instead of waiting, so the slot count follows the real GPU latency. A slot
// that overflows mid-frame gets a larger buffer (the old one stays alive for
// the commands already recorded); slots grow to twice the largest frame seen.
// The first Map of a new buffer is a DISCARD of a buffer nothing has used.
//
// Ownership: the thread that owns the immediate context, like ConstantRing.
class FencedUploadRing {
public:
    explicit FencedUploadRing(UINT stride) noexcept : stride_(stride) {}

    // Appends one block; srv views the whole slot buffer (made only when the
    // buffer is created) and first is the block's first element.
    HRESULT write(ID3D11Device* device, ID3D11DeviceContext* context, const void* data, UINT bytes,
                  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& srv, UINT& first) noexcept try {
        if (!device || !context || !data || !bytes || bytes > 0x40000000u) return E_INVALIDARG;
        if (slots_.empty()) slots_.resize(1);
        Slot& slot = slots_[current_];
        const UINT size = (bytes + stride_ - 1u) / stride_ * stride_;
        bool fresh = false;
        if (!slot.buffer || slot.used + std::uint64_t(size) > slot.capacity) {
            std::uint64_t want = std::max<std::uint64_t>({std::uint64_t(slot.capacity) * 2u, std::uint64_t(largest_frame_) * 2u,
                                                          std::uint64_t(size) * 2u, kInitialBytes});
            want = std::min<std::uint64_t>(want, 0x40000000u);
            const UINT capacity = UINT((want + stride_ - 1u) / stride_ * stride_);
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = capacity;
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            desc.StructureByteStride = stride_;
            Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
            HRESULT hr = device->CreateBuffer(&desc, nullptr, &buffer);
            if (FAILED(hr)) return hr;
            if (FAILED(hr = device->CreateShaderResourceView(buffer.Get(), nullptr, &view))) return hr;
            slot.buffer = std::move(buffer); slot.srv = std::move(view);
            slot.capacity = capacity; slot.used = 0; fresh = true;
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        x64remote::declare_map_range(context, slot.buffer.Get(), slot.used, bytes);
        const HRESULT hr = context->Map(slot.buffer.Get(), 0, fresh ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE, 0, &mapped);
        if (FAILED(hr)) return hr;
        std::memcpy(static_cast<std::uint8_t*>(mapped.pData) + slot.used, data, bytes);
        context->Unmap(slot.buffer.Get(), 0);
        first = slot.used / stride_;
        slot.used += size;
        frame_bytes_ += size;
        srv = slot.srv;
        return S_OK;
    } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }

    // Frame boundary: fence the slot written this frame and move to a slot
    // the GPU has finished with (or a new one). Nothing to do for an unused frame.
    void end_frame(ID3D11Device* device, ID3D11DeviceContext* context) noexcept try {
        if (slots_.empty() || !frame_bytes_ || !device || !context) return;
        largest_frame_ = std::max(largest_frame_, frame_bytes_);
        frame_bytes_ = 0;
        Slot& slot = slots_[current_];
        if (!slot.fence) {
            const D3D11_QUERY_DESC desc{D3D11_QUERY_EVENT, 0};
            if (FAILED(device->CreateQuery(&desc, &slot.fence))) slot.fence.Reset();
        }
        if (slot.fence) { context->End(slot.fence.Get()); slot.pending = true; }
        else slot.buffer.Reset(); // no fence: never reuse this buffer (a new one is made)
        const std::size_t next = (current_ + 1) % slots_.size();
        Slot& candidate = slots_[next];
        if (next != current_ && (!candidate.pending || finished(context, candidate))) {
            candidate.pending = false; candidate.used = 0; current_ = next;
            return;
        }
        slots_.insert(slots_.begin() + std::ptrdiff_t(current_ + 1), Slot{});
        ++current_;
    } catch (const std::bad_alloc&) {}

    void release() noexcept {
        slots_.clear();
        current_ = 0;
        frame_bytes_ = largest_frame_ = 0;
    }

    std::uint64_t capacity_bytes() const noexcept {
        std::uint64_t total = 0;
        for (const auto& slot : slots_) total += slot.buffer ? slot.capacity : 0u;
        return total;
    }

private:
    static constexpr UINT kInitialBytes = 64u * 1024u;
    struct Slot {
        Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
        Microsoft::WRL::ComPtr<ID3D11Query> fence;
        UINT capacity = 0, used = 0;
        bool pending = false;
    };
    static bool finished(ID3D11DeviceContext* context, const Slot& slot) noexcept {
        BOOL done = FALSE;
        return slot.fence && context->GetData(slot.fence.Get(), &done, sizeof(done), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK && done;
    }

    std::vector<Slot> slots_;
    std::size_t current_ = 0;
    UINT stride_;
    UINT frame_bytes_ = 0, largest_frame_ = 0;
};

} // namespace owned
