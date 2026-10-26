#include "StdAfx.h"
#include "NavigationWorld.h"
#include "aiMap.h"
#include "Unit.h"
#include "WorldServer.h"
#include "MapObjectServer.h"
#include "GameJournal.h"
#include "ModRuntime.h"
#include "ModPackage.h"
#include "RuntimePaths64.h"
#include <bcrypt.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <sstream>
#include <iomanip>
#include <cctype>
#pragma comment(lib,"bcrypt.lib")

namespace {
// A bounded registry shared by loading and queries. Never round a radius down.
constexpr float RadiusClasses[]={.1f,.2f,.3f,.4f,.5f,.6f,.7f,.8f,.9f,1.f,1.25f,1.5f,2.f,3.f,4.f};
constexpr float ClassHeights[]={2.f,.4f,1.f,1.6f,2.2f,1.7f,3.f,6.f};
constexpr float DefaultRadii[]={.4f,.4f,.4f,.4f,.4f,.7f,.7f,1.5f};
Navigation::Point Point(const CPlot3D& p) { return {p.x,p.y,p.z}; }
std::string ResourceName(const char* name) {
    auto result=std::filesystem::path(name?name:"").filename().generic_string();
    for(auto& c:result)if(static_cast<unsigned char>(c)<128)c=char(std::tolower(static_cast<unsigned char>(c)));
    return result;
}
std::string Digest(const Navigation::Bytes& bytes) {
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("Nav64: SHA-256 unavailable");
    unsigned char digest[32]{};
    auto s=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0);
    if(s>=0)s=BCryptHashData(hash,const_cast<PUCHAR>(bytes.data()),ULONG(bytes.size()),0);
    if(s>=0)s=BCryptFinishHash(hash,digest,32,0);
    if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);
    if(s<0)throw std::runtime_error("Nav64: SHA-256 failed");
    std::ostringstream text;for(auto v:digest)text<<std::hex<<std::setw(2)<<std::setfill('0')<<int(v);return text.str();
}
Navigation::Bytes Read(const std::filesystem::path& path) {
    auto n=std::filesystem::file_size(path);
    if(n>512ull*1024*1024)throw std::runtime_error("Nav64: input/cache exceeds 512 MiB");
    std::ifstream file(path,std::ios::binary);Navigation::Bytes bytes(static_cast<size_t>(n));
    if(!file || (n&&!file.read(reinterpret_cast<char*>(bytes.data()),n)))throw std::runtime_error("Nav64: incomplete file read");
    return bytes;
}
void Atomic(const std::filesystem::path& path,const Navigation::Bytes& data) {
    auto temp=path;temp+=L"."+std::to_wstring(GetCurrentProcessId())+L".tmp";
    HANDLE f=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f==INVALID_HANDLE_VALUE)throw std::runtime_error("Nav64: cannot create temporary cache");
    DWORD done=0;bool ok=WriteFile(f,data.data(),DWORD(data.size()),&done,nullptr)&&done==data.size()&&FlushFileBuffers(f);CloseHandle(f);
    if(ok)ok=MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(!ok){DeleteFileW(temp.c_str());throw std::runtime_error("Nav64: cannot commit cache atomically");}
}
}
void NavigationWorld::Changed() {meshes.clear();++generation;}
void NavigationWorld::Source(const std::string& name,const void* data,size_t size) {
    if(size>512u*1024*1024)throw std::runtime_error("Nav64: oversized source");
    auto p=static_cast<const std::uint8_t*>(data);auto& target=sources[name];
    target.assign(p,p+size);Changed();
}
void NavigationWorld::MobSource(const char* path) {
    auto bytes=Read(ModRuntime::MapPath(path));Source(std::string("mob:")+ResourceName(path),bytes.data(),bytes.size());
}
void NavigationWorld::Scene(const char* mpr,const char* mob) {
    scene=ResourceName(mpr)+"|"+ResourceName(mob)+"|navsrc:none";
    for(auto i=sources.begin();i!=sources.end();)if(i->first.compare(0,4,"mob:")==0)i=sources.erase(i);else ++i;
    Changed();failures=0;
}
void NavigationWorld::Object(CMapObject* object,bool remove,CAIMap& map) {
    if(object->WhoAmI()==HPC_UNIT)return;
    auto id=object->GetID();
    if(remove){if(objects.erase(id))Changed();return;}
    object->CreateCurrentPlacement();
    auto* specific=static_cast<CMapObjectServer*>(object->GetMapObjectSpecific());
    const CStringArray& parts=specific->szNames.GetSize()?specific->szNames:object->GetBodyPartNames();
    std::vector<Navigation::Solid> solids;
    for(INT_PTR n=0;n<parts.GetSize();++n){int i=object->GetFigureIndexByName(parts[n]);if(i<0)continue;
        const auto& bv=object->fbvs[i];
        if(bv.IsIgnored()||bv.IsBridge()||bv.GetPassability()!=AI_PASS_NO)continue;
        Navigation::Solid solid;auto base=object->GetCP();base.z+=map.GetOriginalTileHeight(base);
        for(int j=0;j<8;++j){CPlot3D p((j&1)?bv.ptMax.x:bv.ptMin.x,(j&2)?bv.ptMax.y:bv.ptMin.y,(j&4)?bv.ptMax.z:bv.ptMin.z);
            p+=bv.ptBone+bv.ptCenter;p=object->RotateVector(p);p+=base;solid.corners[j]=Point(p);}
        solids.push_back(solid);
    }
    if(!solids.empty()){objects[id]=std::move(solids);Changed();}
}
Navigation::Mesh* NavigationWorld::ForProfile(float radius,int aiClass,const char* phase) {
    try {
        if(!std::isfinite(radius)||radius<=0||radius>4||aiClass<0||aiClass>=8)throw std::runtime_error("Nav64: invalid agent size/class");
        const auto* radiusClass=std::lower_bound(std::begin(RadiusClasses),std::end(RadiusClasses),radius);
        const auto key=std::make_pair(int(radiusClass-std::begin(RadiusClasses)),aiClass);
        auto existing=meshes.find(key);if(existing!=meshes.end())return existing->second.get();
        auto start=std::chrono::steady_clock::now();Navigation::Profile profile;profile.radius=*radiusClass;profile.height=ClassHeights[aiClass];
        Navigation::Geometry geometry=terrain;for(const auto& entry:objects)geometry.solids.insert(geometry.solids.end(),entry.second.begin(),entry.second.end());geometry.Bounds();
        Navigation::Bytes source;
        // Each resource is delimited and named; effective payloads, not timestamps.
        for(const auto& entry:sources){for(auto v:entry.first)source.push_back(v);source.push_back(0);auto digest=Digest(entry.second);source.insert(source.end(),digest.begin(),digest.end());}
        auto fingerprint=Digest(Navigation::Inputs(geometry,profile,scene,source));
        std::unique_ptr<Navigation::Mesh> mesh;bool cached=false;
        const auto cache=RuntimePaths64::Directory(L"Cache64/Nav")/(fingerprint+".nav64");
        // The future mapper and the runtime cache use exactly the same container.
        const auto* mod=ModRuntime::Current();
        const auto published=mod?std::filesystem::path(mod->Directory())/"Maps"/"Nav64"/(fingerprint+".nav64"):std::filesystem::path();
        for(const auto& path:{published,cache})if(!path.empty()&&std::filesystem::exists(path)) {
            try{mesh=Navigation::Load(Read(path),profile,fingerprint);cached=true;break;}
            catch(const std::exception& e){GameJournal::Event("NAV","cache_rejected","scene=%s reason=%s",GameJournal::Quote(scene.c_str()).c_str(),GameJournal::Quote(e.what()).c_str());}
        }
        if(!mesh){mesh=Navigation::Build(geometry,profile,fingerprint);Atomic(cache,Navigation::Store(*mesh));}
        mesh->generation=generation;
        auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
        GameJournal::Event("NAV","ready","scene=%s tiles=%u ms=%lld source=%s class=%d radius=%.2f height=%.2f phase=%s",GameJournal::Quote(scene.c_str()).c_str(),mesh->tiles,static_cast<long long>(elapsed),cached?"cache":"built",aiClass,profile.radius,profile.height,phase);
        auto result=mesh.get();meshes.emplace(key,std::move(mesh));error.clear();return result;
    }catch(const std::exception& e){error=e.what();if(failures++<8)GameJournal::Event("NAV","unavailable","scene=%s reason=%s",GameJournal::Quote(scene.c_str()).c_str(),GameJournal::Quote(e.what()).c_str());return nullptr;}
}
Navigation::Mesh* NavigationWorld::ForUnit(CUnit* unit,int aiClass,const char* phase) {
    int c=unit?unit->GetAIClass():aiClass;
    return ForProfile(unit?(std::max)(.05f,unit->GetCollisionDistance()):(c>=0&&c<8?DefaultRadii[c]:0),c,phase);
}
bool NavigationWorld::Prepare(CWorldServer& world,const char* phase) {
    // Keep the default human probe, then warm every size actually in the world.
    if(!ForUnit(nullptr,3,phase))return false;
    for(const auto& entry:world.mapObjects)if(entry.second->WhoAmI()==HPC_UNIT) {
        if(!ForUnit(static_cast<CUnit*>(entry.second),3,phase))return false;
    }
    return true;
}
Navigation::Point NavigationWorld::Position(CUnit* unit,CAIMap& map) const {
    auto p=Point(unit->GetCP());
    // GetCP.z is the legacy ground-relative altitude, never the world support Z.
    p.z=map.GetOriginalTileHeight(unit->GetCP());
    if(unit->navLocation.surface==1 && std::hypot(p.x-unit->navLocation.position.x,p.y-unit->navLocation.position.y)<.1f)p.z=unit->navLocation.position.z;
    return p;
}
Navigation::Route NavigationWorld::Find(CUnit* unit,Navigation::Point from,Navigation::Point goal,float maxLength,float goalRadius,const float* costs,int aiClass) {
    auto* mesh=ForUnit(unit,aiClass);if(!mesh){Navigation::Route route;route.status=Navigation::Status::NoMesh;return route;}
    Navigation::Request request;request.source=from;request.goal=goal;request.maxLength=maxLength;request.goalRadius=goalRadius;
    if(costs)std::copy(costs,costs+16,request.areaCosts);
    auto route=Navigation::Find(*mesh,request);
    if(route.status!=Navigation::Status::Complete&&failures++<16)GameJournal::Event("NAV","query_failed","unit=%d reason=%s from=(%.2f,%.2f,%.2f) goal=(%.2f,%.2f,%.2f)",unit?unit->GetID():-1,Navigation::StatusName(route.status),from.x,from.y,from.z,goal.x,goal.y,goal.z);
    return route;
}
bool NavigationWorld::CanPlace(CUnit* unit,Navigation::Point point,int aiClass) {
    auto* mesh=ForUnit(unit,aiClass);Navigation::Location location;return mesh&&Navigation::Project(*mesh,point,.05f,4.f,location);
}
bool NavigationWorld::Step(CUnit* unit,Navigation::Point from,Navigation::Point desired,Navigation::Location& result) {
    auto* mesh=ForUnit(unit);return mesh&&Navigation::Constrain(*mesh,from,desired,result);
}
bool NavigationWorld::Restore(CUnit* unit,CAIMap& map) {
    auto& saved=unit->navLocation;if(!saved.surface)return true;
    auto cp=unit->GetCP();
    if(saved.surface!=1||!Navigation::Finite(saved.position)||!std::isfinite(saved.radius)||saved.radius<=0||saved.radius>4||
        !std::isfinite(saved.height)||saved.height<=0||saved.height>10||std::hypot(cp.x-saved.position.x,cp.y-saved.position.y)>.1f)return false;
    auto* mesh=ForUnit(unit);Navigation::Location location;
    if(!mesh||!Navigation::Project(*mesh,saved.position,.05f,.25f,location))return false;
    saved=location;return true;
}
