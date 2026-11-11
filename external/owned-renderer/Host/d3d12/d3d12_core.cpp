// Native D3D12 backend core; see d3d12_core.h.
#include "frame_waits.h"
#include "d3d12_core.h"

#include <psapi.h>

#include <cstdio>

namespace remote { void host_log(const char* format, ...); }

namespace d3d12 {

std::string hresult_text(const char* call, HRESULT hr) {
    const char* name = "";
    switch (hr) {
    case DXGI_ERROR_DEVICE_REMOVED: name = " DXGI_ERROR_DEVICE_REMOVED"; break;
    case DXGI_ERROR_DEVICE_HUNG: name = " DXGI_ERROR_DEVICE_HUNG"; break;
    case DXGI_ERROR_DEVICE_RESET: name = " DXGI_ERROR_DEVICE_RESET"; break;
    case DXGI_ERROR_DRIVER_INTERNAL_ERROR: name = " DXGI_ERROR_DRIVER_INTERNAL_ERROR"; break;
    case DXGI_ERROR_INVALID_CALL: name = " DXGI_ERROR_INVALID_CALL"; break;
    case DXGI_ERROR_UNSUPPORTED: name = " DXGI_ERROR_UNSUPPORTED"; break;
    case E_OUTOFMEMORY: name = " E_OUTOFMEMORY"; break;
    case E_INVALIDARG: name = " E_INVALIDARG"; break;
    default: break;
    }
    char text[256];
    std::snprintf(text, sizeof(text), "%s failed (0x%08lX%s)", call, static_cast<unsigned long>(hr), name);
    return text;
}

// ---- DescriptorHeap ----------------------------------------------------------------

bool DescriptorHeap::create(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, std::uint32_t persistent,
                            std::uint32_t transient, std::string& reason) {
    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = type;
    // A table of the D3D11 register model spans up to 128 descriptors from
    // its first: the padding keeps a table starting anywhere in the ring
    // inside the heap (its unwritten tail is volatile and never read).
    desc.NumDescriptors = persistent + transient + (transient ? kD3D11ResourceSlots : 0u);
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    const HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap_));
    if (FAILED(hr)) {
        reason = hresult_text(type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER ? "CreateDescriptorHeap (samplers)"
                                                                          : "CreateDescriptorHeap (CBV/SRV/UAV)", hr);
        return false;
    }
    cpu_ = heap_->GetCPUDescriptorHandleForHeapStart();
    gpu_ = heap_->GetGPUDescriptorHandleForHeapStart();
    stride_ = device->GetDescriptorHandleIncrementSize(type);
    if (persistent) {
        desc.NumDescriptors = persistent;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        const HRESULT staging_hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&persistent_heap_));
        if (FAILED(staging_hr)) { reason = hresult_text("CreateDescriptorHeap (persistent CPU views)", staging_hr); return false; }
        persistent_cpu_ = persistent_heap_->GetCPUDescriptorHandleForHeapStart();
    }
    persistent_ = persistent;
    transient_ = transient;
    type_ = type;
    return true;
}

