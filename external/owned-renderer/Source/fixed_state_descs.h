#pragma once
// Fixed-function (D3D7) render states as D3D11 state descriptions. One
// conversion for both executors: the D3D11 executor creates state objects from
// them (apply_state) and the x64 host's D3D12 executor builds pipeline states
// from the same values (D3D12's blend, comparison, cull, fill, filter and
// address values are numerically D3D11's).
#define DIRECTDRAW_VERSION 0x0700
#define DIRECT3D_VERSION 0x0700
#include <windows.h>
#include <d3d.h>
#include <d3d11.h>

namespace owned {

inline D3D11_COMPARISON_FUNC fixed_comparison(DWORD f) noexcept {
    return f >= D3DCMP_NEVER && f <= D3DCMP_ALWAYS ? static_cast<D3D11_COMPARISON_FUNC>(f) : D3D11_COMPARISON_LESS_EQUAL;
}

inline D3D11_BLEND fixed_blend(DWORD b) noexcept {
    switch (b) {
    case D3DBLEND_ZERO: return D3D11_BLEND_ZERO;
    case D3DBLEND_ONE: return D3D11_BLEND_ONE;
    case D3DBLEND_SRCCOLOR: return D3D11_BLEND_SRC_COLOR;
    case D3DBLEND_INVSRCCOLOR: return D3D11_BLEND_INV_SRC_COLOR;
    case D3DBLEND_SRCALPHA: return D3D11_BLEND_SRC_ALPHA;
    case D3DBLEND_INVSRCALPHA: return D3D11_BLEND_INV_SRC_ALPHA;
    case D3DBLEND_DESTALPHA: return D3D11_BLEND_DEST_ALPHA;
    case D3DBLEND_INVDESTALPHA: return D3D11_BLEND_INV_DEST_ALPHA;
    case D3DBLEND_DESTCOLOR: return D3D11_BLEND_DEST_COLOR;
    case D3DBLEND_INVDESTCOLOR: return D3D11_BLEND_INV_DEST_COLOR;
    default: return D3D11_BLEND_ONE;
    }
}

// The blend a fixed state draws with: a faded HUD widget (opacity below 1)
// blends with its source alpha whatever the game set.
struct FixedBlend {
    DWORD enabled = FALSE, source = D3DBLEND_ONE, destination = D3DBLEND_ZERO;
};
inline FixedBlend fixed_blend_choice(DWORD alpha_blend_enable, DWORD source_blend, DWORD destination_blend, bool faded) noexcept {
    return faded ? FixedBlend{TRUE, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA}
                 : FixedBlend{alpha_blend_enable, source_blend, destination_blend};
}
inline D3D11_BLEND_DESC fixed_blend_desc(const FixedBlend& choice) noexcept {
    D3D11_BLEND_DESC bd{};
    auto& rt = bd.RenderTarget[0];
    rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    rt.BlendEnable = choice.enabled != 0;
    rt.SrcBlend = fixed_blend(choice.source);
    rt.DestBlend = fixed_blend(choice.destination);
    rt.BlendOp = D3D11_BLEND_OP_ADD;
    // Preserve source alpha in the target. Reusing the RGB blend factors for
    // alpha makes later legacy passes depend on an artificial destination alpha.
    rt.SrcBlendAlpha = D3D11_BLEND_ONE;
    rt.DestBlendAlpha = D3D11_BLEND_ZERO;
    rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    return bd;
}

inline D3D11_DEPTH_STENCIL_DESC fixed_depth_desc(DWORD z_enable, DWORD z_write_enable, DWORD z_func) noexcept {
    D3D11_DEPTH_STENCIL_DESC dd{};
    dd.DepthEnable = z_enable != D3DZB_FALSE;
    dd.DepthWriteMask = z_write_enable ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
    dd.DepthFunc = fixed_comparison(z_func);
    return dd;
}

inline D3D11_RASTERIZER_DESC fixed_raster_desc(DWORD cull_mode, bool wireframe, bool scissor) noexcept {
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = wireframe ? D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID;
    rd.CullMode = cull_mode == D3DCULL_NONE ? D3D11_CULL_NONE : D3D11_CULL_BACK;
    // D3D7 names the winding that is rejected. D3D11 names which winding is
    // front-facing and then rejects the back face, so the mapping is inverse:
    // CULL_CW => CCW is front; CULL_CCW => CW is front.
    rd.FrontCounterClockwise = cull_mode == D3DCULL_CW;
    rd.DepthClipEnable = TRUE;
    rd.ScissorEnable = scissor;
    return rd;
}

} // namespace owned
