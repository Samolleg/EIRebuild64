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
    const std::array<float,3>& object, ModelPreviewLighting& output,
    owned::PreviewStudioStyle style = owned::PreviewStudioStyle::portrait) {
    owned::PreviewStudioLighting studio;
    if(!owned::make_preview_studio_lighting(clip,object,studio,style))return false;
    output.world={};
    output.world.ambient=studio.ambient;
    output.world.sunlight_diffuse=studio.diffuse;
    output.world.sunlight_direction=studio.direction;
    output.points.clear();
    if (style == owned::PreviewStudioStyle::soft_figure) {
        constexpr float strength[] = {.32f,.235f};
        for (unsigned i=0; i<studio.fill_centers.size(); ++i) {
            owned::RetainedWorldPointLight fill{};
            fill.center=studio.fill_centers[i];
            fill.radius=36.f;
            fill.color={strength[i],strength[i],strength[i]};
            output.points.push_back(fill);
        }
    }
    return true;
}
// Existing decorative-menu policy before X64GLTF12. Full ambient and FIG
// colors belong to this UI role; character previews never enumerate these lights.
template<class Next>
bool produce_interface_decoration_lighting(const owned::ModelLightingFacts& source,
    const std::array<float,3>& object, Next&& next, ModelPreviewLighting& output) {
    for(float v:object)if(!std::isfinite(v))return false;
    auto world=source;world.ambient=0xffffffffu;
    output.points.clear();
    if(!owned::produce_model_lighting(world,output.world))return false;
    owned::ModelPointLightFacts light;
    while(next(light)) {
        if(owned::admit_model_point_light(light)) {
            float distance=0;
            for(unsigned i=0;i<3;++i){const float delta=object[i]-light.value.center[i];distance+=delta*delta;}
            const float reach=light.value.radius+10.f;
            if(distance<=reach*reach) {
                if(!light.value.source_id)light.value.source_id=int(output.points.size())+1;
                output.points.push_back(light.value);
            }
        }
        light={};
    }
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
