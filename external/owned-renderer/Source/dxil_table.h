#pragma once
// The x64 host's DXIL table: the renderer's fixed program list (the same
// append_*_shader_programs list as the DXBC table, shader_cache.h) compiled
// with dxc.exe (DXC 1.9.2609, found by Host\build.cmd) for shader model 6.6 and linked into the
// host as one RCDATA resource a program. shader_embed --dxil-sources writes
// the sources and build rules (Tools/shader_embed.cpp); Host\host.mak runs
// dxc.exe; the D3D12 backend finds a program's DXIL by dxil_name.
//
// A program that dxc rejected is an empty resource (its log is beside the
// build's .dxil files), so the backend can name it instead of missing it.

#include "shader_cache.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace owned {

// Bump when the dxc command line shader_embed writes or the DXC release
// changes: every name changes with it, so nmake compiles every program again
// (a different dxc.exe is usually older than the outputs, so timestamps
// cannot tell). 2: DXC 1.9.2609 instead of the Windows SDK's 1.6.
// 3: -HV 2018.
// 4: -D EID3D12=1 (the D3D12 backend's bindless reads, M2a).
// 5: the GPU report (EI_RENDERER_D3D12_HAZARDS) only in the hazards variants.
constexpr std::uint32_t kDxilTableVersion = 5;

// The SM 6.6 profile of an FXC profile ("ps_5_0", "vs_4_0_level_9_3" ->
// "ps_6_6", "vs_6_6"); false for a profile of no known stage.
inline bool dxil_profile(const char* fxc, char (&out)[7]) noexcept {
    if (!fxc || std::strlen(fxc) < 6 || fxc[1] != 's' || fxc[2] != '_' || !fxc[0] || !std::strchr("vphdgc", fxc[0]))
        return false;
    std::memcpy(out, fxc, 3);
    std::memcpy(out + 3, "6_6", 4);
    return true;
}

// The program's resource name in the host: "X" and 32 hex digits of its
// DXBC key folded with the table version (the target is part of the name).
// hazards: the program's EID3D12_HAZARDS variant (dxil_hazards_variant), the
// one EI_RENDERER_D3D12_HAZARDS runs use. Production programs carry no GPU
// report: its UAV (u0 space3) made every native pixel shader test depth late,
// so the depth prepass rejected nothing before shading.
inline std::string dxil_name(const ShaderProgram& program, bool hazards = false) {
    const ShaderKey key = shader_key(program);
    shader_cache_detail::Hasher h;
    h.bytes(&key, sizeof(key));
    h.bytes(&kDxilTableVersion, sizeof(kDxilTableVersion));
    if (hazards) h.text("EID3D12_HAZARDS");
    char text[40];
    std::snprintf(text, sizeof(text), "X%016llX%016llX", static_cast<unsigned long long>(h.a),
                  static_cast<unsigned long long>(h.b));
    return text;
}

} // namespace owned
