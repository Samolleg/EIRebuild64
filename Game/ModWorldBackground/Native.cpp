#include "StdAfx.h"
#include "Native.h"
#include "Pipeline.h"
#include "../ModPackage.h"
#include "../ModRuntime.h"
#include <filesystem>
#include <fstream>
#include <shellapi.h>

namespace ModWorldBackground {
namespace {
struct State { GroupWorker worker; };
State& Runtime(){static State* state=new State;return *state;}
}
void Unload(){Runtime().worker.Stop();}
}

// Four bounded value/CPU-ownership traces on the same production contract.
// No producer/resource loader, worker thread, device, world or mod host starts.
namespace {
nlohmann::json InspectGroupPublication(){
    using namespace ModWorldBackground;using namespace WorldBackground;using G=GroupPublication;
    constexpr uint64_t MiB=1024ull*1024;auto check=[](bool good,const char* why){if(!good)throw std::runtime_error(why);};
    auto meta=[](unsigned id,uint64_t bytes,uint64_t revision=1){G::Meta m;m.key={{1,0x80000000u,1,id}};m.bytes=bytes;m.geometry=m.texture=revision;
        m.width=1536;m.height=1024;m.box.low={{double(id),0,0}};m.box.high={{double(id)+.1,.1,.1}};return m;};
    auto prepare=[&](G& p,unsigned g,uint64_t token,uint64_t light,uint64_t now,const std::vector<G::Meta>& items){
        p.Request(g,1,token,light,now);G::Ticket t;check(p.Begin(g,now,t),"Trace admission selection");
        check(p.Submit(t,G::Describe(items,p.Read(g).current,t.reason==G::Lighting),now)==G::None,"Trace submit");return t;};
    auto commit=[&](G& p,const G::Ticket& t){check(p.Upload(t)&&p.MarkReady(t)&&p.Commit(t),"Trace publication");p.Released(t.group);};
    auto seed=[&](G& p,unsigned g,const std::vector<G::Meta>& items){auto t=prepare(p,g,1,1,1,items);commit(p,t);
        if(p.Read(g).current.partial){t=prepare(p,g,1,1,2,items);commit(p,t);}};
    auto visible=[](const G& p){return nlohmann::json::array({p.Read(0).current.token==2?"T1":"T0",p.Read(1).current.token==2?"D1":"D0"});};
    nlohmann::json cases=nlohmann::json::array();
    {
        G p;p.Reset(1);seed(p,0,{meta(1,MiB)});seed(p,1,{meta(2,MiB)});
        auto t=prepare(p,0,2,1,100,{meta(1,MiB,2)});const auto generation=p.Read(0).generation;
        p.Request(1,1,2,1,100);G::Ticket d;check(p.Begin(1,100,d),"Decor validation job");
        // Abstract callback reports the same named-lane CPU validation fault.
        check(p.Failure(1,E_INVALIDARG,S_OK,100)==G::Local,"CPU lane failure scope");commit(p,t);const auto after=visible(p);
        d=prepare(p,1,2,1,3000000,{meta(2,MiB,2)});check(p.Upload(d),"Decor retry upload");
        const auto local=p.Failure(1,E_OUTOFMEMORY,S_OK,4000000);const auto state=p.Read(1);
        p.Request(0,1,3,1,4000001);G::Ticket next;const bool continues=p.Begin(0,4000001,next);p.Cancel(0,4000001,false);
        cases.push_back({{"id","asymmetric-local-failure"},{"observed",{{"visible_after_terrain",after},{"visible_after_decor_retry_fault",visible(p)},
            {"decor_pending",state.pending.items.empty()?nlohmann::json(nullptr):nlohmann::json("present")},{"decor_retry_at_ms",state.retryAt/1000},
            {"worker_continues",local==G::Local&&continues},{"terrain_generation_unchanged_by_decor",generation==next.generation}}}});
    }
    {
        G p;p.Reset(1);seed(p,1,{meta(1,4*MiB),meta(2,6*MiB),meta(3,6*MiB)});
        const std::vector<G::Meta> target={meta(2,6*MiB,2),meta(3,6*MiB,2)};
        auto t=prepare(p,1,2,1,10,target);const auto early=p.Read(1).pending;uint64_t uploaded=0;for(const auto& item:early.items)uploaded+=item.transfer;
        commit(p,t);auto names=[](const G::Set& set){nlohmann::json names=nlohmann::json::array();for(const auto& item:set.items){const char prefix=char('A'+item.meta.key[3]-1);names.push_back(std::string(1,prefix)+(item.meta.geometry==2?"1":"0"));}return names;};
        const bool settled=p.Settled(1);t=prepare(p,1,2,1,11,target);const auto full=p.Read(1).pending;commit(p,t);
        cases.push_back({{"id","early-stage-full-continuation"},{"observed",{{"early_entries",names(early)},{"early_resident_mib",early.bytes/MiB},
            {"early_upload_mib",uploaded/MiB},{"early_partial",early.partial},{"early_scene_settled",settled},{"full_entries",names(full)},
            {"full_resident_mib",full.bytes/MiB},{"full_partial",full.partial},{"full_scene_settled",p.Settled(1)},{"stage_applied_to_continuation",full.partial}}}});
    }
    {
        G p;p.Reset(1);auto outside=meta(2,8*MiB);outside.box.low={{2,0,0}};outside.box.high={{3,.1,.1}};
        auto inside=meta(1,8*MiB);inside.box.low={{0,0,0}};inside.box.high={{.1,.1,.1}};
        seed(p,0,{meta(4,240*MiB)});seed(p,1,{inside,outside});
        p.Request(0,1,2,1,10);G::Ticket t;check(p.Begin(0,10,t),"Pressure candidate prepare");const auto initialFinal=248*MiB+p.Read(1).current.bytes;const auto denied=p.Submit(t,G::Describe({meta(4,248*MiB,2)},p.Read(0).current),10);p.Cancel(0,10,false);
        std::array<float,16> clip={{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}};
        check(p.Reduce(8*MiB,clip,11),"Guarded reduction prepare");const auto reduction=p.Read(1);auto moved=clip;moved[3]=-2;
        const bool unsafe=p.Safe(reduction.pending,moved);check(!unsafe,"Reduction newly visible");p.Cancel(1,11,false);
        p.Retired(MiB,1);const bool blocked=p.Select(12)<0;p.Retired(0,0);
        check(p.Reduce(8*MiB,clip,13),"Safe reduction prepare");commit(p,p.Read(1).accepted);
        t=prepare(p,0,2,1,1000014,{meta(4,248*MiB,2)});const auto final=p.Read(0).pending.bytes+p.Read(1).current.bytes;commit(p,t);
        // Separate clock partition of this trace exercises actual rank/aging.
        G q;q.Reset(1);seed(q,0,{meta(1,MiB)});seed(q,1,{meta(2,MiB)});
        auto t1=prepare(q,0,2,1,10,{meta(1,MiB,2)});commit(q,t1);t1=prepare(q,0,3,1,20,{meta(1,MiB,3)});commit(q,t1);
        check(q.Admissions()==4,"Four admission source partition");q.Request(1,1,1,2,100);q.Request(0,1,4,1,200);
        const int first=q.Select(300);const int aged=q.Select(2000100);G::Ticket low;check(q.Begin(1,2000100,low),"Oldest eligible light");
        auto lit=meta(2,MiB,2);lit.texture=1;check(q.Submit(low,G::Describe({lit},q.Read(1).current,true),2000100)==G::None,"Light geometry submit");const auto count=q.Admissions();check(q.Upload(low),"Light upload begin");
        G::Ticket high;check(q.Begin(0,2000101,high),"Terrain concurrent GPU peer");check(q.Submit(high,G::Describe({meta(1,MiB,4)},q.Read(0).current),2000101)==G::None&&q.Upload(high),"Terrain submit upload");
        const auto accepted=q.Read(0).accepted;q.Request(0,1,5,3,2000102);const bool restarted=q.Read(0).accepted.revision!=accepted.revision||q.Read(0).cancel;
        const auto order=q.UploadOrder(4000100,4);UploadBudget budget;uint64_t bytes=0;unsigned operations=0;
        for(unsigned i=0;i<20;++i){const auto n=budget.Take(UploadBudget::BytesPerFrame/16);if(n){bytes+=n;++operations;}}
        cases.push_back({{"id","shared-pressure-age-priority"},{"observed",{{"initial_final_mib",initialFinal/MiB},{"initial_denial",denied==G::Active?"ACTIVE":"OTHER"},
            {"unsafe_reduction_commit",unsafe},{"retired_payload_blocks_admission",blocked},{"safe_final_mib",final/MiB},{"safe_final_fits",final<=Capacity::PayloadBytes},
            {"default_first_rank",first==0?"terrain-residency":"OTHER"},{"aged_admission_selected",aged==1?"oldest-eligible-light":"OTHER"},
            {"admissions_after_aged_grant",count},{"upload_first_when_lower_aged",order[0]==1?"lower-priority":"OTHER"},{"shared_upload_bytes",bytes},
            {"shared_upload_operations",operations},{"admitted_job_restarted",restarted}}}});
    }
    {
        G p;p.Reset(1);WorkerRetirement<GroupResult> retired;auto pool=p.Roots();
        auto make=[&](){auto lease=pool->Acquire();if(!lease)return std::shared_ptr<GroupResult>{};auto root=std::make_shared<GroupResult>();root->lease=std::move(lease);retired.Keep(root);return root;};
        auto terrain=make(),decor=make(),older=make();auto upload=older;older.reset();const auto initial=pool->Size();const auto fourth=make();
        upload.reset();retired.Collect();const auto after=pool->Size();auto next=make();const auto again=pool->Size();
        seed(p,0,{meta(1,MiB)});seed(p,1,{meta(2,MiB)});auto t=prepare(p,0,2,1,10,{meta(1,MiB,2)});check(p.Upload(t)&&p.MarkReady(t),"Stale ready trace");
        auto wrong=t;wrong.epoch=2;const bool staleEpoch=p.Commit(wrong);p.Cancel(0,10,false);const bool staleGeneration=p.Commit(t);
        const auto scope=p.Failure(0,E_OUTOFMEMORY,DXGI_ERROR_DEVICE_REMOVED,11);
        const auto beforeReset=pool->Size();if(scope==G::Global)p.Reset(1);
        const bool workerStopped=beforeReset!=pool->Size();
        nlohmann::json visibleAfter=nlohmann::json::array();if(!p.Read(0).current.items.empty())visibleAfter.push_back("terrain");if(!p.Read(1).current.items.empty())visibleAfter.push_back("decor");
        terrain.reset();decor.reset();next.reset();retired.Collect();
        cases.push_back({{"id","aggregate-borrows-cancel-global-device"},{"observed",{{"initial_root_count",initial},{"fourth_acquire",bool(fourth)},
            {"fourth_allocation",bool(fourth)},{"count_after_alias_release_and_worker_collect",after},{"count_after_next_acquire",again},
            {"stale_epoch_commit",staleEpoch},{"stale_generation_commit",staleGeneration},{"device_scope",scope==G::Global?"GLOBAL":"LOCAL"},
            {"gpu_visible_after_device_scope",visibleAfter},{"cpu_worker_stopped_by_renderer_catch",workerStopped},{"roots_after_full_ordered_unload_join",pool->Size()}}}});
    }
    return cases;
}
}

