#pragma once
// Native D3D12 backend of the x64 host, core (DX12 plan M1, sections 2.1-2.5):
// adapter and device with the SM 6.6 / binding tier 3 check, the direct
// queue, the frame fence with frames in flight and release by fence value,
// the two shader-visible descriptor heaps (directly indexed: persistent
// indices plus a per-frame transient ring region) and the one global root
// signature. Rings, timestamps, pipelines, barriers and the presenter have
// their own files beside this one.
//
// Threading: everything here belongs to the thread that records frames,
// except DescriptorHeap::allocate/free (locked; resource creation may run on
// workers) and the device itself (free-threaded).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "../../../../OwnedRenderer/Module/DeviceError.h"

namespace d3d12 {

using Microsoft::WRL::ComPtr;

// "call failed (0x887A0005 DXGI_ERROR_DEVICE_REMOVED)": a readable HRESULT.


// One shader-visible descriptor heap read by index from shaders
// (ResourceDescriptorHeap[] / SamplerDescriptorHeap[]). Indices
// [0, persistent) are allocated for a resource's lifetime and freed by fence;
// [persistent, persistent + transient) is a ring handed out per frame and
// reclaimed when that frame's fence completes. The transient ring is sized
// by demand: grow() replaces the heap with a larger one between frames
// (persistent descriptors are copied over at the same indices; the old heap
// is released by fence through the returned pointer).
class DescriptorHeap {
public:
    bool create(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, std::uint32_t persistent,
                std::uint32_t transient, std::string& reason);
    ID3D12DescriptorHeap* heap() const noexcept { return heap_.Get(); }
    // A heap with a transient ring of `transient` descriptors; the previous
    // heap comes back in `previous` (keep it alive until frames in flight end).
    // persistent may grow as well (bindless indices); indices keep their
    // descriptors and positions.
    bool grow(ID3D12Device* device, std::uint32_t persistent, std::uint32_t transient, ComPtr<ID3D12DescriptorHeap>& previous,
              std::string& reason);
    std::uint32_t persistent_capacity() const noexcept { return persistent_; }
    // Persistent indices in use (allocated and not yet returned).
    std::uint32_t persistent_used() noexcept;
    std::uint32_t transient_capacity() const noexcept { return transient_; }
    // Ring descriptors not held by this frame or the frames in flight.
    std::uint32_t transient_free() const noexcept { return transient_ - used_; }
    std::uint32_t size() const noexcept { return persistent_ + transient_; }
    std::uint32_t stride() const noexcept { return stride_; }

    // A persistent index, or UINT32_MAX when every one is in use (logged by
    // the caller as a named failure, never wrapped).
    std::uint32_t allocate() noexcept;
    // count consecutive persistent indices that are never returned (contents
    // written once), or UINT32_MAX when the region has no such run left.
    std::uint32_t allocate_block(std::uint32_t count) noexcept;
    // The index returns to the free list once fence_value has completed.
    void free(std::uint32_t index, std::uint64_t fence_value) noexcept;

    // count consecutive transient indices for the frame being recorded, or
    // UINT32_MAX when the ring is full (frames in flight hold the rest).
    std::uint32_t transient(std::uint32_t count) noexcept;
    // Frame boundaries: end_frame marks what the frame took with its fence
    // value; retire reclaims transient ranges and freed indices up to completed.
    void end_frame(std::uint64_t fence_value) noexcept;
    void retire(std::uint64_t completed) noexcept;

