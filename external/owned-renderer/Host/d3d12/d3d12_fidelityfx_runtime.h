#pragma once
// Optional entry points and independent upscaling/frame-generation providers.
#include <ffx_api.h>
#include <string>

namespace d3d12::fidelityfx {
struct Api {
    PfnFfxCreateContext create = nullptr;
    PfnFfxDestroyContext destroy = nullptr;
    PfnFfxConfigure configure = nullptr;
    PfnFfxQuery query = nullptr;
    PfnFfxDispatch dispatch = nullptr;
};
extern Api api;
bool load(std::string& reason, bool upscaler = false) noexcept;
} // namespace d3d12::fidelityfx
