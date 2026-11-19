#pragma once
#include <dxgi.h>
#include <string>
#include <cstdio>

namespace d3d12 {
inline std::string hresult_text(const char* call, HRESULT hr) {
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
}