    D3D12_CPU_DESCRIPTOR_HANDLE cpu(std::uint32_t index) const noexcept { return {cpu_.ptr + std::size_t(index) * stride_}; }
    // Persistent views are authored in a CPU-readable heap, then published.
    // Heap growth copies that heap; shader-visible heaps are copy destinations only.
    D3D12_CPU_DESCRIPTOR_HANDLE persistent_cpu(std::uint32_t index) const noexcept {
        return {persistent_cpu_.ptr + std::size_t(index) * stride_};
    }
    // Also into each replaced heap a recorded or in-flight frame still binds:
    // after a growth during the frame, commands recorded before the switch
    // read the old heap, and GPU-driven passes among them may read records
    // written later in the frame with an index published after the switch.
    void publish(ID3D12Device* device, std::uint32_t index) noexcept {
        device->CopyDescriptorsSimple(1, cpu(index), persistent_cpu(index), type_);
        for (const auto& old : previous_)
            if (index < old.persistent)
                device->CopyDescriptorsSimple(1, {old.cpu.ptr + std::size_t(index) * stride_}, persistent_cpu(index), type_);
    }
    D3D12_GPU_DESCRIPTOR_HANDLE gpu(std::uint32_t index) const noexcept { return {gpu_.ptr + std::uint64_t(index) * stride_}; }

private:
    ComPtr<ID3D12DescriptorHeap> heap_, persistent_heap_;
    D3D12_CPU_DESCRIPTOR_HANDLE persistent_cpu_{};
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_{};
    std::uint32_t stride_ = 0;
    std::mutex mutex_;  // persistent allocation only
    std::uint32_t persistent_ = 0, next_unused_ = 0;
    std::vector<std::uint32_t> free_;
    std::deque<std::pair<std::uint64_t, std::uint32_t>> pending_free_;  // fence value, index
    // Transient ring over [persistent_, persistent_ + transient_): head is the
    // next index, tail the oldest still in flight, used the count in flight.
    std::uint32_t transient_ = 0, head_ = 0, used_ = 0, frame_taken_ = 0;
    std::deque<std::pair<std::uint64_t, std::uint32_t>> frames_;  // fence value, count taken
    // Heaps replaced by grow(), until the last frame that can bind them
    // completes (fence 0: the frame being recorded; end_frame sets it).
    struct Replaced {
        ComPtr<ID3D12DescriptorHeap> heap;
        D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
        std::uint32_t persistent = 0;
        std::uint64_t fence = 0;
    };
    std::vector<Replaced> previous_;
    D3D12_DESCRIPTOR_HEAP_TYPE type_ = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
};

// CPU-only (non-shader-visible) descriptors: every resource's own view,
// copied into the shader-visible ring for the draws that read it. Grows in
// blocks; a freed descriptor is reusable at once (copies happen on the CPU
// when recorded).
class StagingDescriptors {
public:
    bool create(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, std::string& reason);
    // A handle, or {0} when a new block could not be created (named by the caller).
    D3D12_CPU_DESCRIPTOR_HANDLE allocate() noexcept;
    void free(D3D12_CPU_DESCRIPTOR_HANDLE handle) noexcept;
    std::size_t blocks() const noexcept { return blocks_.size(); }
    // Counts allocations and frees: while it is unchanged no handle was
    // handed out again, so an equal handle still names the same view (the
    // executor reuses a copied table only then).
    std::uint64_t epoch() const noexcept { return epoch_; }

private:
    static constexpr std::uint32_t kBlock = 1024;
    ID3D12Device* device_ = nullptr;
    D3D12_DESCRIPTOR_HEAP_TYPE type_ = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    std::uint32_t stride_ = 0;
    std::vector<ComPtr<ID3D12DescriptorHeap>> blocks_;
    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> free_;
    std::uint64_t epoch_ = 0;
};

// Root signature parameter slots (plan 2.3), shared by graphics and compute.
// Native D3D12 programs bind in register space 1 (b0-b3, s0-s3 static
// samplers) and read resources from the heaps by index. Space 0 is the D3D11
// register model, so the renderer's shared HLSL compiles to DXIL unchanged:
// the per-draw constant buffer b0 as a root CBV and tables for b1-b13,
// t0-t127, u0-u7 and s0-s15 pointing at transient heap ranges the executor
// fills per draw (unwritten slots are allowed: the ranges are volatile).
enum RootSlot : UINT {
    kRootConstants = 0,  // b0 space1: kRootConstantCount DWORDs (draw/batch id, pass id, per-pass indices)
    kRootFrame = 1,      // b1 space1: frame constants (camera, lighting, fog, wind, time)
    kRootPass = 2,       // b2 space1: pass parameter block (upload ring address)
    kRootDraw = 3,       // b3 space1: per-draw ring range (CPU-drawn UI and effect paths)
    kRootD3D11Constants = 4,  // b0 space0: root CBV
    kRootD3D11ConstantTable = 5,  // b1-b13 space0
    kRootD3D11Resources = 6,  // t0-t127 space0
    kRootD3D11Unordered = 7,  // u0-u7 space0
    kRootD3D11Samplers = 8,   // s0-s15 space0 (sampler heap)
    kRootGpuReport = 9,       // u0 space3: root UAV, the native programs' GPU report buffer (EI_RENDERER_D3D12_HAZARDS)
    kRootD3D11RawConstants = 10,  // t0 space4: root raw view of the b0 block (native_shader.h reads b0 through it)
    kRootD3D11RawLighting = 11,   // t1 space4: root raw view of the b1 block
    kRootSlotCount = 12,
};
constexpr UINT kRootConstantCount = 8;
// The D3D11 register model's slot counts (space 0 tables).
constexpr UINT kD3D11ConstantTableSlots = 13, kD3D11ResourceSlots = 128, kD3D11UnorderedSlots = 16, kD3D11SamplerSlots = 16;

class Core {
public:
    // Frames the CPU may record ahead of the GPU.
    static constexpr unsigned kFramesInFlight = 3;
    // CBV/SRV/UAV heap, sized by demand (the API-limit million cost 30.6 MB
    // of video memory, M1a measurement): a small persistent region (the
    // bring-up's own indices; the executor's resources keep CPU staging
    // descriptors instead) and the per-frame transient ring, which starts at
    // kViewTransientStart and grows (views().grow) when a frame needed more,
    // up to the tier-2 guaranteed limit. Samplers: the API maximum of a
    // shader-visible sampler heap (2048 descriptors, 64 KB), all persistent.
    static constexpr std::uint32_t kViewDescriptorLimit = D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_2;
    // Bindless start: a whole map load's textures arrive in one frame (a zone12 ->
    // zone13 travel took ~1350, and 1024 dropped that frame's draws while it grew
    // next frame). Growing inside the frame is not safe: passes recorded before the
    // switch read records written later with indices the old heap does not hold
    // (TDR). 16384 views cost 512 KB; the region still doubles by demand.
    static constexpr std::uint32_t kViewPersistent = 16384;
    static constexpr std::uint32_t kViewTransientStart = 16384;
    static constexpr std::uint32_t kSamplerDescriptors = D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE;
    // Persistent samplers: the UI's single samplers and the native draws'
    // s0-s15 tables, each distinct set written once and kept (Executor
    // sampler_tables_); the ring holds per-frame tables only once that region
    // is full. With the 128-descriptor table padding they fill the 2048 maximum.
    static constexpr std::uint32_t kSamplerPersistent = 1408, kSamplerTransient = 512;

