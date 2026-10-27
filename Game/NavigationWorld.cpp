#include "StdAfx.h"
#include "NavigationWorld.h"
#include "NavigationBake.h"
#include "NavigationObject.h"
#include "../Navigation/Profiles.h"
#include "aiMap.h"
#include "Unit.h"
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
    loaded=false;error.clear();failures=0;++generation;
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
        for(auto& mesh:meshes)mesh->support=meshes[Levitates(mesh->profile)?LevitateTiny:Tiny].get();
        loaded=true;
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
    if(!loaded||!NavigationObject(object))return;
    CorePerf::Scope total(CorePerf::NavigationTotal);
    unsigned solids=0,walkable=0;
    for(INT_PTR i=0;i<object->fbvs.GetSize();++i) {const auto& bv=object->fbvs[i];if(bv.IsIgnored())continue;
        if(bv.IsBridge())++walkable;else if(bv.GetPassability()==AI_PASS_NO)++solids;
    }
    if(solids||walkable)GameJournal::Event("NAV","geometry_deferred","NID=%d operation=%s solids=%u walkable_parts=%u reason=X64NAV05 baked_unchanged=1",object->GetID(),remove?"remove":"add",solids,walkable);
}
std::uint64_t NavigationWorld::Bake(const std::filesystem::path& directory,const Navigation::Bytes& table,bool fingerprintOnly) {
    if(!baker)throw std::runtime_error("Nav64: baking requires NavBake tool");
    auto path=std::filesystem::path(ModRuntime::MapPath(mapName.c_str()));path.replace_extension(".navsrc");
    const auto authored=std::filesystem::exists(path)?Read(path):Navigation::Bytes{};
    if(fingerprintOnly) {
        const auto start=std::chrono::steady_clock::now();const auto key=baker->Fingerprint(mapName,mobName,table,authored);
        printf("FINGERPRINT mpr=%s mob=%s algorithm=%u key=%s ms=%.3f\n",mapName.c_str(),mobName.c_str(),AlgorithmVersion,key.c_str(),std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
        return 0;
    }
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
    if(unit->navLocation.surface==1 && std::hypot(p.x-unit->navLocation.position.x,p.y-unit->navLocation.position.y)<.1f)p.z=unit->navLocation.position.z;
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
    auto* mesh=ForUnit(unit);return mesh&&Navigation::Constrain(*mesh,from,desired,result,Height(unit));
}
bool NavigationWorld::Restore(CUnit* unit,CAIMap& map) {
    CorePerf::Scope total(CorePerf::NavigationTotal),timing(CorePerf::NavigationRestore);
    auto& saved=unit->navLocation;if(!saved.surface)return true;
    auto cp=unit->GetCP();
    if(saved.surface!=1||!Navigation::Finite(saved.position)||!std::isfinite(saved.radius)||saved.radius<0||saved.radius>4||
        !std::isfinite(saved.height)||saved.height<=0||saved.height>10||std::hypot(cp.x-saved.position.x,cp.y-saved.position.y)>.1f)return false;
    auto* mesh=ForUnit(unit);Navigation::Location location;
    if(!mesh||!Navigation::StartLocation(*mesh,saved.position,location,Height(unit))||std::abs(location.position.z-saved.position.z)>.25f)return false;
    saved=location;return true;
}
