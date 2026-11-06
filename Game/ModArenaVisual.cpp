#include "StdAfx.h"
#include "ModArenaVisual.h"
#include "ModArena.h"
#include "Object3DClientSpecific.h"
#include "Effect.h"
#include "EffectManager.h"
#include "Particle.h"
#include "Camera.h"
#include "Renderer.h"
#include "GameJournal.h"
#include "OwnedRender/E1Scene.h"
#include "OwnedRender/E1World.h"
#include "embedded_arena.h"

namespace ModArenaVisual {
namespace {
struct Lease {int tick=-100000;float time=0;ULONGLONG wall=0;};
Lease dome,flow;
bool armed=false;
constexpr int towers[3]={990200,990300,990310},crystals[3]={990202,990302,990312};
constexpr float xy[3][2]={{49,56},{58.107695f,26.741670f},{78.892305f,49.258330f}};
bool Fresh(const Lease& lease,int tick) {return tick>=lease.tick&&tick-lease.tick<4;}
float Fraction(const Lease& lease) {
    if(theWorld.IsCombatPause())return 0;
    return float((std::min)(GetTickCount64()-lease.wall,ULONGLONG(56)))*.001f;
}
bool Near(CMapObject* object,float x,float y) {
    if(!object||object->IsInvalid())return false;
    const auto p=object->GetCP();
    return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&std::fabs(p.x-x)<.8f&&std::fabs(p.y-y)<.8f;
}
Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> Texture(int id) {
    auto* object=theWorld.GetMapObject(id);
    if(!object||object->IsInvalid()||!object->FinalDataLoading())return {};
    auto* specific=dynamic_cast<CObject3DClientSpecific*>(object->GetObjectSpecific());
    auto* texture=specific?specific->GetPrimaryTexture():nullptr;
    auto* surface=texture?texture->GetSurface():nullptr;
    return surface?surface->GetShaderResource():Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>{};
}
void Particles(CScript* owner,int tick) {
    if(!owner)return;
    const int wave=int(ModEncounter::Read(theWorldServer,"A67.Wave"));
    const float radius=ModEncounter::Read(theWorldServer,"A67.WaveRadius");
    if(!std::isfinite(radius)||radius<0||radius>124)return;
    auto* manager=static_cast<CEffectManager*>(theWorld.GetEffectManager());if(!manager)return;
    for(POSITION pos=owner->particles.GetHeadPosition();pos;) {
        const auto& info=owner->particles.GetNext(pos);
        const int kind=info.nScriptID-8690000;
        auto* source=info.pParticle;
        if(kind<1||kind>3||!source||source->IsInvalid()||source->typeID!=PTID_SMOKE)continue;
        auto* client=manager->FindByID(source->GetID());
        if(!client||client->IsInvalid()||client->WhoAmI()!=HPC_EFFECTPARTICLE)continue;
        auto* effect=static_cast<CEffectParticle*>(client);
        if(effect->typeID==PTID_SMOKE&&effect->pParticle)
            effect->pParticle->ConfigureArenaWave(kind,wave==kind?radius:-1.f,radius==0,tick);
    }
}
}
void Reset() noexcept {dome={};flow={};armed=false;}
void Arm(CMapObject* carrier,float time,CScript* owner) {
    if(!ModArena::Active()||!carrier||carrier->IsInvalid()||!std::isfinite(time))return;
    const int id=carrier->GetID();if(id!=990203&&id!=990205)return;
    auto* server=theWorldServer.GetMapObject(id);if(server!=carrier)return;
    const int tick=theWorldServer.GetSegmentTimeCounter();
    (id==990203?dome:flow)={tick,time,GetTickCount64()};armed=true;
    if(id==990205)Particles(owner,tick);
}
void Draw() {
    // Ordinary frames pay one branch only until an arena command arms a lease.
    if(!armed)return;
    const int tick=theWorldServer.GetSegmentTimeCounter();
    if((!Fresh(dome,tick)&&!Fresh(flow,tick))||!ModArena::Active()){armed=false;return;}
    auto* boss=theWorld.GetMapObject(1000105);if(!Near(boss,62,44))return;
    OwnedRender::E1WorldFrame world;
    if(!OwnedRender::E1ReadWorldFrame(world)||!world.camera.valid||!world.lightingValid)return;
    auto* dx=theRenderer.GetD3D();if(!dx||!dx->IsOwnedWorldActive())return;
    owned::EmbeddedArenaInput input;
    input.world_epoch=OwnedRender::E1Scene().Epoch();input.boss_id=boss->GetID();input.tick=tick;
    const auto p=boss->GetCP(),camera=theCamera.GetCP();
    input.base={p.x,p.y,p.z};input.camera={camera.x,camera.y,camera.z};
    input.world_to_clip=world.camera.world_to_clip;
    input.dome=Fresh(dome,tick)&&Near(theWorld.GetMapObject(990203),62,44);
    input.flow=Fresh(flow,tick)&&Near(theWorld.GetMapObject(990205),62,44);
    if(!input.dome&&!input.flow)return;
    input.dome_time=dome.time+Fraction(dome);input.flow_fraction=Fraction(flow);
    input.flow_time=flow.time+input.flow_fraction;
    input.phase=int(ModEncounter::Read(theWorldServer,"A62.Phase"));
    input.wave=int(ModEncounter::Read(theWorldServer,"A67.Wave"));
    input.wave_radius=ModEncounter::Read(theWorldServer,"A67.WaveRadius");
    std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>,8> retained;
    std::array<ID3D11ShaderResourceView*,8> textures{};
    if(input.dome)retained[0]=Texture(990203);
    if(input.flow)retained[1]=Texture(990205);
    for(int i=0;i<3;++i) {
        auto& tower=input.towers[i];char key[48];
        std::snprintf(key,sizeof(key),"A67.Broken%d",i);tower.broken=ModEncounter::Read(theWorldServer,key)!=0;
        std::snprintf(key,sizeof(key),"A67.BreakAt%d",i);tower.break_time=ModEncounter::Read(theWorldServer,key);
        auto* crystal=theWorld.GetMapObject(crystals[i]);tower.present=Near(crystal,xy[i][0],xy[i][1]);
        if(tower.present){const auto cp=crystal->GetCP();tower.crystal={cp.x,cp.y,cp.z};}
        if(input.flow&&tower.broken) {retained[2+i*2]=Texture(towers[i]);retained[3+i*2]=Texture(crystals[i]);}
    }
    for(size_t i=0;i<textures.size();++i)textures[i]=retained[i].Get();
    dx->SubmitArenaVisual(input,textures);
}
}
