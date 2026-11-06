#include "StdAfx.h"
#include "NavigationWorld.h"
#include "ModArenaBoundary.h"
#include "ModFinalGolem.h"
#include "NavigationBake.h"
#include "NavigationObject.h"
#include "../Navigation/Profiles.h"
#include "aiMap.h"
#include "Unit.h"
#include "UnitServer.h"
#include "StuffMapObjects.h"
#include "WorldServer.h"
#include "GameJournal.h"
#include "CorePerf.h"
#include "ModRuntime.h"
#include "ModPackage.h"
#include "ModCliTool.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <future>
#include <cstdio>
#include "nlohmann/json.hpp"
#include <regex>
#include "RandomGen.h"
#include "MapEditObject.h"
#include "ObjectDBReader.h"
using namespace Navigation;
namespace {
Navigation::Point GamePoint(const CPlot3D& p) {return {p.x,p.y,p.z};}
Navigation::Bytes Read(const std::filesystem::path& path) {
    const auto n=std::filesystem::file_size(path);
    if(n>512ull*1024*1024)throw std::runtime_error("Nav64: file exceeds 512 MiB");
    std::ifstream file(path,std::ios::binary);Navigation::Bytes bytes(static_cast<size_t>(n));
    if(!file||(n&&!file.read(reinterpret_cast<char*>(bytes.data()),n)))throw std::runtime_error("Nav64: incomplete file read");
    return bytes;
}
}
NavigationWorld::NavigationWorld() {
    if(ModCliTool::CurrentKind()==ModCliTool::Kind::NavBake)baker=std::make_unique<NavigationBake>();
}
NavigationWorld::~NavigationWorld()=default;
Navigation::Geometry* NavigationWorld::Terrain() {return baker?&baker->terrain:nullptr;}
void NavigationWorld::Scene(const char* mpr,const char* mob,CWorldServer& world) {
    owner=&world;
    // LoadMainMob retires old dynamic objects after Scene(). Retain their
    // detached membership until removal; SourceObject clears reused fresh IDs.
    ++observerRevision;
    mapName=Navigation::ResourceName(mpr);mobName=Navigation::ResourceName(mob);
    if(sourceJob.valid())try{sourceJob.get();}catch(const std::exception& e){GameJournal::Event("NAV","update_discarded","reason=%s",GameJournal::Quote(e.what()).c_str());}
    iceVariant=false;preparedTiles.clear();originalSources.reset();sourceTransforms.clear();liveTransforms.clear();appliedTransforms.clear();
    for(auto& mesh:meshes)mesh.reset();bodySizes.clear();bodyTable.samples.clear();
    loaded=false;error.clear();failures=stepFailures=0;nextStepLog=0;++generation;queues.clear();airJournal.clear();queryObstacles=nullptr;queryReuse=nullptr;
    sourceRetries.clear();flyingUnits.clear();meshDirectory.clear();flightDeferred=false;sources.reset();absentStatic.clear();sourceDirty.clear();liveDoors.clear();
    liveObjects.clear();appliedObjects.clear();liveFlightObjects.clear();appliedFlightObjects.clear();bakedStatic.clear();dirtyObjects.clear();
    if(baker) {
        authoring={};auto path=std::filesystem::path(ModRuntime::MapPath(mapName.c_str()));path.replace_extension(".navsrc");
        if(std::filesystem::exists(path)) {
            auto input=Read(path);auto json=nlohmann::json::parse(input.begin(),input.end());
            if(json.at("version").get<int>()!=1)throw std::runtime_error("Nav64: unsupported navsrc version");
            for(int id:json.value("dynamic",std::vector<int>{}))authoring.dynamic.insert(id);
            for(int id:json.value("static",std::vector<int>{})) {
                if(authoring.dynamic.count(id))throw std::runtime_error("Nav64: contradictory navsrc object policy");
                authoring.fixed.insert(id);
            }
            for(const auto& value:json.value("doors",nlohmann::json::array())) {
                Door d;d.object=value.at("id").get<int>();d.openState=value.value("open_state",1);
                const auto a=value.at("min").get<std::array<float,3>>(),b=value.at("max").get<std::array<float,3>>();
                d.minimum={a[0],a[1],a[2]};d.maximum={b[0],b[1],b[2]};
                if(!Finite(d.minimum)||!Finite(d.maximum)||d.minimum.x>=d.maximum.x||d.minimum.y>=d.maximum.y||d.minimum.z>=d.maximum.z)throw std::runtime_error("Nav64: invalid door aperture");
                authoring.doors.emplace(d.object,d);
            }
            for(const auto& part:json.value("walkable",nlohmann::json::array()))authoring.walkable[part.at("id").get<int>()].insert(Navigation::ResourceName(part.at("part").get<std::string>().c_str()));
        }
        // Literal script edits are dynamic even when their MOB class is a
        // building/tree/stone. Variable references remain authoring decisions.
        auto mobPath=std::filesystem::path(ModRuntime::MapPath(mobName.c_str()));auto script=Read(mobPath);
        const std::string text(script.begin(),script.end());const std::regex edits(R"((?:RemoveUnitFromServer|SetCP)\s*\(\s*([0-9]+))");
        for(auto i=std::sregex_iterator(text.begin(),text.end(),edits);i!=std::sregex_iterator();++i)if(!authoring.fixed.count(std::stoi((*i)[1])))authoring.dynamic.insert(std::stoi((*i)[1]));
        baker->Configure(authoring);
    }
}
bool NavigationWorld::Load() {return Load({});}
bool NavigationWorld::Load(const std::filesystem::path& inputDirectory, bool synchronize) {
    if(loaded)return true;
    const auto start=std::chrono::steady_clock::now();
    auto milliseconds=[](auto from){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-from).count();};
    std::array<std::array<double,2>,AgentCount> meshTimes{};
    std::array<double,3> sourceTimes{};
    double bodyMs=0,catalogueMs=0,sourceReadMs=0,sourceDecodeMs=0;
    try {
        const auto* mod=ModRuntime::Resources();if(!mod)throw std::runtime_error("Nav64: no loaded mod");
        const auto root=inputDirectory.empty()?std::filesystem::path(mod->Directory())/"Maps"/"Nav64":inputDirectory;
        const auto directory=iceVariant?root/"variants"/"zone6"/"ice":root;
        meshDirectory=directory;
        auto bodiesPending=std::async(std::launch::async,[&] {
            const auto begin=std::chrono::steady_clock::now();
            auto bytes=Read(root/Navigation::BodyFilename);const auto size=bytes.size();
            auto result=std::make_pair(Navigation::LoadBodies(bytes),size);bodyMs=milliseconds(begin);return result;
        });
        auto cataloguePending=std::async(std::launch::async,[&] {
            const auto begin=std::chrono::steady_clock::now();
            const auto bytes=Read(directory/(std::filesystem::path(mapName).stem().string()+"--"+std::filesystem::path(mobName).stem().string()+".navworld"));
            auto result=nlohmann::json::parse(bytes.begin(),bytes.end());catalogueMs=milliseconds(begin);return result;
        });
        // Independent ready meshes and lossless source planes are decoded
        // together; neither depends on the other until IndexSurfaces below.
        auto sourcePending=std::async(std::launch::async,[&] {
            const auto begin=std::chrono::steady_clock::now();
            auto bytes=Read(directory/(std::filesystem::path(mapName).stem().string()+"--"+std::filesystem::path(mobName).stem().string()+".navgeom"));sourceReadMs=milliseconds(begin);
            const auto decode=std::chrono::steady_clock::now();auto result=LoadSources(std::move(bytes),&sourceTimes);sourceDecodeMs=milliseconds(decode);return result;
        });
        struct Loaded {std::unique_ptr<Navigation::Mesh> mesh;size_t bytes;};
        std::array<std::future<Loaded>,AgentCount> pending;
        for(int type=0;type<AgentCount;++type)if(type!=Flying||!flyingUnits.empty()||ModCliTool::CurrentKind()==ModCliTool::Kind::NavBake)pending[type]=std::async(std::launch::async,[&,type]{
            const auto path=directory/Navigation::MeshFilename(mapName,mobName,type);
            try {
                const auto begin=std::chrono::steady_clock::now();
                auto bytes=Read(path);const auto size=bytes.size();meshTimes[type][0]=milliseconds(begin);
                const auto decode=std::chrono::steady_clock::now();
                auto mesh=Navigation::Load(std::move(bytes),Agents[type].profile,mapName,mobName,Agents[type].name);meshTimes[type][1]=milliseconds(decode);
                mesh->generation=generation;
                return Loaded{std::move(mesh),size};
            }catch(const std::exception& e) {
                throw std::runtime_error("Nav64: map "+mapName+" / "+mobName+" profile "+Agents[type].name+": "+e.what()+"; re-bake map in mapper");
            }
        });
        auto bodies=bodiesPending.get();bodyTable=std::move(bodies.first);size_t bytes=bodies.second;
        for(int type=0;type<AgentCount;++type)if(pending[type].valid()){auto result=pending[type].get();bytes+=result.bytes;meshes[type]=std::move(result.mesh);}
        const auto json=cataloguePending.get();
        if(json.at("version").get<int>()!=4)throw std::runtime_error("Nav64: incompatible world catalogue");
        for(const auto& entry:json.at("dynamic")) {
            const int id=entry.at("id").get<int>();auto& boxes=appliedObjects[id];dirtyObjects.insert(id);
            for(const auto& value:entry.at("boxes")) {
                if(value.size()!=8)throw std::runtime_error("Nav64: invalid initial obstacle");
                Solid box;for(size_t i=0;i<8;++i) {
                    const auto p=value.at(i).get<std::array<float,3>>();box.corners[i]={p[0],p[1],p[2]};
                    if(!Finite(box.corners[i]))throw std::runtime_error("Nav64: non-finite initial obstacle");
                }
                boxes.push_back(box);
            }
        }
        for(const auto& entry:json.at("flight_dynamic")) {
            const int id=entry.at("id").get<int>();auto& boxes=appliedFlightObjects[id];dirtyObjects.insert(id);
            for(const auto& value:entry.at("boxes")) {
                if(value.size()!=8)throw std::runtime_error("Nav64: invalid visual obstacle");
                Solid box;for(size_t i=0;i<8;++i) {
                    const auto p=value.at(i).get<std::array<float,3>>();box.corners[i]={p[0],p[1],p[2]};
                    if(!Finite(box.corners[i]))throw std::runtime_error("Nav64: non-finite visual obstacle");
                }
                boxes.push_back(box);
            }
        }
        for(int id:json.at("static")){bakedStatic.insert(id);dirtyObjects.insert(id);liveFlightObjects.erase(id);}
        sources=sourcePending.get();originalSources=sources;
        // Detached save loading constructs world objects directly, bypassing
        // AddMapObject. Recover only their original MOB placement metadata;
        // Read() does not instantiate objects or compile/execute scripts.
        auto missingSource=[&](){return std::any_of(bakedStatic.begin(),bakedStatic.end(),[&](int id){return !sourceTransforms.count(id);});};
        if(owner&&missingSource()) {
            SourceMob(mobName.c_str(),owner->aiMap);
        }
        for(int id:bakedStatic)if(sourceTransforms.count(id))appliedTransforms[id]=sourceTransforms.at(id);
        // Scene() can retain native static objects on a same-zone reload. Their
        // absence from the Add callbacks is not evidence of removal.
        if(owner)for(auto i=detachedObjects.begin();i!=detachedObjects.end();) {
            if(!owner->GetMapObject(*i))i=detachedObjects.erase(i);else ++i;
        }
        if(synchronize&&owner)for(const auto& entry:owner->mapObjects)Object(entry.second,false,owner->aiMap);
        for(auto& mesh:meshes)if(mesh){mesh->sources=sources;IndexSurfaces(*mesh);}
        const auto syncStart=std::chrono::steady_clock::now();loaded=true;if(synchronize){ModFinalGolem::RestoreCollapse();SyncObjects(true);}
        const double syncMs=milliseconds(syncStart);
        const auto initialTiles=sourceDirty.size();const auto updateStart=std::chrono::steady_clock::now();if(synchronize){UpdateSources(true);ModFinalGolem::CommitCollapse();}
        if(ModCliTool::CurrentKind()==ModCliTool::Kind::NavBake) {
            printf("LOAD_STAGES bodies_ms=%.3f catalogue_ms=%.3f source_read_ms=%.3f source_decode_ms=%.3f sync_ms=%.3f update_ms=%.3f\n",bodyMs,catalogueMs,sourceReadMs,sourceDecodeMs,syncMs,milliseconds(updateStart));
            printf("LOAD_SOURCE_PHASE unpack_ms=%.3f arrays_ms=%.3f parse_ms=%.3f\n",sourceTimes[0],sourceTimes[1],sourceTimes[2]);
            for(int type=0;type<AgentCount;++type)printf("LOAD_PROFILE type=%s read_ms=%.3f decode_ms=%.3f\n",Agents[type].name,meshTimes[type][0],meshTimes[type][1]);
        }
        if(ModCliTool::CurrentKind()==ModCliTool::Kind::NavBake) {
            printf("LOAD_INITIAL_UPDATES profile_tiles=%zu\n",initialTiles);fflush(stdout);
        }
        // Actual world placements are resolved by the zone/party activation.

        const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        double profileDecodeMs=0;for(const auto& time:meshTimes)profileDecodeMs=(std::max)(profileDecodeMs,time[1]);
        GameJournal::Event("NAV","loaded","mpr=%s mob=%s profiles=%zu ms=%.3f bytes=%zu generation=%llu source_read_ms=%.3f source_unpack_ms=%.3f source_arrays_ms=%.3f source_meta_ms=%.3f profile_decode_max_ms=%.3f",mapName.c_str(),mobName.c_str(),size_t(std::count_if(meshes.begin(),meshes.end(),[](const auto& mesh){return bool(mesh);})),ms,bytes,static_cast<unsigned long long>(generation),sourceReadMs,sourceTimes[0],sourceTimes[1],sourceTimes[2],profileDecodeMs);
        return true;
    }catch(const std::exception& e){loaded=false;error=e.what();return false;}
}
std::unique_ptr<NavigationWorld> NavigationWorld::PrepareVariant(bool ice) {
    if(mapName!="zone6.mpr" || mobName!="zone6.mob")throw std::runtime_error("Nav64: ice variant requires zone6/zone6");
    auto candidate=std::make_unique<NavigationWorld>();
    candidate->mapName=mapName;candidate->mobName=mobName;
    candidate->generation=generation+1;candidate->iceVariant=ice;
    candidate->observerRevision=observerRevision+1;
    candidate->flyingUnits=flyingUnits;candidate->owner=owner;candidate->sourceTransforms=sourceTransforms;candidate->detachedObjects=detachedObjects;
    if(ice&&owner)candidate->SourceMob("zone6_ice.mob",owner->aiMap);
    if(!candidate->Load({},false))throw std::runtime_error(candidate->Error());
    for(int id=600001;id<=600019;++id) {
        if(candidate->bakedStatic.count(id)!=size_t(ice))throw std::runtime_error("Nav64: ice variant static catalogue mismatch");
        if(ice && !candidate->sources->objectTiles.count(id))throw std::runtime_error("Nav64: ice walkable source is missing");
    }
    return candidate;
}
void NavigationWorld::CommitVariant(std::unique_ptr<NavigationWorld> candidate, CWorldServer& world) {
    // Finish the worker before retiring any source meshes. The candidate owns
    // new queues, source tasks, object bindings and polygon generations.
    if(sourceJob.valid())sourceJob.get();
    for(const auto& source:sourceTransforms)candidate->sourceTransforms.emplace(source);
    for(const auto& entry:world.mapObjects)candidate->Object(entry.second,false,world.aiMap);
    candidate->SyncObjects(true);candidate->UpdateSources(true);
    *this=std::move(*candidate);
    for(const auto& entry:world.mapObjects)if(auto* unit=dynamic_cast<CUnit*>(entry.second)) {
        unit->navLocation.polygon=0;unit->navLocation.generation=0;
        Restore(unit,world.aiMap);
        if(auto* server=dynamic_cast<CUnitServer*>(unit->GetUnitSpecific()))server->RestoreNavigation(true);
    }
}
const Navigation::BodySize& NavigationWorld::Body(CUnit* unit) {
    const auto& c=unit->GetComplection();const std::string mask=Navigation::ResourceName(unit->GetMaskName());
    if(!std::isfinite(c.strength)||!std::isfinite(c.grace)||!std::isfinite(c.tall))throw std::runtime_error("Nav64: invalid body constitution");
    const Navigation::BodyKey key{mask,c.strength,c.grace,c.tall};
    auto found=bodySizes.find(key);if(found!=bodySizes.end())return found->second;
    return bodySizes.emplace(key,bodyTable.Lookup(mask,c.strength,c.grace,c.tall)).first->second;
}
Navigation::Mesh* NavigationWorld::ForUnit(CUnit* unit,int aiClass) {
    if(!loaded)return nullptr;
    try {
        const int type=Type(unit,unit?unit->GetAIClass():aiClass);
        if(type==Flying){if(unit)flyingUnits.insert(unit->GetID());if(!meshes[Flying])LoadFlying();else if(flightDeferred){FlushFlight();UpdateSources(true);}}
        return type<AgentCount?meshes[type].get():nullptr;
    }
    catch(const std::exception& e) {
        error=e.what();if((unit&&unit->IsHero())||failures++<16)GameJournal::Event("NAV","query_failed","unit=%d stage=body reason=%s",unit?unit->GetID():-1,GameJournal::Quote(e.what()).c_str());
        return nullptr;
    }
}
void NavigationWorld::Object(CMapObject* object,bool remove,CAIMap& map) {
    if(baker){baker->Object(object,remove,map);return;}
    if(auto* unit=dynamic_cast<CUnit*>(object)) {
        // AI-index removal also happens during ordinary movement/pose changes.
        // Final world removal owns queue and consumer cleanup.
        if(!remove) {
            if(unit->GetUserUnitStats()->cLocomotion==3)flyingUnits.insert(unit->GetID());
            else flyingUnits.erase(unit->GetID());
        }
        return;
    }
    if(!NavigationObject(object))return;
    const int id=object->GetID();remove=remove||detachedObjects.count(id);if(dirtyObjects.insert(id).second)++observerRevision;
    if(object->WhoAmI()==HPC_LEVER) {
        auto* lever=static_cast<CLeverObject*>(object);
        liveDoors[id]=remove?-1:lever->GetCurrentState();
    }
    if(remove){liveObjects.erase(id);liveFlightObjects.erase(id);liveTransforms.erase(id);return;}
    const auto cp=object->GetCP();const auto& q=object->GetOrientation();
    SceneTransform transform;transform.origin={cp.x,cp.y,cp.z+map.GetOriginalTileHeight(cp)};
    transform.axis={GamePoint(q.RotateX()),GamePoint(q.RotateY()),GamePoint(q.RotateZ())};liveTransforms[id]=transform;
    liveObjects[id]=NavigationBoxes(object,map);
    if(!loaded||Dynamic(id))liveFlightObjects[id]=NavigationBoxes(object,map,true);
}
void NavigationWorld::SourceObject(const CMEBasicObject& object,CAIMap& map) {
    if(object.p_nID<0)return;
    if(owner&&!owner->GetMapObject(object.p_nID))detachedObjects.erase(object.p_nID);
    const auto cp=object.p_ptPosition;const auto& q=object.p_qRotation;
    SceneTransform transform;transform.origin={cp.x,cp.y,cp.z+map.GetOriginalTileHeight(cp)};
    transform.axis={GamePoint(q.RotateX()),GamePoint(q.RotateY()),GamePoint(q.RotateZ())};
    // The loader calls this before CheckAddObject, including static records
    // skipped during save restoration. A later spawn cannot replace its source.
    sourceTransforms.emplace(object.p_nID,transform);
}
void NavigationWorld::SourceMob(const char* name,CAIMap& map) {
    CGameMOBFile reader;
    if(!reader.Open(ModRuntime::MapPath(name).c_str(),CFile::modeRead|CFile::shareDenyWrite|CFile::typeBinary))
        throw std::runtime_error("Nav64: cannot read source placements");
    CBasicObjectMap objects;
    struct Cleanup {
        CBasicObjectMap& objects;
        ~Cleanup(){for(const auto& entry:objects)delete entry.second;}
    } cleanup{objects};
    SDiplomation diplomacy;CString script;reader.Read(objects,diplomacy,script);
    for(const auto& entry:objects)SourceObject(*entry.second,map);
}
void NavigationWorld::TransformObject(int id,Navigation::Point position,float yaw,bool present) {
 if(!owner||!std::isfinite(yaw))throw std::runtime_error("Nav64: invalid scenery yaw");
 auto* object=owner->GetMapObject(id);if(!object)throw std::runtime_error("Nav64: scenery object missing");
 const auto q=object->GetOrientation();const auto axis=q.RotateX();auto rotation=CQuaternion(CPlot3D(0,0,1),yaw-std::atan2(axis.y,axis.x))*q;
 std::array<float,4> value;rotation.GetComponents(value[0],value[1],value[2],value[3]);TransformObject(id,position,value,present);
}
void NavigationWorld::TransformObject(int id,Navigation::Point position,const std::array<float,4>& qv,bool present) {
    if(!loaded||!owner)throw std::runtime_error("Nav64: scene is not loaded");
    auto* object=owner->GetMapObject(id);
    if(!object||dynamic_cast<CUnit*>(object)||!NavigationObject(object)||object->WhoAmI()==HPC_MAGICTRAP||object->IsInvalid())
        throw std::runtime_error("Nav64: transform requires an existing scenery object");
    if(!Finite(position)||!std::all_of(qv.begin(),qv.end(),[](float v){return std::isfinite(v);})||position.x<0||position.y<0||
       !owner->aiMap.TileInBounds(Float2AITile(position.x),Float2AITile(position.y)))
        throw std::runtime_error("Nav64: invalid scenery transform");
    auto& map=owner->aiMap;const auto q=object->GetOrientation();const auto x=q.RotateX();
    const CQuaternion rotation(qv[0],qv[1],qv[2],qv[3]);
    const float norm=qv[0]*qv[0]+qv[1]*qv[1]+qv[2]*qv[2]+qv[3]*qv[3];
    if(std::fabs(norm-1.f)>.001f)throw std::runtime_error("Nav64: non-unit scenery rotation");
    const bool wasPresent=!detachedObjects.count(id);const auto previous=object->GetCP();
    if(wasPresent&&!map.RemoveObject(object))throw std::runtime_error("Nav64: scenery is not registered in the AI map");
    object->SetCP(CPlot3D(position.x,position.y,position.z));object->SetOrientation(rotation);
    if(present) {
        detachedObjects.erase(id);
        if(!map.AddObject(object)) {
            object->SetCP(previous);object->SetOrientation(q);
            if(wasPresent)map.AddObject(object);else {detachedObjects.insert(id);Object(object,true,map);}
            throw std::runtime_error("Nav64: scenery registration failed; original placement restored");
        }
    } else {detachedObjects.insert(id);Object(object,true,map);}
    object->SetUpdateMask(CMapObject::VLD_MASK_POSROT);
    GameJournal::Event("NAV","object_transform","NID=%d position=(%.3f,%.3f,%.3f) present=%d",id,position.x,position.y,position.z,int(present));
}
std::uint64_t NavigationWorld::Bake(const std::filesystem::path& directory,Navigation::Bytes& table,CWorldServer& world) {
    if(!baker)throw std::runtime_error("Nav64: baking requires NavBake tool");
    auto path=std::filesystem::path(ModRuntime::MapPath(mapName.c_str()));path.replace_extension(".navsrc");
    const auto authored=std::filesystem::exists(path)?Read(path):Navigation::Bytes{};
    baker->Bind(world,world.aiMap);
    const auto bytes=baker->Bake(mapName,mobName,directory,table,authored);
    // Validate release loading through the same loader and live-object diff
    // used by the game, without running a game tick or any scripts.
    auto importer=std::move(baker);
    for(const auto& entry:world.mapObjects)Object(entry.second,false,world.aiMap);
    const auto start=std::chrono::steady_clock::now();
    if(!Load(directory))throw std::runtime_error(error);
    printf("LOAD_RELEASE mpr=%s mob=%s ms=%.3f pending=%zu\n",mapName.c_str(),mobName.c_str(),std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count(),sourceDirty.size());fflush(stdout);
    if(mapName=="zone1.mpr"&&mobName=="zone1.mob") {
        CUnit* probe=nullptr;
        for(const auto& entry:world.mapObjects)if(auto* unit=dynamic_cast<CUnit*>(entry.second)) {
            if(Type(unit,unit->GetAIClass())==Small&&unit->GetAIClass()!=0){probe=unit;break;}
        }
        if(!probe)throw std::runtime_error("Nav64: restore release has no small unit");
        const auto originalCP=probe->GetCP();const auto originalLocation=probe->navLocation;
        auto check=[&](const char* name,Point point,std::uint64_t surface) {
            world.aiMap.MoveObject(probe,CPlot3D(point.x,point.y,originalCP.z));
            probe->navLocation={};probe->navLocation.position=point;probe->navLocation.surface=surface;
            const bool placed=Place(probe,world.aiMap);const auto saved=probe->navLocation;
            Location strict;const bool r2=placed&&StartLocation(*ForUnit(probe),saved.position,strict,Height(probe))&&strict.surface==saved.surface&&std::abs(strict.position.z-saved.position.z)<=.25f;
            // Match the serialized fields: polygon/generation are not stored.
            probe->navLocation.polygon=probe->navLocation.generation=0;
            const bool restored=placed&&Restore(probe,world.aiMap);
            const auto loaded=probe->navLocation;
            const bool same=restored&&Distance(saved.position,loaded.position)<.02f;
            // Surface identity can change; only height/position select a floor.
            probe->navLocation.surface=~std::uint64_t(0);
            const bool stale=same&&Restore(probe,world.aiMap)&&Distance(saved.position,probe->navLocation.position)<.02f;
            printf("RESTORE_RELEASE name=%s placed=%d r2_strict=%d restored=%d stale_hint=%d surface=%llu at=(%.6f,%.6f,%.6f)\n",name,int(placed),int(r2),int(same),int(stale),static_cast<unsigned long long>(loaded.surface),loaded.position.x,loaded.position.y,loaded.position.z);fflush(stdout);
            if(!stale)throw std::runtime_error(std::string("Nav64: placement/restore release failure: ")+name);
        };
        check("HERO_START",{55.55f,138.78f,19.24f},1);
        for(int id:{195,1492,1748}) {
            bool checked=false;
            for(const auto& triangle:sources->ground)if(triangle.object==id&&triangle.area&&triangle.surface>1) {
                Point p{(triangle.a.x+triangle.b.x+triangle.c.x)/3,(triangle.a.y+triangle.b.y+triangle.c.y)/3,(triangle.a.z+triangle.b.z+triangle.c.z)/3};
                Location location;
                if(!ResolveLocation(*ForUnit(probe),p,nullptr,location,Height(probe))||int(location.surface>>32)!=id)continue;
                check(("BASE_"+std::to_string(id)).c_str(),location.position,location.surface);checked=true;break;
            }
            // A paired MOB can legitimately contain no such model.
            const bool present=std::any_of(sources->ground.begin(),sources->ground.end(),[&](const Triangle& t){return t.object==id&&t.surface>1;});
            if(present&&!checked)throw std::runtime_error("Nav64: BASE restore release has no placement: "+std::to_string(id));
        }
        world.aiMap.MoveObject(probe,originalCP);probe->navLocation=originalLocation;
    }
    if(mapName=="zone1.mpr"&&mobName=="zone1.mob") {
        std::ofstream sites(NavigationBake::reportDirectory/"r15-sites.csv");
        sites<<"x,y,profile,project,surface,z\n";
        std::ofstream water(NavigationBake::reportDirectory/"r15-water-poses.csv");water<<"x,y,height,standing,laying\n";
        for(const auto p:{Point{102.974f,84.9555f,6.f},Point{103.887f,52.188f,6.f},Point{58.158f,63.208f,4.f}}) {
            Location standing,laying;const bool s=Project(*meshes[Small],p,.001f,4.f,standing,1.6f,1u<<7),l=Project(*meshes[Small],p,.001f,4.f,laying,.4f,1u<<5);
            water<<p.x<<','<<p.y<<','<<p.z<<','<<s<<','<<l<<'\n';
        }
        for(const auto p:{Point{193.487f,112.524f,7.76405f},Point{194.246f,112.665f,7.76405f},Point{197.751f,112.244f,7.89577f},Point{205.953f,112.161f,7.7f}})
            for(int type=Small;type<=Human;++type){Location at;const bool ok=Project(*meshes[type],p,.001f,4.f,at,1.6f);sites<<p.x<<','<<p.y<<','<<Agents[type].name<<','<<ok<<','<<at.surface<<','<<std::setprecision(9)<<at.position.z<<'\n';}
    }
    if(!NavigationBake::replayInput.empty()) {
        const auto input=Read(NavigationBake::replayInput);
        auto records=nlohmann::json::parse(input.begin(),input.end());
        nlohmann::json output=nlohmann::json::array();
        for(auto record:records) {
            if(record.at("scene").at(0)!=mapName||record.at("scene").at(1)!=mobName)continue;
            auto point=[](const nlohmann::json& j){return Point{j.at(0).get<float>(),j.at(1).get<float>(),j.at(2).get<float>()};};
            const auto from=point(record.at("source")),goal=point(record.at("goal"));
            auto* unit=world.GetUnit(record.at("unit").get<int>());
            int type=unit?Type(unit,unit->GetAIClass()):Small;
            if(record.contains("profile"))for(int i=0;i<AgentCount;++i)if(record["profile"]==Agents[i].name)type=i;
            auto& mesh=*meshes[type];const float height=record.value("height",unit?Height(unit):1.6f);
            record["profile_used"]=Agents[type].name;record["height_used"]=height;
            if(record.at("kind")=="random_goal"||record.at("kind")=="class_support") {
                const int c=record.value("class",unit?unit->GetAIClass():int(mesh.profile.materialClass));
                const auto flags=ExcludeClass(c);const bool expected=record.value("expected",true);
                Location at;Point selected;bool accepted=false;
                if(record.at("kind")=="class_support") {
                    accepted=Project(mesh,goal,.001f,4.f,at,height,flags);
                    if(accepted)selected=at.position;
                } else {
                    const auto shape=record.value("shape",std::string("circle"))=="square"?RandomShape::Square:RandomShape::Circle;
                    const bool placed=ResolveLocation(mesh,from,nullptr,at,height,flags);
                    accepted=placed&&RandomReachable(mesh,at.position,goal,record.at("radius").get<float>(),height,[](){return ::random.GetFloat(0.f,1.f);},selected,flags,&at,shape);
                }
                record["accepted"]=accepted;record["class_used"]=c;
                if(accepted)record["selected"]={selected.x,selected.y,selected.z};
                const bool passed=accepted==expected;record["passed"]=passed;output.push_back(record);
                printf("GOAL_POLICY_RELEASE name=%s kind=%s profile=%s class=%d accepted=%d expected=%d passed=%d\n",record.value("name",std::string("goal")).c_str(),record.at("kind").get<std::string>().c_str(),Agents[type].name,c,int(accepted),int(expected),int(passed));fflush(stdout);
                if(record.value("required",false)&&!passed) {
                    std::ofstream(NavigationBake::reportDirectory/"goal-policy-failure.json")<<record.dump(2);
                    throw std::runtime_error("Nav64: shared goal/class release failed");
                }
                continue;
            }
            Location start,finish,desired;
            const bool placed=ResolveLocation(mesh,from,nullptr,start,height);
            record["placed"]=placed;
            if(placed)record["placement"]={start.position.x,start.position.y,start.position.z,start.surface};
            const char* reason="no_start";
            const bool stepped=placed&&Constrain(mesh,start.position,goal,finish,height,&reason,&start);
            record["step"]=stepped;record["step_reason"]=reason;
            const bool projected=Project(mesh,goal,.001f,4.f,desired,height);
            record["goal_support"]=projected;
            if(projected)record["support"]={desired.position.x,desired.position.y,desired.position.z,desired.surface};
            Request request;request.source=from;request.goal=goal;request.height=height;request.maxLength=record.value("max_length",1.e6f);
            const bool flat=record.value("flat_cost",true);
            if(!flat) {
                SRouteCostsInfo costs;world.aiMap.GetRouteCosts(unit?unit->GetAIClass():3,costs);
                for(int i=1;i<16;++i)request.areaCosts[i]=float(costs.pTerrainCosts[i])/1024.f;
            }
            record["area_costs"]=request.areaCosts;
            const auto raw=Navigation::Find(mesh,request);record["find_raw"]=StatusName(raw.status);
            record["planned_cost"]=raw.plannedCost;record["selected_cost"]=raw.cost;
            record["straight_cost"]=raw.straightCost;record["straightened"]=raw.straightened;
            record["route_points"]=nlohmann::json::array();for(const auto& p:raw.points)record["route_points"].push_back({p.position.x,p.position.y,p.position.z});
            record["route_length"]=raw.length;
            if(placed){request.source=start.position;request.start=&start;record["find_placed"]=StatusName(Navigation::Find(mesh,request).status);}
            auto polygons=nlohmann::json::array();
            for(int tileIndex=0;tileIndex<mesh.data->getMaxTiles();++tileIndex) {
                const auto* tile=static_cast<const dtNavMesh&>(*mesh.data).getTile(tileIndex);if(!tile||!tile->header)continue;
                if(from.x<tile->header->bmin[0]-.3f||from.x>tile->header->bmax[0]+.3f||-from.y<tile->header->bmin[2]-.3f||-from.y>tile->header->bmax[2]+.3f)continue;
                for(int pi=0;pi<tile->header->polyCount;++pi) {
                    const auto& p=tile->polys[pi];auto verts=nlohmann::json::array();float d=1.e30f;
                    for(int i=0;i<p.vertCount;++i){const auto* v=tile->verts+p.verts[i]*3;verts.push_back({v[0],-v[2],v[1]});d=(std::min)(d,std::hypot(v[0]-from.x,-v[2]-from.y));}
                    if(d>.5f)continue;
                    auto links=nlohmann::json::array();for(auto i=p.firstLink;i!=DT_NULL_LINK;i=tile->links[i].next)links.push_back({tile->links[i].ref,tile->links[i].edge,tile->links[i].bmin,tile->links[i].bmax});
                    polygons.push_back({{"ref",mesh.data->getPolyRefBase(tile)|dtPolyRef(pi)},{"verts",verts},{"flags",p.flags},{"links",links}});
                }
            }
            record["polygons"]=polygons;
            auto faces=nlohmann::json::array();
            for(const auto& t:sources->ground) {
                const float ax=t.b.x-t.a.x,ay=t.b.y-t.a.y,bx=t.c.x-t.a.x,by=t.c.y-t.a.y,det=ax*by-ay*bx;
                if(det<=1.e-8f)continue;
                const float u=((goal.x-t.a.x)*by-(goal.y-t.a.y)*bx)/det,v=(ax*(goal.y-t.a.y)-ay*(goal.x-t.a.x))/det;
                if(u<0||v<0||u+v>1)continue;
                const float z=t.a.z+u*(t.b.z-t.a.z)+v*(t.c.z-t.a.z);
                const float nx=ay*(t.c.z-t.a.z)-(t.b.z-t.a.z)*by,ny=(t.b.z-t.a.z)*bx-ax*(t.c.z-t.a.z);
                auto* object=world.GetMapObject(t.object);
                faces.push_back({{"object",t.object},{"model",object?object->GetMaskName():"terrain"},{"surface",t.surface},{"z",z},{"area",t.area},{"slope",std::acos(det/std::sqrt(nx*nx+ny*ny+det*det))*180.f/3.14159265f}});
            }
            record["faces_at_goal"]=faces;
            output.push_back(record);
        }
        const auto file=NavigationBake::reportDirectory/(std::filesystem::path(mapName).stem().string()+"--"+std::filesystem::path(mobName).stem().string()+".replay.json");
        std::ofstream(file)<<output.dump(2);
        // Replay the game's route conversion, speed assignment, walker and
        // collision/avoidance tick. No world scripts, animations or UI run.
        struct SavedUnit {CUnit* unit;CPlot3D cp;Location location;};
        std::vector<SavedUnit> saved;
        CUnit* human=nullptr;CUnit* neighbour=nullptr;
        for(const auto& entry:world.mapObjects)if(auto* unit=dynamic_cast<CUnit*>(entry.second)) {
            saved.push_back({unit,unit->GetCP(),unit->navLocation});world.aiMap.RemoveObject(unit);
            if(Type(unit,unit->GetAIClass())==Small&&Height(unit)>=1.6f&&!human)human=unit;
            else if(!neighbour)neighbour=unit;
        }
        auto startWalker=[&](CUnit* unit,SAIPath& path) {
            float speed=0,angular=0;unit->GetSpeeds(speed,angular);
            unit->walker.SetSpeeds(speed,angular);unit->walker.CreateWalkerRoute(path.curSegment);
        };
        CUnit* const defaultNeighbour=neighbour;
        auto movementRows=nlohmann::json::array();unsigned requiredFailures=0;
        const auto oldTime=world.GetSegmentTimeCounter();
        for(const auto& record:records) {
            if(record.at("scene").at(0)!=mapName||record.at("scene").at(1)!=mobName)continue;
            if(record.at("kind")=="random_goal"||record.at("kind")=="class_support")continue;
            const bool required=record.value("required",false);
            if(record.at("kind")!="walk"&&!required)continue;
            // A previous route may have exhausted its budget. Set the clock
            // before constructing the next route/walker, not on its first step.
            world.SetSegmentTimeCounter(oldTime);
            if(!human)throw std::runtime_error("Nav64: requested movement release has no standing small body");
            const auto a=record.at("source").get<std::array<float,3>>(),b=record.at("goal").get<std::array<float,3>>();
            const Point from{a[0],a[1],a[2]};auto* unit=human;
            const auto cp=unit->GetCP();unit->navLocation={};unit->navLocation.position=from;unit->navLocation.surface=1;
            world.aiMap.MoveObject(unit,CPlot3D(from.x,from.y,cp.z));
            bool ok=Place(unit,world.aiMap);world.aiMap.AddObject(unit);
            SAIPath path;unsigned wait=0,ticks=0,waitTicks=0,avoidTicks=0,avoidAttempts=0;float maxDelta=0;double detourMaxMs=0;std::string failure="no_path";
            const CPlot3D goal(b[0],b[1],0);Location goalSupport;goalSupport.position={b[0],b[1],b[2]};
            const BOOL savedFastPath=unit->bNeedFastPath;
            unit->bNeedFastPath=record.value("flat_cost",true)?TRUE:FALSE;
            Navigation::Route decision;
            ok=ok&&world.aiMap.NewPath(unit,goal,path,1.e6f,0,Navigation::GoalPolicy::Exact,&decision,&goalSupport);
            unit->bNeedFastPath=savedFastPath;
            if(ok)startWalker(unit,path);
            auto routeLength=[](const SAIPath& path) {
                double length=0;Point last;bool first=true;
                for(const auto* nodes:{&path.curSegment.route,&path.toGo})for(POSITION i=nodes->route.GetHeadPosition();i;) {
                    const auto& n=nodes->route.GetNext(i);Point p{AITile2Meter(n.point.x),AITile2Meter(n.point.y),n.supportZ};
                    if(!first)length+=std::hypot(p.x-last.x,p.y-last.y);first=false;last=p;
                }
                return length;
            };
            double straightLength=ok?routeLength(path):0,walkedLength=0;
            auto routePoints=[](const SAIPath& path) {
                std::vector<Point> points;
                for(const auto* nodes:{&path.curSegment.route,&path.toGo})for(POSITION i=nodes->route.GetHeadPosition();i;) {
                    const auto& n=nodes->route.GetNext(i);Point p{AITile2Meter(n.point.x),AITile2Meter(n.point.y),n.supportZ};
                    if(points.empty()||std::hypot(p.x-points.back().x,p.y-points.back().y)>.0001f)points.push_back(p);
                }
                return points;
            };
            auto plannedPoints=routePoints(path);
            Point previousDirection;float previousStep=0;unsigned unplannedTurns=0;
            auto positions=nlohmann::json::array();
            const bool beside=record.value("neighbour",false);
            neighbour=record.contains("neighbour_unit")?world.GetUnit(record.at("neighbour_unit").get<int>()):defaultNeighbour;
            if(beside&&(!neighbour||neighbour==unit))throw std::runtime_error("Nav64: release neighbour is unavailable");
            if(ok&&beside&&neighbour) {
                auto n=neighbour->GetCP();const auto here=unit->GetCP();
                Point direction{1,0,0};
                for(POSITION i=path.curSegment.route.route.GetHeadPosition();i;) {
                    const auto& node=path.curSegment.route.route.GetNext(i);const float dx=AITile2Meter(node.point.x)-here.x,dy=AITile2Meter(node.point.y)-here.y,len=std::hypot(dx,dy);
                    if(len>.001f){direction={dx/len,dy/len,0};break;}
                }
                const float distance=record.value("neighbour_distance",Radius(unit)+Radius(neighbour)+.02f);
                n.x=here.x+direction.x*distance;n.y=here.y+direction.y*distance;
                neighbour->navLocation={};world.aiMap.MoveObject(neighbour,n);Place(neighbour,world.aiMap);world.aiMap.AddObject(neighbour);
            }
            bool done=false;
            for(;ok&&!done&&ticks<10000;++ticks) {
                world.SetSegmentTimeCounter(oldTime+int(ticks)+1);
                const auto previous=unit->navLocation;
                Movement movement;ok=Move(unit,path,float(world.GetSegmentTimeCounter()),wait,movement);
                if(!ok){
                    failure="step_rejected";
                    auto detail=nlohmann::json::object();const auto* mesh=ForUnit(unit);
                    std::vector<Point> corners;for(POSITION i=path.curSegment.route.route.GetHeadPosition();i;){const auto& n=path.curSegment.route.route.GetNext(i);corners.push_back({AITile2Meter(n.point.x),AITile2Meter(n.point.y),n.supportZ});}
                    Location next;const char* followReason="none";const char* directReason="none";
                    const bool follow=Follow(*mesh,previous.position,movement.desired,corners,next,Height(unit),&followReason,&previous);
                    const bool direct=Constrain(*mesh,previous.position,movement.desired,next,Height(unit),&directReason,&previous);
                    detail["follow"]=follow;detail["follow_reason"]=followReason;detail["direct"]=direct;detail["direct_reason"]=directReason;
                    detail["from"]={previous.position.x,previous.position.y,previous.position.z};detail["polygon"]=previous.polygon;
                    detail["desired"]={movement.desired.x,movement.desired.y,movement.desired.z};detail["corners"]=nlohmann::json::array();for(auto c:corners)detail["corners"].push_back({c.x,c.y,c.z});
                    detail["polygons"]=nlohmann::json::array();
                    const auto nav=ToNav(previous.position);const float p[]={nav.x,nav.y,nav.z},ext[]={.5f,10,.5f};dtPolyRef refs[512];int count=0;dtQueryFilter filter;
                    mesh->query->queryPolygons(p,ext,&filter,refs,&count,512);
                    for(int i=0;i<count;++i){const dtMeshTile* tile=nullptr;const dtPoly* poly=nullptr;mesh->data->getTileAndPolyByRef(refs[i],&tile,&poly);auto vertices=nlohmann::json::array(),links=nlohmann::json::array();for(int j=0;j<poly->vertCount;++j){const auto* v=tile->verts+poly->verts[j]*3;vertices.push_back({v[0],-v[2],v[1]});}for(auto j=poly->firstLink;j!=DT_NULL_LINK;j=tile->links[j].next)links.push_back({tile->links[j].ref,tile->links[j].edge});detail["polygons"].push_back({{"ref",refs[i]},{"vertices",vertices},{"links",links},{"surfaces",mesh->polygonSurfaces.at({tile->header->x,tile->header->y})[size_t(poly-tile->polys)]}});}
                    std::ofstream(NavigationBake::reportDirectory/(record.value("name",std::string("walk"))+".tick-failure.json"))<<detail.dump(2);
                    break;
                }
                avoidAttempts+=movement.avoidanceAttempts;detourMaxMs=(std::max)(detourMaxMs,movement.detourMs);
                if(movement.replanned){++avoidTicks;straightLength=walkedLength+routeLength(path);plannedPoints=routePoints(path);previousStep=0;continue;}
                if(movement.waiting) {
                    ++waitTicks;path.curSegment.Freeze(1);
                    // A standing neighbour holds the corridor through the bounded wait,
                    // then clears it. The production queue/avoidance runs first.
                    if(beside&&neighbour&&record.value("release_after_wait",55u)>0&&waitTicks>=record.value("release_after_wait",55u)) {
                        // Circle collisions query the world's spatial index;
                        // removing AI locks alone leaves a stationary collider.
                        const auto original=std::find_if(saved.begin(),saved.end(),[&](const SavedUnit& s){return s.unit==neighbour;});
                        world.aiMap.MoveObject(neighbour,original->cp);neighbour->navLocation=original->location;
                    }
                    continue;
                }
                const auto next=movement.support;Location displayed;
                const float delta=std::abs(next.position.z-previous.position.z);
                maxDelta=(std::max)(maxDelta,delta);
                const auto* mesh=ForUnit(unit);
                const float limit=mesh->profile.climb+std::tan(mesh->profile.slope*3.14159265f/180.f)*std::hypot(next.position.x-previous.position.x,next.position.y-previous.position.y)+.001f;
                ok=delta<=limit&&Support(unit,next.position,displayed)&&Distance(displayed.position,next.position)<.003f&&displayed.surface==next.surface;
                if(!ok){failure="client_height_or_continuity";break;}
                const float dx=next.position.x-previous.position.x,dy=next.position.y-previous.position.y,step=std::hypot(dx,dy);
                if(step>.001f) {
                    const Point direction{dx/step,dy/step,0};
                    if(previousStep>.001f&&previousDirection.x*direction.x+previousDirection.y*direction.y<.8660254f) {
                        bool corner=false;const auto& nodes=plannedPoints;
                        for(size_t i=1;i+1<nodes.size();++i) {
                            const auto a=nodes[i-1],b=nodes[i],c=nodes[i+1];
                            const float ax=b.x-a.x,ay=b.y-a.y,bx=c.x-b.x,by=c.y-b.y;
                            if(ax*bx+ay*by<.999f*std::hypot(ax,ay)*std::hypot(bx,by)&&std::hypot(b.x-previous.position.x,b.y-previous.position.y)<=step+previousStep+.003f)corner=true;
                        }
                        if(!corner)++unplannedTurns;
                    }
                    previousDirection=direction;previousStep=step;walkedLength+=step;
                }
                positions.push_back({next.position.x,next.position.y,next.position.z});
                auto position=unit->GetCP();position.x=next.position.x;position.y=next.position.y;
                ok=world.aiMap.MoveObject(unit,position);unit->navLocation=next;
                unit->SetOrientationXY(CPlot3D(movement.direction.x,movement.direction.y,0));
                if(movement.finished) {
                    if(path.IsPathFinished())done=true;
                    else {ok=world.aiMap.ContinuePath(unit,path);if(ok)startWalker(unit,path);}
                }
                if(ok)failure=done?"none":"tick_limit";
            }
            ok=ok&&done;
            if(beside&&(!avoidAttempts||(!record.value("require_detour",false)&&!waitTicks))){ok=false;failure="avoidance_not_exercised";}
            if(record.value("require_detour",false)&&!avoidTicks){ok=false;failure="standing_detour_not_exercised";}
            if(beside&&avoidAttempts>1){ok=false;failure="unchanged_obstacle_retried";}
            if(ok&&(walkedLength>straightLength*1.05+.003||unplannedTurns)){ok=false;failure="straightness";}
            if(required&&!ok)++requiredFailures;
            auto result=record;
            result["planned_cost"]=decision.plannedCost;result["selected_cost"]=decision.cost;
            result["straight_cost"]=decision.straightCost;result["straightened"]=decision.straightened;
            result["unit_used"]=unit->GetID();result["radius_used"]=Radius(unit);result["height_used"]=Height(unit);
            if(beside){result["neighbour_unit_used"]=neighbour->GetID();result["neighbour_radius_used"]=Radius(neighbour);}
            result["complete"]=ok;result["failure"]=failure;result["ticks"]=ticks;result["wait_ticks"]=waitTicks;result["avoid_ticks"]=avoidTicks;result["avoidance_attempts"]=avoidAttempts;result["detour_max_ms"]=detourMaxMs;result["max_delta"]=maxDelta;
            result["planned_points"]=nlohmann::json::array();for(auto p:plannedPoints)result["planned_points"].push_back({p.x,p.y,p.z});
            result["straight_length"]=straightLength;result["walked_length"]=walkedLength;result["unplanned_turns"]=unplannedTurns;result["positions"]=positions;
            result["finish"]={unit->navLocation.position.x,unit->navLocation.position.y,unit->navLocation.position.z};movementRows.push_back(result);
            world.aiMap.RemoveObject(unit);if(neighbour)world.aiMap.RemoveObject(neighbour);
        }
        world.SetSegmentTimeCounter(oldTime);
        for(const auto& state:saved){world.aiMap.MoveObject(state.unit,state.cp);state.unit->navLocation=state.location;world.aiMap.AddObject(state.unit);}
        std::ofstream(NavigationBake::reportDirectory/(std::filesystem::path(mapName).stem().string()+"--"+std::filesystem::path(mobName).stem().string()+".movement.json"))<<movementRows.dump(2);
        printf("GAME_MOVEMENT_RELEASE routes=%zu required_failures=%u\n",movementRows.size(),requiredFailures);fflush(stdout);
        if(requiredFailures)throw std::runtime_error("Nav64: shared game movement release failed");
        printf("JOURNAL_REPLAY records=%zu file=%s\n",output.size(),file.string().c_str());fflush(stdout);
    }
    baker=std::move(importer);
    return bytes;
}
int NavigationWorld::Type(CUnit* unit,int aiClass) {
    aiClass=std::clamp(aiClass,0,7);
    const float radius=unit?Body(unit).radius:aiClass>=7?1.f:aiClass>=5?.5f:.25f;

    const int type=unit&&unit->GetUserUnitStats()->cLocomotion==3?Navigation::Flying:Navigation::ProfileForRadius(radius,aiClass==0);
    return type;
}
float NavigationWorld::Height(CUnit* unit,int aiClass) {
    const int c=std::clamp(unit?unit->GetAIClass():aiClass,0,7);
    const auto& profile=Agents[Type(unit,c)].profile;
    // Geometry chooses clearance; retain the old class's ceiling as an upper
    // bound, so a taller visual does not lose an already permitted passage.
    const float legacy=Navigation::LegacyClassHeight(unsigned(c));
    const int pose=unit?(unit->GetCurrentPose()==UP_LAY?2:unit->GetCurrentPose()==UP_CRAWL?1:0):0;
    const float height=unit?(std::min)(Body(unit).height[pose],legacy):legacy;
    // A smaller body fits the lowest clearance tier; do not reject it.
    return Navigation::SelectedClearance(profile,height);
}
float NavigationWorld::CollisionRadius(CUnit* unit) {
    // The original circle uses the longer body axis. The baked radius measures
    // transverse passage width and cannot separate long bodies such as boars.
    return (std::max)(Radius(unit),unit->GetCollisionDistance());
}
Navigation::Point NavigationWorld::Position(CUnit* unit,CAIMap& map) const {
    CorePerf::Scope timing(CorePerf::NavigationTotal);
    auto p=GamePoint(unit->GetCP());
    // GetCP.z is the legacy ground-relative altitude, never the world support Z.
    p.z=unit->GetAIClass()==0?map.GetTileSurface(unit->GetCP()):map.GetTileHeight(unit->GetCP());
    if(unit->navLocation.surface>=1 && std::hypot(p.x-unit->navLocation.position.x,p.y-unit->navLocation.position.y)<.1f)p.z=unit->navLocation.position.z;
    return p;
}
bool NavigationWorld::ObserveAir(CUnit* unit,AirEvent event,unsigned& suppressed) {
    if(!unit||ModCliTool::CurrentKind()!=ModCliTool::Kind::None)return false;
    const auto locomotion=unit->GetUserUnitStats()->cLocomotion;
    if(locomotion!=2&&locomotion!=3)return false;
    auto& entry=airJournal[unit->GetID()][size_t(event)];const auto now=GetTickCount64();
    if(now<entry.next){if(entry.suppressed<UINT_MAX)++entry.suppressed;return false;}
    suppressed=entry.suppressed;entry.suppressed=0;entry.next=now+2000;return true;
}
Navigation::Route NavigationWorld::Find(CUnit* unit,Navigation::Point from,Navigation::Point goal,float maxLength,float goalRadius,const float* costs,int aiClass,Navigation::GoalPolicy goalPolicy,const Navigation::Location* goalSupport) {
    CorePerf::Scope total(CorePerf::NavigationTotal),timing(CorePerf::NavigationPath);
    const auto boundary=ModArena::MovementBoundary(unit);
    if(!boundary.Goal(from,goal)){Navigation::Route route;route.status=Status::NoPath;return route;}
    if(ModArena::Charge(unit)) {
        Navigation::Route route;route.status=Status::NoPath;
        const float ex=ModEncounter::Read(theWorldServer,"FGL.EX"),ey=ModEncounter::Read(theWorldServer,"FGL.EY");
        if(std::hypot(goal.x-ex,goal.y-ey)>=.1f)return route;
        Navigation::StraightRequest request;request.source=from;request.distance=std::hypot(ex-from.x,ey-from.y);
        if(request.distance<.001f)return route;
        request.direction={(ex-from.x)/request.distance,(ey-from.y)/request.distance,0};
        request.bodyRadius=(std::max)(.95f,unit->GetCollisionDistance());request.boundaryInset=request.bodyRadius+.16f;request.bodies=ModArena::ChargeBodies();
        auto trace=TraceStraight(unit,request);route.status=trace.status;
        if(trace.status!=Status::Complete)return route;
        if(std::hypot(trace.endpoint.position.x-ex,trace.endpoint.position.y-ey)>.02f){route.status=Status::NoPath;ModArena::Blocked(boundary,trace.endpoint.position,3);return route;}
        route.points=std::move(trace.points);route.length=request.distance;route.cost=route.plannedCost=route.length;route.sampledTail=0;return route;
    }
    auto* mesh=ForUnit(unit,aiClass);if(!mesh){
        Navigation::Route route;route.status=Navigation::Status::NoMesh;
        if(unit&&unit->IsHero())GameJournal::Event("NAV","query_failed","unit=%d reason=no_mesh from=(%.3f,%.3f,%.3f) goal=(%.3f,%.3f,%.3f) detail=%s",unit->GetID(),from.x,from.y,from.z,goal.x,goal.y,goal.z,GameJournal::Quote(error.c_str()).c_str());
        return route;
    }
    if(!dirtyObjects.empty()){Navigation::Route route;route.status=Status::Updating;return route;}
    Navigation::Request request;request.goalPolicy=goalPolicy;request.start=unit?&unit->navLocation:nullptr;request.source=from;request.goal=goal;request.maxLength=maxLength;request.goalRadius=goalRadius;request.height=Height(unit,aiClass);
    request.goalSupport=goalSupport;
    request.reuseRoute=queryReuse;
    request.excludedFlags=static_cast<unsigned short>(1u<<(std::clamp(unit?unit->GetAIClass():aiClass,0,7)+4));
    request.obstacles=queryObstacles;
    std::vector<Navigation::Obstacle> arenaObstacles;
    if(boundary.seal&&std::hypot(from.x-boundary.cx,from.y-boundary.cy)>=boundary.radius){
        if(queryObstacles)arenaObstacles=*queryObstacles;
        arenaObstacles.push_back({{boundary.cx,boundary.cy,from.z},boundary.radius,10000.f,1000105});
        request.obstacles=&arenaObstacles;
    }
    if(costs&&mesh->profile.movement==Navigation::Movement::Ground)std::copy(costs,costs+16,request.areaCosts);
    auto route=Navigation::Find(*mesh,request);
    if(route.status==Status::Complete&&!boundary.Route(route,from)){
        route.status=Status::NoPath;route.points.clear();route.corridor.clear();return route;
    }
    if(!sourceDirty.empty()) {
        const int profile=Type(unit,unit?unit->GetAIClass():aiClass);
        if(Pending(profile)) {
        bool stale=route.status!=Status::Complete||Pending(profile,from,from)||Pending(profile,goal,goal);
        if(!stale)for(auto ref:route.corridor) {
            const dtMeshTile* tile=nullptr;const dtPoly* polygon=nullptr;
            if(dtStatusFailed(mesh->data->getTileAndPolyByRef(ref,&tile,&polygon))||sourceDirty.count(std::make_tuple(profile,tile->header->x,tile->header->y))){stale=true;break;}
        }
        if(stale){route.status=Status::Updating;route.points.clear();route.corridor.clear();return route;}
        }
    }
    unsigned suppressed=0;
    if(ObserveAir(unit,AirEvent::Query,suppressed)) {
        const auto end=route.points.empty()?goal:route.points.back().position;
        GameJournal::Event("NAV","air_query","unit=%d result=%s profile=%s locomotion=%u class=%d pose=%d height=%.3f altitude=%.3f from=(%.3f,%.3f,%.3f) goal=(%.3f,%.3f,%.3f) end=(%.3f,%.3f,%.3f) points=%zu length=%.3f suppressed=%u",unit->GetID(),StatusName(route.status),mesh->profileId.c_str(),unsigned(unit->GetUserUnitStats()->cLocomotion),unit->GetAIClass(),int(unit->GetCurrentPose()),request.height,unit->GetCP().z,from.x,from.y,from.z,goal.x,goal.y,goal.z,end.x,end.y,end.z,route.points.size(),route.length,suppressed);
    }
    if(route.status==Status::Complete&&route.goalStatus!=Status::Complete&&unit&&unit->IsHero()&&!route.points.empty()) {
        const auto at=route.points.back().position;
        GameJournal::Event("NAV","goal_adjusted","unit=%d reason=%s requested=(%.3f,%.3f,%.3f) accepted=(%.3f,%.3f,%.3f) offset_xy=%.3f profile=%s height=%.3f",unit->GetID(),StatusName(route.goalStatus),goal.x,goal.y,goal.z,at.x,at.y,at.z,std::hypot(at.x-goal.x,at.y-goal.y),mesh->profileId.c_str(),request.height);
    }
    // Hero failures remain observable after NPCs exhaust the background quota.
    if(route.status!=Navigation::Status::Complete&&((unit&&unit->IsHero())||failures++<16))
        GameJournal::Event("NAV","query_failed","unit=%d reason=%s from=(%.3f,%.3f,%.3f) goal=(%.3f,%.3f,%.3f) profile=%s class=%d pose=%d height=%.3f goal_radius=%.3f max_length=%.3f route_length=%.3f start_surface=%llu start_polygon=%llu mesh_generation=%llu obstacles=%zu",unit?unit->GetID():-1,Navigation::StatusName(route.status),from.x,from.y,from.z,goal.x,goal.y,goal.z,mesh->profileId.c_str(),unit?unit->GetAIClass():aiClass,unit?int(unit->GetCurrentPose()):-1,request.height,goalRadius,maxLength,route.length,static_cast<unsigned long long>(unit?unit->navLocation.surface:0),static_cast<unsigned long long>(unit?unit->navLocation.polygon:0),static_cast<unsigned long long>(mesh->generation),queryObstacles?queryObstacles->size():0);
    return route;
}
bool NavigationWorld::Goal(CUnit* unit,Navigation::Point reference,Navigation::Location& result) {
    auto* mesh=ForUnit(unit);if(!mesh||!mesh->sources)return false;
    // An untyped AI point has no chosen floor. Resolve original world support
    // across the source height range, preferring the origin's floor by height.
    const float vertical=mesh->sources->maximum.y-mesh->sources->minimum.y+4.f;
    return Navigation::Project(*mesh,reference,.15f,vertical,result,Height(unit),Navigation::ExcludeClass(unit->GetAIClass()));
}
bool NavigationWorld::Support(CUnit* unit,Navigation::Point point,Navigation::Location& result) {
    auto* mesh=ForUnit(unit);
    return mesh&&((dirtyObjects.empty()&&sourceDirty.empty())||!Pending(Type(unit,unit?unit->GetAIClass():3),point,point))&&Navigation::Project(*mesh,point,.001f,4.f,result,Height(unit),Navigation::ExcludeClass(unit?unit->GetAIClass():3));
}
bool NavigationWorld::AttackPosition(CUnit* unit,CUnit* target,Navigation::Point goal,float reach,float maxLength,Navigation::Location& result) {
    auto& map=static_cast<CWorldServer*>(unit->GetWorld())->aiMap;
    const auto from=Position(unit,map),center=Position(target,map);
    const float inner=CollisionRadius(unit)+CollisionRadius(target)+.01f;
    const float outer=reach-.01f;
    if(outer<inner)return false;
    // Original FindPathDestination order: toward the attacker, then both sides
    // in 45-degree steps. Each candidate must be free and reachable on Nav64.
    const float bearing=std::atan2(from.y-goal.y,from.x-goal.x);
    const int directions[]={0,1,-1,2,-2,3,-3,4};
    const int rings=(std::max)(1,int(std::ceil((outer-inner)/AITile2Meter(1.f))));
    for(int ring=0;ring<=rings;++ring) {
        const float radius=outer-(outer-inner)*float(ring)/float(rings);
        for(int direction:directions) {
            const float angle=bearing+float(direction)*FP_PI/4;
            const Point point{goal.x+radius*std::cos(angle),goal.y+radius*std::sin(angle),center.z};
            Location candidate;
            if(!Support(unit,point,candidate))continue;
            const CPlot3D position(candidate.position.x,candidate.position.y,unit->GetCP().z);
            // Escaping a loaded overlap may cross its boundary, but its
            // destination must be completely outside every other body.
            if(map.CheckCircleCollision(unit,position,&candidate,false))continue;
            CAIRoute route;
            if(!map.NavRoute(unit,from,candidate.position,route,maxLength,0,unit->GetAIClass(),nullptr,GoalPolicy::Exact,nullptr,&candidate))continue;
            result=candidate;return true;
        }
    }
    return false;
}
bool NavigationWorld::CanPlace(CUnit* unit,Navigation::Point point,int aiClass) {
    CorePerf::Scope total(CorePerf::NavigationTotal),timing(CorePerf::NavigationPlace);
    auto* mesh=ForUnit(unit,aiClass);if(!mesh||((!dirtyObjects.empty()||!sourceDirty.empty())&&Pending(Type(unit,unit?unit->GetAIClass():aiClass),point,point)))return false;
    if(Navigation::Levitates(mesh->profile)&&unit)point.z=static_cast<CWorldServer*>(unit->GetWorld())->aiMap.GetTileSurface(CPlot3D(point.x,point.y,0));
    // A pose must fit at the actual point; projecting it by 5 cm can admit a
    // taller pose beside a crown, then leave the new route outside its mesh.
    Navigation::Location location;return Navigation::Project(*mesh,point,.001f,4.f,location,Height(unit,aiClass),static_cast<unsigned short>(1u<<(std::clamp(unit?unit->GetAIClass():aiClass,0,7)+4)));
}
bool NavigationWorld::Step(CUnit* unit,Navigation::Point from,Navigation::Point desired,Navigation::Location& result,const CAIRoute* route) {
    CorePerf::Scope total(CorePerf::NavigationTotal),timing(CorePerf::NavigationStep);
    const auto boundary=ModArena::MovementBoundary(unit);
    if(boundary.Block(from,desired))return false;
    auto* mesh=ForUnit(unit);
    const char* reason=mesh?"clearance":"no_mesh";
    struct Corners final:Navigation::PathView {
        const CAIRoute* route;POSITION cursor=nullptr;
        explicit Corners(const CAIRoute* r):route(r){}
        void Reset() override {cursor=route?route->route.GetHeadPosition():nullptr;}
        bool Next(Point& p) override {
            if(!cursor)return false;const auto& node=route->route.GetNext(cursor);
            p={AITile2Meter(node.point.x),AITile2Meter(node.point.y),node.supportZ};return true;
        }
    } corners(route);
    const auto* address=unit?&unit->navLocation:nullptr;
    const bool accepted=mesh&&((dirtyObjects.empty()&&sourceDirty.empty())||!Pending(Type(unit,unit->GetAIClass()),from,desired))&&(route?Navigation::Follow(*mesh,from,desired,corners,result,Height(unit),&reason,address,static_cast<unsigned short>(1u<<(unit->GetAIClass()+4))):Navigation::Constrain(*mesh,from,desired,result,Height(unit),&reason,address,static_cast<unsigned short>(1u<<(unit->GetAIClass()+4))));
    if(!accepted) {
        const auto now=GetTickCount64();
        if((unit&&unit->IsHero())||stepFailures++<16||now>=nextStepLog) {
            nextStepLog=now+2000;
            GameJournal::Event("NAV","step_rejected","unit=%d reason=%s from=(%.3f,%.3f,%.3f) desired=(%.3f,%.3f,%.3f) profile=%s height=%.3f pose=%d path_points=%zu start_polygon=%llu mesh_generation=%llu",unit?unit->GetID():-1,reason,from.x,from.y,from.z,desired.x,desired.y,desired.z,mesh?mesh->profileId.c_str():"none",mesh?Height(unit):0.f,unit?int(unit->GetCurrentPose()):-1,size_t(route?route->route.GetCount():0),static_cast<unsigned long long>(address?address->polygon:0),static_cast<unsigned long long>(mesh?mesh->generation:0));
        }
        return false;
    }
    if(ModArena::Charge(unit)&&std::hypot(result.position.x-desired.x,result.position.y-desired.y)>.001f)return false;
    return !boundary.Block(from,result.position);
}
Navigation::StraightResult NavigationWorld::TraceStraight(CUnit* unit,const Navigation::StraightRequest& request) {
    Navigation::StraightResult result;
    if(!unit||!Finite(request.source)||!Finite(request.direction)||!std::isfinite(request.distance)||request.distance<0||request.distance>100||
       !std::isfinite(request.bodyRadius)||request.bodyRadius<=0||std::fabs(std::hypot(request.direction.x,request.direction.y)-1)>.001f)return result;
    auto* mesh=ForUnit(unit);if(!mesh){result.status=Status::NoMesh;return result;}
    const auto end=Point{request.source.x+request.direction.x*request.distance,request.source.y+request.direction.y*request.distance,request.source.z};
    if(Pending(Type(unit,unit->GetAIClass()),request.source,end)){result.status=Status::Updating;return result;}
    Location current;const auto flags=Navigation::ExcludeClass(unit->GetAIClass());
    if(!StartLocation(*mesh,request.source,current,Height(unit),flags)){result.status=Status::NoStart;return result;}
    result.endpoint=current;result.points.push_back(current);result.status=Status::Complete;
    const int count=(std::max)(1,int(std::ceil(request.distance/.08f)));
    const float extra=(std::max)(0.f,request.bodyRadius-mesh->profile.radius);
    auto& map=static_cast<CWorldServer*>(unit->GetWorld())->aiMap;
    for(int i=1;i<=count;++i) {
        const float distance=request.distance*float(i)/count;
        Point wanted{request.source.x+request.direction.x*distance,request.source.y+request.direction.y*distance,current.position.z};
        const float r=request.boundaryInset;
        if(wanted.x<request.left+r||wanted.x>request.right-r||wanted.y<request.bottom+r||wanted.y>request.top-r)break;
        Location next;
        if(!Constrain(*mesh,current.position,wanted,next,Height(unit),nullptr,&current,flags)||std::hypot(next.position.x-wanted.x,next.position.y-wanted.y)>.001f)break;
        bool clear=true;
        for(float side:{-extra,extra})if(extra>.001f) {
            const Point a{current.position.x-request.direction.y*side,current.position.y+request.direction.x*side,current.position.z};
            const Point b{wanted.x-request.direction.y*side,wanted.y+request.direction.x*side,next.position.z};
            Location at,edge;
            if(!StartLocation(*mesh,a,at,Height(unit),flags)||!Constrain(*mesh,a,b,edge,Height(unit),nullptr,&at,flags)||
               std::hypot(edge.position.x-b.x,edge.position.y-b.y)>.001f){clear=false;break;}
        }
        if(!clear||map.CheckCircleCollision(unit,CPlot3D(wanted.x,wanted.y,unit->GetCP().z),&next,false,&request.bodies))break;
        result.points.push_back(next);result.endpoint=current=next;
    }
    return result;
}
bool NavigationWorld::GuardTurn(CUnit* unit,SAIPath& path,float time,bool& pending) {
    pending=false;auto* mesh=ForUnit(unit);if(!mesh)return false;
    auto& map=static_cast<CWorldServer*>(unit->GetWorld())->aiMap;
    const auto from=Position(unit,map);auto current=unit->navLocation;current.position=from;
    float speed=0,angular=0;unit->GetSpeeds(speed,angular);
    if(!std::isfinite(speed)||speed<=0)return false;
    const float ahead=1.f+(Radius(unit)+.15f)*2.f/speed;
    const int samples=std::clamp(int(std::ceil(ahead*speed/.16f)),2,128);
    const int type=Type(unit,unit->GetAIClass());
    for(int i=0;i<=samples;++i) {
        CPlot3D point(Meter2AITile(from.x),Meter2AITile(from.y),0),direction;
        bool rotating=false;float v=0,z=from.z;int x=-1,y=-1;
        const bool active=unit->walker.GetPosition(unit->GetWorldBase(),path.curSegment,time+ahead*i/samples,rotating,point,direction,v,x,y,&z);
        const Point desired{AITile2Meter(point.x),AITile2Meter(point.y),z};
        if(!Finite(desired)||Distance(current.position,desired)>4.f)return false;
        if((!dirtyObjects.empty()||!sourceDirty.empty())&&Pending(type,current.position,desired)){pending=true;return true;}
        Location next;
        // Exact constrained sweep, with no funnel, nearest-floor search or detour.
        if(!Constrain(*mesh,current.position,desired,next,Height(unit),nullptr,&current,ExcludeClass(unit->GetAIClass()))||
           std::hypot(next.position.x-desired.x,next.position.y-desired.y)>.02f||
           std::abs(next.position.z-desired.z)>mesh->profile.climb+.001f)return false;
        current=next;if(!active)break;
    }
    return true;
}
bool NavigationWorld::Move(CUnit* unit,SAIPath& path,float time,unsigned& wait,Movement& move) {
    auto& world=*static_cast<CWorldServer*>(unit->GetWorld());auto& map=world.aiMap;
    auto& segment=path.curSegment;
    const auto from=Position(unit,map);
    auto waiting=[&](){move.waiting=true;move.finished=false;move.support=unit->navLocation;move.support.position=from;move.desired=from;move.speed=0;wait=0;return true;};
    if(path.navigationPending||(segment.IsMoving()&&path.navigationGeneration!=generation)) {
        auto* mesh=ForUnit(unit);if(!mesh)return false;
        const int profile=Type(unit,unit->GetAIClass());
        if(Pending(profile,from,from)||(path.navigationPending&&Pending(profile)))return waiting();
        // Rebind at the same XY and height. Never place a moving unit on a
        // neighbouring floor just because its old polygon was retired.
        Location support;
        if(!StartLocation(*mesh,from,support,Height(unit),Navigation::ExcludeClass(unit->GetAIClass()))||
           std::hypot(support.position.x-from.x,support.position.y-from.y)>.02f||std::abs(support.position.z-from.z)>mesh->profile.climb+.001f)return false;
        unit->navLocation=support;
        if(path.navigationTurned)path.navigationGeneration=generation;
        else {
        const auto goal=path.ptDestination;const auto goalSupport=path.navigationGoal;
        if(!map.NewPath(unit,goal,path,path.navigationMaxLength,path.navigationMinDistance,path.navigationPolicy,nullptr,&goalSupport))return false;
        if(path.navigationPending)return waiting();
        float speed=0,angular=0;unit->GetSpeeds(speed,angular);unit->walker.SetSpeeds(speed,angular);unit->walker.CreateWalkerRoute(path.curSegment);
        move.replanned=true;move.support=unit->navLocation;move.desired=from;wait=0;queues.erase(unit->GetID());return true;
        }
    }
    if(path.navigationTurned) {
        bool pending=false;
        if(!GuardTurn(unit,path,time,pending))return false;
        if(pending)return waiting();
    }
    if(!wait)queues.erase(unit->GetID());
    auto pos=unit->GetCP();pos.x=Meter2AITile(pos.x);pos.y=Meter2AITile(pos.y);
    CPlot3D direction;int x=-1,y=-1;float z=0;
    move.finished=!unit->walker.GetPosition(unit->GetWorldBase(),segment,time,move.rotating,pos,direction,move.speed,x,y,&z);
    auto observed=[&](bool accepted) {
        unsigned suppressed=0;
        if(ObserveAir(unit,AirEvent::Step,suppressed))
            GameJournal::Event("NAV","air_step","unit=%d accepted=%d moving_segment=%d rotating=%d finished=%d waiting=%d replanned=%d blocker=%d speed=%.6f from=(%.3f,%.3f,%.3f) desired=(%.3f,%.3f,%.3f) support=(%.3f,%.3f,%.3f) suppressed=%u",unit->GetID(),int(accepted),int(segment.IsMoving()),int(move.rotating),int(move.finished),int(move.waiting),int(move.replanned),move.blocker,move.speed,from.x,from.y,from.z,move.desired.x,move.desired.y,move.desired.z,move.support.position.x,move.support.position.y,move.support.position.z,suppressed);
        return accepted;
    };
    move.desired={AITile2Meter(pos.x),AITile2Meter(pos.y),segment.IsMoving()?z:from.z};
    move.direction={direction.x,direction.y,0};
    const auto boundary=ModArena::MovementBoundary(unit);
    if(segment.IsMoving()&&boundary.Active()){
        auto walker=unit->walker;auto previous=from;
        for(int i=0;i<=5;++i){
            CPlot3D sample(Meter2AITile(from.x),Meter2AITile(from.y),0),axis;
            bool rotating=false;float speed=0,height=from.z;int sx=-1,sy=-1;
            const bool active=walker.GetPosition(unit->GetWorldBase(),segment,time+1.25f*i/5,rotating,sample,axis,speed,sx,sy,&height);
            const Point at{AITile2Meter(sample.x),AITile2Meter(sample.y),height};
            const int reason=boundary.Block(previous,at);
            if(reason){move.boundaryBlocked=true;ModArena::Blocked(boundary,at,reason);return observed(false);}
            previous=at;if(!active)break;
        }
    }
    // Script/patrol turns do not move the body. A clearance projection must
    // neither reject them nor replace their heading with a corridor tangent.
    if(!segment.IsMoving()) {
        move.desired=from;move.support=unit->navLocation;
        move.support.position=from;wait=0;queues.erase(unit->GetID());
        return observed(true);
    }
    if((!dirtyObjects.empty()||!sourceDirty.empty())&&Pending(Type(unit,unit->GetAIClass()),from,move.desired))return waiting();
    if(!Step(unit,from,move.desired,move.support,&segment.route)){
        move.boundaryBlocked=boundary.Active();ModArena::Blocked(boundary,move.desired,3);return observed(false);
    }
    pos.x=move.support.position.x;pos.y=move.support.position.y;
    if(std::hypot(pos.x-from.x,pos.y-from.y)<.00001f)return observed(true);
    pos.z=move.support.position.z;
    const auto chargeBodies=ModArena::Charge(unit)?ModArena::ChargeBodies():Navigation::BodyPolicy{};
    auto* neighbour=map.CheckCircleCollision(unit,pos,&move.support,true,ModArena::Charge(unit)?&chargeBodies:nullptr);
    if(neighbour&&ModArena::Charge(unit)){move.blocker=neighbour->GetID();ModArena::Blocked(boundary,move.desired,4);return observed(false);}
    if(neighbour) {
        if(path.navigationTurned){move.blocker=neighbour->GetID();return observed(false);}
        wait=(std::min)(wait+1,1000000u);
        auto& queue=queues[unit->GetID()];
        const Point goal{path.ptDestination.x,path.ptDestination.y,path.ptDestination.z};
        if(Distance(queue.goal,goal)>.001f){queue={goal,neighbour->GetID()};wait=1;}
        else queue.blocker=neighbour->GetID();
        // A changing neighbour must not restart the bounded wait forever.
        // Its movement flag is not proof that this body can make progress.
        // A stationary body cannot clear the corridor by waiting. Moving
        // neighbours keep the bounded queue, including mutual congestion.
        if((!neighbour->IsMoving()||wait>=27)&&!move.rotating) {
            ++move.avoidanceAttempts;
            const auto start=std::chrono::steady_clock::now();
            std::vector<Navigation::Obstacle> blockers;CUnitList nearby;
            map.GetUnitsNear(CPlot3D(from.x,from.y,from.z),nearby,24.f);
            for(POSITION i=nearby.GetHeadPosition();i;) {auto* other=nearby.GetNext(i);
                if(other==unit||other->IsDead())continue;
                const auto at=Position(other,map);
                if(std::hypot(at.x-from.x,at.y-from.y)>24.f)continue;
                blockers.push_back({at,CollisionRadius(other)+CollisionRadius(unit),Height(other),other->GetID()});
            }
            if(ModCliTool::CurrentKind()==ModCliTool::Kind::NavBake)for(const auto& obstacle:blockers)
                printf("DETOUR_INPUT unit=%d from=(%.6f,%.6f,%.6f) obstacle=%d at=(%.6f,%.6f,%.6f) radius=%.6f start_distance=%.6f\n",unit->GetID(),from.x,from.y,from.z,obstacle.id,obstacle.position.x,obstacle.position.y,obstacle.position.z,obstacle.radius,std::hypot(from.x-obstacle.position.x,from.y-obstacle.position.y));
            SAIPath alternate;
            struct QueryScope {
                NavigationWorld& owner;
                ~QueryScope(){owner.queryObstacles=nullptr;owner.queryReuse=nullptr;}
            } scope{*this};
            Navigation::Route reused;reused.status=Navigation::Status::Complete;reused.sampledTail=0;
            auto append=[&](const CAIRoute& route){for(POSITION i=route.route.GetHeadPosition();i;){const auto& n=route.route.GetNext(i);Navigation::Location at;at.position={AITile2Meter(n.point.x),AITile2Meter(n.point.y),n.supportZ};at.area=n.terrain;if(reused.points.empty()||Distance(reused.points.back().position,at.position)>.00001f)reused.points.push_back(at);}};
            append(path.curSegment.route);append(path.toGo);
            size_t first=1;float nearest=FLT_MAX;
            for(size_t i=1;i<reused.points.size();++i) {
                const auto a=reused.points[i-1].position,b=reused.points[i].position;const float dx=b.x-a.x,dy=b.y-a.y,d=dx*dx+dy*dy;
                const float f=d>0?std::clamp(((from.x-a.x)*dx+(from.y-a.y)*dy)/d,0.f,1.f):0;
                const float score=std::hypot(from.x-a.x-dx*f,from.y-a.y-dy*f)+std::abs(from.z-a.z-(b.z-a.z)*f);
                if(score<nearest){nearest=score;first=i;}
            }
            if(!reused.points.empty())reused.points.erase(reused.points.begin(),reused.points.begin()+(std::min)(first,reused.points.size()));
            reused.points.insert(reused.points.begin(),unit->navLocation);reused.points.front().position=from;
            queryObstacles=&blockers;queryReuse=&reused;
            Navigation::Route detour;const auto destination=reused.points.back();
            const bool found=reused.points.size()>1&&map.NewPath(unit,path.ptDestination,alternate,1.e6f,0,Navigation::GoalPolicy::Exact,&detour,&destination);
            move.detourMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            GameJournal::Event("NAV","queue_detour","unit=%d blocker=%d result=%s reason=%s wait=%u blocker_moving=%d distance=%.3f body_radius=%.3f obstacles=%zu ms=%.3f from=(%.3f,%.3f,%.3f) goal=(%.3f,%.3f)",unit->GetID(),neighbour->GetID(),found?"ready":"stopped",Navigation::StatusName(detour.status),wait,int(neighbour->IsMoving()),std::hypot(from.x-Position(neighbour,map).x,from.y-Position(neighbour,map).y),CollisionRadius(unit)+CollisionRadius(neighbour),blockers.size(),move.detourMs,from.x,from.y,from.z,path.ptDestination.x,path.ptDestination.y);
            if(found) {
                path.curSegment=alternate.curSegment;path.toGo=alternate.toGo;
                path.ptFinish=alternate.ptFinish;path.bFinished=alternate.bFinished;
                path.bRouteFailed=alternate.bRouteFailed;path.nPathCost=alternate.nPathCost;path.navigationGeneration=alternate.navigationGeneration;path.navigationPending=alternate.navigationPending;
                float speed=0,angular=0;unit->GetSpeeds(speed,angular);
                unit->walker.SetSpeeds(speed,angular);unit->walker.CreateWalkerRoute(path.curSegment);
                move.replanned=true;move.finished=false;move.support=unit->navLocation;
                wait=0;queues.erase(unit->GetID());return observed(true);
            }
            // An impossible detour is a failed movement command. Let its owner
            // choose the next action instead of freezing an unchanged queue.
            move.blocker=neighbour->GetID();queues.erase(unit->GetID());return observed(false);
        }
        move.waiting=true;move.blocker=neighbour->GetID();return observed(true);
    }
    wait=0;queues.erase(unit->GetID());
    if(!move.rotating) {
        const float dx=move.support.position.x-from.x,dy=move.support.position.y-from.y,length=std::hypot(dx,dy);
        if(length>.00001f)move.direction={dx/length,dy/length,0};
    }
    return observed(true);
}
bool NavigationWorld::Restore(CUnit* unit,CAIMap& map) {
    CorePerf::Scope total(CorePerf::NavigationTotal),timing(CorePerf::NavigationRestore);
    // Saved navigation is a placement hint, never an admission condition for
    // the world. Place uses saved Z to choose the floor and rebuilds all refs.
    const auto saved=unit->navLocation;const auto cp=unit->GetCP();
    if(!Navigation::Finite(saved.position)||std::hypot(cp.x-saved.position.x,cp.y-saved.position.y)>.1f)
        unit->navLocation={};
    if(Place(unit,map))return true;
    unit->navLocation={};
    GameJournal::Event("NAV","restore_unplaced","unit=%d at=(%.3f,%.3f,%.3f) hint=%llu",unit->GetID(),cp.x,cp.y,saved.position.z,static_cast<unsigned long long>(saved.surface));
    return false;
}