bool DescriptorHeap::grow(ID3D12Device* device, std::uint32_t persistent, std::uint32_t transient,
                          ComPtr<ID3D12DescriptorHeap>& previous, std::string& reason) {
    if (persistent < persistent_) persistent = persistent_;
    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = type_;
    desc.NumDescriptors = persistent + transient + kD3D11ResourceSlots;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ComPtr<ID3D12DescriptorHeap> heap;
    const HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap));
    if (FAILED(hr)) { reason = hresult_text("CreateDescriptorHeap (larger CBV/SRV/UAV ring)", hr); return false; }
    // Shader-visible descriptors cannot be a copy source. Keep the same
    // persistent indices using the CPU-readable views that authored them.
    ComPtr<ID3D12DescriptorHeap> persistent_heap;
    if (persistent > persistent_) {
        desc.NumDescriptors = persistent;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        const HRESULT staging_hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&persistent_heap));
        if (FAILED(staging_hr)) { reason = hresult_text("CreateDescriptorHeap (larger persistent CPU views)", staging_hr); return false; }
        const auto destination = persistent_heap->GetCPUDescriptorHandleForHeapStart();
        if (next_unused_) device->CopyDescriptorsSimple(next_unused_, destination, persistent_cpu_, type_);
    }
    const auto source = persistent_heap ? persistent_heap->GetCPUDescriptorHandleForHeapStart() : persistent_cpu_;
    if (next_unused_) device->CopyDescriptorsSimple(next_unused_, heap->GetCPUDescriptorHandleForHeapStart(), source, type_);
    if (persistent_heap) { persistent_heap_ = std::move(persistent_heap); persistent_cpu_ = source; }
    previous = std::move(heap_);
    heap_ = std::move(heap);
    cpu_ = heap_->GetCPUDescriptorHandleForHeapStart();
    gpu_ = heap_->GetGPUDescriptorHandleForHeapStart();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        persistent_ = persistent;
    }
    // The old ring's ranges stay in the old heap with the frames using them.
    transient_ = transient;
    head_ = used_ = frame_taken_ = 0;
    frames_.clear();
    return true;
}

bool StagingDescriptors::create(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, std::string& reason) {
    device_ = device;
    type_ = type;
    stride_ = device->GetDescriptorHandleIncrementSize(type);
    if (!allocate().ptr) { reason = "CreateDescriptorHeap (staging descriptors) failed"; return false; }
    free_.clear();
    const auto start = blocks_.front()->GetCPUDescriptorHandleForHeapStart();
    for (std::uint32_t index = kBlock; index-- > 0;) free_.push_back({start.ptr + std::size_t(index) * stride_});
    return true;
}

D3D12_CPU_DESCRIPTOR_HANDLE StagingDescriptors::allocate() noexcept try {
    if (free_.empty()) {
        D3D12_DESCRIPTOR_HEAP_DESC desc{};
        desc.Type = type_;
        desc.NumDescriptors = kBlock;
        ComPtr<ID3D12DescriptorHeap> heap;
        if (FAILED(device_->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap)))) return {};
        const auto start = heap->GetCPUDescriptorHandleForHeapStart();
        blocks_.push_back(std::move(heap));
        free_.reserve(free_.size() + kBlock);
        for (std::uint32_t index = kBlock; index-- > 0;) free_.push_back({start.ptr + std::size_t(index) * stride_});
    }
    const D3D12_CPU_DESCRIPTOR_HANDLE handle = free_.back();
    free_.pop_back();
    ++epoch_;
    return handle;
} catch (...) {
    return {};
}

void StagingDescriptors::free(D3D12_CPU_DESCRIPTOR_HANDLE handle) noexcept try {
    if (handle.ptr) { free_.push_back(handle); ++epoch_; }
} catch (...) {
}

bool Core::grow_views(std::uint32_t persistent, std::uint32_t transient, std::string& reason) {
    ComPtr<ID3D12DescriptorHeap> previous;
    if (!views_.grow(device_.Get(), persistent, transient, previous, reason)) return false;
    release_later(previous);
    return true;
}

std::uint32_t DescriptorHeap::persistent_used() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return next_unused_ - std::uint32_t(free_.size());  // pending frees are still read by frames in flight
}

std::uint32_t DescriptorHeap::allocate() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!free_.empty()) {
        const std::uint32_t index = free_.back();
        free_.pop_back();
        return index;
    }
    return next_unused_ < persistent_ ? next_unused_++ : UINT32_MAX;
}

std::uint32_t DescriptorHeap::allocate_block(std::uint32_t count) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!count || next_unused_ + count > persistent_) return UINT32_MAX;
    const std::uint32_t first = next_unused_;
    next_unused_ += count;
    return first;
}

void DescriptorHeap::free(std::uint32_t index, std::uint64_t fence_value) noexcept try {
    if (index >= persistent_) return;
    std::lock_guard<std::mutex> lock(mutex_);
    pending_free_.emplace_back(fence_value, index);
} catch (...) {
    // Losing one index to an allocation failure is harmless.
}