    ~Core();
    // Adapter 0 (the one the D3D11 host's D3D11CreateDevice(nullptr) uses),
    // feature level 11_0, then the M1 requirements: shader model 6.6 and
    // resource binding tier 3. On failure reason names the missing
    // capability or the failed call; nothing is left half-created.
    // debug_layer: the D3D12 debug layer (EI_RENDERER_X64_D3D12_DEBUG=1).
    bool create(bool debug_layer, std::string& reason, bool gpu_validation = true);
    // Empty when the device came from the process's runtime (the bundled
    // Agility runtime, or Windows' own where that is newer); otherwise why it
    // came from Windows' own runtime through a device factory instead.
    const std::string& runtime_note() const noexcept { return runtime_note_; }

    ID3D12Device* device() const noexcept { return device_.Get(); }
    IDXGIAdapter1* adapter() const noexcept { return adapter_.Get(); }
    ID3D12CommandQueue* queue() const noexcept { return queue_.Get(); }
    IDXGIFactory4* factory() const noexcept { return factory_.Get(); }
    ID3D12RootSignature* root_signature() const noexcept { return root_.Get(); }
    const std::wstring& adapter_name() const noexcept { return adapter_name_; }
    DescriptorHeap& views() noexcept { return views_; }
    DescriptorHeap& samplers() noexcept { return samplers_; }
    // The descriptor heaps' measured memory cost (one log line).
    const std::string& heap_cost() const noexcept { return heap_cost_; }
    // CPU staging descriptors (CBV/SRV/UAV) for resources' own views.
    StagingDescriptors& staging() noexcept { return staging_; }
    // Between frames (no list open): a transient ring of `transient`
    // descriptors; the old heap is released once frames in flight finish.
    bool grow_views(std::uint32_t persistent, std::uint32_t transient, std::string& reason);

