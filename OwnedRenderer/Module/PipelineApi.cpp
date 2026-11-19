#include "PipelineCache.h"
#include <cstdio>
#include <memory>
#include <new>

namespace {
void copy_text(char* dest, SIZE_T size, const char* text) noexcept {
    if (dest && size) std::snprintf(dest, size, "%s", text);
}

class PipelineSet final : public EiPipelineSetV1 {
public:
    d3d12::PipelineCache cache;
    void destroy() noexcept override { delete this; }
    void endStartup() noexcept override { cache.end_startup_progress(); }
    HRESULT graphics(const wchar_t* name, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc, UINT* id) noexcept override {
        if (!name || !desc || !id) return E_POINTER;
        try { *id = cache.request(name, *desc); return S_OK; } catch (...) { return E_OUTOFMEMORY; }
    }
    HRESULT compute(const wchar_t* name, const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc, UINT* id) noexcept override {
        if (!name || !desc || !id) return E_POINTER;
        try { *id = cache.request(name, *desc); return S_OK; } catch (...) { return E_OUTOFMEMORY; }
    }
    ID3D12PipelineState* get(UINT id) const noexcept override { return cache.get(id); }
    ID3D12PipelineState* wait(UINT id) noexcept override { return cache.wait(id); }
    void waitAll() noexcept override { cache.wait_all(); }
    BOOL idle() const noexcept override { return cache.idle(); }
    double milliseconds(UINT id) const noexcept override { return cache.milliseconds(id); }
    void failure(UINT id, char* text, SIZE_T size) const noexcept override {
        try { copy_text(text, size, cache.failure(id).c_str()); }
        catch (...) { copy_text(text, size, "Cannot read pipeline error"); }
    }
    void stats(EiPipelineStatsV1* out) const noexcept override {
        *out = {cache.loaded(), cache.created(), cache.requested(), cache.saved_count(),
                cache.save_refused(), cache.saved_bytes()};
    }
    UINT threadIds(DWORD* ids, UINT capacity) noexcept override { return cache.thread_ids(ids, capacity); }
    BOOL save(char* error, SIZE_T size, BOOL* written) noexcept override {
        try {
            bool changed = false;
            std::string reason;
            const bool ok = cache.save(reason, &changed);
            if (written) *written = changed;
            copy_text(error, size, reason.c_str());
            return ok;
        } catch (...) {
            copy_text(error, size, "Cannot save pipeline library");
            return FALSE;
        }
    }
};
}

HRESULT EiOpenPipelines12V1(ID3D12Device* device, const wchar_t* path, unsigned workers,
                            EiPipelineHostV1 host, char* note, SIZE_T noteSize, char* error,
                            SIZE_T errorSize, EiPipelineSetV1** output) noexcept {
    if (!output || !device || !path) return E_POINTER;
    *output = nullptr;
    try {
        auto set = std::make_unique<PipelineSet>();
        std::string info, reason;
        const bool ok = set->cache.open(device, path, workers, info, reason, host);
        copy_text(note, noteSize, info.c_str());
        copy_text(error, errorSize, reason.c_str());
        if (!ok) return E_FAIL;
        *output = set.release();
        return S_OK;
    } catch (...) {
        copy_text(error, errorSize, "Cannot initialize pipeline workers");
        return E_OUTOFMEMORY;
    }
}
