#include "StdAfx.h"
#include "PhysicsFxClient.h"
#include "PhysicsFxTerrain.h"
#include "WorldClient.h"
#include "Terrain.h"
#include "TextureManager.h"
#include "OwnedRender/E1Terrain.h"
#include "GameJournal.h"
#include "retained_static_scene.h"
#include <chrono>

namespace PhysicsFx {
struct Client::Impl {
    World world;Events events;
    ITexturePtr stoneTexture;bool textureAttempted=false;
    struct Request {int id,start;Vec position;float power,radius;};
    std::vector<Request> pending;
    std::uint64_t generation=0,revision=0;
    int lastReport=0;bool reported=false;std::string reportedError;
};
Client::Client():impl(std::make_unique<Impl>()){}
Client::~Client()=default;
void Client::Reset(int tick){impl=std::make_unique<Impl>();impl->events.Reset(tick);}
void Client::Fireblast(CWorldClient& host,int id,int start,const CPlot3D& point,float power,float radius){
    if(!impl->events.Admit(id,start,host.GetSegmentTimeCounter(),host.GetFirstSegmentsCounter()>2))return;
    if(impl->pending.size()>=16||!std::isfinite(point.x)||!std::isfinite(point.y)||!std::isfinite(point.z)||!std::isfinite(power)||!std::isfinite(radius))return;
    auto* terrain=host.GetTerrain();
    if(!terrain||terrain->GetObjectFogAlpha(point)==0||host.IsBridge(point.x,point.y))return;
    impl->pending.push_back({id,start,{point.x,point.y,point.z},power,radius});
}
void Client::Tick(CWorldClient& host,bool paused){
    if(paused)return;
    auto& s=*impl;
    owned::RetainedTerrainSourceSnapshot source;
    if(!OwnedRender::E1PhysicsTerrainSource(source)) {s.pending.clear();s.world.Clear();s.reported=false;s.reportedError.clear();return;}
    if(s.generation!=source.source_generation||s.revision!=source.source_revision){
        s.world.Clear();s.reported=false;s.reportedError.clear();s.generation=source.source_generation;s.revision=source.source_revision;
    }
    const auto reportFailure=[&](){if(s.reportedError!=s.world.Error()){
        s.reportedError=s.world.Error();GameJournal::Event("PHYSICS_FX","gpu_unavailable","backend=PhysX-5.6.1 fallback=none reason=%s",s.reportedError.c_str());
    }};
    if(!s.world.Initialize()){s.pending.clear();reportFailure();return;}
    if(!s.reported){s.reported=true;GameJournal::Event("PHYSICS_FX","gpu_ready","backend=PhysX-5.6.1 dynamics=GPU broadphase=GPU device=%s",s.world.Device().c_str());}
    unsigned spawned=0;double prepareMs=0;
    for(const auto& request:s.pending){
        if(s.world.Statistics().bodies>=MaxStones)break;
        const auto p=request.position;
        if(p.x<0||p.y<0||host.IsBridge(p.x,p.y))continue;
        // Reject an upper SEC layer or a bridge; never use the stone underneath it.
        const float base=host.GetTerrain()->GetZValue(p.x,p.y,FALSE);
        if(host.GetSurfaceValue(p.x,p.y)>base+.025f||host.GetBridgeSurfaceZValue(p.x,p.y)>base+.025f)continue;
        const int x=int(std::floor(p.x/8)),y=int(std::floor(p.y/8));
        const auto key=(std::uint64_t(std::uint32_t(x))<<32)|std::uint32_t(y);
        Vec center{float(x*8+4),float(y*8+4),base};
        std::vector<Triangle> triangles;
        const auto begin=std::chrono::steady_clock::now();
        const auto hit=ExtractTerrain(*source.terrain,center,p,triangles);
        const float distance=p.z-hit.point.z;
        if(!StoneSurface(hit)||distance<-.15f||distance>std::clamp(request.radius,.35f,2.f)){
            GameJournal::Event("PHYSICS_FX","impact_rejected","id=%d x=%.3f y=%.3f material=%u normal_z=%.4f height_delta=%.3f reason=%s",
                request.id,p.x,p.y,hit.material,hit.normal.z,distance,
                !hit.found?"no_ground":!StoneSurface(hit)?"surface":"height");
            continue;
        }
        GameJournal::Event("PHYSICS_FX","stone_surface","id=%d x=%.3f y=%.3f material=%u normal_z=%.4f source=%s",
            request.id,p.x,p.y,hit.material,hit.normal.z,StoneTerrain(hit.material)?"material":"cliff");
        if(!s.world.HasPatch(key)&&!s.world.AddPatch(key,center,triangles))continue;
        prepareMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
        const std::uint32_t seed=std::uint32_t(request.id)*0x9e3779b9u^std::uint32_t(request.start)*0x85ebca6bu;
        spawned+=s.world.Burst(key,hit.point,hit.normal,request.power,seed);
    }
    if(spawned&&!s.textureAttempted){
        s.textureAttempted=true;
        auto* manager=host.GetTextureManager();
        s.stoneTexture=manager->CreateTexture(TTID_STONE_MAIN,"stones00");
        const bool loaded=!s.stoneTexture.IsEmpty()&&manager->LoadTexture(s.stoneTexture);
        if(!loaded)s.stoneTexture=nullptr;
        GameJournal::Event("PHYSICS_FX",loaded?"texture_ready":"texture_unavailable","name=stones00");
    }
    s.pending.clear();s.world.Tick(false);
    if(!s.world.Error().empty()){reportFailure();return;}
    const auto stats=s.world.Statistics();
    if(spawned||stats.errors||(stats.bodies&&host.GetSegmentTimeCounter()-s.lastReport>=18)){
        GameJournal::Event("PHYSICS_FX","stone_debris","backend=PhysX-5.6.1 spawned=%u bodies=%u active=%u patches=%u prepare_ms=%.3f step_ms=%.3f errors=%u gpu_dynamics=%d gpu_broadphase=%d gpu_heap=%llu gpu_solver=%llu gpu_contacts=%u",
            spawned,stats.bodies,stats.active,stats.patches,prepareMs,stats.stepMs,stats.errors,int(stats.gpuDynamics),int(stats.gpuBroadPhase),stats.gpuHeapBytes,stats.gpuSolverBytes,stats.gpuContacts);
        s.lastReport=host.GetSegmentTimeCounter();
    }
}
std::vector<Stone> Client::Sample(float fraction) const{return impl->world.Sample(fraction);}
std::uint64_t Client::TextureSurface() const{
    const auto* texture=impl->stoneTexture.GetPtr();
    auto* surface=texture?texture->GetSurface():nullptr;
    return surface&&surface->EnsureOwnedSurfacePublished()?surface->OwnedSurfaceId():0;
}
}
