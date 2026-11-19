#include "GraphicsApi.h"
#include "DeviceError.h"
#include "../../external/owned-renderer/Host/d3d12/d3d12_core.h"

using Microsoft::WRL::ComPtr;
namespace d3d12 {
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
    // A device from a device factory (Windows' own runtime after the bundled
    // one failed, Core::create) serializes through its own configuration.
    ComPtr<ID3D12DeviceConfiguration> configuration;
    if (FAILED(hr) && SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&configuration)))) {
        blob.Reset();
        errors.Reset();
        hr = configuration->SerializeVersionedRootSignature(&desc, &blob, &errors);
    }
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
} // namespace d3d12

HRESULT EiCreateRootSignature12V1(ID3D12Device* device, ID3D12RootSignature** output,
                                  char* error, SIZE_T size) noexcept {
    if (!device || !output || !error || !size) return E_POINTER;
    *output = nullptr;
    try {
        ComPtr<ID3D12RootSignature> root;
        std::string reason;
        if (!d3d12::create_root_signature(device, root, reason)) {
            std::snprintf(error, size, "%s", reason.c_str());
            return E_FAIL;
        }
        *output = root.Detach();
        return S_OK;
    } catch (...) {
        std::snprintf(error, size, "Cannot allocate Direct3D 12 root signature");
        return E_OUTOFMEMORY;
    }
}

HRESULT EiCreateDevice12V1(BOOL debug_layer, BOOL gpu_validation, IDXGIFactory4** outFactory,
                           IDXGIAdapter1** outAdapter, ID3D12Device** outDevice,
                           char* error, SIZE_T size) noexcept try {
    if (!outFactory || !outAdapter || !outDevice || !error || !size) return E_POINTER;
    *outFactory = nullptr; *outAdapter = nullptr; *outDevice = nullptr;
    ComPtr<IDXGIFactory4> factory;
    ComPtr<IDXGIAdapter1> gpu;
    ComPtr<ID3D12Device> device;
    using d3d12::hresult_text;
    const auto fail = [&](const std::string& reason) {
        std::snprintf(error, size, "%s", reason.c_str());
        return E_FAIL;
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
    if (FAILED(hr = CreateDXGIFactory2(debug_layer ? DXGI_CREATE_FACTORY_DEBUG : 0, IID_PPV_ARGS(&factory))))
        return fail(hresult_text("CreateDXGIFactory2", hr));
    if (FAILED(hr = factory->EnumAdapters1(0, &gpu))) return fail(hresult_text("IDXGIFactory::EnumAdapters1(0)", hr));
    DXGI_ADAPTER_DESC1 adapter{};
    gpu->GetDesc1(&adapter);
    if (adapter.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) return fail("the default adapter is a software adapter; D3D12 mode needs a GPU");

    if (FAILED(hr = D3D12CreateDevice(gpu.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
        return fail(hresult_text("D3D12CreateDevice with packaged Agility runtime",hr));
    }

    // M1 requirements (plan 2.2): SM 6.6 for ResourceDescriptorHeap and
    // binding tier 3. No descriptor-table variant exists.
    D3D12_FEATURE_DATA_SHADER_MODEL model{D3D_SHADER_MODEL_6_6};
    if (FAILED(hr = device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model))))
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
    if (FAILED(hr = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))))
        return fail(hresult_text("CheckFeatureSupport(D3D12_OPTIONS)", hr));
    if (options.ResourceBindingTier < D3D12_RESOURCE_BINDING_TIER_3) {
        char text[160];
        std::snprintf(text, sizeof(text), "the GPU has resource binding tier %d; D3D12 mode needs tier 3 (use the D3D11 host)",
                      int(options.ResourceBindingTier));
        return fail(text);
    }

    *outFactory = factory.Detach();
    *outAdapter = gpu.Detach();
    *outDevice = device.Detach();
    return S_OK;
} catch (...) {
    std::snprintf(error, size, "Cannot allocate Direct3D 12 device");
    return E_OUTOFMEMORY;
}