// Diagnostic mode returns before mod host/game startup, never instantiates a
// world, device, audio output, mapped live terrain, Lua main or user save.
bool InspectWorldBackgroundCommandLine(int& result){
    int count=0;LPWSTR* args=CommandLineToArgvW(GetCommandLineW(),&count);if(!args)return false;
    struct Free {LPWSTR* p;~Free(){LocalFree(p);}} free{args};
    if(count>=2&&!wcscmp(args[1],L"--inspect-world-background-publication-g2")){
        result=1;nlohmann::json report={{"success",false},{"actualProductionContract",true},{"actualCpuOwnership",true}};
        for(const char* field:{"resourceOpened","rendererCreated","luaExecuted","unitCreated","worldCreated","hostCreated","deviceCreated","audioStarted","saveRead","uiExecuted"})report[field]=false;
        try{if(count!=3)throw std::runtime_error("Expected one report path");report["cases"]=InspectGroupPublication();report["success"]=true;result=0;}
        catch(const std::exception& e){report["error"]=e.what();}
        if(count>=3){std::ofstream stream{std::filesystem::path(args[2])};stream<<report.dump(2);if(!stream)result=1;}return true;
    }
    if(count<2||wcscmp(args[1],L"--check-world-background-g2"))return false;
    result=1;nlohmann::json report={{"success",false},{"liveWorld",false},{"deviceCreated",false},{"cases",nlohmann::json::array()}};
    std::filesystem::path output=count>=5?args[4]:L"world-background-g2.json";
    try{
        if(count!=5)throw std::runtime_error("Expected mod directory, original resource directory and report path");
        using namespace ModWorldBackground;using namespace WorldBackground;
        size_t assertions=0;auto check=[&](bool good,const char* why){++assertions;if(!good)throw std::runtime_error(why);};
        auto add=[&](const char* name){report["cases"].push_back(name);};
        CModPackage package;package.Load(std::filesystem::path(args[2]).string());check(package.WorldBackgroundEnabled(),"Installed JMVWorldBackground must be enabled");
        const auto layoutPath=std::filesystem::path(args[2])/L"world-map-layouts.json";
        const auto raw=ReadFileBounded(layoutPath.wstring(),4u*1024*1024);const auto layout=ParseLayout(std::string(raw.begin(),raw.end()));check(!layout.empty(),"Installed layout empty");
        const auto selected=std::find_if(layout.begin(),layout.end(),[](const Placement& p){return p.identity.profile=="deja-vu"&&MapName(p.identity.mpr)=="zone1";});
        check(selected!=layout.end(),"Installed Dejavu zone1 layout absent");WorldBackground::Identity active=selected->identity;
        auto input=CaptureInput(package,args[3],std::filesystem::path(args[3]).parent_path().wstring(),active);
        check(std::filesystem::equivalent(std::filesystem::path(input.layout),layoutPath),"Layout owner mismatch");check(input.files.profile=="deja-vu","Profile mismatch");
        report["layoutEntries"]=layout.size();report["active"]={{"profile",active.profile},{"allod",active.allod},{"zone",active.zone},{"mpr",active.mpr}};add("actual config.reg/ai.reg and layout capture");
        check(ResolveActiveLayout(layout,active).zone==active.zone,"Exact active layout mismatch");
        auto menu=active;menu.zone.clear();check(ResolveActiveLayout(layout,menu).zone==active.zone,"Unique menu MPR resolution failed");add("exact zone and menu resolution");
        bool rejected=false;try{auto wrong=active;wrong.mpr="__absent__";ResolveActiveLayout(layout,wrong);}catch(const std::exception&){rejected=true;}check(rejected,"Unknown map accepted");add("unknown map fail closed");
        std::vector<std::string> textures;bool ignore=false;package.ResourcePlan("textures.res",textures,ignore);check(!textures.empty(),"Configured texture chain empty");
        check(input.files.textureArchives.size()==textures.size()+(ignore?0:1),"Texture base ignore mismatch");
        for(size_t i=0;i<textures.size();++i)check(input.files.textureArchives[i]==std::filesystem::path(textures[i]).wstring(),"Texture order mismatch");
        report["textureArchives"]=textures;add("configured first-hit texture order and base fallback");
        std::vector<std::string> figures;bool skipFigures=false;package.ResourcePlan("figures.res",figures,skipFigures);
        for(size_t i=0;i<figures.size();++i)check(input.files.figureArchives[i]==std::filesystem::path(figures[i]).wstring(),"Figure replacement order mismatch");
        std::vector<std::string> menus;bool skipMenus=false;package.ResourcePlan("menus.res",menus,skipMenus);
        std::vector<std::wstring> expectedFigures;for(const auto& p:figures)expectedFigures.push_back(std::filesystem::path(p).wstring());for(const auto& p:menus)expectedFigures.push_back(std::filesystem::path(p).wstring());
        if(!skipFigures)expectedFigures.push_back(std::wstring(args[3])+L"/figures.res");if(!skipMenus)expectedFigures.push_back(std::wstring(args[3])+L"/menus.res");
        check(input.files.figureArchives==expectedFigures,"Combined figure/menu resource order mismatch");
        report["figureArchives"]=figures;report["figureMenuOverrides"]=menus;add("combined figure/menu overrides before unsuppressed originals");
        rejected=false;try{input.files.LayoutMap("__wrong_profile__",layout.front().mprPath);}catch(const std::exception&){rejected=true;}check(rejected,"Cross-profile path accepted");add("cross-profile resource rejected");
        auto neighbors=Select(layout,LayoutEntries(layout),active);check(!neighbors.empty(),"Installed component has no neighbors");report["neighborCount"]=neighbors.size();
        check(std::none_of(neighbors.begin(),neighbors.end(),[&](const Neighbor& n){return n.entry.identity.zone==active.zone;}),"Active terrain selected as background");add("same component neighbors and active exclusion");
        Producer producer(input);producer.Load([]{return false;});check(!producer.Data().neighbors.empty(),"No real neighbor MPR prepared");check(producer.Data().bytes<=64u*1024*1024,"Data budget exceeded");
        report["dataBytes"]=producer.Data().bytes;report["preparedNeighbors"]=producer.Data().neighbors.size();report["resourceDiagnostics"]=producer.Data().diagnostics;add("actual detached neighbor MPR and textures");
        const auto& first=producer.Data().neighbors.front();check(!first.textures.empty(),"Neighbor texture payload absent");
        const auto member=first.textures.front().name;Blob provider=input.files.Texture(active.profile,member),firstHit;std::wstring owner;
        for(const auto& path:input.files.textureArchives)if(std::filesystem::is_regular_file(path)&&ReadArchiveMember(path,member,firstHit,64u*1024*1024)){owner=path;break;}
        check(!owner.empty()&&provider==firstHit&&provider==first.textures.front().mmp,"First-hit archive ownership mismatch");report["sampleTexture"]={{"member",member},{"owner",std::filesystem::path(owner).string()},{"bytes",provider.size()}};add("real overlapping RES first-hit resolution");
        Request request;request.epoch=17;request.environment.epoch=17;
        request.view.camera={first.placement.offset.x+first.placement.entry.width*.5,first.placement.offset.y+first.placement.entry.height*.5};
        // Explicit detached fixture environment, not evidence of a live sun.
        request.environment.valid=VisualLight|VisualSunPosition|VisualTerrainSun;
        request.environment.light={{.8f,.7f,.6f,-.5f,-.5f,-.707f,.2f,.2f,.2f}};request.environment.sunPosition={{0,0,1}};request.environment.terrainSun={{1,1,1}};request.environment.terrainSteps={{0,0,0}};request.seconds=1;
        auto prepared=producer.Prepare(request,nullptr,{},[]{return false;});check(prepared&&prepared->render->view.count>0,"Real terrain visual empty");
        check(prepared->render->bytes<=producer.Budget()&&prepared->render->meshes.size()<=256,"Snapshot budget violated");report["terrainMeshes"]=prepared->render->meshes.size();report["terrainBytes"]=prepared->render->bytes;report["visualDiagnostics"]=prepared->render->diagnostics;add("real sector streaming visual and CPU lighting fixture");
        check(input.payloadBudget==Capacity::PayloadBytes&&prepared->capacity==Capacity::PayloadBytes,"Native capture did not use renderer capacity");
        check(Capacity::PayloadBytes==MaxPayloadBudget&&Capacity::Meshes==256,"Native capability differs from source provider");add("native renderer capability propagated through capture and producer");
        for(const auto& mesh:prepared->render->meshes){check(std::isfinite(mesh.mesh.worldX)&&std::isfinite(mesh.mesh.worldY),"Nonfinite placement");check(mesh.mesh.vertexCount%3==0,"Invalid terrain triangle topology");}
        add("saved XY origins without Z translation");
        check(!producer.Prepare(request,nullptr,prepared,[]{return false;}),"Unchanged input rebuilt snapshot");add("unchanged visual retained");
        request.environment.light[6]=.4f;auto relit=producer.Prepare(request,nullptr,prepared,[]{return false;});check(relit&&relit->render->view.contentRevision!=prepared->render->view.contentRevision,"Ambient change did not invalidate");add("authoritative lighting revision invalidation");
        rejected=false;try{LightingPacket::Build(first,17,1,1,[]{return false;});}catch(const std::exception&){rejected=true;}check(rejected,"Lighting budget not enforced");add("lighting input budget rejection");
        rejected=false;try{auto canceled=request;canceled.epoch=18;canceled.environment.epoch=18;producer.Prepare(canceled,nullptr,relit,[]{return true;});}catch(const std::exception&){rejected=true;}check(rejected,"Cancelled visual published");add("cancelled preparation rejects completion");
        Worker worker;worker.Start(input);check(worker.RequestFrame(request),"Worker request rejected");auto complete=worker.Wait(17,true,std::chrono::seconds(45));check(bool(complete),"Terrain/decor worker timeout");
        report["workerMeshes"]=complete->render->meshes.size();report["decorMeshes"]=std::count_if(complete->render->meshes.begin(),complete->render->meshes.end(),[](const NeighborVisual& m){return (m.key[1]&0x80000000u)!=0;});
        check(complete->decorPrepared,"Decoration worker not joined into snapshot");check(!worker.Borrow(99),"Stale map epoch borrow accepted");add("actual asynchronous terrain and MOB FIG BON MMP decor pipeline");
        {
            auto pressure=request;pressure.environment.light[6]+=.01f;
            check(worker.RequestFrame(pressure),"Second retained frame request rejected");
            auto second=worker.Wait(17,true,std::chrono::seconds(15),complete->render->view.contentRevision);check(bool(second),"Second retained frame timeout");
            pressure.environment.light[6]+=.01f;check(worker.RequestFrame(pressure),"Third retained frame request rejected");
            auto third=worker.Wait(17,true,std::chrono::seconds(15),second->render->view.contentRevision);check(bool(third),"Third retained frame timeout");
            pressure.environment.light[6]+=.01f;check(worker.RequestFrame(pressure),"Backpressure request rejected");
            check(!worker.Wait(17,true,std::chrono::milliseconds(150),third->render->view.contentRevision),"Borrowed snapshots did not impose backpressure");
            complete.reset();second.reset();check(worker.RequestFrame(pressure),"Post-release request rejected");
            complete=worker.Wait(17,true,std::chrono::seconds(15),third->render->view.contentRevision);check(bool(complete),"Worker did not resume after renderer borrows released");
            add("actual CPU worker stalls at three borrowed snapshots and resumes after release");
        }
        std::weak_ptr<const BackgroundData> lifetime=complete->data;worker.Stop();check(!lifetime.expired(),"Borrow lost data after worker join");complete.reset();check(lifetime.expired(),"Map data retained after all owners released");add("stop join and borrowed source lifetime");
        worker.Start(input);worker.RequestFrame(request);worker.Stop();check(!worker.Borrow(17),"Cancelled map retained ready snapshot");add("immediate cancellation during real load and restart");
        WorkerRetirement<int> retired;auto item=std::make_shared<int>(7);retired.Keep(item);check(retired.Collect()==0,"Borrowed object prematurely retired");item.reset();check(retired.Collect()==1&&retired.Size()==0,"Unborrowed object retained");add("worker retirement waits for readers");
        uint64_t remaining=64u*1024*1024;unsigned frames=0;while(remaining){UploadBudget budget;auto n=budget.Take(remaining,4096*4);check(n<=UploadBudget::BytesPerFrame&&n%16384==0,"Upload frame byte bound violated");check(!budget.Take(1),"Exhausted frame budget accepted");remaining-=n;++frames;}check(frames==16,"Large texture row budget wrong");add("DX11 shared incremental upload byte budget");
        UploadBudget operations;for(unsigned i=0;i<16;++i)check(operations.Allocate(),"Upload allocation budget too short");check(!operations.Allocate(),"Upload allocation budget unbounded");add("DX11 shared upload operation budget");
        // Compare actual admission against the old fallback, with independent
        // fresh source policies, not mesh counts standing in for candidate coverage.
        {
            SectorStreaming legacy,reference;
            const auto legacyPlan=legacy.Update(producer.Data(),17,1,true,request.view,LegacyPayloadBudget);
            const auto large=reference.Update(producer.Data(),17,1,true,request.view,Capacity::PayloadBytes);
            check(large.sectors.size()>legacyPlan.sectors.size()&&legacyPlan.budgetSkipped>large.budgetSkipped,"Real installed fixture must reproduce legacy terrain truncation");
            check(prepared->terrainSelection.sectors==large.sectors,"Native terrain selection differs from reference capability");
            std::vector<std::array<size_t,2>> lost;
            for(const auto& key:large.sectors)if(std::find(legacyPlan.sectors.begin(),legacyPlan.sectors.end(),key)==legacyPlan.sectors.end())lost.push_back({key.neighbor,key.sector});
            report["capacityComparison"]={{"legacy",{{"bytes",legacyPlan.bytes},{"sectors",legacyPlan.sectors.size()},{"budgetSkipped",legacyPlan.budgetSkipped},{"visibleSkipped",legacyPlan.visibleSkipped}}},{"native",{{"bytes",large.bytes},{"sectors",large.sectors.size()},{"budgetSkipped",large.budgetSkipped},{"visibleSkipped",large.visibleSkipped}}},{"legacyLostSectors",lost}};
            add("actual legacy terrain omissions versus source renderer capacity");
            Decor::Scene scene;scene.Prepare(producer.Data(),producer.Files(),[]{return false;},&request.view);
            const auto oldDecor=Decor::SelectObjects(scene,producer.Data(),request.view,LegacyPayloadBudget-legacyPlan.bytes,256-legacyPlan.meshes,legacyPlan.meshes<128?128-legacyPlan.meshes:0);
            const auto newDecor=Decor::SelectObjects(scene,producer.Data(),request.view,Capacity::PayloadBytes-large.bytes,256-large.meshes,large.meshes<128?128-large.meshes:0);
            auto withDecor=producer.Prepare(request,&scene,relit,[]{return false;});
            check(bool(withDecor)&&withDecor->decorationSelection.objects==newDecor.objects&&withDecor->decorationSelection.bytes==newDecor.bytes,"Native decoration selection differs from source capacity");
            check(withDecor->render->bytes==large.bytes+newDecor.bytes,"Exact terrain/decor residency accounting mismatch");
            std::vector<std::array<size_t,2>> lostDecor;
            for(size_t i:newDecor.objects)if(std::find(oldDecor.objects.begin(),oldDecor.objects.end(),i)==oldDecor.objects.end())lostDecor.push_back({scene.Objects()[i].neighbor,scene.Objects()[i].id});
            report["capacityComparison"]["decor"]={{"legacyObjects",oldDecor.objects.size()},{"nativeObjects",newDecor.objects.size()},{"legacySkipped",oldDecor.skipped},{"nativeSkipped",newDecor.skipped},{"legacyVisibleSkipped",oldDecor.visibleSkipped},{"nativeVisibleSkipped",newDecor.visibleSkipped},{"legacyLostObjects",lostDecor},{"nativeBytes",newDecor.bytes},{"nativeCell",newDecor.cellSize},{"sceneObjects",scene.Objects().size()}};
            add("actual decoration candidate identities and exact admitted payload");
            auto moved=request;moved.seconds=1.1;moved.view.camera.x+=1;
            auto waiting=producer.Prepare(moved,nullptr,withDecor,[]{return false;});
            check(bool(waiting)&&waiting->futureBudget==producer.Budget()-(std::min)(producer.Budget(),Producer::DecorBytes(withDecor)),"Look-ahead consumed established decoration reserve");
            check(waiting->decorSource==withDecor&&Producer::DecorBytes(waiting)<=Producer::DecorBytes(withDecor),"Pending decoration source ownership mismatch");add("look-ahead reserve and pending decoration ownership");
        }
        check(Capacity::Accepts(MaxPayloadBudget,256)&&!Capacity::Accepts(MaxPayloadBudget+1,256)&&!Capacity::Accepts(MaxPayloadBudget,257),"Native upload capacity boundary mismatch");
        check(Capacity::CanPrepare(0)&&Capacity::CanPrepare(2)&&!Capacity::CanPrepare(3)&&!Capacity::CanPrepare(4),"Retained CPU snapshot backpressure boundary mismatch");
        check(Capacity::RetainedPayloadBytes==3*MaxPayloadBudget,"Retained logical payload bound mismatch");
        report["capacity"]={{"payload",Capacity::PayloadBytes},{"retainedSnapshots",Capacity::Snapshots},{"retainedLogicalPayload",Capacity::RetainedPayloadBytes},{"visiblePendingGpuLogicalPayload",2*Capacity::PayloadBytes}};add("shared upload capability and retained snapshot backpressure boundaries");
        report["assertions"]=assertions;report["success"]=true;result=0;
    }catch(const std::exception& e){report["error"]=e.what();}
    std::ofstream stream(output,std::ios::binary);stream<<report.dump(2);return true;
}
