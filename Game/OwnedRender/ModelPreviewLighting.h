#pragma once
#include "model_lighting.h"
#include "model_preview_lighting.h"

namespace OwnedRender {
struct ModelPreviewLighting {
    owned::NativeWorldLighting world{};
    std::vector<owned::RetainedWorldPointLight> points;
};
// The embedded x64 host owns this path. No world ambient, sun, point-light
// manager, or legacy full-ambient flag can override the interface studio.
inline bool produce_model_preview_lighting(const std::array<float,16>& clip,
    const std::array<float,3>& object, ModelPreviewLighting& output) {
    owned::PreviewStudioLighting studio;
    if(!owned::make_preview_studio_lighting(clip,object,studio))return false;
    output.world={};
    output.world.ambient=studio.ambient;
    output.world.sunlight_diffuse=studio.diffuse;
    output.world.sunlight_direction=studio.direction;
    output.points.clear();
    return true;
}
inline owned::NativeLighting model_preview_tinted_lighting(const std::array<float,16>& world,
    std::uint32_t color) noexcept {
    owned::NativeLighting result;
    result.enabled=1;result.object_to_world=world;
    result.material_diffuse={float((color>>16)&255)/255.f,float((color>>8)&255)/255.f,float(color&255)/255.f};
    result.material_alpha=float(color>>24)/255.f;
    return result;
}
} // namespace OwnedRender