std::uint32_t DescriptorHeap::transient(std::uint32_t count) noexcept {
    if (!count || count > transient_) return UINT32_MAX;
    std::uint32_t start = head_, skipped = 0;
    if (start + count > transient_) {  // ranges are contiguous: skip the ring's end
        skipped = transient_ - start;
        start = 0;
    }
    if (used_ + skipped + count > transient_) return UINT32_MAX;
    used_ += skipped + count;
    frame_taken_ += skipped + count;
    head_ = start + count == transient_ ? 0 : start + count;
    return persistent_ + start;
}

void DescriptorHeap::end_frame(std::uint64_t fence_value) noexcept try {
    frames_.emplace_back(fence_value, frame_taken_);
    frame_taken_ = 0;
} catch (...) {
}

void DescriptorHeap::retire(std::uint64_t completed) noexcept try {
    while (!frames_.empty() && frames_.front().first <= completed) {
        used_ -= frames_.front().second;
        frames_.pop_front();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    while (!pending_free_.empty() && pending_free_.front().first <= completed) {
        free_.push_back(pending_free_.front().second);
        pending_free_.pop_front();
    }
} catch (...) {
}

// ---- Core --------------------------------------------------------------------------

Core::~Core() {
    if (queue_ && idle_fence_) wait_idle();
    releases_.clear();
    if (fence_event_) CloseHandle(fence_event_);
    if (idle_event_) CloseHandle(idle_event_);
}

namespace {

bool create_root_signature(ID3D12Device* device, ComPtr<ID3D12RootSignature>& root, std::string& reason) {
    D3D12_ROOT_PARAMETER1 parameters[kRootSlotCount]{};
    parameters[kRootConstants].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[kRootConstants].Constants = {0, 1, kRootConstantCount};
    for (UINT slot : {UINT(kRootFrame), UINT(kRootPass), UINT(kRootDraw)}) {
        parameters[slot].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        // Ring data is written before the list executes and not changed after.
        parameters[slot].Descriptor = {slot, 1, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE};
    }
    // Space 0: the D3D11 register model (see RootSlot).
    parameters[kRootD3D11Constants].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[kRootD3D11Constants].Descriptor = {0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE};
    constexpr auto volatile_views = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
    const D3D12_DESCRIPTOR_RANGE1 ranges[4] = {
        {D3D12_DESCRIPTOR_RANGE_TYPE_CBV, kD3D11ConstantTableSlots, 1, 0, volatile_views, 0},
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, kD3D11ResourceSlots, 0, 0, volatile_views, 0},
        {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, kD3D11UnorderedSlots, 0, 0, volatile_views, 0},
        {D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, kD3D11SamplerSlots, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE, 0},
    };
    for (UINT i = 0; i < 4; ++i) {
        auto& table = parameters[kRootD3D11ConstantTable + i];
        table.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        table.DescriptorTable = {1, &ranges[i]};
    }
    // u0 space3: the GPU report buffer the native programs write runaway loops
    // and dead bindless indices to (always bound; zero unless the switch is on).
    parameters[kRootGpuReport].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    parameters[kRootGpuReport].Descriptor = {0, 3, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE};
    // t0/t1 space4: the b0 and b1 blocks again, as raw views (native_shader.h:
    // its b0/b1 constant-buffer reads came back with other draws' values).
    for (UINT slot : {UINT(kRootD3D11RawConstants), UINT(kRootD3D11RawLighting)}) {
        parameters[slot].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        parameters[slot].Descriptor = {slot - kRootD3D11RawConstants, 4, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE};
    }
    for (auto& parameter : parameters) parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    // The fixed point/linear x clamp/wrap set (plan 2.3); anisotropic samplers
    // follow a setting and live in the sampler heap.
    D3D12_STATIC_SAMPLER_DESC samplers[4]{};
    const D3D12_FILTER filters[4] = {D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_FILTER_MIN_MAG_MIP_POINT,
                                     D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_FILTER_MIN_MAG_MIP_LINEAR};
    for (UINT i = 0; i < 4; ++i) {
        const D3D12_TEXTURE_ADDRESS_MODE address = (i & 1) ? D3D12_TEXTURE_ADDRESS_MODE_WRAP : D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[i].Filter = filters[i];
        samplers[i].AddressU = samplers[i].AddressV = samplers[i].AddressW = address;
        samplers[i].MaxAnisotropy = 1;
        samplers[i].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        samplers[i].MaxLOD = D3D12_FLOAT32_MAX;
        samplers[i].ShaderRegister = i;  // s0 point clamp, s1 point wrap, s2 linear clamp, s3 linear wrap
        samplers[i].RegisterSpace = 1;
        samplers[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc{};
    desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    desc.Desc_1_1.NumParameters = kRootSlotCount;
    desc.Desc_1_1.pParameters = parameters;
    desc.Desc_1_1.NumStaticSamplers = 4;
    desc.Desc_1_1.pStaticSamplers = samplers;
    desc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                          D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
                          D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;
    ComPtr<ID3DBlob> blob, errors;
    HRESULT hr = D3D12SerializeVersionedRootSignature(&desc, &blob, &errors);
    if (FAILED(hr)) {
        reason = hresult_text("D3D12SerializeVersionedRootSignature", hr);
        if (errors) reason += std::string(": ") + static_cast<const char*>(errors->GetBufferPointer());
        return false;
    }
    hr = device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root));
    if (FAILED(hr)) { reason = hresult_text("CreateRootSignature", hr); return false; }
    root->SetName(L"eid3d11 global root signature");
    return true;
}

} // namespace