void NavigationWorld::LoadFlying() {
    ++observerRevision;
    auto mesh=Navigation::Load(Read(meshDirectory/Navigation::MeshFilename(mapName,mobName,Flying)),Agents[Flying].profile,mapName,mobName,Agents[Flying].name);
    mesh->sources=sources;mesh->generation=generation;IndexSurfaces(*mesh);meshes[Flying]=std::move(mesh);
    FlushFlight();SyncObjects(true);UpdateSources(true);
}
void NavigationWorld::FlushFlight() {
    if(!flightDeferred||flyingUnits.empty())return;
    Solid box;for(int i=0;i<8;++i)box.corners[i]={(i&1)?flightHigh.x:flightLow.x,(i&2)?flightHigh.y:flightLow.y,(i&4)?flightHigh.z:flightLow.z};
    flightDeferred=false;QueueFlightBox(box);
}
void NavigationWorld::QueueTile(int type,int x,int z) {
    const auto key=std::make_tuple(type,x,z);sourceDirty.insert(key);sourceRetries.erase(key);preparedTiles.erase(key);
    if(sourceJob.valid()&&sourceTask==key)sourceChanged=true;
}
void NavigationWorld::QueueFlightBox(const Solid& box) {
    if(flyingUnits.empty()) {
        for(auto p:box.corners) {
            if(!flightDeferred){flightLow=flightHigh=p;flightDeferred=true;}
            else {flightLow.x=(std::min)(flightLow.x,p.x);flightLow.y=(std::min)(flightLow.y,p.y);flightLow.z=(std::min)(flightLow.z,p.z);
                  flightHigh.x=(std::max)(flightHigh.x,p.x);flightHigh.y=(std::max)(flightHigh.y,p.y);flightHigh.z=(std::max)(flightHigh.z,p.z);}
        }
        return;
    }
    float top=sources->minimum.y;
    for(auto p:box.corners)top=(std::max)(top,p.z);
    const auto* params=meshes[0]->data->getParams();const float width=params->tileWidth;
    // Include the entire possible change of the propagated approach ramp.
    const float halo=(std::max)(0.f,top+.5f-sources->minimum.y)+Agents[Flying].profile.radius+3.3f;
    float lx=FLT_MAX,lz=FLT_MAX,hx=-FLT_MAX,hz=-FLT_MAX;
    for(auto p:box.corners){p=ToNav(p);lx=(std::min)(lx,p.x);lz=(std::min)(lz,p.z);hx=(std::max)(hx,p.x);hz=(std::max)(hz,p.z);}
    const int nx=int(std::ceil((sources->maximum.x-sources->minimum.x)/width)),nz=int(std::ceil((sources->maximum.z-sources->minimum.z)/width));
    const int x0=(std::max)(0,int(std::floor((lx-halo-params->orig[0])/width))),x1=(std::min)(nx-1,int(std::floor((hx+halo-params->orig[0])/width)));
    const int z0=(std::max)(0,int(std::floor((lz-halo-params->orig[2])/width))),z1=(std::min)(nz-1,int(std::floor((hz+halo-params->orig[2])/width)));
    for(int z=z0;z<=z1;++z)for(int x=x0;x<=x1;++x)QueueTile(Flying,x,z);
}
void NavigationWorld::QueueBox(const Solid& box) {
    const auto* params=meshes[0]->data->getParams();const float width=params->tileWidth,border=1.3f;
    float lowX=FLT_MAX,lowZ=FLT_MAX,highX=-FLT_MAX,highZ=-FLT_MAX;
    for(auto corner:box.corners){const auto p=ToNav(corner);lowX=(std::min)(lowX,p.x);lowZ=(std::min)(lowZ,p.z);highX=(std::max)(highX,p.x);highZ=(std::max)(highZ,p.z);}
    const int nx=int(std::ceil((sources->maximum.x-sources->minimum.x)/width)),nz=int(std::ceil((sources->maximum.z-sources->minimum.z)/width));
    const int x0=(std::max)(0,int(std::floor((lowX-border-params->orig[0])/width))),x1=(std::min)(nx-1,int(std::floor((highX+border-params->orig[0])/width)));
    const int z0=(std::max)(0,int(std::floor((lowZ-border-params->orig[2])/width))),z1=(std::min)(nz-1,int(std::floor((highZ+border-params->orig[2])/width)));
    for(int z=z0;z<=z1;++z)for(int x=x0;x<=x1;++x)for(int type=0;type<AgentCount;++type)if(type!=Flying)QueueTile(type,x,z);
    QueueFlightBox(box);
}
void NavigationWorld::SyncObjects(bool initial) {
    if(!loaded)return;
    if(!dirtyObjects.empty())++observerRevision;
    const auto start=std::chrono::steady_clock::now();
    auto same=[](const auto& a,const auto& b,int id) {
        auto old=a.find(id),live=b.find(id);
        if(old==a.end()||live==b.end())return old==a.end()&&live==b.end();
        if(old->second.size()!=live->second.size())return false;
        for(size_t i=0;i<old->second.size();++i)for(size_t j=0;j<8;++j)
            if(Distance(old->second[i].corners[j],live->second[i].corners[j])>.00001f)return false;
        return true;
    };
    while(!dirtyObjects.empty()) {
        const int id=*dirtyObjects.begin();
        if(!same(appliedFlightObjects,liveFlightObjects,id)) {
            if(appliedFlightObjects.count(id))for(const auto& box:appliedFlightObjects.at(id))QueueFlightBox(box);
            if(liveFlightObjects.count(id))for(const auto& box:liveFlightObjects.at(id))QueueFlightBox(box);
            if(liveFlightObjects.count(id))appliedFlightObjects[id]=liveFlightObjects.at(id);else appliedFlightObjects.erase(id);
        }
        const auto door=std::find_if(sources->doors.begin(),sources->doors.end(),[&](const Door& d){return d.object==id;});
        if(door!=sources->doors.end()) {
            // The aperture belongs to the original opening. A displaced gate
            // opens that aperture and contributes its current physical boxes.
            const bool displaced=liveTransforms.count(id)&&sourceTransforms.count(id)&&!liveTransforms.at(id).Same(sourceTransforms.at(id));
            const bool open=displaced||(liveDoors.count(id)?(liveDoors.at(id)<0||liveDoors.at(id)==door->openState):!liveObjects.count(id));
            bool doorChanged=false;
            for(auto& mesh:meshes)if(mesh) {
                auto state=mesh->doorOpen.find(id);
                if(state==mesh->doorOpen.end()||state->second!=open){DoorState(*mesh,id,open);doorChanged=true;}
            }
            if(doorChanged)++generation;
            if(!displaced) {
                if(appliedObjects.count(id)){for(const auto& box:appliedObjects.at(id))QueueBox(box);appliedObjects.erase(id);}
                dirtyObjects.erase(id);continue;
            }
        }
        if(!Dynamic(id)&&door==sources->doors.end()) {
            const bool absent=!liveObjects.count(id);
            const bool presenceChanged=absent?absentStatic.insert(id).second:absentStatic.erase(id)!=0;
            bool moved=false;
            if(!absent) {
                if(!sourceTransforms.count(id))throw std::runtime_error("Nav64: missing baked object source transform NID="+std::to_string(id));
                const auto previous=appliedTransforms.find(id);
                moved=previous==appliedTransforms.end()||!previous->second.Same(liveTransforms.at(id));
            }
            if(presenceChanged||moved) {
                for(const auto& tile:sources->objectTiles[id])for(int type=0;type<AgentCount;++type)if(type!=Flying)QueueTile(type,tile.first,tile.second);
                auto queueFlight=[&](const Geometry& geometry){for(const auto& box:geometry.flightBoxes)if(box.object==id) {
                    Solid bounds;for(int i=0;i<8;++i)bounds.corners[i]={(i&1)?box.maximum.x:box.minimum.x,(i&2)?box.maximum.y:box.minimum.y,(i&4)?box.maximum.z:box.minimum.z};QueueFlightBox(bounds);
                }};
                queueFlight(*sources);
                if(moved) {
                    auto changed=std::make_shared<Geometry>(*sources);
                    TransformSource(*changed,*originalSources,id,sourceTransforms.at(id),liveTransforms.at(id));
                    IndexSources(*changed,sources->tileWidth);sources=std::move(changed);
                    appliedTransforms[id]=liveTransforms.at(id);
                    for(const auto& tile:sources->objectTiles[id])for(int type=0;type<AgentCount;++type)if(type!=Flying)QueueTile(type,tile.first,tile.second);
                    queueFlight(*sources);
                }
                GameJournal::Event("NAV","static_changed","NID=%d absent=%d transformed=%d queued_profile_tiles=%zu",id,int(absent),int(moved),sourceDirty.size());
            }
        } else if(!same(appliedObjects,liveObjects,id)) {
            if(appliedObjects.count(id))for(const auto& box:appliedObjects.at(id))QueueBox(box);
            if(liveObjects.count(id)){for(const auto& box:liveObjects.at(id))QueueBox(box);appliedObjects[id]=liveObjects.at(id);}
            else appliedObjects.erase(id);
        }
        dirtyObjects.erase(id);
        if(!initial&&std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()>=2)break;
    }
}
bool NavigationWorld::Pending(int profile) const {
    if(!dirtyObjects.empty())return true;
    const auto i=sourceDirty.lower_bound(std::make_tuple(profile,INT_MIN,INT_MIN));
    return i!=sourceDirty.end()&&std::get<0>(*i)==profile;
}
bool NavigationWorld::Pending(int profile,Point from,Point to) const {
    if(!dirtyObjects.empty())return true;
    if(!Pending(profile)||!meshes[profile])return false;
    const auto* params=meshes[profile]->data->getParams();const float width=params->tileWidth;
    const auto a=ToNav(from),b=ToNav(to);
    const int x0=int(std::floor(((std::min)(a.x,b.x)-params->orig[0])/width)),x1=int(std::floor(((std::max)(a.x,b.x)-params->orig[0])/width));
    const int z0=int(std::floor(((std::min)(a.z,b.z)-params->orig[2])/width)),z1=int(std::floor(((std::max)(a.z,b.z)-params->orig[2])/width));
    for(int z=z0;z<=z1;++z)for(int x=x0;x<=x1;++x)if(sourceDirty.count(std::make_tuple(profile,x,z)))return true;
    return false;
}
void NavigationWorld::PublishSources() {
    if(preparedTiles.empty())return;
    const auto start=std::chrono::steady_clock::now();size_t count=0;++observerRevision;++generation;
    // No query or unit step interleaves this publication. Keep old support
    // planes until every eligible dirty tile has a replacement for this scene.
    // Pending tiles stay inadmissible even if an apply fails partway through.
    // Already applied tiles must immediately use their matching source planes.
    for(auto& mesh:meshes)if(mesh){mesh->sources=sources;mesh->excludedObjects=absentStatic;}
    for(auto i=preparedTiles.begin();i!=preparedTiles.end();) {
        const auto [type,x,z]=i->first;
        if(type==Flying&&flyingUnits.empty()){i=preparedTiles.erase(i);continue;}
        auto& mesh=*meshes[type];ReplaceTile(mesh,*i->second,x,z);
        for(const auto& door:sources->doors) {
            auto state=mesh.doorOpen.find(door.object);if(state!=mesh.doorOpen.end())DoorState(mesh,door.object,state->second);
        }
        sourceDirty.erase(i->first);sourceRetries.erase(i->first);i=preparedTiles.erase(i);++count;
    }
    for(auto& mesh:meshes)if(mesh){mesh->sources=sources;mesh->excludedObjects=absentStatic;}
    GameJournal::Event("NAV","scene_published","generation=%llu profile_tiles=%zu pending=%zu apply_ms=%.3f",static_cast<unsigned long long>(generation),count,sourceDirty.size(),std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
}
void NavigationWorld::Advance() {
    if(!loaded||(dirtyObjects.empty()&&sourceDirty.empty()&&!sourceJob.valid()))return;
    CorePerf::Scope total(CorePerf::NavigationTotal);
    try{if(!flyingUnits.empty()&&!meshes[Flying])LoadFlying();FlushFlight();if(!dirtyObjects.empty())SyncObjects();UpdateSources(false);ModFinalGolem::CommitCollapse();}catch(const std::exception& e) {error=e.what();GameJournal::Event("NAV","update_failed","reason=%s",GameJournal::Quote(e.what()).c_str());}
}
bool NavigationWorld::Spawn(CUnit* unit,CAIMap& map) {
    auto* mesh=ForUnit(unit);if(!mesh)return false;
    const auto point=Position(unit,map);Navigation::Location location;
    if(!Support(unit,point,location)||
       std::hypot(location.position.x-point.x,location.position.y-point.y)>.001f||
       std::abs(location.position.z-point.z)>mesh->profile.climb+mesh->profile.cellHeight)return false;
    if(map.CheckCircleCollision(unit,unit->GetCP(),&location,false))return false;
    unit->navLocation=location;return true;
}
bool NavigationWorld::Place(CUnit* unit,CAIMap& map) {
    if(!loaded)return false;auto* mesh=ForUnit(unit);if(!mesh)return false;
    auto from=Position(unit,map);if((!dirtyObjects.empty()||!sourceDirty.empty())&&Pending(Type(unit,unit->GetAIClass()),from,from))return false;Navigation::Location location;
    if(!Navigation::ResolveLocation(*mesh,from,&unit->navLocation,location,Height(unit),static_cast<unsigned short>(1u<<(unit->GetAIClass()+4)))) {
        if(failures++<16)GameJournal::Event("NAV","placement_failed","unit=%d profile=%s radius=1",unit->GetID(),mesh->profileId.c_str());
        return false;
    }
    auto cp=unit->GetCP();cp.x=location.position.x;cp.y=location.position.y;
    if(std::hypot(cp.x-from.x,cp.y-from.y)>.001f) {
        if(!map.MoveObject(unit,cp))return false;
        GameJournal::Event("NAV","placement","unit=%d distance=%.3f",unit->GetID(),std::hypot(cp.x-from.x,cp.y-from.y));
    }
    unit->navLocation=location;return true;
}

bool NavigationWorld::RandomGoal(CUnit* unit,Navigation::Point center,float radius,Navigation::Point& goal,Navigation::RandomShape shape) {
    auto* mesh=ForUnit(unit);if(!mesh||((!dirtyObjects.empty()||!sourceDirty.empty())&&Pending(Type(unit,unit->GetAIClass()))))return false;
    auto& map=static_cast<CWorldServer*>(unit->GetWorld())->aiMap;
    const auto from=Position(unit,map);
    const bool found=Navigation::RandomReachable(*mesh,from,center,radius,Height(unit),[](){return ::random.GetFloat(0.f,1.f);},goal,Navigation::ExcludeClass(unit->GetAIClass()),&unit->navLocation,shape);
    unsigned suppressed=0;
    if(ObserveAir(unit,AirEvent::RandomGoal,suppressed)) {
        const auto selected=found?goal:center;
        GameJournal::Event("NAV","air_random_goal","unit=%d found=%d profile=%s from=(%.3f,%.3f,%.3f) center=(%.3f,%.3f,%.3f) radius=%.3f square=%d selected=(%.3f,%.3f,%.3f) suppressed=%u",unit->GetID(),int(found),mesh->profileId.c_str(),from.x,from.y,from.z,center.x,center.y,center.z,radius,int(shape==Navigation::RandomShape::Square),selected.x,selected.y,selected.z,suppressed);
    }
    return found;
}

void NavigationWorld::UpdateSources(bool initial) {
    do {
        if(!dirtyObjects.empty())return;
        const char* stage="worker";
        try {
            if(sourceJob.valid()) {
                if(!initial&&sourceJob.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
                auto replacement=sourceJob.get();const auto [type,x,z]=sourceTask;
                stage="prepare";
                if(!sourceChanged&&(type!=Flying||!flyingUnits.empty())) {
                    preparedTiles[sourceTask]=std::move(replacement);
                }
            }
            const auto now=GetTickCount64();
            const auto next=std::find_if(sourceDirty.begin(),sourceDirty.end(),[&](const auto& key) {
                const int type=std::get<0>(key);
                if(preparedTiles.count(key))return false;
                if(type==Flying&&(flyingUnits.empty()||!meshes[Flying]))return false;
                const auto retry=sourceRetries.find(key);return retry==sourceRetries.end()||now>=retry->second.after;
            });
            if(next==sourceDirty.end()) {
                const bool incomplete=std::any_of(sourceDirty.begin(),sourceDirty.end(),[&](const auto& key){
                    return (std::get<0>(key)!=Flying||(!flyingUnits.empty()&&meshes[Flying]))&&!preparedTiles.count(key);
                });
                if(!incomplete)PublishSources();return;
            }
            sourceTask=*next;sourceChanged=false;stage="snapshot";const auto [type,x,z]=sourceTask;
            auto snapshot=sources;auto absent=absentStatic;auto profile=meshes[type]->profile;
            const auto triangles=sources->tileTriangles[{x,z}],ceilings=sources->tileCeilings[{x,z}];
            std::vector<Solid> boxes,airBoxes;
            if(Flies(profile))for(const auto& entry:appliedFlightObjects)airBoxes.insert(airBoxes.end(),entry.second.begin(),entry.second.end());
            const float width=profile.tileCells*profile.cellSize,border=1.3f;
            const float lowX=sources->minimum.x+x*width-border,highX=lowX+width+border*2;
            const float lowZ=sources->minimum.z+z*width-border,highZ=lowZ+width+border*2;
            for(const auto& entry:appliedObjects)for(const auto& box:entry.second) {
                float a=FLT_MAX,b=FLT_MAX,c=-FLT_MAX,d=-FLT_MAX;
                for(auto p:box.corners){p=ToNav(p);a=(std::min)(a,p.x);b=(std::min)(b,p.z);c=(std::max)(c,p.x);d=(std::max)(d,p.z);}
                if(Flies(profile)||(c>=lowX&&a<=highX&&d>=lowZ&&b<=highZ))boxes.push_back(box);
            }
            sourceJob=std::async(std::launch::async,[snapshot,absent,profile,x,z,triangles,ceilings,boxes=std::move(boxes),airBoxes=std::move(airBoxes)] {
                Geometry input;input.minimum=snapshot->minimum;input.maximum=snapshot->maximum;input.doors=snapshot->doors;input.solids=boxes;
                if(Flies(profile)) {
                    input.flightSolids=airBoxes;
                    for(const auto& t:snapshot->ground)if(!t.object||!absent.count(t.object))input.ground.push_back(t);
                    for(const auto& box:snapshot->flightBoxes)if(!absent.count(box.object))input.flightBoxes.push_back(box);
                } else {
                    for(auto index:triangles){const auto& t=snapshot->ground[index];if(!t.object||!absent.count(t.object))input.ground.push_back(t);}
                    for(auto index:ceilings){const auto& c=snapshot->ceilings[index];if(!absent.count(c.object))input.ceilings.push_back(c);}
                }
                return RebuildTile(input,profile,x,z);
            });
        }catch(const std::exception& e) {
            auto& retry=sourceRetries[sourceTask];++retry.failures;
            retry.after=retry.failures<3?GetTickCount64()+(1000ull<<(retry.failures-1)):~std::uint64_t(0);
            const auto [type,x,z]=sourceTask;error=e.what();
            GameJournal::Event("NAV","update_failed","profile=%d tile=%d,%d stage=%s attempt=%u queued=1 retry=%s reason=%s",type,x,z,stage,retry.failures,retry.failures<3?"delayed":"on_change",GameJournal::Quote(e.what()).c_str());
            if(initial)throw;
            return;
        }
    }while(initial);
}