    // Frame: begin_frame waits until the slot's previous frame finished,
    // releases what was deferred up to the completed fence, resets the slot's
    // allocator and returns the open list with the heaps and root signature
    // set. end_frame closes and submits it and signals the frame's fence
    // value (returned). Failures are device-level: false/0 with reason.
    ID3D12GraphicsCommandList* begin_frame(std::string& reason);
    std::uint64_t end_frame(std::string& reason);
    // The fence value the frame being recorded will signal.
    std::uint64_t recording_value() const noexcept { return next_value_; }
    std::uint64_t completed() const noexcept { return fence_->GetCompletedValue(); }
    ID3D12Fence* fence() const noexcept { return fence_.Get(); }  // the direct queue's frame fence (the FG pacer waits on it)
    std::uint64_t frame_number() const noexcept { return frame_number_; }
    unsigned frame_slot() const noexcept { return slot_; }
    // Waits for the direct queue's current tail, including work queued by
    // Present/overlays after end_frame's signal. Its separate idle fence
    // leaves an open list's recording/resource fence value unchanged.
    bool wait_idle() noexcept;
    // Waits only for submitted frame lists, not a later Present/overlay
    // tail. The frame executor uses this while its current list is open.
    bool wait_submitted() noexcept { return next_value_ <= 1 || wait_for(next_value_ - 1); }

    // Keeps object alive until the frame being recorded has finished.
    void release_later(ComPtr<IUnknown> object);
    // Binds the global root signature and heaps (begin_frame does it for its
    // list; a second list recorded in the frame calls this).
    void bind(ID3D12GraphicsCommandList* list) const noexcept;

    // The removal reason when the device is gone, else S_OK.
    HRESULT removed_reason() const noexcept { return device_ ? device_->GetDeviceRemovedReason() : S_OK; }
    // After a removal: the reason code and what DRED recorded (the next GPU
    // operation of each unfinished command list, a GPU page fault and the
    // allocations at its address). Empty while the device is alive.
    std::string removal_report() const;

private:
    bool wait_for(std::uint64_t value) noexcept;
    void retire(std::uint64_t completed) noexcept;

    ComPtr<IDXGIFactory4> factory_;
    ComPtr<IDXGIAdapter1> adapter_;
    std::wstring adapter_name_;
    std::string runtime_note_;
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<ID3D12Fence> fence_;
    HANDLE fence_event_ = nullptr;
    std::uint64_t next_value_ = 1;  // the value the next submission signals
    ComPtr<ID3D12Fence> idle_fence_;  // queue-tail waits, independent of recorded frames
    HANDLE idle_event_ = nullptr;
    std::uint64_t next_idle_value_ = 1;
    struct Frame {
        ComPtr<ID3D12CommandAllocator> allocator;
        std::uint64_t fence_value = 0;
    };
    Frame frames_[kFramesInFlight];
    ComPtr<ID3D12GraphicsCommandList> list_;
    unsigned slot_ = 0;
    std::uint64_t frame_number_ = 0;
    std::deque<std::pair<std::uint64_t, ComPtr<IUnknown>>> releases_;  // fence value, object
    DescriptorHeap views_, samplers_;
    StagingDescriptors staging_;
    std::string heap_cost_;
    ComPtr<ID3D12RootSignature> root_;
};

} // namespace d3d12