bool Core::create(bool debug_layer, std::string& reason, bool gpu_validation) {
    const auto fail = [&](std::string text) {
        reason = std::move(text);
        list_.Reset();
        for (auto& frame : frames_) frame.allocator.Reset();
        root_.Reset();
        fence_.Reset();
        idle_fence_.Reset();
        queue_.Reset();
        device_.Reset();
        return false;
    };
    HRESULT hr = S_OK;
    if (debug_layer) {
        ComPtr<ID3D12Debug> debug;
        if (FAILED(hr = D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
            return fail(hresult_text("D3D12GetDebugInterface (EI_RENDERER_X64_D3D12_DEBUG=1 needs the Graphics Tools feature)", hr));
        debug->EnableDebugLayer();
        // GPU-based validation too: resource states and descriptors as the GPU reads them.
        ComPtr<ID3D12Debug1> debug1;
        if (gpu_validation && SUCCEEDED(debug.As(&debug1))) debug1->SetEnableGPUBasedValidation(TRUE);
    }
    // DRED (a runtime feature, no Graphics Tools needed): breadcrumbs and
    // page fault data so a removal names the GPU operation. Costs some CPU a
    // command; the bring-up keeps it on, M3 measures whether it stays.
    ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dred;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dred)))) {
        dred->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        dred->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        // Breadcrumb contexts: the frame list's pass markers (Executor::marker).
        ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> dred1;
        if (SUCCEEDED(dred.As(&dred1))) dred1->SetBreadcrumbContextEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
    }
    if (FAILED(hr = CreateDXGIFactory2(debug_layer ? DXGI_CREATE_FACTORY_DEBUG : 0, IID_PPV_ARGS(&factory_))))
        return fail(hresult_text("CreateDXGIFactory2", hr));
    if (FAILED(hr = factory_->EnumAdapters1(0, &adapter_))) return fail(hresult_text("IDXGIFactory::EnumAdapters1(0)", hr));
    DXGI_ADAPTER_DESC1 adapter{};
    adapter_->GetDesc1(&adapter);
    adapter_name_ = adapter.Description;
    if (adapter.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) return fail("the default adapter is a software adapter; D3D12 mode needs a GPU");
    if (FAILED(hr = D3D12CreateDevice(adapter_.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_))))
        return fail(hresult_text("D3D12CreateDevice (feature level 11_0)", hr));

    // M1 requirements (plan 2.2): SM 6.6 for ResourceDescriptorHeap and
    // binding tier 3. No descriptor-table variant exists.
    D3D12_FEATURE_DATA_SHADER_MODEL model{D3D_SHADER_MODEL_6_6};
    if (FAILED(hr = device_->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model))))
        return fail(hresult_text("CheckFeatureSupport(SHADER_MODEL 6.6): the D3D12 runtime does not know shader model 6.6; "
                                 "update Windows", hr));
    if (model.HighestShaderModel < D3D_SHADER_MODEL_6_6) {
        char text[200];
        std::snprintf(text, sizeof(text), "the GPU driver supports shader model %u.%u; D3D12 mode needs 6.6 "
                      "(update the driver, or use the D3D11 host)",
                      unsigned(model.HighestShaderModel) >> 4, unsigned(model.HighestShaderModel) & 15);
        return fail(text);
    }
    D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
    if (FAILED(hr = device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))))
        return fail(hresult_text("CheckFeatureSupport(D3D12_OPTIONS)", hr));
    if (options.ResourceBindingTier < D3D12_RESOURCE_BINDING_TIER_3) {
        char text[160];
        std::snprintf(text, sizeof(text), "the GPU has resource binding tier %d; D3D12 mode needs tier 3 (use the D3D11 host)",
                      int(options.ResourceBindingTier));
        return fail(text);
    }

    D3D12_COMMAND_QUEUE_DESC queue{};
    queue.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(hr = device_->CreateCommandQueue(&queue, IID_PPV_ARGS(&queue_)))) return fail(hresult_text("CreateCommandQueue", hr));
    queue_->SetName(L"eid3d11 direct queue");
    if (FAILED(hr = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)))) return fail(hresult_text("CreateFence", hr));
    fence_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!fence_event_) return fail(hresult_text("CreateEvent (frame fence)", HRESULT_FROM_WIN32(GetLastError())));
    if (FAILED(hr = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&idle_fence_))))
        return fail(hresult_text("CreateFence (queue idle)", hr));
    idle_fence_->SetName(L"eid3d11 direct queue idle fence");
    idle_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!idle_event_) return fail(hresult_text("CreateEvent (queue idle)", HRESULT_FROM_WIN32(GetLastError())));
    for (auto& frame : frames_)
        if (FAILED(hr = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frame.allocator))))
            return fail(hresult_text("CreateCommandAllocator", hr));
    if (FAILED(hr = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, frames_[0].allocator.Get(), nullptr,
                                               IID_PPV_ARGS(&list_))))
        return fail(hresult_text("CreateCommandList", hr));
    list_->Close();
    list_->SetName(L"eid3d11 frame list");
    std::string heap_reason;
    // What the two shader-visible heaps cost (GPU segments and process
    // memory), measured around their creation for the M1a hand-off.
    ComPtr<IDXGIAdapter3> adapter3;
    adapter_.As(&adapter3);
    const auto sample = [&](DXGI_QUERY_VIDEO_MEMORY_INFO& local, DXGI_QUERY_VIDEO_MEMORY_INFO& non_local, SIZE_T& private_bytes) {
        if (adapter3) {
            adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local);
            adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &non_local);
        }
        PROCESS_MEMORY_COUNTERS_EX counters{};
        counters.cb = sizeof(counters);
        private_bytes = GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                                             sizeof(counters)) ? counters.PrivateUsage : 0;
    };
    DXGI_QUERY_VIDEO_MEMORY_INFO local0{}, non_local0{}, local1{}, non_local1{};
    SIZE_T private0 = 0, private1 = 0;
    sample(local0, non_local0, private0);
    if (!views_.create(device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kViewPersistent, kViewTransientStart, heap_reason) ||
        !samplers_.create(device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, kSamplerPersistent, kSamplerTransient, heap_reason) ||
        !staging_.create(device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, heap_reason))
        return fail(heap_reason);
    sample(local1, non_local1, private1);
    {
        const auto mb = [](std::uint64_t after, std::uint64_t before) {
            return double(static_cast<std::int64_t>(after - before)) / (1024.0 * 1024.0);
        };
        char text[256];
        std::snprintf(text, sizeof(text),
                      "descriptor heaps: %u view (ring %u, grows by demand) + %u sampler descriptors (%u + %u bytes each, "
                      "%.2f MB of descriptors); measured: local %+.1f MB, non-local %+.1f MB, process private %+.1f MB",
                      views_.size(), views_.transient_capacity(), kSamplerDescriptors,
                      device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV),
                      device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER),
                      (double(views_.size()) * device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) +
                       double(kSamplerDescriptors) * device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)) /
                          (1024.0 * 1024.0),
                      mb(local1.CurrentUsage, local0.CurrentUsage), mb(non_local1.CurrentUsage, non_local0.CurrentUsage),
                      mb(private1, private0));
        heap_cost_ = text;
    }
    std::string root_reason;
    if (!create_root_signature(device_.Get(), root_, root_reason)) return fail(root_reason);
    return true;
}

