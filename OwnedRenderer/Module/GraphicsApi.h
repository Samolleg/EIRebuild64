#pragma once
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_5.h>

#ifdef EI_RENDER_MODULE_BUILD
#define EI_RENDER_PUBLIC __declspec(dllexport)
#else
#define EI_RENDER_PUBLIC __declspec(dllimport)
#endif

// Owned COM references are returned through output parameters. Callbacks are
// synchronous and borrowed only for the duration of their enclosing call.
using EiRenderStage = void (*)(void* context, const char* stage) noexcept;
extern "C" EI_RENDER_PUBLIC HRESULT EiCreateDevice11V1(
    D3D_DRIVER_TYPE driver, ID3D11Device** device, D3D_FEATURE_LEVEL* level,
    ID3D11DeviceContext** immediate) noexcept;
extern "C" EI_RENDER_PUBLIC HRESULT EiCreateSwapChain11V1(
    ID3D11Device* device, HWND window, UINT width, UINT height, EiRenderStage stage,
    void* context, IDXGISwapChain** swapChain, UINT* flags, BOOL* tearing) noexcept;

extern "C" EI_RENDER_PUBLIC HRESULT EiCreateDevice12V1(
    BOOL debugLayer, BOOL gpuValidation, IDXGIFactory4** factory, IDXGIAdapter1** adapter,
    ID3D12Device** device, char* error, SIZE_T errorSize) noexcept;
extern "C" EI_RENDER_PUBLIC HRESULT EiCreateRootSignature12V1(
    ID3D12Device* device, ID3D12RootSignature** root, char* error, SIZE_T errorSize) noexcept;

// All pipeline workers, strings, driver-cache bytes and PSOs belong to the DLL.
// Callback context must outlive the set. The progress target can be detached.
struct EiPipelineHostV1 {
    void* context;
    void (*progress)(void*, UINT64) noexcept;
    void (*pending)(UINT) noexcept;
};
struct EiPipelineStatsV1 {
    UINT loaded, created, requested, saved, refused;
    SIZE_T savedBytes;
};
struct EiPipelineSetV1 {
    virtual void destroy() noexcept = 0;
    virtual void endStartup() noexcept = 0;
    virtual HRESULT graphics(const wchar_t*, const D3D12_GRAPHICS_PIPELINE_STATE_DESC*, UINT*) noexcept = 0;
    virtual HRESULT compute(const wchar_t*, const D3D12_COMPUTE_PIPELINE_STATE_DESC*, UINT*) noexcept = 0;
    // The returned PSO is borrowed until destroy().
    virtual ID3D12PipelineState* get(UINT) const noexcept = 0;
    virtual ID3D12PipelineState* wait(UINT) noexcept = 0;
    virtual void waitAll() noexcept = 0;
    virtual BOOL idle() const noexcept = 0;
    virtual double milliseconds(UINT) const noexcept = 0;
    virtual void failure(UINT, char*, SIZE_T) const noexcept = 0;
    virtual void stats(EiPipelineStatsV1*) const noexcept = 0;
    virtual UINT threadIds(DWORD*, UINT) noexcept = 0;
    virtual BOOL save(char*, SIZE_T, BOOL*) noexcept = 0;
protected:
    ~EiPipelineSetV1() = default;
};
extern "C" EI_RENDER_PUBLIC HRESULT EiOpenPipelines12V1(
    ID3D12Device* device, const wchar_t* path, unsigned workers, EiPipelineHostV1 host,
    char* note, SIZE_T noteSize, char* error, SIZE_T errorSize, EiPipelineSetV1** set) noexcept;
