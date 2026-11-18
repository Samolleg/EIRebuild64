#include "StdAfx.h"
#include "PhysicsFxClient.h"
#include "PhysicsFxTerrain.h"
#include "PhysicsFxPlayback.h"
#include "WorldClient.h"
#include "Terrain.h"
#include "TextureManager.h"
#include "OwnedRender/E1Terrain.h"
#include "GameJournal.h"
#include "retained_static_scene.h"
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace PhysicsFx {
namespace {
struct Impact {int id,start;Vec position;float power,radius,base=0;};
struct DebrisTick {
    int tick=0;
    std::uint64_t sequence=0;
    std::shared_ptr<const owned::TerrainCpuAsset> terrain;
    std::vector<Impact> impacts;
};
class DebrisWorker {
    World world;
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<DebrisTick> queue;
    bool stopping=false, discard=false;
    DebrisHistory history;
    bool reported=false;
    int lastReport=0;
    std::string reportedError;
    std::thread thread;
    void step(const DebrisTick& job);
    void run() noexcept {
        try {
            for(;;) {
                DebrisTick job;
                bool clear=false;
                {
                    std::unique_lock lock(mutex);
                    wake.wait(lock,[&]{return stopping||!queue.empty();});
                    if(stopping)return;
                    clear=discard;discard=false;
                    job=std::move(queue.front());queue.pop_front();
                }
                if(clear){world.Clear();reported=false;reportedError.clear();}
                step(job);
                auto snapshot=std::make_shared<StoneTracks>(world.Snapshot());
                std::lock_guard lock(mutex);
                history.publish(job.sequence,std::move(snapshot));
            }
        } catch(const std::exception& error) {
            GameJournal::Event("PHYSICS_FX","worker_failed","reason=%s",error.what());
            std::lock_guard lock(mutex);history={};stopping=true;queue.clear();
        }
    }
public:
    DebrisWorker():thread([this]{run();}){}
    ~DebrisWorker(){
        {std::lock_guard lock(mutex);stopping=true;}
        wake.notify_one();thread.join();
    }
    bool submit(DebrisTick job) {
        std::lock_guard lock(mutex);
        if(!stopping) {
            // Beyond five seconds all bodies from the discarded ticks have expired.
            if(queue.size()>=92){queue.pop_front();discard=true;}
            queue.push_back(std::move(job));
            wake.notify_one();
        }
        return history.hasStones();
    }
    void sample(double clock,DebrisPlayback& cursor){
        std::lock_guard lock(mutex);
        history.sample(clock,cursor);
    }
};
}
struct Client::Impl {
    Events events;
    ITexturePtr stoneTexture;bool textureAttempted=false;
    std::vector<Impact> pending;
    std::unique_ptr<DebrisWorker> worker;
    DebrisPlayback playback;
    std::uint64_t sequence=0;
    unsigned sampledFrames=0,advancedFrames=0;
    double reportedPosition=-1;
    std::chrono::steady_clock::time_point reportStart{};
    unsigned remaining=0;
    std::uint64_t generation=0,revision=0;
};
Client::Client():impl(std::make_unique<Impl>()){InstallCharacterPhysics();}
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
    if(!OwnedRender::E1PhysicsTerrainSource(source)) {
        s.pending.clear();s.worker.reset();s.playback={};return;
    }
    if(s.generation!=source.source_generation||s.revision!=source.source_revision) {
        s.worker.reset();s.playback={};s.generation=source.source_generation;s.revision=source.source_revision;
    }
    for(auto it=s.pending.begin();it!=s.pending.end();) {
        const auto p=it->position;
        if(p.x<0||p.y<0||host.IsBridge(p.x,p.y)){it=s.pending.erase(it);continue;}
        it->base=host.GetTerrain()->GetZValue(p.x,p.y,FALSE);
        if(host.GetSurfaceValue(p.x,p.y)>it->base+.025f||host.GetBridgeSurfaceZValue(p.x,p.y)>it->base+.025f){
            it=s.pending.erase(it);continue;
        }
        ++it;
    }
    if(!s.worker) {
        if(s.pending.empty())return;
        s.worker=std::make_unique<DebrisWorker>();
    }
    // A fragment lives at most five game seconds. Leave the worker asleep
    // between bursts once the final immutable frame has aged out.
    if(!s.pending.empty()) {
        if(!s.remaining)s.playback={};
        s.remaining=95;
    }
    if(!s.remaining){s.playback={};return;}
    --s.remaining;
    const bool stones=s.worker->submit({host.GetSegmentTimeCounter(),++s.sequence,source.terrain,std::move(s.pending)});
    s.pending.clear();
    if(stones&&!s.textureAttempted){
        s.textureAttempted=true;
        auto* manager=host.GetTextureManager();
        s.stoneTexture=manager->CreateTexture(TTID_STONE_MAIN,"stones00");
        const bool loaded=!s.stoneTexture.IsEmpty()&&manager->LoadTexture(s.stoneTexture);
        if(!loaded)s.stoneTexture=nullptr;
        GameJournal::Event("PHYSICS_FX",loaded?"texture_ready":"texture_unavailable","name=stones00");
    }
}
void DebrisWorker::step(const DebrisTick& job) {
    const auto reportFailure=[&](){if(reportedError!=world.Error()){
        reportedError=world.Error();GameJournal::Event("PHYSICS_FX","gpu_unavailable","backend=PhysX-5.6.1 fallback=none reason=%s",reportedError.c_str());
    }};
    if(!world.Initialize()){reportFailure();return;}
    if(!reported){reported=true;GameJournal::Event("PHYSICS_FX","gpu_ready","backend=PhysX-5.6.1 dynamics=GPU broadphase=GPU device=%s",world.Device().c_str());}
    unsigned spawned=0;double prepareMs=0;
    for(const auto& request:job.impacts){
        if(world.Statistics().bodies>=MaxStones)break;
        const auto p=request.position;const float base=request.base;
        const int x=int(std::floor(p.x/8)),y=int(std::floor(p.y/8));
        const auto key=(std::uint64_t(std::uint32_t(x))<<32)|std::uint32_t(y);
        Vec center{float(x*8+4),float(y*8+4),base};
        std::vector<Triangle> triangles;
        const auto begin=std::chrono::steady_clock::now();
        const bool hasPatch=world.HasPatch(key);
        const auto hit=ExtractTerrain(*job.terrain,center,p,triangles,!hasPatch);
        const float distance=p.z-hit.point.z;
        if(!StoneSurface(hit)||distance<-.15f||distance>std::clamp(request.radius,.35f,2.f)){
            GameJournal::Event("PHYSICS_FX","impact_rejected","id=%d x=%.3f y=%.3f material=%u normal_z=%.4f height_delta=%.3f reason=%s",
                request.id,p.x,p.y,hit.material,hit.normal.z,distance,
                !hit.found?"no_ground":!StoneSurface(hit)?"surface":"height");
            continue;
        }
        GameJournal::Event("PHYSICS_FX","stone_surface","id=%d x=%.3f y=%.3f material=%u normal_z=%.4f source=%s",
            request.id,p.x,p.y,hit.material,hit.normal.z,StoneTerrain(hit.material)?"material":"cliff");
        if(!hasPatch&&!world.AddPatch(key,center,triangles))continue;
        prepareMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
        const std::uint32_t seed=std::uint32_t(request.id)*0x9e3779b9u^std::uint32_t(request.start)*0x85ebca6bu;
        spawned+=world.Burst(key,hit.point,hit.normal,request.power,seed);
    }
    world.Tick(false);
    if(!world.Error().empty()){reportFailure();return;}
    const auto stats=world.Statistics();
    if(spawned||stats.errors||(stats.bodies&&job.tick-lastReport>=18)){
        GameJournal::Event("PHYSICS_FX","stone_debris","backend=PhysX-5.6.1 spawned=%u bodies=%u active=%u patches=%u prepare_ms=%.3f step_ms=%.3f substeps=%u errors=%u gpu_dynamics=%d gpu_broadphase=%d gpu_heap=%llu gpu_solver=%llu gpu_contacts=%u",
            spawned,stats.bodies,stats.active,stats.patches,prepareMs,stats.stepMs,stats.substeps,stats.errors,int(stats.gpuDynamics),int(stats.gpuBroadPhase),stats.gpuHeapBytes,stats.gpuSolverBytes,stats.gpuContacts);
        lastReport=job.tick;
    }
}
std::vector<Stone> Client::Sample(float fraction,bool paused) const {
    auto& s=*impl;
    if(!s.worker || !s.remaining || s.stoneTexture.IsEmpty())return {};
    if(!paused)s.worker->sample(double(s.sequence)+std::clamp(fraction,0.f,1.f),s.playback);
    if(!s.playback.visible || s.playback.visible->empty()){
        s.sampledFrames=s.advancedFrames=0;
        return {};
    }
    if(!paused && RuntimeDiagnostics::Enabled()){
        const auto now=std::chrono::steady_clock::now();
        if(!s.sampledFrames)s.reportStart=now;
        ++s.sampledFrames;
        if(s.playback.position>s.reportedPosition)++s.advancedFrames;
        s.reportedPosition=s.playback.position;
        const double elapsed=std::chrono::duration<double>(now-s.reportStart).count();
        if(elapsed>=.5){
            GameJournal::Event("PHYSICS_FX","debris_presentation","frames=%u advanced=%u elapsed_s=%.3f lag_ticks=%.2f",
                s.sampledFrames,s.advancedFrames,elapsed,double(s.sequence)-s.playback.position);
            s.sampledFrames=s.advancedFrames=0;
        }
    }
    return SampleTracks(*s.playback.visible,s.playback.fraction);
}
std::uint64_t Client::TextureSurface() const{
    const auto* texture=impl->stoneTexture.GetPtr();
    auto* surface=texture?texture->GetSurface():nullptr;
    return surface&&surface->EnsureOwnedSurfacePublished()?surface->OwnedSurfaceId():0;
}
}
