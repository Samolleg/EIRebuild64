#pragma once
#include "model_preview_single.h"
#include "model_draw.h"
#include "widescreen_layout.h"

namespace owned {
struct ObjectPreviewComposition {
    ModelPreviewPresentation presentation;
    std::array<PreviewRotation,3> rotations{};
    unsigned rotation_count=0, depth_count=0;
    std::array<std::array<long,4>,6> depth{};
    bool zero_all_depth=false, clear_after=false, local_depth=false;
};
inline ObjectPreviewComposition inventory_model_preview(std::int32_t id,float x,float angle,
    bool armors,float viewing_tangent) {
    constexpr float half_pi=1.57079632679489661923f;
    ObjectPreviewComposition c;
    auto& p=c.presentation;p.id=id;
    p.placed=p.full_ambient=p.default_fog=true;
    p.placement.position={(x/400.f-1.f)*viewing_tangent*6.f,
        (440/400.f-.75f)*viewing_tangent*6.f,6.f};
    p.placement.scale={1.2f,1.2f,1.2f};
    c.rotations[0]={{0,1,0},angle};c.rotations[1]={{1,0,0},half_pi};c.rotation_count=2;
    if(x>400)c.rotations[c.rotation_count++]={{0,0,-1},half_pi*.15f};
    c.zero_all_depth=true;
    if(armors) {
        c.depth={{{290,100,510,500},{272,190,290,260},{272,340,290,410},
            {510,190,528,210},{510,290,528,310},{510,390,528,410}}};c.depth_count=6;
    } else {
        c.depth[0]=x>400?std::array<long,4>{410,100,700,500}:std::array<long,4>{200,100,600,500};
        c.depth_count=1;
    }
    return c;
}
inline ObjectPreviewComposition panel_model_preview(std::int32_t id,const widescreen::Layout& layout,
    int slide,float distance,float altitude,float preview_depth,bool white) {
    constexpr float half_pi=1.57079632679489661923f;
    ObjectPreviewComposition c;
    auto& p=c.presentation;p.id=id;
    p.placed=p.default_fog=p.default_colors=true;
    const auto position=widescreen::native_camera_point(
        float(widescreen::portrait_native_x(layout,slide)),200.f,preview_depth);
    p.placement.position={position.x,position.y+altitude/distance,position.z};
    p.placement.scale={1/distance,1/distance,1/distance};
    p.diffuse=white?0xffffffffu:0xff808080u;
    c.rotations={{{{0,0,1},half_pi*.05f},{{1,0,0},half_pi*1.15f},{{0,0,1},half_pi*.4f}}};
    c.rotation_count=3;
    c.depth[0]={180-slide,0,400-slide,220};c.depth[1]={0,220,400-slide,300};
    c.depth[2]={0,0,20-slide,220};c.depth_count=3;
    c.local_depth=c.clear_after=true;
    return c;
}

// Whole composition ordering, including callback placement between depth clears.
// Adapters only map rectangles, submit clears, supply math primitives and invoke
// the source object's existing simulation/draw. Nothing is retained here.
template<class Map,class Clear,class Quaternion,class Prepare,class Draw,class Flush>
void produce_model_preview_composition(ObjectPreviewComposition c,Map&& map,Clear&& clear,
    Quaternion&& quaternion,Prepare&& prepare,Draw&& draw,Flush&& flush) {
    if(c.zero_all_depth){flush();clear(static_cast<const std::array<long,4>*>(nullptr),0,0.f);}
    auto depth=c.depth;
    for(unsigned i=0;i<c.depth_count;++i)map(depth[i],c.local_depth);
    clear(depth.data(),c.depth_count,1.f);
    prepare();
    c.presentation.placement.quaternion=quaternion(c.rotations,c.rotation_count);
    {
        ModelPreviewPresentationScope presentation(c.presentation);
        ModelDrawScope preview(ModelDrawDomain::preview);
        draw();
    }
    if(c.clear_after)clear(depth.data(),c.depth_count,1.f);
}
} // namespace owned