std::string Core::removal_report() const {
    const HRESULT reason = removed_reason();
    if (SUCCEEDED(reason)) return {};
    char text[1024];
    std::snprintf(text, sizeof(text), "removal reason 0x%08lX%s", static_cast<unsigned long>(reason),
                  reason == DXGI_ERROR_DEVICE_HUNG             ? " DXGI_ERROR_DEVICE_HUNG (GPU took too long: TDR)"
                  : reason == DXGI_ERROR_DEVICE_RESET          ? " DXGI_ERROR_DEVICE_RESET (bad command, e.g. an invalid access)"
                  : reason == DXGI_ERROR_DRIVER_INTERNAL_ERROR ? " DXGI_ERROR_DRIVER_INTERNAL_ERROR"
                  : reason == DXGI_ERROR_INVALID_CALL          ? " DXGI_ERROR_INVALID_CALL (invalid API use)"
                  // DEVICE_REMOVED as the reason itself carries no GPU fault code:
                  // ID3D12Device5::RemoveDevice, a driver update or a removed GPU.
                  : reason == DXGI_ERROR_DEVICE_REMOVED        ? " DXGI_ERROR_DEVICE_REMOVED (RemoveDevice, driver update or GPU "
                                                                 "removed; no GPU fault code)"
                                                               : "");
    std::string report = text;
    ComPtr<ID3D12DeviceRemovedExtendedData> dred;
    if (FAILED(device_->QueryInterface(IID_PPV_ARGS(&dred)))) return report + "; DRED unavailable";
    // DRED 1.2 (breadcrumb contexts): the renderer pass marked last before
    // the first unfinished operation (Executor::marker), and the operations
    // around it.
    ComPtr<ID3D12DeviceRemovedExtendedData1> dred1;
    D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs1{};
    const bool contexts = SUCCEEDED(device_->QueryInterface(IID_PPV_ARGS(&dred1))) && SUCCEEDED(dred1->GetAutoBreadcrumbsOutput1(&breadcrumbs1));
    D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT breadcrumbs{};
    if (contexts) {
        int unfinished = 0;
        for (const auto* node = breadcrumbs1.pHeadAutoBreadcrumbNode; node; node = node->pNext) {
            const UINT32 done = node->pLastBreadcrumbValue ? *node->pLastBreadcrumbValue : 0;
            if (done >= node->BreadcrumbCount || ++unfinished > 4) continue;
            char list[96]{}, pass[96]{};
            if (node->pCommandListDebugNameW)
                WideCharToMultiByte(CP_UTF8, 0, node->pCommandListDebugNameW, -1, list, sizeof(list) - 1, nullptr, nullptr);
            UINT pass_index = UINT_MAX;
            for (UINT index = 0; index < node->BreadcrumbContextsCount; ++index) {
                const auto& context = node->pBreadcrumbContexts[index];
                if (context.BreadcrumbIndex <= done && context.pContextString && (pass_index == UINT_MAX || context.BreadcrumbIndex >= pass_index)) {
                    pass_index = context.BreadcrumbIndex;
                    WideCharToMultiByte(CP_UTF8, 0, context.pContextString, -1, pass, sizeof(pass) - 1, nullptr, nullptr);
                }
            }
            std::string around;
            for (UINT32 index = done >= 4 ? done - 4 : 0; node->pCommandHistory && index < node->BreadcrumbCount && index <= done + 4; ++index)
                around += (around.empty() ? "" : ",") + std::string(index == done ? "*" : "") + std::to_string(int(node->pCommandHistory[index]));
            std::snprintf(text, sizeof(text), "; DRED: list '%s' finished %u of %u operations, next D3D12_AUTO_BREADCRUMB_OP %d%s%s; operations %u..: %s",
                          list[0] ? list : "?", done, node->BreadcrumbCount,
                          node->pCommandHistory ? int(node->pCommandHistory[done]) : -1, pass[0] ? ", in pass " : "", pass,
                          done >= 4 ? done - 4 : 0, around.c_str());
            report += text;
        }
        report += !unfinished ? "; DRED: no unfinished command list" : unfinished > 4 ? "; DRED: further unfinished lists" : "";
    } else if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput(&breadcrumbs))) {
        int unfinished = 0;
        for (const auto* node = breadcrumbs.pHeadAutoBreadcrumbNode; node; node = node->pNext) {
            const UINT32 done = node->pLastBreadcrumbValue ? *node->pLastBreadcrumbValue : 0;
            if (done >= node->BreadcrumbCount || ++unfinished > 4) continue;
            char list[96]{};
            if (node->pCommandListDebugNameW)
                WideCharToMultiByte(CP_UTF8, 0, node->pCommandListDebugNameW, -1, list, sizeof(list) - 1, nullptr, nullptr);
            std::snprintf(text, sizeof(text), "; DRED: list '%s' finished %u of %u operations, next D3D12_AUTO_BREADCRUMB_OP %d",
                          list[0] ? list : "?", done, node->BreadcrumbCount,
                          node->pCommandHistory ? int(node->pCommandHistory[done]) : -1);
            report += text;
        }
        report += !unfinished ? "; DRED: no unfinished command list" : unfinished > 4 ? "; DRED: further unfinished lists" : "";
    } else {
        report += "; DRED breadcrumbs unavailable";
    }
    D3D12_DRED_PAGE_FAULT_OUTPUT fault{};
    if (SUCCEEDED(dred->GetPageFaultAllocationOutput(&fault)) && fault.PageFaultVA) {
        std::snprintf(text, sizeof(text), "; DRED: GPU page fault at VA 0x%llX", static_cast<unsigned long long>(fault.PageFaultVA));
        report += text;
        const auto names = [&](const D3D12_DRED_ALLOCATION_NODE* node, const char* label) {
            for (int listed = 0; node && listed < 3; node = node->pNext, ++listed) {
                char name[96]{};
                if (node->ObjectNameW) WideCharToMultiByte(CP_UTF8, 0, node->ObjectNameW, -1, name, sizeof(name) - 1, nullptr, nullptr);
                report += std::string(", ") + label + " '" + (name[0] ? name : "?") + "'";
            }
        };
        names(fault.pHeadExistingAllocationNode, "existing");
        names(fault.pHeadRecentFreedAllocationNode, "recently freed");
    }
    return report;
}

