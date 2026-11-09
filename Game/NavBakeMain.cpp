#include "../stdafx.h"
#include "WorldServer.h"
#include "ModRuntime.h"
#include "ModPackage.h"
#include "ModCliTool.h"
#include "GeneralSettings.h"
#include "PerkDatabase.h"
#include "QuestDatabase.h"
#include "UnitDatabase.h"
#include "SpellDatabase.h"
#include "ItemDatabase.h"
#include "LeverDatabase.h"
#include "AckDatabase.h"
#include "TerrainPrintsDatabase.h"
#include "GlobalMap.h"
#include "TextManager.h"
#include "2DScreenWait.h"
#include "NavigationBake.h"
#include <filesystem>
#include <map>
#include <set>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <functional>
#include <psapi.h>
#pragma comment(lib,"psapi.lib")

#include "nlohmann/json.hpp"
#include <bcrypt.h>
#pragma comment(lib,"bcrypt.lib")
namespace {
// Stream hashes keep large resource archives out of the map working set.
Navigation::Bytes FileDigest(const std::filesystem::path& path) {
    BCRYPT_HASH_HANDLE hash=nullptr;
    if(BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE,&hash,nullptr,0,nullptr,0,0)<0)throw std::runtime_error("Cannot hash navigation input");
    struct Close {BCRYPT_HASH_HANDLE hash;~Close(){BCryptDestroyHash(hash);}} close{hash};
    std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("Cannot read navigation input");
    std::array<unsigned char,65536> buffer;
    while(file) {
        file.read(reinterpret_cast<char*>(buffer.data()),buffer.size());
        if(BCryptHashData(hash,buffer.data(),ULONG(file.gcount()),0)<0)throw std::runtime_error("Cannot hash navigation input");
    }
    if(!file.eof())throw std::runtime_error("Cannot read navigation input");
    Navigation::Bytes result(32);if(BCryptFinishHash(hash,result.data(),ULONG(result.size()),0)<0)throw std::runtime_error("Cannot finish navigation hash");
    return result;
}
nlohmann::json ReadImportCache(const std::filesystem::path& path) {
    std::ifstream file(path);nlohmann::json result;file>>result;return result;
}
// Include configured archives even when outside res/, their names as well as
// contents, the complete base archive/config inventory (including additions),
// selected scene/authoring and executable. No timestamps decide cache reuse.
Navigation::Bytes ImportKey(const std::filesystem::path& base,const CModPackage& package,
    const std::string& map,const std::string& mob,const std::filesystem::path& settings) {
    std::set<std::filesystem::path> paths;
    for(const auto& path:package.NavigationInputs())if(!path.empty())paths.insert(std::filesystem::absolute(path));
    for(const auto& folder:{base/"Res",base/"config"})if(std::filesystem::exists(folder))
        for(const auto& entry:std::filesystem::recursive_directory_iterator(folder))if(entry.is_regular_file())paths.insert(entry.path());
    paths.insert(settings);
    for(const auto& name:{map,mob})paths.insert(std::filesystem::absolute(ModRuntime::MapPath(name.c_str())));
    auto authoring=std::filesystem::path(ModRuntime::MapPath(map.c_str()));authoring.replace_extension(".navsrc");paths.insert(std::filesystem::absolute(authoring));
    if(!NavigationBake::replayInput.empty())paths.insert(NavigationBake::replayInput);
    wchar_t executable[32768];const auto size=GetModuleFileNameW(nullptr,executable,32768);
    if(!size||size==32768)throw std::runtime_error("Cannot identify navigation executable");paths.insert(std::filesystem::path(executable));
    const std::vector<std::filesystem::path> ordered(paths.begin(),paths.end());
    std::vector<Navigation::Bytes> hashes(ordered.size());std::array<std::future<void>,4> jobs;
    for(size_t worker=0;worker<jobs.size();++worker)jobs[worker]=std::async(std::launch::async,[&,worker] {
        for(size_t i=worker;i<ordered.size();i+=jobs.size())if(std::filesystem::is_regular_file(ordered[i]))hashes[i]=FileDigest(ordered[i]);
    });
    for(auto& job:jobs)job.get();
    nlohmann::json inputs=nlohmann::json::array();
    for(size_t i=0;i<ordered.size();++i)inputs.push_back({ordered[i].u8string(),hashes[i]});
    const auto text=nlohmann::json{{"files",inputs},{"tile_cells",NavigationBake::tileCells},{"algorithm",Navigation::AlgorithmVersion}}.dump();
    return Navigation::Digest(Navigation::Bytes(text.begin(),text.end()));
}
using Clock=std::chrono::steady_clock;
long long Milliseconds(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-start).count();
}
std::string Extension(std::string name,const char* extension) {
    return std::filesystem::path(name).replace_extension(extension).string();
}
// Cooperative map continuations retain importer/release work on this thread.
// Only the existing tile pool runs concurrently. A ready map can finish while
// another map is still building, without nesting one map's lifetime in another.
class MapScheduler {
    struct Job {
        MapScheduler* owner;
        void* fiber=nullptr;
        CWorldServer* world=nullptr;
        std::function<void()> work;
        bool done=false;
    };
    void* coordinator;
    Job* current=nullptr;
    std::vector<std::unique_ptr<Job>> jobs;
    std::exception_ptr error;
    static void WINAPI Entry(void* value) {
        auto& job=*static_cast<Job*>(value);
        try {job.work();}catch(...) {job.owner->error=std::current_exception();}
        job.done=true;
        SwitchToFiber(job.owner->coordinator);
    }
public:
    MapScheduler():coordinator(ConvertThreadToFiber(nullptr)) {
        if(!coordinator)throw std::runtime_error("Cannot create map coordinator");
    }
    ~MapScheduler() {
        // Pending futures own references into their fiber's stack. Drain them
        // even when admission fails, before releasing any stack or callback.
        while(!jobs.empty()) {
            try {Poll();}catch(...) {}
            if(!jobs.empty())Sleep(1);
        }
        ConvertFiberToThread();
    }
    void Add(std::function<void()> work) {
        auto job=std::make_unique<Job>();job->owner=this;job->work=std::move(work);
        job->fiber=CreateFiber(0,Entry,job.get());
        if(!job->fiber)throw std::runtime_error("Cannot create map continuation");
        jobs.push_back(std::move(job));
    }
    void SuspendMap() {
        current->world=&ActiveWorldServer();
        SwitchToFiber(coordinator);
    }
    bool Empty() const {return jobs.empty();}
    void Poll() {
        for(auto& job:jobs) {
            current=job.get();
            SavedWorldScope scope(job->world);
            SwitchToFiber(job->fiber);
        }
        jobs.erase(std::remove_if(jobs.begin(),jobs.end(),[](auto& job) {
            if(!job->done)return false;
            DeleteFiber(job->fiber);return true;
        }),jobs.end());
        if(error&&jobs.empty())std::rethrow_exception(error);
    }
};
}
int wmain(int argc,wchar_t** argv) {
    // This executable has no renderer/audio/window/session initialization.
    // Flush stdout after every map/profile so the mapper can show progress.
    std::map<std::wstring,std::filesystem::path> options;
    bool all=false;
    std::vector<std::filesystem::path> selectedMaps;
    for(int i=1;i<argc;++i) {
        const std::wstring name=argv[i];
        if(name==L"--all"){if(all)return 2;all=true;continue;}
        if(name!=L"--game-root"&&name!=L"--mod"&&name!=L"--map"&&name!=L"--model"&&name!=L"--mob"&&name!=L"--output-root"&&name!=L"--report-root"&&name!=L"--tile-cells"&&name!=L"--replay-input")return 2;
        if(i+1>=argc)return 2;
        if(name==L"--map"){selectedMaps.emplace_back(argv[++i]);options[name]=selectedMaps.back();continue;}
        if(options.count(name))return 2;
        options[name]=argv[++i];
    }
    if(!options.count(L"--game-root")||!options.count(L"--mod")||!options.count(L"--output-root")||!options.count(L"--report-root")||
        (unsigned(all)+unsigned(options.count(L"--map"))+unsigned(options.count(L"--model"))!=1)||(!options.count(L"--map")&&options.count(L"--mob"))) {
        fprintf(stderr,"Usage: EIRebuild64NavBake --game-root DIR --mod DIR (--all | --model NAME | --map NAME [--mob NAME]) --output-root DIR --report-root PRIVATE_DIR [--tile-cells 32|64|96|128] [--replay-input PRIVATE_JSON]\n");
        return 2;
    }
    NavigationBake::reportDirectory=std::filesystem::absolute(options.at(L"--report-root")).lexically_normal();
    if(options.count(L"--replay-input"))NavigationBake::replayInput=std::filesystem::absolute(options.at(L"--replay-input"));
    if(options.count(L"--tile-cells")) {
        const auto value=options.at(L"--tile-cells").wstring();
        if(value!=L"32"&&value!=L"64"&&value!=L"96"&&value!=L"128")return 2;
        NavigationBake::tileCells=unsigned(std::stoul(value));
    }
    const auto outputPath=std::filesystem::absolute(options.at(L"--output-root")).lexically_normal()/"Maps"/"Nav64";
    auto relative=NavigationBake::reportDirectory.lexically_relative(outputPath);
    if(relative.empty()||*relative.begin()!=L"..")return 2;
    ModCliTool::Scope tool(ModCliTool::Kind::NavBake);
    int exitCode=3;
    try {
        const auto root=std::filesystem::absolute(options.at(L"--game-root")).lexically_normal();
        auto mod=options.at(L"--mod");if(mod.is_relative())mod=root/mod;
        const auto output=std::filesystem::absolute(options.at(L"--output-root")).lexically_normal()/"Maps"/"Nav64";
        const auto base=std::filesystem::is_directory(root/"Base"/"Res")?root/"Base":root;
        const auto cacheDirectory=NavigationBake::CacheDirectory();
        std::filesystem::create_directories(cacheDirectory);
        CModPackage package;package.Load(mod.string());
        ModRuntime::ResourceScope resources(package);
        CWorld::InitCurrentDir((base.string()+"\\").c_str());
        const auto settings=std::filesystem::is_regular_file(root/"Config64"/"settings.ini")?root/"Config64"/"settings.ini":base/"config"/"settings.ini";
        theSettings.ReadSettingsFromFile(settings.string().c_str());
        theSettings.szWorkPath=(base.string()+"\\").c_str();
        theSettings.szResourcePath=((base/"Res").string()+"\\").c_str();
        theSettings.szCDPath=theSettings.szWorkPath;
        C2DScreenWait::Ignore(true);
        const auto* res=static_cast<LPCSTR>(theSettings.szResourcePath);
        if(!thePerkDatabase.LoadDatabase(res,"database.res")||!theQuestDatabase.Load(res)||
            !theUnitDatabase.Load(res,"database.res")||!theSpellDatabase.Load(res,"database.res")||
            !theItemDatabase.Load(res,"database.res")||!theLeverDatabase.Load(res)||
            !theAckDatabase.Load(res)||!theTPDatabase.Load(res))
            throw std::runtime_error("Cannot load effective game databases");
        std::set<std::pair<std::string,std::string>> maps;
        if(all||options.count(L"--model")) {
            CTextManager texts;texts.SelectResource((std::string(res)+"texts.res").c_str());
            CGlobalMap registry;
            if(!registry.LoadZones("map.txt",texts))throw std::runtime_error("Cannot load mod map.txt registrations");
            for(int i=0;i<registry.GetZoneNum();++i) {
                const auto* zone=registry.GetZone(i);
                if(zone->szTerrainName.IsEmpty()||zone->szMobName.IsEmpty())continue;
                maps.emplace(Extension((LPCSTR)zone->szTerrainName,".mpr"),Extension((LPCSTR)zone->szMobName,".mob"));
            }
            maps.emplace(GameScenes::MenuTerrain,GameScenes::MenuObjects);
        } else {
            if(selectedMaps.size()>1&&options.count(L"--mob"))return 2;
            for(const auto& selected:selectedMaps) {
                const auto map=Extension(selected.string(),".mpr");
                maps.emplace(map,options.count(L"--mob")?Extension(options.at(L"--mob").string(),".mob"):Extension(map,".mob"));
            }
        }
        const auto total=Clock::now();unsigned completed=0,errors=0,missing=0,skipped=0;std::uint64_t totalBytes=0;
        exitCode=4;
        Navigation::Bytes bodyTable;
        if(!all) {
            std::ifstream file(output/Navigation::BodyFilename,std::ios::binary|std::ios::ate);
            const auto size=file.tellg();
            if(size>0&&size<=16*1024*1024) {
                bodyTable.resize(static_cast<size_t>(size));file.seekg(0);
                if(!file.read(reinterpret_cast<char*>(bodyTable.data()),bodyTable.size()))bodyTable.clear();
                if(!bodyTable.empty()) {
                    try {Navigation::LoadBodies(bodyTable);}
                    catch(const std::runtime_error&) {bodyTable.clear();}
                }
            }
        }
        if(bodyTable.empty()) {
            const auto started=Clock::now();CWorldServer world(true);SavedWorldScope scope(&world);
            struct Destroy {CWorldServer& world;~Destroy(){world.Destroy();}} cleanup{world};
            if(!world.Create())throw std::runtime_error("Cannot create body table importer");
            NavigationBake baker;bodyTable=baker.Bodies(world,{});
            printf("BODIES models_and_shapes=%zu bytes=%zu ms=%lld file=%s\n",Navigation::LoadBodies(bodyTable).samples.size(),bodyTable.size(),Milliseconds(started),Navigation::BodyFilename);fflush(stdout);
            std::filesystem::create_directories(output);
            auto temporary=output/Navigation::BodyFilename;temporary+=L".tmp";
            {
                std::ofstream file(temporary,std::ios::binary|std::ios::trunc);
                if(!file.write(reinterpret_cast<const char*>(bodyTable.data()),bodyTable.size())||!file.flush())throw std::runtime_error("Cannot write body table");
            }
            if(!MoveFileExW(temporary.c_str(),(output/Navigation::BodyFilename).c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot publish body table");
        }
        totalBytes+=bodyTable.size();
        const auto initialBodyBytes=bodyTable.size();
        printf("BAKE_BEGIN maps=%zu output=%s algorithm=%u\n",maps.size(),output.string().c_str(),Navigation::AlgorithmVersion);fflush(stdout);
        MEMORYSTATUSEX memory{};memory.dwLength=sizeof(memory);
        if(!GlobalMemoryStatusEx(&memory))throw std::runtime_error("Cannot query physical memory");
        // The measured old zone7 peak is below 2 GiB. Reserve twice that per
        // resident map plus 32 MiB per tile worker, shared across all maps.
        const std::uint64_t mapReserve=4ull*1024*1024*1024;
        const std::uint64_t memoryBudget=memory.ullTotalPhys/2;
        SYSTEM_INFO cpu{};GetSystemInfo(&cpu);
        const auto tileReserve=std::uint64_t(cpu.dwNumberOfProcessors)*32*1024*1024;
        const unsigned mapLimit=unsigned((std::max)(std::uint64_t(1),(memoryBudget>tileReserve?memoryBudget-tileReserve:0)/mapReserve));
        unsigned active=0;auto nextMap=maps.begin();
        struct ResetOverlap {~ResetOverlap(){NavigationBake::overlap={};}} resetOverlap;
        MapScheduler scheduler;
        NavigationBake::overlap=[&] {scheduler.SuspendMap();};
        printf("BAKE_MEMORY budget=%llu map_reserve=%llu maps_in_flight=%u\n",memoryBudget,mapReserve,mapLimit);
        auto bakeNext=[&](const std::pair<std::string,std::string>& map) {
            ++active;struct ReleaseSlot {unsigned& active;~ReleaseSlot(){--active;}} releaseSlot{active};
            const auto start=Clock::now();
            printf("MAP_BEGIN mpr=%s mob=%s\n",map.first.c_str(),map.second.c_str());fflush(stdout);
            try {
                long long importMs=0;std::uint64_t bytes=0;
                const auto stem=std::filesystem::path(map.first).stem().string()+"--"+std::filesystem::path(map.second).stem().string();
                const auto importCachePath=cacheDirectory/(stem+".navimport");
                Navigation::Bytes importKey;
                // Model-filtered admission must still inspect the actual MOB.
                if(!options.count(L"--model"))try {
                    importKey=ImportKey(base,package,map.first,map.second,settings);
                    auto cache=ReadImportCache(importCachePath);
                    const auto checksum=cache.at("checksum").get<Navigation::Bytes>();cache.erase("checksum");const auto text=cache.dump();
                    if(checksum!=Navigation::Digest(Navigation::Bytes(text.begin(),text.end())))throw std::runtime_error("Damaged navigation cache");
                    if(cache.at("version")!=1||cache.at("input").get<Navigation::Bytes>()!=importKey)throw std::runtime_error("Changed navigation input");
                    std::set<std::string> expected;
                    for(int type=0;type<Navigation::AgentCount;++type)expected.insert(Navigation::MeshFilename(map.first,map.second,type));
                    for(const auto* extension:{".navgeom",".navworld",".navcache"})expected.insert(stem+extension);
                    expected.insert(Navigation::BodyFilename);
                    for(const auto& entry:cache.at("outputs")) {
                        const auto name=entry.at("name").get<std::string>();
                        if(!expected.erase(name)||FileDigest((name==stem+".navcache"?cacheDirectory:output)/name)!=entry.at("hash").get<Navigation::Bytes>())throw std::runtime_error("Changed navigation output");
                    }
                    // Require the entire known set, including the tile cache;
                    // an absent/corrupt companion always causes a full bake.
                    if(!expected.empty())throw std::runtime_error("Incomplete navigation cache");
                    const auto tileCache=ReadImportCache(cacheDirectory/(stem+".navcache"));
                    for(int type=0;type<Navigation::AgentCount;++type) {
                        const auto& profile=tileCache.at("profiles").at(type);const auto& agent=Navigation::Agents[type];
                        printf("PROFILE map=%s mob=%s type=%s radius=%.2f tiles=%u rebuilt=0 ms=0 raster_cpu_ms=0 polygons_cpu_ms=0 bytes=%llu polygons=%u recast_raster_ms=0 filters_compact_ms=0 erosion_ms=0 regions_contours_polys_ms=0 detail_ms=0 serialize_compress_ms=0 write_ms=0 height_outliers=0\n",
                            map.first.c_str(),map.second.c_str(),agent.name,agent.profile.radius,profile.at("tiles").get<unsigned>(),
                            static_cast<unsigned long long>(std::filesystem::file_size(output/Navigation::MeshFilename(map.first,map.second,type))),profile.at("polygons").get<unsigned>());
                    }
                    bytes=cache.at("bytes").get<std::uint64_t>();totalBytes+=bytes;++completed;
                    printf("MAP_DONE mpr=%s mob=%s import_ms=0 total_ms=%lld profiles=%u bytes=%llu\n",map.first.c_str(),map.second.c_str(),Milliseconds(start),unsigned(Navigation::AgentCount),static_cast<unsigned long long>(bytes));fflush(stdout);return;
                }catch(const std::exception&) {}
                {
                CWorldServer world(true);SavedWorldScope worldScope(&world);
                struct DestroyWorld {CWorldServer& world;~DestroyWorld(){world.Destroy();}} cleanup{world};
                const bool haveMpr=std::filesystem::is_regular_file(ModRuntime::MapPath(world.GetFullMobName(map.first.c_str())));
                const bool haveMob=std::filesystem::is_regular_file(ModRuntime::MapPath(world.GetFullMobName(map.second.c_str())));
                if(!haveMpr||!haveMob) {
                    ++missing;
                    fprintf(stderr,"MAP_MISSING mpr=%s mob=%s code=5 mpr_present=%d mob_present=%d reason=no_sources\n",
                        map.first.c_str(),map.second.c_str(),int(haveMpr),int(haveMob));
                    fflush(stderr);return;
                }
                if(!world.Create()||!world.LoadNavigationScene(map.first.c_str(),map.second.c_str()))
                    throw std::runtime_error("Shared MPR/MOB import failed");
                if(options.count(L"--model")) {
                    const auto selected=Navigation::ResourceName(options.at(L"--model").stem().string().c_str());bool present=false;
                    for(const auto& object:world.mapObjects)if(Navigation::ResourceName(object.second->GetMaskName())==selected){present=true;break;}
                    if(!present){++skipped;printf("MAP_SKIPPED mpr=%s mob=%s reason=model_absent\n",map.first.c_str(),map.second.c_str());fflush(stdout);return;}
                }
                importMs=Milliseconds(start);
                bytes=world.aiMap.Navigation().Bake(output,bodyTable,world);totalBytes+=bytes;
                }
                if(!importKey.empty()) {
                    nlohmann::json outputs=nlohmann::json::array();
                    auto add=[&](const std::string& name){outputs.push_back({{"name",name},{"hash",FileDigest((name==stem+".navcache"?cacheDirectory:output)/name)}});};
                    for(int type=0;type<Navigation::AgentCount;++type)add(Navigation::MeshFilename(map.first,map.second,type));
                    for(const auto* extension:{".navgeom",".navworld",".navcache"})add(stem+extension);
                    add(Navigation::BodyFilename);
                    auto manifest=nlohmann::json{{"version",1},{"input",importKey},{"outputs",outputs},{"bytes",bytes}};
                    const auto unsignedText=manifest.dump();manifest["checksum"]=Navigation::Digest(Navigation::Bytes(unsignedText.begin(),unsignedText.end()));
                    const auto cache=manifest.dump();
                    auto temporary=importCachePath;temporary+=L".tmp";
                    {std::ofstream file(temporary,std::ios::binary|std::ios::trunc);if(!file.write(cache.data(),cache.size())||!file.flush())throw std::runtime_error("Cannot write import cache");}
                    if(!MoveFileExW(temporary.c_str(),importCachePath.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot publish import cache");
                }
                ++completed;
                printf("MAP_DONE mpr=%s mob=%s import_ms=%lld total_ms=%lld profiles=%u bytes=%llu\n",
                    map.first.c_str(),map.second.c_str(),importMs,Milliseconds(start),unsigned(Navigation::AgentCount),static_cast<unsigned long long>(bytes));
            }catch(const std::exception& e) {
                ++errors;
                fprintf(stderr,"MAP_ERROR mpr=%s mob=%s code=4 ms=%lld reason=%s\n",map.first.c_str(),map.second.c_str(),Milliseconds(start),e.what());
            }
            fflush(stdout);fflush(stderr);
        };
        while(nextMap!=maps.end()||!scheduler.Empty()) {
            if(nextMap!=maps.end()&&active<mapLimit) {
                PROCESS_MEMORY_COUNTERS_EX peak{};peak.cb=sizeof(peak);
                if(!GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&peak),sizeof(peak)))
                    throw std::runtime_error("Cannot query bake memory");
                if(scheduler.Empty()||peak.PeakWorkingSetSize+mapReserve<=memoryBudget) {
                    const auto map=*nextMap++;
                    scheduler.Add([bakeNext,map] {bakeNext(map);});
                }
            }
            scheduler.Poll();
            if(!scheduler.Empty())Sleep(1);
        }
        // Individual MOBs may extend the shared table with non-un* models.
        totalBytes+=bodyTable.size()-initialBodyBytes;
        if(totalBytes>400ull*1024*1024)fprintf(stderr,"BAKE_WARNING reason=campaign_exceeds_400_MiB\n");
        printf("BAKE_DONE maps=%zu completed=%u missing=%u errors=%u skipped=%u total_ms=%lld bytes=%llu\n",maps.size(),completed,missing,errors,skipped,Milliseconds(total),static_cast<unsigned long long>(totalBytes));fflush(stdout);
        return errors?4:missing?5:0;
    }catch(const std::exception& e) {
        fprintf(stderr,"BAKE_ERROR code=%d reason=%s\n",exitCode,e.what());return exitCode;
    }
}
