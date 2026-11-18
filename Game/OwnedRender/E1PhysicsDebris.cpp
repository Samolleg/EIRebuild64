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
    auto stones=frame.debris;
    const auto depth=[&](const PhysicsFx::Stone& s){const auto& p=s.pose.position;return frame.worldToClip[12]*p.x+frame.worldToClip[13]*p.y+frame.worldToClip[14]*p.z+frame.worldToClip[15];};
    std::stable_sort(stones.begin(),stones.end(),[&](const auto& a,const auto& b){return depth(a)>depth(b);});
    bool success=true;
    for(const auto& stone:stones){
        const float position[]={stone.pose.position.x,stone.pose.position.y,stone.pose.position.z};
        const unsigned char fog=frame.fog?frame.fog(frame.fogContext,position):0;
        if(!fog||stone.alpha<=0)continue;
        auto view=DebrisView(meshes[stone.shape%4],stone,frame.worldToClip,fog);
        view.owned_texture=frame.debrisTexture;
        owned::FixedState state=input;
        state.render[D3DRENDERSTATE_ZENABLE]=TRUE;state.render[D3DRENDERSTATE_ZFUNC]=D3DCMP_LESSEQUAL;
        state.render[D3DRENDERSTATE_ZWRITEENABLE]=stone.alpha>=.999f?TRUE:FALSE;
        state.render[D3DRENDERSTATE_CULLMODE]=D3DCULL_NONE;
        state.render[D3DRENDERSTATE_ALPHATESTENABLE]=FALSE;
        state.render[D3DRENDERSTATE_ALPHABLENDENABLE]=stone.alpha<.999f?TRUE:FALSE;
        state.render[D3DRENDERSTATE_SRCBLEND]=D3DBLEND_SRCALPHA;state.render[D3DRENDERSTATE_DESTBLEND]=D3DBLEND_INVSRCALPHA;
        state.render[D3DRENDERSTATE_SPECULARENABLE]=FALSE;
        state.stage[0][D3DTSS_COLOROP]=D3DTOP_MODULATE;state.stage[0][D3DTSS_COLORARG1]=D3DTA_TEXTURE;state.stage[0][D3DTSS_COLORARG2]=D3DTA_DIFFUSE;
        state.stage[0][D3DTSS_ADDRESSU]=state.stage[0][D3DTSS_ADDRESSV]=D3DTADDRESS_CLAMP;
        state.stage[0][D3DTSS_MINFILTER]=state.stage[0][D3DTSS_MAGFILTER]=D3DTFG_LINEAR;state.stage[0][D3DTSS_MIPFILTER]=D3DTFP_LINEAR;
        state.stage[0][D3DTSS_ALPHAOP]=D3DTOP_SELECTARG1;state.stage[0][D3DTSS_ALPHAARG1]=D3DTA_DIFFUSE;
        state.stage[1][D3DTSS_COLOROP]=D3DTOP_DISABLE;state.stage[1][D3DTSS_ALPHAOP]=D3DTOP_DISABLE;
        owned::NativeGeometryResult result;
        if(FAILED(renderer.queue_native_owned(view,state,&result))){
            static bool reported=false;if(!reported){reported=true;GameJournal::Event("PHYSICS_FX","draw_rejected","reason=%s",result.failure_operation?result.failure_operation:"unknown");}
            success=false;
        }
    }return success;
}catch(...){return false;}
}