void Core::bind(ID3D12GraphicsCommandList* list) const noexcept {
    ID3D12DescriptorHeap* heaps[] = {views_.heap(), samplers_.heap()};
    list->SetDescriptorHeaps(2, heaps);
    list->SetGraphicsRootSignature(root_.Get());
    list->SetComputeRootSignature(root_.Get());
}

bool Core::wait_for(std::uint64_t value) noexcept {
    const auto completed = fence_->GetCompletedValue();
    if (completed != UINT64_MAX && completed >= value) return true;
    const owned::frame_waits::Scope wait(owned::frame_waits::fence);
    const HRESULT hr = fence_->SetEventOnCompletion(value, fence_event_);
    const DWORD waited = SUCCEEDED(hr) ? WaitForSingleObject(fence_event_, 20000) : WAIT_FAILED;
    const auto finished = fence_->GetCompletedValue();
    if (waited == WAIT_OBJECT_0 && finished != UINT64_MAX && finished >= value) return true;
    remote::host_log("d3d12: frame fence wait failed: wanted=%llu completed=%llu wait=%lu event_hr=0x%08lX device_hr=0x%08lX",
                     static_cast<unsigned long long>(value), static_cast<unsigned long long>(finished), waited,
                     static_cast<unsigned long>(hr), static_cast<unsigned long>(device_->GetDeviceRemovedReason()));
    return false;
}

