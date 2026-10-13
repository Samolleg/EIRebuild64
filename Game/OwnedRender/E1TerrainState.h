#pragma once
#include "engine_bridge.h"
#include <d3d.h>
#include <map>

namespace OwnedRender {
// Source render facts only. Geometry, pass ordering and material policy remain
// in c56. Read-only lookup must not insert defaults into the game's state map.
inline owned::NativeOwnedFixedState E1TerrainMaterialState(
    const std::map<int, DWORD>& states, unsigned alphaMode) noexcept {
    owned::NativeOwnedFixedState result;
    if (alphaMode > 2) return result;
    const auto read = [&](int key, DWORD fallback) {
        const auto it = states.find(key);
        return it == states.end() ? fallback : it->second;
    };
    result.enabled = true;
    result.cull_mode = read(D3DRENDERSTATE_CULLMODE, D3DCULL_CCW);
    result.fill_mode = read(D3DRENDERSTATE_FILLMODE, D3DFILL_SOLID);
    result.z_enable = read(D3DRENDERSTATE_ZENABLE, D3DZB_TRUE);
    result.z_write_enable = read(D3DRENDERSTATE_ZWRITEENABLE, TRUE);
    result.z_func = read(D3DRENDERSTATE_ZFUNC, D3DCMP_LESSEQUAL);
    result.alpha_blend_enable = read(D3DRENDERSTATE_ALPHABLENDENABLE, FALSE);
    result.source_blend = read(D3DRENDERSTATE_SRCBLEND, D3DBLEND_ONE);
    result.destination_blend = read(D3DRENDERSTATE_DESTBLEND, D3DBLEND_ZERO);
    result.alpha_test_enable = read(D3DRENDERSTATE_ALPHATESTENABLE, FALSE);
    result.alpha_func = read(D3DRENDERSTATE_ALPHAFUNC, D3DCMP_GREATEREQUAL);
    result.alpha_ref = read(D3DRENDERSTATE_ALPHAREF, 0);
    result.texture_factor = read(D3DRENDERSTATE_TEXTUREFACTOR, 0xffffffffu);
    result.specular_enable = read(D3DRENDERSTATE_SPECULARENABLE, FALSE);
    // RendererDX11's world shader and sampler: texture * diffuse colour,
    // texture/multiplied/vertex alpha selected by SetTexturingAlphaMode.
    result.color_op = D3DTOP_MODULATE;
    result.color_arg1 = D3DTA_TEXTURE; result.color_arg2 = D3DTA_DIFFUSE;
    result.alpha_op = alphaMode == 0 ? D3DTOP_SELECTARG1 :
        alphaMode == 2 ? D3DTOP_SELECTARG2 : D3DTOP_MODULATE;
    result.alpha_arg1 = D3DTA_TEXTURE; result.alpha_arg2 = D3DTA_DIFFUSE;
    result.address_u = result.address_v = D3DTADDRESS_WRAP;
    result.min_filter = result.mag_filter = D3DTFG_LINEAR;
    result.mip_filter = D3DTFP_LINEAR;
    return result;
}
}
