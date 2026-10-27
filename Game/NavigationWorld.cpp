#include "StdAfx.h"
#include "NavigationWorld.h"
#include "NavigationBake.h"
#include "NavigationObject.h"
#include "../Navigation/Profiles.h"
#include "aiMap.h"
#include "Unit.h"
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
#include <future>
#include <cstdio>
#include "nlohmann/json.hpp"
#include <regex>
#include "RandomGen.h"
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
void NavigationWorld::Scene(const char* mpr,const char* mob) {
    mapName=Navigation::ResourceName(mpr);mobName=Navigation::ResourceName(mob);
    for(auto& mesh:meshes)mesh.reset();bodySizes.clear();bodyTable.samples.clear();
    loaded=false;error.clear();failures=stepFailures=0;nextStepLog=0;++generation;
    if(sourceJob.valid())sourceJob.get();sources.reset();absentStatic.clear();sourceDirty.clear();liveDoors.clear();
    liveObjects.clear();appliedObjects.clear();sourceTiles.clear();objectTiles.clear();bakedStatic.clear();dirtyObjects.clear();
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
bool NavigationWorld::Load() {
    if(loaded)return true;
    const auto start=std::chrono::steady_clock::now();
    try {
        const auto* mod=ModRuntime::Resources();if(!mod)throw std::runtime_error("Nav64: no loaded mod");
        const auto directory=std::filesystem::path(mod->Directory())/"Maps"/"Nav64";
        const auto bodies=Read(directory/Navigation::BodyFilename);bodyTable=Navigation::LoadBodies(bodies);
        struct Loaded {std::unique_ptr<Navigation::Mesh> mesh;size_t bytes;};
        std::array<std::future<Loaded>,AgentCount> pending;
        for(int type=0;type<AgentCount;++type)pending[type]=std::async(std::launch::async,[&,type]{
            const auto path=directory/Navigation::MeshFilename(mapName,mobName,type);
            try {
                auto bytes=Read(path);const auto size=bytes.size();
                auto mesh=Navigation::Load(std::move(bytes),Agents[type].profile,mapName,mobName,Agents[type].name);
                mesh->generation=generation;
                return Loaded{std::move(mesh),size};
            }catch(const std::exception& e) {
                throw std::runtime_error("Nav64: map "+mapName+" / "+mobName+" profile "+Agents[type].name+": "+e.what()+"; re-bake map in mapper");
            }
        });
        size_t bytes=bodies.size();
        for(int type=0;type<AgentCount;++type){auto result=pending[type].get();bytes+=result.bytes;meshes[type]=std::move(result.mesh);}
        const auto catalogue=Read(directory/(std::filesystem::path(mapName).stem().string()+"--"+std::filesystem::path(mobName).stem().string()+".navworld"));
        const auto json=nlohmann::json::parse(catalogue.begin(),catalogue.end());
        if(json.at("version").get<int>()!=2)throw std::runtime_error("Nav64: incompatible world catalogue");
        for(int id:json.at("static")){bakedStatic.insert(id);dirtyObjects.insert(id);}
        sources=LoadSources(Read(directory/(std::filesystem::path(mapName).stem().string()+"--"+std::filesystem::path(mobName).stem().string()+".navgeom")));
        for(auto& mesh:meshes){mesh->sources=sources;IndexSurfaces(*mesh);}
        const float width=meshes[0]->data->getParams()->tileWidth,border=1.3f;
        const int nx=int(std::ceil((sources->maximum.x-sources->minimum.x)/width)),nz=int(std::ceil((sources->maximum.z-sources->minimum.z)/width));
        for(size_t i=0;i<sources->ground.size();++i) {
            const auto& t=sources->ground[i];auto a=ToNav(t.a),b=ToNav(t.b),c=ToNav(t.c);
            const int x0=(std::max)(0,int(std::floor(((std::min)({a.x,b.x,c.x})-border-sources->minimum.x)/width))),x1=(std::min)(nx-1,int(std::floor(((std::max)({a.x,b.x,c.x})+border-sources->minimum.x)/width)));
            const int z0=(std::max)(0,int(std::floor(((std::min)({a.z,b.z,c.z})-border-sources->minimum.z)/width))),z1=(std::min)(nz-1,int(std::floor(((std::max)({a.z,b.z,c.z})+border-sources->minimum.z)/width)));
            for(int z=z0;z<=z1;++z)for(int x=x0;x<=x1;++x){sourceTiles[{x,z}].push_back(i);if(t.object)objectTiles[t.object].emplace(x,z);}
        }
        loaded=true;SyncObjects(true);UpdateSources(true);
        // Actual world placements are resolved by the zone/party activation.

        const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        GameJournal::Event("NAV","loaded","mpr=%s mob=%s profiles=8 ms=%.3f bytes=%zu generation=%llu",mapName.c_str(),mobName.c_str(),ms,bytes,static_cast<unsigned long long>(generation));
        return true;
    }catch(const std::exception& e){error=e.what();return false;}
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
    try {return meshes[Type(unit,unit?unit->GetAIClass():aiClass)].get();}
    catch(const std::exception& e) {
        error=e.what();if(failures++<16)GameJournal::Event("NAV","query_failed","unit=%d reason=%s",unit?unit->GetID():-1,GameJournal::Quote(e.what()).c_str());
        return nullptr;
    }
}
void NavigationWorld::Object(CMapObject* object,bool remove,CAIMap& map) {
    if(baker){baker->Object(object,remove,map);return;}
    if(!NavigationObject(object))return;
    const int id=object->GetID();dirtyObjects.insert(id);
    if(object->WhoAmI()==HPC_LEVER) {
        auto* lever=static_cast<CLeverObject*>(object);
        liveDoors[id]=remove?-1:lever->GetCurrentState();
    }
    if(remove){liveObjects.erase(id);return;}
    object->CreateCurrentPlacement();auto& boxes=liveObjects[id];boxes.clear();
    for(INT_PTR i=0;i<object->fbvs.GetSize();++i) {
        const auto& bv=object->fbvs[i];if(bv.IsIgnored()||bv.IsBridge()||bv.GetPassability()!=AI_PASS_NO)continue;
        Navigation::Solid box;auto base=object->GetCP();base.z+=map.GetOriginalTileHeight(base);
        for(int j=0;j<8;++j) {
            CPlot3D corner((j&1)?(std::max)(bv.ptMin.x,bv.ptMax.x):(std::min)(bv.ptMin.x,bv.ptMax.x),
                (j&2)?(std::max)(bv.ptMin.y,bv.ptMax.y):(std::min)(bv.ptMin.y,bv.ptMax.y),
                (j&4)?(std::max)(bv.ptMin.z,bv.ptMax.z):(std::min)(bv.ptMin.z,bv.ptMax.z));
            corner=object->RotateVector(corner+bv.ptBone+bv.ptCenter)+base;box.corners[j]=GamePoint(corner);
        }
        boxes.push_back(box);
    }
}
std::uint64_t NavigationWorld::Bake(const std::filesystem::path& directory,Navigation::Bytes& table,CWorldServer& world) {
    if(!baker)throw std::runtime_error("Nav64: baking requires NavBake tool");
    auto path=std::filesystem::path(ModRuntime::MapPath(mapName.c_str()));path.replace_extension(".navsrc");
    const auto authored=std::filesystem::exists(path)?Read(path):Navigation::Bytes{};
    baker->Bind(world,world.aiMap);
    return baker->Bake(mapName,mobName,directory,table,authored);
}
int NavigationWorld::Type(CUnit* unit,int aiClass) {
    aiClass=std::clamp(aiClass,0,7);
    const float radius=unit?Body(unit).radius:aiClass>=7?1.f:aiClass>=5?.5f:.25f;

    const int type=Navigation::ProfileForRadius(radius,aiClass==0);
    return type;
}
float NavigationWorld::Height(CUnit* unit,int aiClass) {
    const int c=std::clamp(unit?unit->GetAIClass():aiClass,0,7);
    const auto& profile=Agents[Type(unit,c)].profile;
    // Geometry chooses clearance; retain the old class's ceiling as an upper
    // bound, so a taller visual does not lose an already permitted passage.
    constexpr float legacy[]={2,.4f,1,1.6f,2.2f,1.7f,3,6};
    const int pose=unit?(unit->GetCurrentPose()==UP_LAY?2:unit->GetCurrentPose()==UP_CRAWL?1:0):0;
    const float height=unit?(std::min)(Body(unit).height[pose],legacy[c]):legacy[c];
    const auto& tiers=Navigation::ClearanceHeights(profile);float selected=tiers[0];
    // A smaller body fits the lowest clearance tier; do not reject it.
    for(float tier:tiers)if(tier<=height)selected=tier;
    return selected;
}
Navigation::Point NavigationWorld::Position(CUnit* unit,CAIMap& map) const {
    CorePerf::Scope timing(CorePerf::NavigationTotal);
    auto p=GamePoint(unit->GetCP());
    // GetCP.z is the legacy ground-relative altitude, never the world support Z.
    p.z=unit->GetAIClass()==0?map.GetTileSurface(unit->GetCP()):map.GetTileHeight(unit->GetCP());
    if(unit->navLocation.surface>=1 && std::hypot(p.x-unit->navLocation.position.x,p.y-unit->navLocation.position.y)<.1f)p.z=unit->navLocation.position.z;
    return p;
}
Navigation::Route NavigationWorld::Find(CUnit* unit,Navigation::Point from,Navigation::Point goal,float maxLength,float goalRadius,const float* costs,int aiClass) {
    CorePerf::Scope total(CorePerf::NavigationTotal),timing(CorePerf::NavigationPath);
    auto* mesh=ForUnit(unit,aiClass);if(!mesh){Navigation::Route route;route.status=Navigation::Status::NoMesh;return route;}
    Navigation::Request request;request.source=from;request.goal=goal;request.maxLength=maxLength;request.goalRadius=goalRadius;request.height=Height(unit,aiClass);
    if(Navigation::Levitates(mesh->profile)&&unit) {
        auto& map=static_cast<CWorldServer*>(unit->GetWorld())->aiMap;
        request.source.z=map.GetTileSurface(CPlot3D(from.x,from.y,0));
        request.goal.z=map.GetTileSurface(CPlot3D(goal.x,goal.y,0));
    }
    if(costs&&!Navigation::Levitates(mesh->profile))std::copy(costs,costs+16,request.areaCosts);
    auto route=Navigation::Find(*mesh,request);
    if(route.status!=Navigation::Status::Complete&&failures++<16)GameJournal::Event("NAV","query_failed","unit=%d reason=%s from=(%.2f,%.2f,%.2f) goal=(%.2f,%.2f,%.2f) profile=%s height=%.2f max_length=%.3f route_length=%.3f",unit?unit->GetID():-1,Navigation::StatusName(route.status),from.x,from.y,from.z,goal.x,goal.y,goal.z,mesh->profileId.c_str(),request.height,maxLength,route.length);
    return route;
}
bool NavigationWorld::CanPlace(CUnit* unit,Navigation::Point point,int aiClass) {
    CorePerf::Scope total(CorePerf::NavigationTotal),timing(CorePerf::NavigationPlace);
    auto* mesh=ForUnit(unit,aiClass);if(!mesh)return false;
    if(Navigation::Levitates(mesh->profile)&&unit)point.z=static_cast<CWorldServer*>(unit->GetWorld())->aiMap.GetTileSurface(CPlot3D(point.x,point.y,0));
    Navigation::Location location;return Navigation::Project(*mesh,point,.05f,4.f,location,Height(unit,aiClass));
}
bool NavigationWorld::Step(CUnit* unit,Navigation::Point from,Navigation::Point desired,Navigation::Location& result) {
    CorePerf::Scope total(CorePerf::NavigationTotal),timing(CorePerf::NavigationStep);
    auto* mesh=ForUnit(unit);
    const char* reason=mesh?"clearance":"no_mesh";
    if(!mesh||!Navigation::Constrain(*mesh,from,desired,result,Height(unit),&reason)) {
        const auto now=GetTickCount64();
        if(stepFailures++<16||now>=nextStepLog) {
            nextStepLog=now+2000;
            GameJournal::Event("NAV","step_rejected","unit=%d reason=%s from=(%.3f,%.3f,%.3f) desired=(%.3f,%.3f,%.3f)",unit?unit->GetID():-1,reason,from.x,from.y,from.z,desired.x,desired.y,desired.z);
        }
        return false;
    }
    if(unit) {
        auto& world=*static_cast<CWorldServer*>(unit->GetWorld());
        if(result.surface==1)result.position.z=unit->GetAIClass()==0?world.aiMap.GetTileSurface(CPlot3D(result.position.x,result.position.y,0)):world.GetBridgeSurfaceZValue(result.position.x,result.position.y);
    }
    return true;
}
bool NavigationWorld::Restore(CUnit* unit,CAIMap& map) {
    CorePerf::Scope total(CorePerf::NavigationTotal),timing(CorePerf::NavigationRestore);
    auto& saved=unit->navLocation;if(!saved.surface)return true;
    auto cp=unit->GetCP();
    if(!saved.surface||!Navigation::Finite(saved.position)||!std::isfinite(saved.radius)||saved.radius<0||saved.radius>4||
        !std::isfinite(saved.height)||saved.height<=0||saved.height>10||std::hypot(cp.x-saved.position.x,cp.y-saved.position.y)>.1f)return false;
    auto* mesh=ForUnit(unit);Navigation::Location location;
    if(!mesh||!Navigation::StartLocation(*mesh,saved.position,location,Height(unit))||location.surface!=saved.surface||std::abs(location.position.z-saved.position.z)>.25f)return false;
    saved=location;return true;
}

void NavigationWorld::QueueBox(const Solid& box) {
    const auto* params=meshes[0]->data->getParams();const float width=params->tileWidth,border=1.3f;
    float lowX=FLT_MAX,lowZ=FLT_MAX,highX=-FLT_MAX,highZ=-FLT_MAX;
    for(auto corner:box.corners){const auto p=ToNav(corner);lowX=(std::min)(lowX,p.x);lowZ=(std::min)(lowZ,p.z);highX=(std::max)(highX,p.x);highZ=(std::max)(highZ,p.z);}
    const int nx=int(std::ceil((sources->maximum.x-sources->minimum.x)/width)),nz=int(std::ceil((sources->maximum.z-sources->minimum.z)/width));
    const int x0=(std::max)(0,int(std::floor((lowX-border-params->orig[0])/width))),x1=(std::min)(nx-1,int(std::floor((highX+border-params->orig[0])/width)));
    const int z0=(std::max)(0,int(std::floor((lowZ-border-params->orig[2])/width))),z1=(std::min)(nz-1,int(std::floor((highZ+border-params->orig[2])/width)));
    for(int z=z0;z<=z1;++z)for(int x=x0;x<=x1;++x)for(int type=0;type<AgentCount;++type)sourceDirty.emplace(type,x,z);
}
void NavigationWorld::SyncObjects(bool initial) {
    if(!loaded)return;
    const auto start=std::chrono::steady_clock::now();
    while(!dirtyObjects.empty()) {
        const int id=*dirtyObjects.begin();dirtyObjects.erase(dirtyObjects.begin());
        auto door=std::find_if(sources->doors.begin(),sources->doors.end(),[&](const Door& d){return d.object==id;});
        if(door!=sources->doors.end()) {
            const bool open=liveDoors.count(id)?(liveDoors.at(id)<0||liveDoors.at(id)==door->openState):!liveObjects.count(id);
            for(auto& mesh:meshes)DoorState(*mesh,id,open);
        } else if(!Dynamic(id)) {
            const bool absent=!liveObjects.count(id);const bool changed=absent?absentStatic.insert(id).second:absentStatic.erase(id)!=0;
            if(changed) {
                for(const auto& tile:objectTiles[id])for(int type=0;type<AgentCount;++type)sourceDirty.emplace(type,tile.first,tile.second);
                GameJournal::Event("NAV","static_changed","NID=%d absent=%d queued_profile_tiles=%zu",id,int(absent),sourceDirty.size());
            }
        } else {
            auto old=appliedObjects.find(id);if(old!=appliedObjects.end())for(const auto& box:old->second)QueueBox(box);
            auto live=liveObjects.find(id);
            if(live!=liveObjects.end()){for(const auto& box:live->second)QueueBox(box);appliedObjects[id]=live->second;}
            else appliedObjects.erase(id);
        }
        if(!initial&&std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()>=2)break;
    }
}
void NavigationWorld::Advance() {
    if(!loaded||(dirtyObjects.empty()&&sourceDirty.empty()&&!sourceJob.valid()))return;
    CorePerf::Scope total(CorePerf::NavigationTotal);
    try{if(!dirtyObjects.empty())SyncObjects();else UpdateSources(false);}catch(const std::exception& e) {error=e.what();GameJournal::Event("NAV","update_failed","reason=%s",GameJournal::Quote(e.what()).c_str());}
}
bool NavigationWorld::Place(CUnit* unit,CAIMap& map) {
    if(!loaded)return false;auto* mesh=ForUnit(unit);if(!mesh)return false;
    auto from=Position(unit,map);Navigation::Location location;
    if(!Navigation::Project(*mesh,from,1.f,4.f,location,Height(unit))) {
        if(failures++<16)GameJournal::Event("NAV","placement_failed","unit=%d profile=%s radius=1",unit->GetID(),mesh->profileId.c_str());
        return false;
    }
    auto cp=unit->GetCP();cp.x=location.position.x;cp.y=location.position.y;
    if(std::hypot(cp.x-from.x,cp.y-from.y)>.001f) {
        if(!map.MoveObject(unit,cp))return false;
        GameJournal::Event("NAV","placement","unit=%d distance=%.3f",unit->GetID(),std::hypot(cp.x-from.x,cp.y-from.y));
    }
    if(location.surface==1)location.position.z=unit->GetAIClass()==0?map.GetTileSurface(cp):static_cast<CWorldServer*>(unit->GetWorld())->GetBridgeSurfaceZValue(cp.x,cp.y);
    unit->navLocation=location;return true;
}

bool NavigationWorld::RandomGoal(CUnit* unit,Navigation::Point center,float radius,Navigation::Point& goal) {
    auto* mesh=ForUnit(unit);if(!mesh)return false;
    auto& map=static_cast<CWorldServer*>(unit->GetWorld())->aiMap;
    return Navigation::RandomReachable(*mesh,Position(unit,map),center,radius,Height(unit),[](){return ::random.GetFloat(0.f,1.f);},goal);
}

void NavigationWorld::UpdateSources(bool initial) {
    do {
        if(!dirtyObjects.empty())return;
        if(sourceJob.valid()) {
            if(!initial&&sourceJob.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
            auto replacement=sourceJob.get();const auto [type,x,z]=sourceTask;
            if(!sourceDirty.count(sourceTask)) {
                const auto start=std::chrono::steady_clock::now();meshes[type]->excludedObjects=absentStatic;
                ReplaceTile(*meshes[type],*replacement,x,z);
                const auto* params=meshes[type]->data->getParams();const float width=params->tileWidth;
                for(const auto& door:sources->doors) {
                    const float lx=params->orig[0]+x*width,lz=params->orig[2]+z*width;
                    if(door.maximum.x<lx||door.minimum.x>lx+width||-door.minimum.y<lz||-door.maximum.y>lz+width)continue;
                    const auto state=meshes[type]->doorOpen.find(door.object);
                    if(state!=meshes[type]->doorOpen.end())DoorState(*meshes[type],door.object,state->second);
                }
                const double applyMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
                GameJournal::Event("NAV","source_tile","profile=%d tile=%d,%d worker_ms=%llu apply_ms=%.3f pending=%zu",type,x,z,static_cast<unsigned long long>(replacement->buildMs),applyMs,sourceDirty.size());
            }
        }
        if(sourceDirty.empty())return;
        sourceTask=*sourceDirty.begin();sourceDirty.erase(sourceDirty.begin());const auto [type,x,z]=sourceTask;
        auto snapshot=sources;auto absent=absentStatic;auto profile=meshes[type]->profile;
        const auto triangles=sourceTiles[{x,z}];
        std::vector<Solid> boxes;
        const float width=profile.tileCells*profile.cellSize,border=1.3f;
        const float lowX=sources->minimum.x+x*width-border,highX=lowX+width+border*2;
        const float lowZ=sources->minimum.z+z*width-border,highZ=lowZ+width+border*2;
        for(const auto& entry:appliedObjects)for(const auto& box:entry.second) {
            float a=FLT_MAX,b=FLT_MAX,c=-FLT_MAX,d=-FLT_MAX;
            for(auto p:box.corners){p=ToNav(p);a=(std::min)(a,p.x);b=(std::min)(b,p.z);c=(std::max)(c,p.x);d=(std::max)(d,p.z);}
            if(c>=lowX&&a<=highX&&d>=lowZ&&b<=highZ)boxes.push_back(box);
        }
        sourceJob=std::async(std::launch::async,[snapshot,absent,profile,x,z,triangles,boxes=std::move(boxes)] {
            Geometry input;input.minimum=snapshot->minimum;input.maximum=snapshot->maximum;input.doors=snapshot->doors;input.solids=boxes;
            for(auto index:triangles){const auto& t=snapshot->ground[index];if(!t.object||!absent.count(t.object))input.ground.push_back(t);}
            return RebuildTile(input,profile,x,z);
        });
    }while(initial);
}
