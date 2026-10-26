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
#include "FigureManager.h"
#include "../external/owned-renderer/Source/owned_asset_loader.h"
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
bool Same(Navigation::Point a,Navigation::Point b) {return a.x==b.x&&a.y==b.y&&a.z==b.z;}
bool Same(const Navigation::Geometry& a,const Navigation::Geometry& b) {
    if(a.ground.size()!=b.ground.size()||a.solids.size()!=b.solids.size())return false;
    for(size_t i=0;i<a.ground.size();++i) {
        const auto& x=a.ground[i];const auto& y=b.ground[i];
        if(x.area!=y.area||!Same(x.a,y.a)||!Same(x.b,y.b)||!Same(x.c,y.c))return false;
    }
    for(size_t i=0;i<a.solids.size();++i) {
        if(a.solids[i].error!=b.solids[i].error)return false;
        for(int j=0;j<8;++j)if(!Same(a.solids[i].corners[j],b.solids[i].corners[j]))return false;
    }
    return true;
}
}
void NavigationWorld::Changed() {meshes.clear();geometry.reset();++generation;}
void NavigationWorld::SyncObjects() {
    bool changed=false;
    for(auto& entry:pendingObjects) {
        auto old=objects.find(entry.first);auto& next=entry.second;
        bool empty=next.solids.empty()&&next.ground.empty();
        if(old==objects.end()?empty:Same(old->second,next))continue;
        if(!meshes.empty())GameJournal::Event("NAV","geometry_changed","NID=%d operation=%s solids=%zu triangles=%zu generation=%llu",entry.first,empty?"remove":old==objects.end()?"add":"update",next.solids.size(),next.ground.size(),static_cast<unsigned long long>(generation));
        if(empty)objects.erase(entry.first);else objects[entry.first]=std::move(next);
        changed=true;
    }
    pendingObjects.clear();if(changed)Changed();
}
void NavigationWorld::Source(const std::string& name,const void* data,size_t size) {
    if(size>512u*1024*1024)throw std::runtime_error("Nav64: oversized source");
    auto p=static_cast<const std::uint8_t*>(data);auto& target=sources[name];
    if(target.size()==size&&std::equal(target.begin(),target.end(),p))return;
    target.assign(p,p+size);Changed();
}
void NavigationWorld::Scene(const char* mpr,const char* mob) {
    scene=ResourceName(mpr)+"|"+ResourceName(mob)+"|navsrc:none";
    figures.clear();
    Changed();failures=0;
}
std::shared_ptr<const owned::FigureCpuAsset> NavigationWorld::Figure(CMapObject* object,const char* part) {
    const std::string key=std::string(object->GetMaskName())+"/"+part;
    auto found=figures.find(key);if(found!=figures.end())return found->second;
    auto* manager=static_cast<CFigureManager*>(object->GetWorld()->GetFigureManager());
    auto* model=manager->GetModel(object->GetMaskName());DWORD size=0;
    const auto file=std::string(object->GetMaskName())+part+".fig";
    auto* data=model?model->GetModel()->GetFile(part,&size):manager->GetFile(file.c_str(),&size);
    if(!data)throw std::runtime_error("missing walkable FIG");
    owned::ResourceBlob blob;
    try {blob.bytes=std::make_shared<const std::vector<std::uint8_t>>(data,data+size);}
    catch(...) {if(model)model->GetModel()->CloseFile(data);else manager->CloseFile(data);throw;}
    if(model)model->GetModel()->CloseFile(data);else manager->CloseFile(data);
    blob.identity.canonical_name=key;blob.identity.bytes=size;
    auto figure=std::make_shared<owned::FigureCpuAsset>();owned::ResourceFailure failure;
    if(!owned::parse_figure_asset(blob,*figure,failure))throw std::runtime_error(failure.message);
    figures.emplace(key,figure);return figure;
}
void NavigationWorld::Object(CMapObject* object,bool remove,CAIMap& map) {
    if(object->WhoAmI()==HPC_UNIT)return;
    auto id=object->GetID();
    // Move/orient/lever updates remove and add synchronously. Compare their
    // final contribution at the next query, not their intermediate absence.
    if(remove){if(objects.count(id)||pendingObjects.count(id))pendingObjects[id]={};return;}
    object->CreateCurrentPlacement();
    auto* specific=static_cast<CMapObjectServer*>(object->GetMapObjectSpecific());
    const CStringArray& parts=specific->szNames.GetSize()?specific->szNames:object->GetBodyPartNames();
    Navigation::Geometry input;
    for(INT_PTR n=0;n<parts.GetSize();++n){int i=object->GetFigureIndexByName(parts[n]);if(i<0)continue;
        const auto& bv=object->fbvs[i];
        if(bv.IsIgnored()) {
            if(!bv.IsBox()&&!bv.IsSphere())GameJournal::Event("NAV","empty_part","NID=%d model=%s part=%s reason=zero_geometry",id,GameJournal::Quote(object->GetMaskName()).c_str(),GameJournal::Quote(parts[n]).c_str());
            continue;
        }
        if(bv.IsBridge()) {
            std::ostringstream label;label<<"object NID="<<id<<" model="<<object->GetMaskName()<<" part="<<parts[n].GetString();
            try {
                const auto figure=Figure(object,parts[n]);const auto& c=object->GetComplection();
                const float shape[3]={c.strength,c.grace,c.tall};float weights[8]{};
                for(unsigned s=0;s<figure->shape_count;++s) {
                    weights[s]=1;
                    if(figure->shape_count!=1)for(int axis=0;axis<3;++axis)weights[s]*=(s&(1u<<axis))?shape[axis]:1-shape[axis];
                }
                auto base=object->GetCP();base.z+=map.GetOriginalTileHeight(base);
                std::vector<Navigation::Point> vertices(figure->components.size());
                for(size_t j=0;j<vertices.size();++j) {
                    const auto index=figure->components[j].position;CPlot3D p(0,0,0);
                    for(unsigned s=0;s<figure->shape_count;++s) {
                        const auto& group=figure->positions[s*figure->position_groups+index/4];const auto lane=index%4;
                        p+=CPlot3D(group.x[lane],group.y[lane],group.z[lane])*weights[s];
                    }
                    // FIG vertices are origin-relative; ptCenter belongs only
                    // to the collision BV. BON already contains linked offsets.
                    p=object->RotateVector(p+bv.ptBone)+base;vertices[j]=Point(p);
                    if(!Navigation::Finite(vertices[j]))throw std::runtime_error("non-finite transformed triangle vertex");
                }
                const size_t first=input.ground.size();input.groundSources.emplace(first,label.str());
                for(size_t j=0;j<figure->indices.size();j+=3)
                    input.ground.push_back({vertices[figure->indices[j]],vertices[figure->indices[j+1]],vertices[figure->indices[j+2]],map.NavigationBridgeArea()});
            }catch(const std::exception& e) {
                Navigation::Solid failure{};failure.error="Nav64: invalid walkable geometry "+label.str()+" reason="+e.what();input.solids.push_back(std::move(failure));
            }
            continue;
        }
        if(bv.GetPassability()!=AI_PASS_NO)continue;
        Navigation::Solid solid;auto base=object->GetCP();base.z+=map.GetOriginalTileHeight(base);
        bool valid=Navigation::Finite(Point(bv.ptBone))&&Navigation::Finite(Point(bv.ptCenter))&&
            Navigation::Finite(Point(bv.ptMin))&&Navigation::Finite(Point(bv.ptMax))&&Navigation::Finite(Point(base))&&
            bv.ptMin.x<=bv.ptMax.x&&bv.ptMin.y<=bv.ptMax.y&&bv.ptMin.z<=bv.ptMax.z;
        for(int j=0;j<8;++j){CPlot3D p((j&1)?bv.ptMax.x:bv.ptMin.x,(j&2)?bv.ptMax.y:bv.ptMin.y,(j&4)?bv.ptMax.z:bv.ptMin.z);
            p+=bv.ptBone+bv.ptCenter;p=object->RotateVector(p);p+=base;solid.corners[j]=Point(p);valid=valid&&Navigation::Finite(solid.corners[j]);}
        if(!valid) {
            std::ostringstream detail;detail<<"Nav64: invalid solid geometry NID="<<id<<" model="<<object->GetMaskName()<<" part="<<parts[n].GetString();
            auto point=[&](const char* field,const CPlot3D& p){detail<<" "<<field<<"=("<<p.x<<","<<p.y<<","<<p.z<<")";};
            point("ptMin",bv.ptMin);point("ptMax",bv.ptMax);point("ptBone",bv.ptBone);point("ptCenter",bv.ptCenter);point("CP",object->GetCP());point("worldBase",base);
            float w,x,y,z;object->GetOrientation().GetComponents(w,x,y,z);const auto& c=object->GetComplection();
            detail<<" rotation=("<<w<<","<<x<<","<<y<<","<<z<<") constitution=("<<c.strength<<","<<c.grace<<","<<c.tall<<")";
            solid.error=detail.str();
        }
        input.solids.push_back(std::move(solid));
    }
    if(!input.ground.empty()&&!objects.count(id)&&!pendingObjects.count(id))GameJournal::Event("NAV","walkable_object","NID=%d model=%s triangles=%zu",id,GameJournal::Quote(object->GetMaskName()).c_str(),input.ground.size());
    if(!input.solids.empty()||!input.ground.empty()||objects.count(id)||pendingObjects.count(id))pendingObjects[id]=std::move(input);
}
Navigation::Mesh* NavigationWorld::ForProfile(float radius,int aiClass,const char* phase) {
    try {
        SyncObjects();
        if(!std::isfinite(radius)||radius<=0||radius>4||aiClass<0||aiClass>=8)throw std::runtime_error("Nav64: invalid agent size/class");
        const auto* radiusClass=std::lower_bound(std::begin(RadiusClasses),std::end(RadiusClasses),radius);
        const auto key=std::make_pair(int(radiusClass-std::begin(RadiusClasses)),aiClass);
        auto existing=meshes.find(key);if(existing!=meshes.end())return existing->second.get();
        auto start=std::chrono::steady_clock::now();Navigation::Profile profile;profile.radius=*radiusClass;profile.height=ClassHeights[aiClass];
        if(!geometry) {
            auto input=std::make_shared<Navigation::Geometry>(terrain);
            for(const auto& entry:objects) {
                const auto& object=entry.second;const auto first=input->ground.size();
                for(const auto& source:object.groundSources)input->groundSources.emplace(first+source.first,source.second);
                input->ground.insert(input->ground.end(),object.ground.begin(),object.ground.end());
                input->solids.insert(input->solids.end(),object.solids.begin(),object.solids.end());
            }
            input->Bounds();geometry=std::move(input);
        }
        Navigation::Bytes source;
        // Each resource is delimited and named; effective payloads, not timestamps.
        for(const auto& entry:sources){for(auto v:entry.first)source.push_back(v);source.push_back(0);auto digest=Digest(entry.second);source.insert(source.end(),digest.begin(),digest.end());}
        auto fingerprint=Digest(Navigation::Inputs(*geometry,profile,scene,source));
        std::unique_ptr<Navigation::Mesh> mesh;bool cached=false;
        const auto cache=RuntimePaths64::Directory(L"Cache64/Nav")/(fingerprint+".nav64");
        // The future mapper and the runtime cache use exactly the same container.
        const auto* mod=ModRuntime::Current();
        const auto published=mod?std::filesystem::path(mod->Directory())/"Maps"/"Nav64"/(fingerprint+".nav64"):std::filesystem::path();
        for(const auto& path:{published,cache})if(!path.empty()&&std::filesystem::exists(path)) {
            try{mesh=Navigation::Load(Read(path),profile,fingerprint);cached=true;break;}
            catch(const std::exception& e){GameJournal::Event("NAV","cache_rejected","scene=%s reason=%s",GameJournal::Quote(scene.c_str()).c_str(),GameJournal::Quote(e.what()).c_str());}
        }
        if(!mesh){mesh=Navigation::Build(*geometry,profile,fingerprint);Atomic(cache,Navigation::Store(*mesh));}
        mesh->generation=generation;mesh->geometry=geometry;
        auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
        GameJournal::Event("NAV",mesh->tiles?"ready":"empty","scene=%s tiles=%u ms=%lld source=%s class=%d radius=%.2f height=%.2f phase=%s workers=%u raster_cpu_ms=%llu polygons_cpu_ms=%llu key=%s generation=%llu%s",GameJournal::Quote(scene.c_str()).c_str(),mesh->tiles,static_cast<long long>(elapsed),cached?"cache":"built",aiClass,profile.radius,profile.height,phase,mesh->workers,static_cast<unsigned long long>(mesh->rasterMs),static_cast<unsigned long long>(mesh->polygonMs),fingerprint.c_str(),static_cast<unsigned long long>(generation),mesh->tiles?"":" reason=\"no walkable ground\"");
        auto result=mesh.get();meshes.emplace(key,std::move(mesh));error.clear();return result;
    }catch(const std::exception& e){error=e.what();if(failures++<8)GameJournal::Event("NAV","unavailable","scene=%s reason=%s",GameJournal::Quote(scene.c_str()).c_str(),GameJournal::Quote(e.what()).c_str());return nullptr;}
}
Navigation::Mesh* NavigationWorld::ForUnit(CUnit* unit,int aiClass,const char* phase) {
    int c=unit?unit->GetAIClass():aiClass;
    return ForProfile(unit?(std::max)(.05f,unit->GetCollisionDistance()):(c>=0&&c<8?DefaultRadii[c]:0),c,phase);
}
bool NavigationWorld::Prepare(CWorldServer& world,const char* phase) {
    const auto start=std::chrono::steady_clock::now();
    // Keep the default human probe, then warm every size actually in the world.
    if(!ForUnit(nullptr,3,phase))return false;
    for(const auto& entry:world.mapObjects)if(entry.second->WhoAmI()==HPC_UNIT) {
        if(!ForUnit(static_cast<CUnit*>(entry.second),3,phase))return false;
    }
    const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
    GameJournal::Event("NAV","prepared","scene=%s phase=%s profiles=%zu ms=%lld generation=%llu",GameJournal::Quote(scene.c_str()).c_str(),phase,meshes.size(),static_cast<long long>(ms),static_cast<unsigned long long>(generation));
    return true;
}
Navigation::Point NavigationWorld::Position(CUnit* unit,CAIMap& map) const {
    auto p=Point(unit->GetCP());
    // GetCP.z is the legacy ground-relative altitude, never the world support Z.
    p.z=map.GetTileHeight(unit->GetCP());
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
    if(!mesh||!Navigation::StartLocation(*mesh,saved.position,location)||std::abs(location.position.z-saved.position.z)>.25f)return false;
    saved=location;return true;
}
