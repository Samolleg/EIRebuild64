#define NOMINMAX
#include "E1PhysicsDebris.h"
#include "E1VisualFrame.h"
#include "../GameJournal.h"
#include "renderer.h"
#include <algorithm>

namespace OwnedRender {
bool E1DrawPhysicsDebris(owned::Renderer& renderer,const owned::FixedState& input,const E1VisualInputs& frame) noexcept try {
    if(frame.debris.empty()||!frame.lightingValid)return true;
    if(!frame.debrisTexture.identity)return false;
    static const std::array<DebrisMesh,4> meshes={DebrisMesh(0),DebrisMesh(1),DebrisMesh(2),DebrisMesh(3)};
    struct Visible {const PhysicsFx::Stone* stone;float depth;unsigned char fog;};
    std::array<Visible,PhysicsFx::MaxStones> visible;
    if(frame.debris.size()>visible.size())return false;
    size_t count=0;
    for(const auto& stone:frame.debris) {
        const auto& p=stone.pose.position;const float position[]={p.x,p.y,p.z};
        const unsigned char fog=frame.fog?frame.fog(frame.fogContext,position):0;
        if(!fog||stone.alpha<=0)continue;
        const auto& m=frame.worldToClip;
        visible[count++]={&stone,m[12]*p.x+m[13]*p.y+m[14]*p.z+m[15],fog};
    }
    std::stable_sort(visible.begin(),visible.begin()+count,[](const Visible& a,const Visible& b){
        const bool opaque=a.stone->alpha>=.999f;
        if(opaque!=(b.stone->alpha>=.999f))return opaque;
        return opaque?a.stone->shape<b.stone->shape:a.depth>b.depth;
    });
    owned::FixedState state=input;
    state.render[D3DRENDERSTATE_ZENABLE]=TRUE;state.render[D3DRENDERSTATE_ZFUNC]=D3DCMP_LESSEQUAL;
    state.render[D3DRENDERSTATE_ZWRITEENABLE]=TRUE;
    state.render[D3DRENDERSTATE_CULLMODE]=D3DCULL_NONE;
    state.render[D3DRENDERSTATE_ALPHATESTENABLE]=FALSE;
    state.render[D3DRENDERSTATE_ALPHABLENDENABLE]=FALSE;
    state.render[D3DRENDERSTATE_SRCBLEND]=D3DBLEND_SRCALPHA;state.render[D3DRENDERSTATE_DESTBLEND]=D3DBLEND_INVSRCALPHA;
    state.render[D3DRENDERSTATE_SPECULARENABLE]=FALSE;
    state.stage[0][D3DTSS_COLOROP]=D3DTOP_MODULATE;state.stage[0][D3DTSS_COLORARG1]=D3DTA_TEXTURE;state.stage[0][D3DTSS_COLORARG2]=D3DTA_DIFFUSE;
    state.stage[0][D3DTSS_ADDRESSU]=state.stage[0][D3DTSS_ADDRESSV]=D3DTADDRESS_CLAMP;
    state.stage[0][D3DTSS_MINFILTER]=state.stage[0][D3DTSS_MAGFILTER]=D3DTFG_LINEAR;state.stage[0][D3DTSS_MIPFILTER]=D3DTFP_LINEAR;
    state.stage[0][D3DTSS_ALPHAOP]=D3DTOP_SELECTARG1;state.stage[0][D3DTSS_ALPHAARG1]=D3DTA_DIFFUSE;
    state.stage[1][D3DTSS_COLOROP]=D3DTOP_DISABLE;state.stage[1][D3DTSS_ALPHAOP]=D3DTOP_DISABLE;
    std::array<owned::Renderer::FigureInstance,PhysicsFx::MaxStones> instances;
    bool success=true;
    for(size_t first=0;first<count;) {
        const auto& stone=*visible[first].stone;
        const bool opaque=stone.alpha>=.999f;
        const auto& mesh=meshes[stone.shape%4];
        auto view=DebrisView(mesh,stone,frame.worldToClip,visible[first].fog);
        view.owned_texture=frame.debrisTexture;
        if(opaque)view.ordering=owned::NativeOrdering::reorderable;
        size_t end=first;
        while(end<count && visible[end].stone->shape==stone.shape &&
              (visible[end].stone->alpha>=.999f)==opaque) {
            const auto& item=*visible[end].stone;
            const auto part=DebrisView(mesh,item,frame.worldToClip,visible[end].fog);
            instances[end-first]={part.lighting.object_to_world,part.object_to_clip,
                part.lighting.material_diffuse,item.alpha,visible[end].fog};
            ++end;
        }
        state.render[D3DRENDERSTATE_ZWRITEENABLE]=opaque;
        state.render[D3DRENDERSTATE_ALPHABLENDENABLE]=!opaque;
        owned::NativeGeometryResult result;
        if(FAILED(renderer.queue_native_figure_instances(view,state,instances.data(),end-first,&result))){
            static bool reported=false;if(!reported){reported=true;GameJournal::Event("PHYSICS_FX","draw_rejected","reason=%s",result.failure_operation?result.failure_operation:"unknown");}
            success=false;
        }
        first=end;
    }
    return success;
}catch(...){return false;}
}