bool Core::wait_idle() noexcept {
    const owned::frame_waits::Scope wait(owned::frame_waits::fence);
    const std::uint64_t value = next_idle_value_++;
    HRESULT hr = queue_->Signal(idle_fence_.Get(), value);
    DWORD waited = WAIT_OBJECT_0;
    if (SUCCEEDED(hr) && idle_fence_->GetCompletedValue() < value) {
        hr = idle_fence_->SetEventOnCompletion(value, idle_event_);
        waited = SUCCEEDED(hr) ? WaitForSingleObject(idle_event_, 20000) : WAIT_FAILED;
    }
    const std::uint64_t completed = idle_fence_->GetCompletedValue();
    if (FAILED(hr) || waited != WAIT_OBJECT_0 || completed < value || completed == UINT64_MAX) {
        remote::host_log("d3d12: idle fence wait failed: wanted=%llu completed=%llu wait=%lu hr=0x%08lX device_hr=0x%08lX",
                         static_cast<unsigned long long>(value), static_cast<unsigned long long>(completed), waited,
                         static_cast<unsigned long>(hr), static_cast<unsigned long>(device_->GetDeviceRemovedReason()));
        return false;
    }
    retire(fence_->GetCompletedValue());
    return true;
}

void Core::retire(std::uint64_t completed) noexcept {
    while (!releases_.empty() && releases_.front().first <= completed) releases_.pop_front();
    views_.retire(completed);
    samplers_.retire(completed);
}

