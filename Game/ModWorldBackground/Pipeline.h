#pragma once
#include "WorldBackgroundDecorWorker.h"
#include "WorldBackgroundPrefetch.h"
#include "WorldBackgroundRetirement.h"
#include "Capacity.h"
#include "GroupPublication.h"
#include <future>
#include <chrono>

namespace ModWorldBackground {
using namespace WorldBackground;
struct Input {WorldBackground::Identity active;ProfileFiles files;std::wstring layout;uint64_t payloadBudget=Capacity::PayloadBytes;};
inline std::string MapName(std::string name){
    name=Key(name.substr(name.find_last_of("/\\")==std::string::npos?0:name.find_last_of("/\\")+1));
    if(name.size()>4&&name.substr(name.size()-4)==".mpr")name.resize(name.size()-4);return name;
}
// Capture on the package/scene owner only. Explicit resource order is copied;
// workers never consult CModPackage, registry, mapped-resource globals or CWD.
template<class Package> Input CaptureInput(const Package& package,const std::wstring& resourceRoot,const std::wstring& workRoot,WorldBackground::Identity active){
    Input result;result.active=std::move(active);result.active.profile="deja-vu";
    auto wide=[](const std::string& text){
        const int n=MultiByteToWideChar(CP_ACP,0,text.data(),int(text.size()),nullptr,0);
        if(n<=0)throw std::runtime_error("Invalid resource path");std::wstring out(size_t(n),L'\0');
        MultiByteToWideChar(CP_ACP,0,text.data(),int(text.size()),&out[0],n);return out;
    };
    auto chain=[&](const char* category){
        std::vector<std::string> paths;bool ignore=false;package.ResourcePlan(category,paths,ignore);
        std::vector<std::wstring> out;for(const auto& p:paths)out.push_back(wide(p));
        if(!ignore)out.push_back(resourceRoot+L"/"+std::wstring(category,category+strlen(category)));return out;
    };
    std::vector<std::string> database;bool ignore=false;package.ResourcePlan("database.res",database,ignore);
    bool signature=false;for(const auto& p:database){auto name=MapName(p);if(name=="databasejmv.res")signature=true;}
    if(!signature)throw std::runtime_error("No verified Jamais Vu resource profile");
    result.files.profile="deja-vu";result.files.layoutRoot=wide(package.Directory());
    result.files.mapRoots={result.files.layoutRoot+L"/Maps",workRoot+L"/Maps"};
    result.files.textureArchives=chain("textures.res");result.files.materialArchives=chain("materials.res");
    // Config::fileListFig flattens figure replacements, then menu replacements
    // into resReplaceFig. Original figures + menus follow this combined chain.
    std::vector<std::string> figures;bool skipFigures=false,skipMenus=false;
    package.ResourcePlan("figures.res",figures,skipFigures);
    std::vector<std::string> menus;package.ResourcePlan("menus.res",menus,skipMenus);
    for(const auto& p:figures)result.files.figureArchives.push_back(wide(p));
    for(const auto& p:menus)result.files.figureArchives.push_back(wide(p));
    if(!skipFigures)result.files.figureArchives.push_back(resourceRoot+L"/figures.res");
    if(!skipMenus)result.files.figureArchives.push_back(resourceRoot+L"/menus.res");
    result.layout=result.files.layoutRoot+L"/world-map-layouts.json";return result;
}
struct Request {uint32_t epoch=0;SectorView view;VisualEnvironment environment;double seconds=0;};
struct GroupResult;
struct GroupOffer {std::shared_ptr<GroupResult> root;GroupPublication::Ticket ticket;};
struct FrameSnapshot {
    std::array<GroupOffer,2> offers;
    std::shared_ptr<GroupPublication> publication;
    // RenderSnapshot::source and its mesh pointers borrow only these owners.
    std::shared_ptr<const BackgroundData> data;
    std::shared_ptr<RenderSnapshot> render;
    std::shared_ptr<const FrameSnapshot> decorSource;
    bool decorPrepared=false;
    SectorSelection terrainSelection;Decor::Selection decorationSelection;uint64_t capacity=0,futureBudget=0;
};
class Producer {
    friend class GroupProducer;
    Input input;std::shared_ptr<BackgroundData> data=std::make_shared<BackgroundData>();
    Point origin;SectorStreaming stream;CameraPrefetch prefetch;CpuLighting lighting;VisualInvalidation changes;
    std::vector<std::shared_ptr<const LightingPacket>> packets;std::vector<std::string> packetDiagnostics;
    uint64_t topology=0,lastPlan=0,built=0;uint32_t epoch=0,lastCell=0;
    std::vector<size_t> lastObjects;const Decor::Scene* lastScene=nullptr;
public:
    static constexpr uint64_t LightingBudget=64ull*1024*1024;
    explicit Producer(Input value):input(std::move(value)){if(!input.payloadBudget||input.payloadBudget>Capacity::PayloadBytes)throw std::runtime_error("Invalid native background capacity");}
    uint64_t Budget()const{return input.payloadBudget;}
    static uint64_t DecorBytes(const std::shared_ptr<FrameSnapshot>& previous){
        uint64_t bytes=0;if(previous)for(const auto& m:previous->render->meshes)if(m.key[1]&0x80000000u)bytes+=uint64_t(m.mesh.vertexCount)*sizeof(ET_Vertex)+uint64_t(m.mesh.textureWidth)*m.mesh.textureHeight*4;return bytes;
    }
    void Load(const std::function<bool()>& cancelled){
        const auto raw=ReadFileBounded(input.layout,4u*1024*1024);const std::string json(raw.begin(),raw.end());
        const auto layout=ParseLayout(json);input.active=ResolveActiveLayout(layout,input.active);bool found=false;
        for(const auto& p:layout)if(p.identity.profile==input.active.profile&&p.identity.allod==input.active.allod&&p.identity.zone==input.active.zone&&MapName(p.identity.mpr)==MapName(input.active.mpr)){origin=p.origin;found=true;break;}
        if(!found)throw std::runtime_error("Active map absent from saved layout");
        data->Prepare(true,json,LayoutEntries(layout),input.active,[&](const WorldBackground::Identity& id){
            if(cancelled())throw std::runtime_error("Background load cancelled");
            for(const auto& p:layout)if(Key(p.identity.profile)==Key(id.profile)&&Key(p.identity.allod)==Key(id.allod)&&Key(p.identity.zone)==Key(id.zone))return input.files.LayoutMap(id.profile,p.mprPath);
            throw std::runtime_error("Missing layout MPR path");
        },64u*1024*1024,[&](const std::string& profile,const std::string& member){if(cancelled())throw std::runtime_error("Background texture cancelled");return input.files.Texture(profile,member);});
        if(cancelled())throw std::runtime_error("Background load cancelled");
        char diagnostic[192];sprintf_s(diagnostic,"WorldBackground resources ready: neighbors=%u bytes=%llu skipped=%u\n",unsigned(Data().neighbors.size()),uint64_t(Data().bytes),unsigned(Data().diagnostics.size()));OutputDebugStringA(diagnostic);
        for(const auto& message:Data().diagnostics)OutputDebugStringA(message.c_str());
    }
    const Snapshot& Data()const{return data->Get();}
    const ProfileFiles& Files()const{return input.files;}
    std::shared_ptr<FrameSnapshot> Prepare(Request request,const Decor::Scene* decor,const std::shared_ptr<FrameSnapshot>& previous,const std::function<bool()>& cancelled){
        if(!request.epoch||request.environment.epoch!=request.epoch)throw std::runtime_error("Invalid background epoch");
        if(epoch!=request.epoch){
            packets.clear();packetDiagnostics.clear();size_t used=0;
            for(const auto& p:Data().neighbors)try{auto packet=LightingPacket::Build(p,request.epoch,1,size_t(LightingBudget)-used,cancelled);used+=packet->PayloadBytes();packets.push_back(std::move(packet));}
                catch(const std::exception& e){if(cancelled())throw;packetDiagnostics.push_back(p.placement.entry.identity.mpr+": "+e.what());}
            epoch=request.epoch;built=0;
        }
        prefetch.Update(request.view,request.epoch,request.seconds);
        // Optional look-ahead must not consume the established decoration reserve.
        // The native owner commits one whole scene, so current terrain reserves
        // plan.bytes; old GPU scene is charged separately as pending residency.
        const auto futureBudget=Budget()-(std::min)(Budget(),DecorBytes(previous));
        const auto plan=stream.Update(Data(),request.epoch,1,true,request.view,Budget(),Capacity::Meshes,0,false,futureBudget);
        const auto selected=decor?Decor::SelectObjects(*decor,Data(),request.view,Budget()-(std::min)(Budget(),plan.bytes),Capacity::Meshes-plan.meshes,plan.meshes<128?128-plan.meshes:0):Decor::Selection{};
        if(lastPlan!=plan.request||lastObjects!=selected.objects||lastCell!=selected.cellSize||lastScene!=decor){
            ++topology;lastPlan=plan.request;lastObjects=selected.objects;lastCell=selected.cellSize;lastScene=decor;
        }
        const auto revision=changes.Update(request.epoch,topology,request.environment,VisualLight|VisualSunPosition|VisualTerrainSun);
        if(previous&&built==revision)return {};
        auto next=std::make_shared<FrameSnapshot>();next->data=data;next->terrainSelection=plan;next->decorationSelection=selected;next->capacity=Budget();next->futureBudget=futureBudget;
        next->render=PrepareRenderSnapshot(Data(),plan,origin,[](const WorldBackground::Vertex& v,Point,const Material*,uint8_t alpha,float u,float vv){return ET_Vertex{v.x,v.y,v.z,(uint32_t(alpha)<<24)|0xffffffu,0xff000000u,u,vv};},cancelled,Budget(),previous?previous->render.get():nullptr,revision);
        if(!lighting.Apply(*next->render,packets,request.environment,previous?previous->render.get():nullptr,cancelled))next->render->diagnostics.push_back("Authoritative CPU light unavailable; reference static fallback");
        if(decor)Decor::Append(*decor,Data(),selected,origin,request.environment,*next->render,previous?previous->render.get():nullptr,cancelled);
        else if(previous&&previous->render->view.mapEpoch==request.epoch){
            // Hold admitted decoration while its single Scene is owned by the
            // decor worker. Flatten ownership to avoid a chain of old frames.
            const auto source=previous->decorSource?previous->decorSource:previous;
            for(const auto& mesh:source->render->meshes)if(mesh.key[1]&0x80000000u){
                const auto bytes=uint64_t(mesh.mesh.vertexCount)*sizeof(ET_Vertex)+uint64_t(mesh.mesh.textureWidth)*mesh.mesh.textureHeight*4;
                if(bytes>Budget()-next->render->bytes||next->render->meshes.size()==Capacity::Meshes)break;
                next->decorSource=source;next->render->meshes.push_back(mesh);next->render->bytes+=bytes;
            }
            next->render->view.count=uint32_t(next->render->meshes.size());next->render->view.meshes=next->render->meshes.data();
        }
        if(next->render->bytes>Budget()||!Capacity::Accepts(next->render->bytes,uint32_t(next->render->meshes.size())))throw std::runtime_error("Native background residency budget exceeded");
        next->render->diagnostics.insert(next->render->diagnostics.end(),packetDiagnostics.begin(),packetDiagnostics.end());
        next->render->lightingPackets=packets;next->render->view.topologyRevision=topology;next->decorPrepared=decor!=nullptr;built=revision;return next;
    }
};
// One coalescing CPU worker and one source-bound decoration worker. All owner
// data is detached before Start; BeforeDraw only supplies values and borrows an
// immutable result. During map lifetime heavy final destruction stays on worker.
class Worker {
    std::mutex mutex;std::condition_variable wake,completed;std::thread thread;
    std::atomic<bool> closing{true};Request latest;uint64_t sequence=0;
    std::shared_ptr<FrameSnapshot> ready;std::string error;
    void Run(Input input){
        WorkerRetirement<FrameSnapshot> retired;std::shared_ptr<FrameSnapshot> previous;
        try{
            Producer producer(std::move(input));producer.Load([&]{return closing.load();});
            std::unique_ptr<Decor::Scene> decor(new Decor::Scene);bool decorReady=false;SectorView decorView;
            DecorWorker decorWorker([&](Decor::Scene& scene,const SectorView& view,const std::function<bool()>& stop){scene.Prepare(producer.Data(),producer.Files(),[&]{return closing.load()||stop();},&view);});
            uint64_t seen=0;bool decorChanged=false;DWORD diagnosticTime=0;
            while(!closing){
                auto finished=decorWorker.Poll();if(finished.scene){
                    decor=std::move(finished.scene);{std::lock_guard<std::mutex> lock(mutex);decorReady=finished.error.empty()&&finished.epoch==latest.epoch;}
                    decorView=finished.view;decorChanged=true;
                    if(!finished.error.empty())OutputDebugStringA(finished.error.c_str());
                }
                Request request;
                {std::unique_lock<std::mutex> lock(mutex);
                    if(sequence==seen&&!decorChanged){wake.wait_for(lock,std::chrono::milliseconds(50),[&]{return closing.load()||sequence!=seen;});if(closing)return;if(sequence==seen)continue;}
                    request=latest;seen=sequence;
                }
                retired.Collect();if(!request.epoch||!Capacity::CanPrepare(retired.Size()))continue;
                auto next=producer.Prepare(request,decor&&decorReady?decor.get():nullptr,previous,[&]{return closing.load();});decorChanged=false;
                if(next){retired.Keep(next);
                    {std::lock_guard<std::mutex> lock(mutex);if(closing)return;if(next->render->view.mapEpoch!=latest.epoch)continue;ready=next;}
                    previous=std::move(next);completed.notify_all();
                    if(!diagnosticTime||GetTickCount()-diagnosticTime>=1000){diagnosticTime=GetTickCount();char message[320];sprintf_s(message,"WorldBackground worker ready: epoch=%u camera=%g,%g sectors=%u meshes=%u bytes=%llu decor=%u light_valid=%u retired=%u\n",request.epoch,request.view.camera.x,request.view.camera.y,unsigned(previous->render->sectors.size()),unsigned(previous->render->meshes.size()),previous->render->bytes,unsigned(previous->decorPrepared),request.environment.valid,unsigned(retired.Size()));OutputDebugStringA(message);char admission[256];sprintf_s(admission,"WorldBackground selection: capacity=%llu terrain_skipped=%u visible_terrain_skipped=%u decor_skipped=%u visible_decor_skipped=%u future_budget=%llu\n",previous->capacity,previous->terrainSelection.budgetSkipped,previous->terrainSelection.visibleSkipped,previous->decorationSelection.skipped,previous->decorationSelection.visibleSkipped,previous->futureBudget);OutputDebugStringA(admission);for(const auto& error:previous->render->diagnostics)OutputDebugStringA(error.c_str());}
                }
                // Publish terrain first. Decoration preparation never blocks it.
                if(decor&&(!decorReady||std::hypot(request.view.camera.x-decorView.camera.x,request.view.camera.y-decorView.camera.y)>=16)){
                    if(decorWorker.Submit(decor,request.view,request.epoch))decorReady=false;
                }
            }
        }catch(const std::exception& e){if(!closing){std::lock_guard<std::mutex> lock(mutex);error=e.what();OutputDebugStringA(error.c_str());completed.notify_all();}}
        // Retain published owner/source until the render owner has released its
        // borrow. Stop releases ready before join and resets GPU first.
        std::unique_lock<std::mutex> lock(mutex);wake.wait(lock,[&]{return closing.load();});
    }
public:
    ~Worker(){Stop();}
    Worker()=default;Worker(const Worker&)=delete;Worker& operator=(const Worker&)=delete;
    void Start(Input input){Stop();closing=false;sequence=0;latest={};error.clear();thread=std::thread([this,input=std::move(input)]()mutable{Run(std::move(input));});}
    bool RequestFrame(const Request& request){std::unique_lock<std::mutex> lock(mutex,std::try_to_lock);if(!lock.owns_lock()||closing)return false;latest=request;++sequence;lock.unlock();wake.notify_one();return true;}
    std::shared_ptr<FrameSnapshot> Borrow(uint32_t epoch){std::unique_lock<std::mutex> lock(mutex,std::try_to_lock);return lock.owns_lock()&&ready&&ready->render->view.mapEpoch==epoch?ready:nullptr;}
    void Stop(){closing=true;{std::lock_guard<std::mutex> lock(mutex);ready.reset();++sequence;}wake.notify_one();completed.notify_all();if(thread.joinable())thread.join();}
    // Headless contract only; the render owner never waits for preparation.
    std::shared_ptr<FrameSnapshot> Wait(uint32_t epoch,bool decor,std::chrono::milliseconds timeout,uint64_t afterRevision=0){
        std::unique_lock<std::mutex> lock(mutex);completed.wait_for(lock,timeout,[&]{return closing.load()||!error.empty()||(ready&&ready->render->view.mapEpoch==epoch&&(!decor||ready->decorPrepared)&&ready->render->view.contentRevision>afterRevision);});
        if(!error.empty())throw std::runtime_error(error);return ready&&ready->render->view.mapEpoch==epoch&&(!decor||ready->decorPrepared)&&ready->render->view.contentRevision>afterRevision?ready:nullptr;
    }
};

// Runtime grouped path. Legacy Producer/Worker above remain for the old, unrun
// resource inspector; game callers use this actual publication/ownership owner.
struct GroupResult {
    std::shared_ptr<GroupPublication::RootPool::Lease> lease;
    std::shared_ptr<const BackgroundData> data;
    std::shared_ptr<RenderSnapshot> render;
    uint64_t token=0,light=0;unsigned group=0;
};
inline std::vector<GroupPublication::Meta> GroupMetadata(const RenderSnapshot& render){
    std::vector<GroupPublication::Meta> result;
    for(const auto& v:render.meshes){const auto& m=v.mesh;
        if(!m.vertexCount||m.vertexCount>65535||m.vertexCount%3||!m.vertices||!m.argb||!m.textureWidth||m.textureWidth>4096||!m.textureHeight||m.textureHeight>4096||
            (m.material.pass!=ET_OPAQUE&&m.material.pass!=ET_ALPHA)||(m.material.flags&~(ET_MATERIAL_ALPHA_TEST|ET_MATERIAL_ADDITIVE))||m.material.alphaRef>255||
            ((m.material.flags&ET_MATERIAL_ADDITIVE)&&m.material.pass!=ET_ALPHA)||!std::isfinite(m.worldX)||!std::isfinite(m.worldY))throw std::runtime_error("Invalid group mesh payload");
        GroupPublication::Meta meta;
        meta.key=v.key;meta.geometry=v.geometryRevision;meta.texture=v.textureRevision;meta.vertices=m.vertexCount;
        meta.width=m.textureWidth;meta.height=m.textureHeight;meta.material=m.material;
        meta.x=m.worldX-render.view.activeWorldX;meta.y=m.worldY-render.view.activeWorldY;
        meta.bytes=uint64_t(m.vertexCount)*sizeof(ET_Vertex)+uint64_t(m.textureWidth)*m.textureHeight*4;
        meta.box.low={{1e300,1e300,1e300}};meta.box.high={{-1e300,-1e300,-1e300}};
        for(uint32_t i=0;i<m.vertexCount;++i){const auto& vertex=m.vertices[i];const double xyz[3]={vertex.x+meta.x,vertex.y+meta.y,vertex.z};
            for(unsigned a=0;a<3;++a){if(!std::isfinite(xyz[a]))throw std::runtime_error("Invalid group bounds");meta.box.low[a]=(std::min)(meta.box.low[a],xyz[a]);meta.box.high[a]=(std::max)(meta.box.high[a],xyz[a]);}}
        result.push_back(meta);
    }return result;
}
class GroupProducer {
    Producer source;VisualInvalidation light;uint64_t decorToken=0,sceneSerial=0,lastSceneSerial=0;
    std::vector<size_t> objects;uint32_t cell=0;double viewStamp=-1;SectorView selectedView;
public:
    struct Plan {SectorSelection terrain;Decor::Selection decor;uint64_t light=0,decorToken=0;};
    explicit GroupProducer(Input input):source(std::move(input)){}
    void Load(const std::function<bool()>& stop){source.Load(stop);}
    const Snapshot& Data()const{return source.Data();}
    const ProfileFiles& Files()const{return source.Files();}
    void SceneChanged(){++sceneSerial;}
    Plan Select(Request& request,const Decor::Scene* decor,const GroupPublication& publication){
        Plan plan;const auto resident=publication.Read(1);const auto decorBytes=(std::max)(resident.current.bytes,resident.finalBytes);
        if(viewStamp!=request.seconds){source.prefetch.Update(request.view,request.epoch,request.seconds);viewStamp=request.seconds;selectedView=request.view;}else request.view=selectedView;
        plan.terrain=source.stream.Update(Data(),request.epoch,1,true,request.view,source.Budget(),Capacity::Meshes,0,false,source.Budget()-(std::min)(source.Budget(),decorBytes));
        if(decor){plan.decor=Decor::SelectObjects(*decor,Data(),request.view,source.Budget()-(std::min)(source.Budget(),plan.terrain.bytes),Capacity::Meshes-plan.terrain.meshes,plan.terrain.meshes<128?128-plan.terrain.meshes:0);
            if(!decorToken||objects!=plan.decor.objects||cell!=plan.decor.cellSize||sceneSerial!=lastSceneSerial){++decorToken;objects=plan.decor.objects;cell=plan.decor.cellSize;lastSceneSerial=sceneSerial;}}
        plan.decorToken=decorToken;plan.light=light.Update(request.epoch,0,request.environment,VisualLight|VisualSunPosition|VisualTerrainSun);return plan;
    }
    std::shared_ptr<GroupResult> Prepare(const GroupPublication::Ticket& ticket,const Request& request,const Plan& plan,const Decor::Scene* decor,
        const std::shared_ptr<GroupResult>& previous,std::shared_ptr<GroupPublication::RootPool::Lease> lease,const std::function<bool()>& cancelled){
        auto result=std::make_shared<GroupResult>();result->lease=std::move(lease);result->data=source.data;result->token=ticket.token;result->light=ticket.light;result->group=ticket.group;
        if(ticket.group==0){
            if(source.epoch!=request.epoch){source.packets.clear();source.packetDiagnostics.clear();size_t used=0;
                for(const auto& n:Data().neighbors)try{auto packet=LightingPacket::Build(n,request.epoch,1,size_t(Producer::LightingBudget)-used,cancelled);used+=packet->PayloadBytes();source.packets.push_back(std::move(packet));}
                    catch(const std::exception& e){if(cancelled())throw;source.packetDiagnostics.push_back(n.placement.entry.identity.mpr+": "+e.what());}
                source.epoch=request.epoch;}
            result->render=PrepareRenderSnapshot(Data(),plan.terrain,source.origin,[](const WorldBackground::Vertex& v,Point,const Material*,uint8_t alpha,float u,float vv){return ET_Vertex{v.x,v.y,v.z,(uint32_t(alpha)<<24)|0xffffffu,0xff000000u,u,vv};},cancelled,source.Budget(),previous?previous->render.get():nullptr,ticket.revision);
            if(!source.lighting.Apply(*result->render,source.packets,request.environment,previous?previous->render.get():nullptr,cancelled))result->render->diagnostics.push_back("Authoritative CPU light unavailable; reference static fallback");
            result->render->lightingPackets=source.packets;
        }else{
            if(!decor)throw std::runtime_error("Decoration scene unavailable");
            SectorSelection empty;empty.epoch=request.epoch;empty.layoutRevision=1;empty.request=ticket.topology;
            result->render=PrepareRenderSnapshot(Data(),empty,source.origin,{},cancelled,source.Budget(),nullptr,ticket.revision);
            Decor::Append(*decor,Data(),plan.decor,source.origin,request.environment,*result->render,previous?previous->render.get():nullptr,cancelled);
        }
        result->render->view.topologyRevision=ticket.topology;
        if(!Capacity::Accepts(result->render->bytes,uint32_t(result->render->meshes.size())))throw std::runtime_error("Group payload exceeds common capacity");return result;
    }
};
class GroupWorker {
    std::mutex mutex;std::condition_variable wake;std::thread thread;std::atomic<bool> closing{true};Request latest;uint64_t sequence=0;
    std::shared_ptr<FrameSnapshot> ready;
    std::shared_ptr<GroupPublication> publication=std::make_shared<GroupPublication>();
    void Run(Input input){
        WorkerRetirement<GroupResult> retired;std::array<std::shared_ptr<GroupResult>,2> previous;std::array<GroupOffer,2> offers;
        try{
            GroupProducer producer(std::move(input));producer.Load([&]{return closing.load();});
            std::unique_ptr<Decor::Scene> decor(new Decor::Scene);bool decorReady=false;SectorView decorView;
            DecorWorker decorWorker([&](Decor::Scene& scene,const SectorView& view,const std::function<bool()>& stop){scene.Prepare(producer.Data(),producer.Files(),[&]{return closing.load()||stop();},&view);});
            uint64_t seen=0;
            while(!closing){
                auto finished=decorWorker.Poll();if(finished.scene){decor=std::move(finished.scene);decorReady=finished.error.empty();decorView=finished.view;producer.SceneChanged();if(!finished.error.empty())OutputDebugStringA(finished.error.c_str());}
                Request request;{std::unique_lock<std::mutex> lock(mutex);if(sequence==seen)wake.wait_for(lock,std::chrono::milliseconds(50));if(closing)break;request=latest;seen=sequence;}
                retired.Collect();if(!request.epoch)continue;
                const auto plan=producer.Select(request,decor&&decorReady?decor.get():nullptr,*publication);const auto now=GroupPublication::Now();
                publication->Request(0,request.epoch,plan.terrain.request,plan.light,now,plan.terrain.bytes);
                if(decor&&decorReady)publication->Request(1,request.epoch,plan.decorToken,plan.light,now,plan.decor.bytes);
                const int selected=publication->Select(now);
                if(selected>=0){const auto g=unsigned(selected);const auto desired=publication->Read(g).desired;
                    const bool reuse=previous[g]&&previous[g]->render->view.mapEpoch==request.epoch&&previous[g]->token==desired.token&&previous[g]->light==desired.light;
                    GroupPublication::Ticket ticket;
                    try{auto lease=reuse?std::shared_ptr<GroupPublication::RootPool::Lease>{}:publication->Roots()->Acquire();
                    if((reuse||lease)&&publication->Begin(g,now,ticket)){
                        std::shared_ptr<GroupResult> root;
                        if(reuse)root=previous[g];else{
                            // One CPU preparation owns the captured immutable plan.
                            // The coordinator keeps coalescing requests so native
                            // young-decor preemption is observable while it runs.
                            auto job=std::async(std::launch::async,[this,&producer,ticket,request,plan,scene=decor&&decorReady?decor.get():nullptr,base=previous[g],lease=std::move(lease)]()mutable{
                                return producer.Prepare(ticket,request,plan,scene,base,std::move(lease),[&]{return closing.load()||!publication->Valid(ticket);});});
                            while(job.wait_for(std::chrono::milliseconds(50))!=std::future_status::ready){
                                if(closing)continue;Request newest;{std::lock_guard<std::mutex> lock(mutex);newest=latest;}
                                if(newest.epoch!=request.epoch)continue;
                                const auto desiredPlan=producer.Select(newest,decor&&decorReady?decor.get():nullptr,*publication);const auto stamp=GroupPublication::Now();
                                publication->Request(0,newest.epoch,desiredPlan.terrain.request,desiredPlan.light,stamp,desiredPlan.terrain.bytes);
                                if(decor&&decorReady)publication->Request(1,newest.epoch,desiredPlan.decorToken,desiredPlan.light,stamp,desiredPlan.decor.bytes);
                            }root=job.get();
                        }
                        auto set=GroupPublication::Describe(GroupMetadata(*root->render),publication->Read(g).current,ticket.reason==GroupPublication::Lighting);
                        const auto refusal=publication->Submit(ticket,std::move(set),GroupPublication::Now(),request.view.camera.x,request.view.camera.y);
                        if(refusal==GroupPublication::None){if(!reuse)retired.Keep(root);previous[g]=root;offers[g]={root,ticket};
                            auto frame=std::make_shared<FrameSnapshot>();frame->offers=offers;frame->publication=publication;
                            {std::lock_guard<std::mutex> lock(mutex);if(!closing&&request.epoch==latest.epoch)ready=std::move(frame);}
                        }else publication->Cancel(g,GroupPublication::Now(),refusal!=GroupPublication::Stale);
                    }}catch(const std::exception& e){if(closing)break;const bool fault=!ticket.revision||publication->Valid(ticket);publication->Cancel(g,GroupPublication::Now(),fault);OutputDebugStringA(e.what());}
                }
                // Preserve the sole source scene through early/full continuation.
                if(previous[0]&&decor&&(!decorReady||std::hypot(request.view.camera.x-decorView.camera.x,request.view.camera.y-decorView.camera.y)>=16||
                    std::hypot(request.view.anticipated.x-decorView.anticipated.x,request.view.anticipated.y-decorView.anticipated.y)>=16)){
                    if(!decorReady||publication->Settled(1))if(decorWorker.Submit(decor,request.view,request.epoch))decorReady=false;
                }
            }
        }catch(const std::exception& e){OutputDebugStringA(e.what());}
        // The render owner releases borrows before Stop joins this thread.
        std::unique_lock<std::mutex> lock(mutex);wake.wait(lock,[&]{return closing.load();});
    }
public:
    ~GroupWorker(){Stop();}
    void Start(Input input){Stop();publication=std::make_shared<GroupPublication>();closing=false;latest={};sequence=0;thread=std::thread([this,input=std::move(input)]()mutable{Run(std::move(input));});}
    bool RequestFrame(const Request& value){std::unique_lock<std::mutex> lock(mutex,std::try_to_lock);if(!lock.owns_lock()||closing)return false;if(!latest.epoch)publication->Reset(value.epoch);latest=value;++sequence;lock.unlock();wake.notify_one();return true;}
    std::shared_ptr<FrameSnapshot> Borrow(uint32_t epoch){std::unique_lock<std::mutex> lock(mutex,std::try_to_lock);if(!lock.owns_lock()||!ready)return {};for(const auto& offer:ready->offers)if(offer.root&&offer.ticket.epoch!=epoch)return {};return ready;}
    void Stop(){closing=true;{std::lock_guard<std::mutex> lock(mutex);ready.reset();++sequence;}wake.notify_one();if(thread.joinable())thread.join();}
};

// Shared by DX11 incremental uploads and the no-device contract checker.
class UploadBudget {
    uint64_t left;uint32_t operations;
public:
    static constexpr uint64_t BytesPerFrame=4u*1024*1024;
    static constexpr uint32_t OperationsPerFrame=16;
    UploadBudget():left(BytesPerFrame),operations(OperationsPerFrame){}
    uint64_t Take(uint64_t bytes,uint64_t alignment=1){if(!operations||!alignment)return 0;auto n=(std::min)(bytes,left);n-=n%alignment;if(!n)return 0;left-=n;--operations;return n;}
    bool Allocate(){if(!operations)return false;--operations;return true;}
    uint64_t Remaining()const{return left;}
};
}
