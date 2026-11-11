#pragma once
// Optional entry points, resolved only when FSR FG is selected.
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
bool load(std::string& reason) noexcept;
} // namespace d3d12::fidelityfx
