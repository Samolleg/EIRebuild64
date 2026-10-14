#pragma once
#ifdef EID3D11_EMBEDDED
#include "native_instance_flags.h"
#include <string>
namespace owned {
// Used by both shader generation and the embedded runtime source composer.
inline void embedded_triangle_shader(std::string& text) {
    const std::string marker = "o.p=mul(instance.objectToClip,float4(position.xyz,1));";
    const std::string extra = "if((instance.nativeFlags&" +
        std::to_string(native_embedded_homogeneous_flag) +
        "u)!=0)o.p=mul(instance.objectToClip,position);";
    const auto at = text.find(marker);
    if (at != std::string::npos && text.find(extra) == std::string::npos)
        text.insert(at + marker.size(), extra);
}
}
#endif