void Core::release_later(ComPtr<IUnknown> object) {
    if (object) releases_.emplace_back(next_value_, std::move(object));
}

ID3D12GraphicsCommandList* Core::begin_frame(std::string& reason) {
    slot_ = static_cast<unsigned>(frame_number_ % kFramesInFlight);
    Frame& frame = frames_[slot_];
    if (!wait_for(frame.fence_value)) { reason = "the GPU did not finish a frame within 20 s"; return nullptr; }
    const std::uint64_t completed = fence_->GetCompletedValue();
    if (completed == UINT64_MAX) { reason = "the D3D12 device was removed"; return nullptr; }  // the caller adds removal_report()
    retire(completed);
    HRESULT hr = frame.allocator->Reset();
    if (FAILED(hr)) { reason = hresult_text("ID3D12CommandAllocator::Reset", hr); return nullptr; }
    if (FAILED(hr = list_->Reset(frame.allocator.Get(), nullptr))) { reason = hresult_text("ID3D12GraphicsCommandList::Reset", hr); return nullptr; }
    bind(list_.Get());
    return list_.Get();
}

std::uint64_t Core::end_frame(std::string& reason) {
    HRESULT hr = list_->Close();
    if (FAILED(hr)) { reason = hresult_text("ID3D12GraphicsCommandList::Close", hr); return 0; }
    ID3D12CommandList* lists[] = {list_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    const std::uint64_t value = next_value_++;
    if (FAILED(hr = queue_->Signal(fence_.Get(), value))) { reason = hresult_text("ID3D12CommandQueue::Signal", hr); return 0; }
    frames_[slot_].fence_value = value;
    views_.end_frame(value);
    samplers_.end_frame(value);
    ++frame_number_;
    return value;
}

} // namespace d3d12
