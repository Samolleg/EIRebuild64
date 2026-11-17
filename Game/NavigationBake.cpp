#include "StdAfx.h"
#include "NavigationBake.h"
#include "environment_asset.h"
#include "NavigationCanopy.h"
#include "TextureManager.h"
#include "NavigationObject.h"
#include "../Navigation/Profiles.h"
#include "../Navigation/Query.h"
#include <fstream>
#include <future>
#include "aiMap.h"
#include "Unit.h"
#include "WorldServer.h"
#include "WorldClient.h"
#include "MapObjectServer.h"
#include "StuffMapObjects.h"
#include "MapObjectManager.h"
#include "FigureManager.h"
#include "UnitDatabase.h"
#include "../external/owned-renderer/Source/owned_asset_loader.h"
#include <bcrypt.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <sstream>
#include <iomanip>
#include "nlohmann/json.hpp"
#pragma comment(lib,"bcrypt.lib")
using namespace Navigation;
std::function<void()> NavigationBake::overlap;
unsigned NavigationBake::tileCells=96;
std::filesystem::path NavigationBake::reportDirectory;
std::filesystem::path NavigationBake::replayInput;
namespace {
Navigation::Point GamePoint(const CPlot3D& p) {return {p.x,p.y,p.z};}
void Atomic(const std::filesystem::path& path,const Navigation::Bytes& data) {
    auto temp=path;temp+=L"."+std::to_wstring(GetCurrentProcessId())+L".tmp";
    HANDLE f=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f==INVALID_HANDLE_VALUE)throw std::runtime_error("Nav64: cannot create temporary nav64");
    DWORD done=0;bool ok=WriteFile(f,data.data(),DWORD(data.size()),&done,nullptr)&&done==data.size()&&FlushFileBuffers(f);CloseHandle(f);
    if(ok)ok=MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(!ok){DeleteFileW(temp.c_str());throw std::runtime_error("Nav64: cannot commit nav64 atomically");}
}
// Release validation runs on the actual Detour vertices before any profile
// of this map is written. Never repair/clamp a bad height after construction.
void CheckReleaseHeights(const Navigation::Mesh& mesh) {
    const dtNavMesh& data=*mesh.data;
    const float tolerance=mesh.profile.cellHeight*2+1.e-4f; // 10 cm, including Recast's +ch detail offset.
    for(int i=0;i<data.getMaxTiles();++i) {
        const auto* tile=data.getTile(i);if(!tile||!tile->header)continue;
        const auto& h=*tile->header;
        auto check=[&](const float* vertices,int count,const char* kind) {
            for(int vertex=0;vertex<count;++vertex) {
                const float y=vertices[vertex*3+1];
                if(std::isfinite(y)&&y>=h.bmin[1]-tolerance&&y<=h.bmax[1]+tolerance)continue;
                std::ostringstream error;
                error<<"Nav64: height_out_of_bounds mpr="<<mesh.mapName<<" mob="<<mesh.mobName
                    <<" profile="<<mesh.profileId<<" tile="<<h.x<<","<<h.y<<","<<h.layer
                    <<" kind="<<kind<<" vertex="<<vertex<<" y="<<y
                    <<" min="<<h.bmin[1]<<" max="<<h.bmax[1]<<" tolerance="<<tolerance;
                throw std::runtime_error(error.str());
            }
        };
        check(tile->verts,h.vertCount,"polygon");
        check(tile->detailVerts,h.detailVertCount,"detail");
    }
}
}
std::shared_ptr<const owned::FigureCpuAsset> NavigationBake::Figure(CFigureManager& manager,const std::string& mask,const char* part) {
    return QueueFigure(manager,mask,part).get();
}
NavigationBake::FigureJob NavigationBake::QueueFigure(CFigureManager& manager,const std::string& mask,const char* part) {
    const std::string key=mask+"/"+part;
    auto found=figures.find(key);if(found!=figures.end())return found->second;

    auto* model=manager.GetModel(mask);DWORD size=0;
    const auto file=mask+part+".fig";
    auto* data=model?model->GetModel()->GetFile(part,&size):manager.GetFile(file.c_str(),&size);
    if(!data)throw std::runtime_error("missing navigation FIG: "+file);
    owned::ResourceBlob blob;
    try {blob.bytes=std::make_shared<const std::vector<std::uint8_t>>(data,data+size);}
    catch(...) {if(model)model->GetModel()->CloseFile(data);else manager.CloseFile(data);throw;}
    if(model)model->GetModel()->CloseFile(data);else manager.CloseFile(data);
    blob.identity.canonical_name=key;blob.identity.bytes=size;
    // Resource-manager caches stay on their owner thread. Decoders consume
    // immutable copied payloads and can overlap both I/O and other decoders.
    const auto csr=_mm_getcsr();
    auto pending=std::async(std::launch::async,[blob=std::move(blob),key,csr]() -> std::shared_ptr<const owned::FigureCpuAsset> {
        _mm_setcsr(csr);
        auto figure=std::make_shared<owned::FigureCpuAsset>();owned::ResourceFailure failure;
        if(!owned::parse_figure_asset(blob,*figure,failure))throw std::runtime_error("Nav64: "+key+": "+failure.message);
        return figure;
    }).share();
    figures.emplace(key,pending);return pending;
}

const NavigationCanopy& NavigationBake::Canopy(const CMapObjectServer& object,const owned::FigureCpuAsset& figure) {
    owned::StaticObjectSource source;
    source.primary_texture=Navigation::ResourceName((LPCSTR)object.szPrimTexture);
    source.secondary_texture=Navigation::ResourceName((LPCSTR)object.szSecTexture);
    const auto* selected=owned::figure_texture_for_type(source,figure.texture_number);
    if(!selected)throw std::runtime_error("Nav64: canopy has no selected texture");
    auto found=canopyTextures.find(*selected);if(found!=canopyTextures.end())return *found->second;
    if(!canopyResources) {
        auto resources=std::make_shared<CTextureManager>();resources->SetWorld(sourceWorld);
        const auto base=sourceWorld->GetResourcePath();
        if(!resources->AddGraphicsResources("textures.res")||!resources->AddGraphicsResources("redress.res")||
            !resources->AddNewResource(base+"textures.res")||!resources->AddNewResource(base+"redress.res"))
            throw std::runtime_error("Nav64: cannot mount effective canopy textures");
        canopyResources=std::move(resources);
    }
    const auto name=*selected+".mmp";DWORD size=0;auto* data=canopyResources->GetFile(name.c_str(),&size);
    if(!data)throw std::runtime_error("Nav64: missing canopy texture "+name);
    owned::ResourceBlob blob;
    try {blob.bytes=std::make_shared<const std::vector<std::uint8_t>>(data,data+size);}
    catch(...) {canopyResources->CloseFile(data);throw;}
    canopyResources->CloseFile(data);blob.identity.canonical_name=name;blob.identity.bytes=size;
    owned::TextureCpuAsset texture;owned::ResourceFailure failure;
    if(!owned::parse_texture_asset(blob,texture,failure))throw std::runtime_error("Nav64: "+name+": "+failure.message);
    auto coverage=std::make_shared<NavigationCanopy>(texture);
    canopyTextures.emplace(*selected,coverage);return *coverage;
}

const Navigation::BodySize& NavigationBake::Body(CWorldServer& world,const std::string& mask,float strength,float grace,float tall) {

    if(!std::isfinite(strength)||!std::isfinite(grace)||!std::isfinite(tall))
        throw std::runtime_error("Nav64: invalid body constitution: "+mask);
    const auto key=std::make_tuple(mask,strength,grace,tall);
    auto size=bodyTable.samples.find(key);if(size!=bodyTable.samples.end())return size->second;
    auto* manager=static_cast<CFigureManager*>(world.GetFigureManager());
    auto* model=manager->GetModel(mask);
    auto read=[](auto* resource,const std::string& name) {
        DWORD count=0;auto* data=resource?resource->GetFile(name.c_str(),&count):nullptr;
        if(!data)throw std::runtime_error("Nav64: missing body resource: "+name);
        owned::ResourceBlob blob;
        try {blob.bytes=std::make_shared<const std::vector<std::uint8_t>>(data,data+count);}
        catch(...) {resource->CloseFile(data);throw;}
        resource->CloseFile(data);blob.identity.canonical_name=name;blob.identity.bytes=count;return blob;
    };
    auto body=bodies.find(mask);
    if(body==bodies.end()) {
        auto skeleton=std::make_shared<owned::ModelCpuAsset>();owned::ResourceFailure failure;
        const auto links=model?read(model->GetModel(),mask):read(manager,mask+".lnk");
        auto bone=[&](const std::string& name,owned::ResourceBlob& blob,owned::ResourceFailure&) {
            blob=model?read(model->GetBones(),name):read(manager,mask+name+".bon");return true;
        };
        if(!owned::decode_selected_skeleton(links,mask,bone,*skeleton,failure))
            throw std::runtime_error("Nav64: invalid body skeleton: "+mask+": "+failure.message);
        body=bodies.emplace(mask,std::move(skeleton)).first;
    }
    const auto& skeleton=*body->second;
    std::vector<bool> included(skeleton.nodes.size());
    for(size_t i=0;i<skeleton.nodes.size();++i) {
        auto name=skeleton.nodes[i].name;for(auto& ch:name)ch=char(std::tolower(static_cast<unsigned char>(ch)));
        const auto parent=skeleton.links[i].parent_index;
        included[i]=name.find('.')==std::string::npos&&name.rfind("base",0)!=0&&name.rfind("empty",0)!=0&&name.rfind("eye",0)!=0&&(parent>=i||included[parent]);
    }
    const float shape[]={strength,grace,tall};float weights[8]{};
    for(unsigned s=0;s<8;++s){weights[s]=1;for(int a=0;a<3;++a)weights[s]*=(s&(1u<<a))?shape[a]:1-shape[a];}
    CADEntryPtrArray entries;CString modelName(mask.c_str());static_cast<CMapObjectManager*>(world.GetMapObjectManager())->GetAllAnimationEntries(modelName,entries);
    const SADEntry* selected[3]{};
    for(int pose=0;pose<3;++pose) {
        const DWORD state=pose==0?ADB_STATE_NEUTRAL:pose==1?ADB_STATE_WARRY:ADB_STATE_LIE;
        const DWORD caps=state|ADB_ACTION_IDLE|ADB_MODIFIER_IDLE_STAY;
        for(INT_PTR i=0;i<entries.GetSize();++i)if((entries[i]->dwFlags&0x3fff8000u)==caps){selected[pose]=entries[i];break;}
        // Monsters use the combat idle as their ordinary standing posture.
        if(!selected[pose]&&pose==0)for(INT_PTR i=0;i<entries.GetSize();++i)
            if((entries[i]->dwFlags&0x3fff8000u)==(caps|ADB_STATE_ATTACK)){selected[pose]=entries[i];break;}
        if(!selected[pose])selected[pose]=selected[0];
    }
    // No idle entry is a valid bind-pose creature, not a navigation error.
    Navigation::BodySize measured;
    for(int pose=0;pose<3;++pose) {
        const std::string poseKey=mask+"/"+(selected[pose]?selected[pose]->pszAnimationName:"<bind>");
        auto clip=poses.find(poseKey);
        if(clip==poses.end()) {
            auto decoded=std::make_shared<owned::AnimationClipAsset>();owned::ResourceFailure failure;
            auto* animation=selected[pose]?manager->GetModelAnimation(mask,selected[pose]->pszAnimationName):nullptr;
            // Missing tracks have the game's identity transform. Do not mutate
            // live animation state or use random animation selection.
            for(size_t i=0;i<skeleton.nodes.size();++i) {
                if(!included[i])continue;const auto& node=skeleton.nodes[i];
                owned::AnimationTrackAsset track;DWORD count=0;auto* data=animation?animation->GetFile(node.name.c_str(),&count):nullptr;
                if(data) {
                    owned::ResourceBlob bytes;
                    try {bytes.bytes=std::make_shared<const std::vector<std::uint8_t>>(data,data+count);}
                    catch(...) {animation->CloseFile(data);throw;}
                    animation->CloseFile(data);bytes.identity.canonical_name=poseKey+"/"+node.name;
                    if(!owned::parse_animation_track(bytes,track,failure))throw std::runtime_error(failure.message);
                }
                decoded->tracks.emplace(node.name,std::move(track));
            }
            clip=poses.emplace(poseKey,std::move(decoded)).first;
        }
        std::vector<CPlot3D> positions(skeleton.nodes.size());
        std::vector<CQuaternion> rotations(skeleton.nodes.size(),QNULL);
        float lowX=FLT_MAX,highX=-FLT_MAX,bodyLowX=FLT_MAX,bodyHighX=-FLT_MAX,lowZ=FLT_MAX,highZ=-FLT_MAX;
        for(size_t i=0;i<skeleton.nodes.size();++i) {
            if(!included[i])continue;
            const auto& node=skeleton.nodes[i];CPlot3D bone(0,0,0);
            auto name=node.name;for(auto& ch:name)ch=char(std::tolower(static_cast<unsigned char>(ch)));
            if(!node.transform_available||node.bone_positions.empty())throw std::runtime_error("Nav64: unavailable body bone: "+mask+"/"+node.name);
            const auto count=node.bone_positions.size()/3;
            for(size_t s=0;s<count;++s)bone+=CPlot3D(node.bone_positions[s*3],node.bone_positions[s*3+1],node.bone_positions[s*3+2])*(count==1?1:weights[s]);
            const auto& track=clip->second->tracks.at(node.name);
            CQuaternion local=QNULL;if(!track.rotations.empty()){const auto& q=track.rotations[0];local=CQuaternion(q[0],q[1],q[2],q[3]);}
            const auto parent=skeleton.links[i].parent_index;
            if(parent<i) {positions[i]=rotations[parent].Rotate(bone)+positions[parent];rotations[i]=local*rotations[parent];}
            else {positions[i]=CPlot3D(0,0,0);rotations[i]=local;}
            const auto figure=Figure(*manager,mask,node.name.c_str());
            const bool torso=name=="bd"||name=="hp"||name=="chest"||name=="body"||name=="abdomen"||name=="column"||name.rfind("bd0",0)==0;
            const bool shoulder=name=="lh1"||name=="rh1";
            std::vector<CPlot3D> vertices;float armLength2=0;
            for(const auto& component:figure->components) {
                const auto index=component.position;CPlot3D p(0,0,0);
                for(unsigned s=0;s<figure->shape_count;++s) {
                    const auto& group=figure->positions[s*figure->position_groups+index/4];const auto lane=index%4;
                    p+=CPlot3D(group.x[lane],group.y[lane],group.z[lane])*(figure->shape_count==1?1:weights[s]);
                }
                vertices.push_back(p);if(shoulder)armLength2=(std::max)(armLength2,p*p);
            }
            for(const auto& vertex:vertices) {
                const auto p=rotations[i].Rotate(vertex)+positions[i];
                if(!Navigation::Finite(GamePoint(p)))throw std::runtime_error("Nav64: non-finite body geometry: "+mask+"/"+node.name);
                lowZ=(std::min)(lowZ,p.z);highZ=(std::max)(highZ,p.z);
                if(name!="lh1"&&name!="rh1"&&name!="lh2"&&name!="rh2"&&name!="lh3"&&name!="rh3"){bodyLowX=(std::min)(bodyLowX,p.x);bodyHighX=(std::max)(bodyHighX,p.x);}
                // The proximal quarter of the upper arm is the shoulder, not an outstretched arm.
                if(pose==0&&(torso||(shoulder&&vertex*vertex<=armLength2*.0625f))) {lowX=(std::min)(lowX,p.x);highX=(std::max)(highX,p.x);}
            }
        }
        measured.height[pose]=highZ-lowZ;
        if(pose==0)measured.radius=(highX>=lowX?highX-lowX:bodyHighX-bodyLowX)*.5f;
        if(!std::isfinite(measured.height[pose])||measured.height[pose]<=0)throw std::runtime_error("Nav64: invalid body height: "+mask);
    }
    if(!std::isfinite(measured.radius)||measured.radius<=0)throw std::runtime_error("Nav64: missing torso/shoulder geometry: "+mask);
    return bodyTable.samples.emplace(key,measured).first->second;
}
float NavigationBake::GroundHeight(Point p) {
    // Match CMapObject::NewVerticalPos on the client: the MPR surface at
    // the object's origin, not the averaged half-metre legacy AI cell.
    if(!terrain.tileWidth) {terrain.Bounds();IndexSources(terrain,9.6f);}
    const auto found=terrain.tileTriangles.find({int(std::floor((p.x-terrain.minimum.x)/terrain.tileWidth)),int(std::floor((-p.y-terrain.minimum.z)/terrain.tileWidth))});
    if(found!=terrain.tileTriangles.end())for(auto index:found->second) {
        const auto& t=terrain.ground[index];
        const float ax=t.b.x-t.a.x,ay=t.b.y-t.a.y,bx=t.c.x-t.a.x,by=t.c.y-t.a.y,det=ax*by-ay*bx;
        if(std::abs(det)<1.e-8f)continue;
        const float u=((p.x-t.a.x)*by-(p.y-t.a.y)*bx)/det,v=(ax*(p.y-t.a.y)-ay*(p.x-t.a.x))/det;
        if(u>=-.00001f&&v>=-.00001f&&u+v<=1.00001f)return t.a.z+u*(t.b.z-t.a.z)+v*(t.c.z-t.a.z);
    }
    throw std::runtime_error("Nav64: object origin outside MPR surface at "+std::to_string(p.x)+","+std::to_string(p.y));
}
void NavigationBake::Object(CMapObject* object,bool remove,CAIMap& map) {
    if(!NavigationObject(object))return;
    const auto idValue=object->GetID();
    const auto kind=object->WhoAmI();
    if(authored.doors.count(idValue)||(kind==HPC_LEVER&&static_cast<CLeverObject*>(object)->IsNavigationDoor())) {
        if(remove){doors.erase(idValue);return;}
        if(authored.doors.count(idValue))doors[idValue]=authored.doors.at(idValue);
        else {
            object->CreateCurrentPlacement();Door d;d.object=idValue;
            d.minimum={FLT_MAX,FLT_MAX,FLT_MAX};d.maximum={-FLT_MAX,-FLT_MAX,-FLT_MAX};
            auto base=object->GetCP();base.z+=GroundHeight(GamePoint(base));bool solid=false;
            for(INT_PTR i=0;i<object->fbvs.GetSize();++i) {
                const auto& bv=object->fbvs[i];if(bv.IsIgnored()||bv.GetPassability()!=AI_PASS_NO)continue;
                solid=true;for(int j=0;j<8;++j) {
                    auto p=object->RotateVector(CPlot3D((j&1)?bv.ptMax.x:bv.ptMin.x,(j&2)?bv.ptMax.y:bv.ptMin.y,(j&4)?bv.ptMax.z:bv.ptMin.z)+bv.ptBone+bv.ptCenter)+base;
                    d.minimum.x=(std::min)(d.minimum.x,p.x);d.minimum.y=(std::min)(d.minimum.y,p.y);d.minimum.z=(std::min)(d.minimum.z,p.z);
                    d.maximum.x=(std::max)(d.maximum.x,p.x);d.maximum.y=(std::max)(d.maximum.y,p.y);d.maximum.z=(std::max)(d.maximum.z,p.z);
                }
            }
            if(!solid)throw std::runtime_error("Nav64: open door needs explicit navsrc aperture NID="+std::to_string(idValue));
            d.minimum.z-=.35f;doors[idValue]=d;
        }
        return;
    }
    const bool staticKind=kind==HPC_TREE||kind==HPC_STONE||kind==HPC_BUILDING;
    const bool fixed=authored.fixed.count(idValue)||(!authored.dynamic.count(idValue)&&staticKind&&!object->IsDynamic());
    if(!fixed)return;
    if(remove)staticIds.erase(idValue);else staticIds.insert(idValue);
    auto id=object->GetID();
    // Move/orient/lever updates remove and add synchronously. Compare their
    // final contribution at the next query, not their intermediate absence.
    if(remove){objects.erase(id);return;}
    object->CreateCurrentPlacement();
    auto* specific=static_cast<CMapObjectServer*>(object->GetMapObjectSpecific());
    const CStringArray& parts=specific->szNames.GetSize()?specific->szNames:object->GetBodyPartNames();
    Navigation::Geometry input;
    bool hasTrunk=false;
    for(INT_PTR i=0;i<object->fbvs.GetSize();++i) {
        const auto& bv=object->fbvs[i];
        if(!bv.IsIgnored()&&!bv.IsBridge()&&bv.GetPassability()==AI_PASS_NO)hasTrunk=true;
    }
    // Admit all selected part payloads before waiting for the first decoder.
    // Failure is reported by the original labelled per-part path below.
    for(INT_PTR n=0;n<parts.GetSize();++n) {
        const int i=object->GetFigureIndexByName(parts[n]);if(i<0||object->fbvs[i].IsIgnored())continue;
        if(object->GetEnvironment())continue;
        try {QueueFigure(*static_cast<CFigureManager*>(object->GetWorld()->GetFigureManager()),object->GetMaskName(),parts[n]);}catch(const std::exception&) {}
    }
    constexpr float canopyCell=.3f;
    std::map<std::pair<int,int>,float> canopy;
    for(INT_PTR n=0;n<parts.GetSize();++n){int i=object->GetFigureIndexByName(parts[n]);if(i<0)continue;
        const auto& bv=object->fbvs[i];
        if(bv.IsIgnored())continue;
        const bool groundPart=!bv.IsIgnored();
        const auto partName=Navigation::ResourceName(parts[n]);
        const bool walkable=bv.IsBridge()||(authored.walkable.count(id)&&authored.walkable.at(id).count(partName));
        const bool foliage=!walkable&&bv.GetPassability()!=AI_PASS_NO;
        const bool groundFoliage=!foliage||(kind==HPC_TREE&&hasTrunk);
        {
            std::ostringstream label;label<<"object NID="<<id<<" model="<<object->GetMaskName()<<" part="<<parts[n].GetString();
            try {
                const auto figure=object->GetEnvironment()?object->GetEnvironment()->parts.at(i).geometry:Figure(*static_cast<CFigureManager*>(object->GetWorld()->GetFigureManager()),object->GetMaskName(),parts[n]);const auto& c=object->GetComplection();
                const float shape[3]={c.strength,c.grace,c.tall};float weights[8]{};
                for(unsigned s=0;s<figure->shape_count;++s) {
                    weights[s]=1;
                    if(figure->shape_count!=1)for(int axis=0;axis<3;++axis)weights[s]*=(s&(1u<<axis))?shape[axis]:1-shape[axis];
                }
                auto base=object->GetCP();base.z+=GroundHeight(GamePoint(base));
                std::vector<Navigation::Point> vertices(figure->components.size());
                auto transform=[&](size_t begin,size_t end) {for(size_t j=begin;j<end;++j) {
                    const auto index=figure->components[j].position;CPlot3D p(0,0,0);
                    for(unsigned s=0;s<figure->shape_count;++s) {
                        const auto& group=figure->positions[s*figure->position_groups+index/4];const auto lane=index%4;
                        p+=CPlot3D(group.x[lane],group.y[lane],group.z[lane])*weights[s];
                    }
                    // FIG vertices are origin-relative; ptCenter belongs only
                    // to the collision BV. BON already contains linked offsets.
                    p=object->RotateVector(p+bv.ptBone)+base;vertices[j]=GamePoint(p);
                    if(!Navigation::Finite(vertices[j]))throw std::runtime_error("non-finite transformed triangle vertex");
                }
                };
                if(vertices.size()<4096)transform(0,vertices.size());
                else {
                    const auto csr=_mm_getcsr();std::array<std::future<void>,4> jobs;
                    for(size_t k=0;k<jobs.size();++k)jobs[k]=std::async(std::launch::async,[&,k]{_mm_setcsr(csr);transform(vertices.size()*k/jobs.size(),vertices.size()*(k+1)/jobs.size());});
                    for(auto& job:jobs)job.get();
                }
                FlightBox top;top.object=id;top.minimum={FLT_MAX,FLT_MAX,FLT_MAX};top.maximum={-FLT_MAX,-FLT_MAX,-FLT_MAX};
                for(auto p:vertices) {
                    top.minimum.x=(std::min)(top.minimum.x,p.x);top.minimum.y=(std::min)(top.minimum.y,p.y);top.minimum.z=(std::min)(top.minimum.z,p.z);
                    top.maximum.x=(std::max)(top.maximum.x,p.x);top.maximum.y=(std::max)(top.maximum.y,p.y);top.maximum.z=(std::max)(top.maximum.z,p.z);
                }
                if(!vertices.empty())input.flightBoxes.push_back(top);
                if(!groundPart||!groundFoliage)continue;
                const auto* alpha=foliage?&Canopy(*specific,*figure):nullptr;
                const size_t first=input.ground.size();input.groundSources.emplace(first,label.str());
                for(size_t j=0;j<figure->indices.size();j+=3) {
                    Triangle t{vertices[figure->indices[j]],vertices[figure->indices[j+1]],vertices[figure->indices[j+2]],walkable?map.NavigationBridgeArea():BYTE(0)};
                    if(foliage) {
                        // Clip world position and UV together. Only visible texture
                        // coverage contributes to the lower envelope of this column.
                        const int x0=int(std::floor((std::min)({t.a.x,t.b.x,t.c.x})/canopyCell)),x1=int(std::floor((std::max)({t.a.x,t.b.x,t.c.x})/canopyCell)),
                            y0=int(std::floor((std::min)({t.a.y,t.b.y,t.c.y})/canopyCell)),y1=int(std::floor((std::max)({t.a.y,t.b.y,t.c.y})/canopyCell));
                        NavigationCanopy::Polygon triangle;
                        for(size_t k=0;k<3;++k) {
                            const auto index=figure->indices[j+k];const auto& uv=figure->texture_coordinates[figure->components[index].texture];
                            triangle.push_back({vertices[index],uv.u,uv.v});
                        }
                        for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x) {
                            auto clipped=NavigationCanopy::Clip(triangle,0,x*canopyCell,true);
                            clipped=NavigationCanopy::Clip(clipped,0,(x+1)*canopyCell,false);
                            clipped=NavigationCanopy::Clip(clipped,1,y*canopyCell,true);
                            clipped=NavigationCanopy::Clip(clipped,1,(y+1)*canopyCell,false);
                            const auto key=std::make_pair(x,y);const auto previous=canopy.find(key);
                            const float low=alpha->Minimum(clipped,previous==canopy.end()?FLT_MAX:previous->second);
                            if(low==FLT_MAX)continue;
                            if(previous==canopy.end())canopy.emplace(key,low);else previous->second=low;
                        }
                        continue;
                    }
                    t.object=id;t.surface=walkable?(std::uint64_t(unsigned(id))<<32)|unsigned(i+2):0;
                    input.ground.push_back(t);
                }
            }catch(const std::exception& e) {
                Navigation::Solid failure{};failure.error="Nav64: invalid walkable geometry "+label.str()+" reason="+e.what();input.solids.push_back(std::move(failure));
            }
        }
    }
    if(!canopy.empty()) {
        CeilingGrid grid;grid.object=id;grid.x=grid.y=INT_MAX;
        int x1=INT_MIN,y1=INT_MIN;
        for(const auto& cell:canopy) {
            grid.x=(std::min)(grid.x,cell.first.first);grid.y=(std::min)(grid.y,cell.first.second);
            x1=(std::max)(x1,cell.first.first);y1=(std::max)(y1,cell.first.second);
        }
        grid.width=unsigned(x1-grid.x+1);grid.height=unsigned(y1-grid.y+1);grid.base=terrain.minimum.y-2;
        grid.heights.assign(size_t(grid.width)*grid.height,0xffff);
        for(const auto& cell:canopy) {
            const int height=int(std::floor((cell.second-grid.base)/Agents[Human].profile.cellHeight));
            if(height<0||height>=0xffff)throw std::runtime_error("Nav64: canopy height outside grid range");
            grid.heights[size_t(cell.first.second-grid.y)*grid.width+cell.first.first-grid.x]=static_cast<std::uint16_t>(height);
        }
        input.ceilings.push_back(std::move(grid));
    }
    if(input.solids.empty()&&input.ground.empty()&&input.ceilings.empty()&&input.flightBoxes.empty())objects.erase(id);else objects[id]=std::move(input);
}

Navigation::Geometry NavigationBake::Geometry() {
    Navigation::Geometry input=terrain;
    for(const auto& door:doors)input.doors.push_back(door.second);
    for(const auto& entry:objects) {
        const auto& object=entry.second;const auto first=input.ground.size();
        for(const auto& source:object.groundSources)input.groundSources.emplace(first+source.first,source.second);
        input.ground.insert(input.ground.end(),object.ground.begin(),object.ground.end());
        input.solids.insert(input.solids.end(),object.solids.begin(),object.solids.end());
        input.ceilings.insert(input.ceilings.end(),object.ceilings.begin(),object.ceilings.end());
        input.flightBoxes.insert(input.flightBoxes.end(),object.flightBoxes.begin(),object.flightBoxes.end());
    }
    // Close sub-cell gaps between facing walkable BASE edges, including
    // neighbouring objects. Each half retains its original plane and source;
    // a small step stays a step, and steep side faces never become floors.
    struct Edge { Point a,b; size_t triangle=0; unsigned count=0; };
    using Vertex=std::array<float,3>;
    using EdgeKey=std::pair<Vertex,Vertex>;
    std::map<std::uint64_t,std::map<EdgeKey,Edge>> boundaries;
    const float slopeCos=std::cos(40.f*FP_PI/180.f),gapLimit=.1f,climb=.35f;
    for(size_t i=0;i<input.ground.size();++i) {
        const auto& t=input.ground[i];if(t.terrain||!t.area||t.surface<=1)continue;
        const auto a=t.a,b=t.b,c=t.c;
        const float nx=(b.y-a.y)*(c.z-a.z)-(b.z-a.z)*(c.y-a.y),
            ny=(b.z-a.z)*(c.x-a.x)-(b.x-a.x)*(c.z-a.z),
            nz=(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
        if(nz<=1.e-8f||nz/std::sqrt(nx*nx+ny*ny+nz*nz)<=slopeCos)continue;
        auto& group=boundaries[t.surface];
        for(auto edge:std::array<std::pair<Point,Point>,3>{{{a,b},{b,c},{c,a}}}) {
            Vertex va{edge.first.x,edge.first.y,edge.first.z},vb{edge.second.x,edge.second.y,edge.second.z};
            const EdgeKey key=va<vb?EdgeKey{va,vb}:EdgeKey{vb,va};
            auto inserted=group.emplace(key,Edge{edge.first,edge.second,i,0});++inserted.first->second.count;
        }
    }
    std::vector<Triangle> seams;
    auto plane=[](const Triangle& t,Point p) {
        const float ax=t.b.x-t.a.x,ay=t.b.y-t.a.y,bx=t.c.x-t.a.x,by=t.c.y-t.a.y,det=ax*by-ay*bx;
        const float u=((p.x-t.a.x)*by-(p.y-t.a.y)*bx)/det,v=(ax*(p.y-t.a.y)-ay*(p.x-t.a.x))/det;
        p.z=t.a.z+u*(t.b.z-t.a.z)+v*(t.c.z-t.a.z);return p;
    };
    constexpr float edgeBin=4.f;
    std::vector<Edge> edges;std::map<std::pair<int,int>,std::vector<size_t>> edgeBins;
    auto lowBin=[&](const Edge& e){return std::make_pair(int(std::floor(((std::min)(e.a.x,e.b.x)-gapLimit)/edgeBin)),int(std::floor(((std::min)(e.a.y,e.b.y)-gapLimit)/edgeBin)));};
    for(const auto& group:boundaries)for(const auto& entry:group.second)if(entry.second.count==1) {
        const auto& e=entry.second;const auto low=lowBin(e);const size_t index=edges.size();edges.push_back(e);
        const int x1=int(std::floor(((std::max)(e.a.x,e.b.x)+gapLimit)/edgeBin)),y1=int(std::floor(((std::max)(e.a.y,e.b.y)+gapLimit)/edgeBin));
        for(int y=low.second;y<=y1;++y)for(int x=low.first;x<=x1;++x)edgeBins[{x,y}].push_back(index);
    }
    for(const auto& bin:edgeBins) {
        const auto& candidates=bin.second;
        for(size_t i=0;i<candidates.size();++i)for(size_t j=i+1;j<candidates.size();++j) {
            const auto a=edges[candidates[i]],b=edges[candidates[j]];
            const auto alow=lowBin(a),blow=lowBin(b);
            if(bin.first!=std::make_pair((std::max)(alow.first,blow.first),(std::max)(alow.second,blow.second)))continue;
            const float length=std::hypot(a.b.x-a.a.x,a.b.y-a.a.y),other=std::hypot(b.b.x-b.a.x,b.b.y-b.a.y);
            if(length<.001f||other<.001f)continue;
            const float dx=(a.b.x-a.a.x)/length,dy=(a.b.y-a.a.y)/length,
                vx=b.b.x-b.a.x,vy=b.b.y-b.a.y,dot=dx*vx+dy*vy;
            if(dot>-.98f*other)continue;
            const float d0=dx*(b.a.y-a.a.y)-dy*(b.a.x-a.a.x),d1=dx*(b.b.y-a.a.y)-dy*(b.b.x-a.a.x);
            if(d0>=-.0001f||d1>=-.0001f||d0< -gapLimit||d1< -gapLimit)continue;
            const float t0=dx*(b.a.x-a.a.x)+dy*(b.a.y-a.a.y),t1=t0+dot,
                lo=(std::max)(0.f,t1),hi=(std::min)(length,t0);
            if(hi-lo<.001f)continue;
            Point ap{a.a.x+dx*lo,a.a.y+dy*lo,0},aq{a.a.x+dx*hi,a.a.y+dy*hi,0},
                bp{b.a.x+vx*((lo-t0)/dot),b.a.y+vy*((lo-t0)/dot),0},
                bq{b.a.x+vx*((hi-t0)/dot),b.a.y+vy*((hi-t0)/dot),0},
                mp{(ap.x+bp.x)*.5f,(ap.y+bp.y)*.5f,0},mq{(aq.x+bq.x)*.5f,(aq.y+bq.y)*.5f,0};
            const auto& ta=input.ground[a.triangle];const auto& tb=input.ground[b.triangle];
            if(std::abs(plane(ta,mp).z-plane(tb,mp).z)>climb||std::abs(plane(ta,mq).z-plane(tb,mq).z)>climb)continue;
            auto add=[&](const Triangle& source,Point p,Point q,Point v) {
                Triangle t=source;t.a=plane(source,p);t.b=plane(source,q);t.c=plane(source,v);
                if((t.b.x-t.a.x)*(t.c.y-t.a.y)-(t.b.y-t.a.y)*(t.c.x-t.a.x)<0)std::swap(t.b,t.c);
                seams.push_back(t);
            };
            add(ta,ap,mp,mq);add(ta,ap,mq,aq);add(tb,mp,bp,bq);add(tb,mp,bq,mq);
        }
    }
    if(!seams.empty())input.groundSources.emplace(input.ground.size(),"derived facing-BASE sub-cell support seams");
    input.ground.insert(input.ground.end(),seams.begin(),seams.end());
    input.Bounds();return input;
}
Navigation::Bytes NavigationBake::Bodies(CWorldServer& world,const Navigation::Bytes& previous) {
    std::set<std::string> models,missing;
    // Partial map/model releases retain the other maps' model coverage, but
    // remeasure it from current resources instead of copying old body sizes.
    if(!previous.empty())for(const auto& entry:Navigation::LoadBodies(previous).samples)
        models.insert(std::get<0>(entry.first));
    auto* manager=static_cast<CFigureManager*>(world.GetFigureManager());
    for(int i=0;i<theUnitDatabase.prototypes.GetNumEntries();++i) {
        const auto& prototype=*theUnitDatabase.prototypes[i];
        const auto* race=theUnitDatabase.races[int(prototype.dwRaceIndex)];
        if(!race||race->szMaskName.IsEmpty())throw std::runtime_error("Nav64: prototype has no body model");
        const auto mask=Navigation::ResourceName(race->szMaskName);
        if(missing.count(mask))continue;
        if(!models.count(mask)&&!manager->GetModel(mask)) {
            const auto name=mask+".lnk";auto* links=manager->GetFile(name.c_str());
            if(!links) {
                // Some historical prototypes have no effective model. They
                // cannot spawn in the game either; never invent body sizes.
                missing.insert(mask);printf("BODY_UNAVAILABLE model=%s reason=no_model_or_skeleton\n",mask.c_str());fflush(stdout);continue;
            }
            manager->CloseFile(links);
        }
        models.insert(mask);Body(world,mask,prototype.fStrength,prototype.fGrace,prototype.fTall);
    }
    for(const char* pattern:{"un*.mod","un*.lnk"}) {
        CStringArray names;manager->GetFileList(names,pattern);
        for(INT_PTR i=0;i<names.GetSize();++i) {
            const auto mask=Navigation::ResourceName(std::filesystem::path(names[i].GetString()).stem().string().c_str());
            if(!missing.count(mask))models.insert(mask);
        }
    }
    for(const auto& mask:models)for(int strength=0;strength<=2;++strength)for(int grace=0;grace<=2;++grace)for(int tall=0;tall<=2;++tall)
        Body(world,mask,float(strength),float(grace),float(tall));
    return Navigation::StoreBodies(bodyTable);
}
std::uint64_t NavigationBake::Bake(const std::string& mapName,const std::string& mobName,const std::filesystem::path& directory,Navigation::Bytes& table,const Navigation::Bytes& authored) {
    bodyTable=Navigation::LoadBodies(table);
    // MOB units may use models outside the conventional un* resource names.
    // Measure those real bodies offline and retain their full morph grid in
    // the shared campaign table; the game still only reads that table.
    const auto previousSamples=bodyTable.samples.size();
    for(const auto& entry:sourceWorld->mapObjects)if(entry.second->WhoAmI()==HPC_UNIT) {
        auto* unit=static_cast<CUnit*>(entry.second);
        const auto mask=ResourceName(unit->GetMaskName());
        if(!bodyTable.samples.count({mask,0.f,0.f,0.f}))
            for(int strength=0;strength<=2;++strength)for(int grace=0;grace<=2;++grace)for(int tall=0;tall<=2;++tall)
                Body(*sourceWorld,mask,float(strength),float(grace),float(tall));
    }
    if(bodyTable.samples.size()!=previousSamples) {
        auto expanded=StoreBodies(bodyTable);
        Atomic(directory/BodyFilename,expanded);table=std::move(expanded);
        printf("BODIES_EXTENDED models_and_shapes=%zu bytes=%zu\n",bodyTable.samples.size(),table.size());fflush(stdout);
    }
    // Build, runtime support and tile rebuild consume lossless imported planes.
    const auto sourceStart=std::chrono::steady_clock::now();
    SourceReuse sourceReuse;
    const auto sourceStem=std::filesystem::path(mapName).stem().string()+"--"+std::filesystem::path(mobName).stem().string();
    try {
        auto read=[](const std::filesystem::path& path) {
            std::ifstream file(path,std::ios::binary|std::ios::ate);const auto size=file.tellg();
            if(size<0||size>512ll*1024*1024)throw std::runtime_error("Nav64: unavailable source cache");
            Bytes bytes(static_cast<size_t>(size));file.seekg(0);
            if(!file.read(reinterpret_cast<char*>(bytes.data()),bytes.size()))throw std::runtime_error("Nav64: unreadable source cache");
            return bytes;
        };
        const auto bytes=read(CacheDirectory()/(sourceStem+".navcache"));auto cache=nlohmann::json::parse(bytes.begin(),bytes.end());
        const auto checksum=cache.at("checksum").get<Bytes>();cache.erase("checksum");const auto text=cache.dump();
        if(cache.at("version")!=1||cache.at("algorithm")!=AlgorithmVersion||checksum!=Digest(Bytes(text.begin(),text.end())))
            throw std::runtime_error("Nav64: stale source cache");
        sourceReuse.previous=read(directory/(sourceStem+".navgeom"));
        if(cache.at("sources").get<Bytes>()!=Digest(sourceReuse.previous))throw std::runtime_error("Nav64: stale source cache");
        sourceReuse.keys=cache.at("source_keys").get<Bytes>();
    }catch(const std::exception&) {sourceReuse={};}
    auto input=Geometry();IndexSources(input,tileCells*.1f);auto sourceBytes=StoreSources(input,&sourceReuse);
    const auto sourceStoreMs=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-sourceStart).count();
    auto sources=LoadSources(sourceBytes);const auto& geometry=*sources;
    std::vector<Navigation::BuildRequest> requests;
    for(int type=0;type<AgentCount;++type){
        auto p=Agents[type].profile;p.tileCells=tileCells;BuildRequest request{p,{}};
        requests.push_back(std::move(request));
    }
    const auto cacheStem=std::filesystem::path(mapName).stem().string()+"--"+std::filesystem::path(mobName).stem().string();
    auto read=[](const std::filesystem::path& path) {
        std::ifstream file(path,std::ios::binary|std::ios::ate);
        const auto size=file.tellg();
        if(size<0||size>512ll*1024*1024)throw std::runtime_error("Nav64: unavailable cache");
        Bytes bytes(static_cast<size_t>(size));file.seekg(0);
        if(!file.read(reinterpret_cast<char*>(bytes.data()),bytes.size()))throw std::runtime_error("Nav64: unreadable cache");
        return bytes;
    };
    // This disposable companion binds the input signatures to the complete
    // published set. A missing, interrupted or damaged set is a full bake.
    try {
        const auto bytes=read(CacheDirectory()/(cacheStem+".navcache"));
        auto cache=nlohmann::json::parse(bytes.begin(),bytes.end());
        const auto checksum=cache.at("checksum").get<Bytes>();cache.erase("checksum");
        const auto text=cache.dump();
        if(cache.at("version")!=1||cache.at("algorithm")!=AlgorithmVersion||
            checksum!=Digest(Bytes(text.begin(),text.end()))||
            cache.at("sources").get<Bytes>()!=Digest(read(directory/(cacheStem+".navgeom")))||
            cache.at("world").get<Bytes>()!=Digest(read(directory/(cacheStem+".navworld"))))
            throw std::runtime_error("Nav64: stale cache");
        for(int type=0;type<AgentCount;++type) {
            auto bytes=read(directory/MeshFilename(mapName,mobName,type));
            const auto& profile=cache.at("profiles").at(type);
            if(profile.at("mesh").get<Bytes>()!=Digest(bytes))throw std::runtime_error("Nav64: stale mesh cache");
            requests[type].previous=Navigation::Load(bytes,requests[type].profile,mapName,mobName,Agents[type].name);
            requests[type].previousBytes=std::move(bytes);
            requests[type].tileKeys=profile.at("keys").get<Bytes>();
        }
    }catch(const std::exception&) {
        for(auto& request:requests){request.previous.reset();request.tileKeys.clear();request.previousBytes.clear();}
    }
    std::map<int,std::vector<Solid>> initialObjects,initialFlightObjects;
    auto initial=geometry;
    for(const auto& entry:sourceWorld->mapObjects) {
        auto* object=entry.second;
        if(!NavigationObject(object)||staticIds.count(entry.first))continue;
        auto visual=NavigationBoxes(object,*sourceMap,true);
        initial.flightSolids.insert(initial.flightSolids.end(),visual.begin(),visual.end());
        initialFlightObjects.emplace(entry.first,std::move(visual));
        if(doors.count(entry.first))continue;
        auto boxes=NavigationBoxes(object,*sourceMap);
        initial.solids.insert(initial.solids.end(),boxes.begin(),boxes.end());
        initialObjects.emplace(entry.first,std::move(boxes));
    }
    // Initial dynamic occupancy is baked once; later worker rebuilds use the
    // static source triangles plus the current live obstacle snapshot.
    const auto tilesStart=std::chrono::steady_clock::now();
    const auto sourceMs=std::chrono::duration_cast<std::chrono::milliseconds>(tilesStart-sourceStart).count();
    auto pending=std::async(std::launch::async,[&]{return Navigation::BuildProfiles(initial,requests);});
    if(overlap)while(pending.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready)overlap();
    auto built=pending.get();
    const auto tilesMs=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-tilesStart).count();
    for(auto& mesh:built){mesh->sources=sources;IndexSurfaces(*mesh);}
    std::uint64_t totalBytes=0;std::vector<Navigation::Bytes> outputs(AgentCount);
    std::array<bool,AgentCount> retained{};
    for(int type=0;type<AgentCount;++type)retained[type]=built[type]->rebuilt==0&&!requests[type].previousBytes.empty();
    for(int type=0;type<AgentCount;++type) {
        auto& mesh=*built[type];mesh.mapName=mapName;mesh.mobName=mobName;mesh.profileId=Agents[type].name;CheckReleaseHeights(mesh);
    }
    const auto serializeStart=std::chrono::steady_clock::now();
    std::array<std::uint64_t,AgentCount> storeMs{},writeMs{};
    auto serialize=[&](int type) {
        const auto start=std::chrono::steady_clock::now();outputs[type]=retained[type]?std::move(requests[type].previousBytes):Navigation::Store(*built[type]);
        storeMs[type]=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
    };
    // Independent LZMS streams, one per existing profile container. The bytes
    // within a stream retain their original order and compression settings.
    std::vector<std::future<void>> serializers;
    for(int type=0;type<AgentCount;++type)serializers.push_back(std::async(std::launch::async,[&,type]{serialize(type);}));
    for(auto& job:serializers)job.get();
    for(int type=0;type<AgentCount;++type) {
        totalBytes+=outputs[type].size();
        // The cached container was authenticated and decoded before BuildProfiles.
        // Its unchanged tiles already populate this queryable mesh.
        if(retained[type])continue;
        auto ready=Navigation::Load(outputs[type],built[type]->profile,mapName,mobName,Agents[type].name);
        ready->rebuilt=built[type]->rebuilt;ready->tileKeys=std::move(built[type]->tileKeys);
        ready->buildMs=built[type]->buildMs;ready->rasterMs=built[type]->rasterMs;ready->polygonMs=built[type]->polygonMs;
        ready->stagesMs=built[type]->stagesMs;
        ready->sources=sources;IndexSurfaces(*ready);built[type]=std::move(ready);
    }
    const auto qualityStart=std::chrono::steady_clock::now();
    const auto serializeMs=std::chrono::duration_cast<std::chrono::milliseconds>(qualityStart-serializeStart).count();
    {
        const auto& mesh=*built[Flying];unsigned samples=0;float minimum=FLT_MAX;
        auto check=[&](Point p) {
            const auto& g=geometry;const float edge=.2f;
            if(p.x<g.minimum.x+edge||p.x>g.maximum.x-edge||-p.y<g.minimum.z+edge||-p.y>g.maximum.z-edge)return;
            Location at;
            if(!Project(mesh,p,.001f,1.e6f,at,.2f))throw std::runtime_error("Nav64: missing flight coverage at "+std::to_string(p.x)+","+std::to_string(p.y)+","+std::to_string(p.z));
            const float gap=at.position.z-p.z;minimum=(std::min)(minimum,gap);++samples;
            if(gap<.1f)throw std::runtime_error("Nav64: flight intersects imported obstacle");
        };
        for(const auto& box:geometry.flightBoxes) {
            const auto a=box.minimum,b=box.maximum;
            for(auto p:{Point{a.x,a.y,b.z},Point{a.x,b.y,b.z},Point{b.x,a.y,b.z},Point{b.x,b.y,b.z},Point{(a.x+b.x)*.5f,(a.y+b.y)*.5f,b.z}})check(p);
        }
        for(const auto& box:initial.flightSolids)for(auto p:box.corners)check(p);
        for(const auto& t:geometry.ground)if(t.terrain)check({(t.a.x+t.b.x+t.c.x)/3,(t.a.y+t.b.y+t.c.y)/3,(t.a.z+t.b.z+t.c.z+t.waterDepth.x+t.waterDepth.y+t.waterDepth.z)/3});
        printf("FLIGHT_COVERAGE samples=%u visual_bounds=%zu minimum_clearance=%.6f\n",samples,geometry.flightBoxes.size(),minimum);fflush(stdout);
    }
    printf("CANDIDATE_BYTES mpr=%s mob=%s meshes=%llu sources=%zu\n",mapName.c_str(),mobName.c_str(),static_cast<unsigned long long>(totalBytes),sourceBytes.size());fflush(stdout);
    std::filesystem::create_directories(directory);
    // Release-data quality is part of the baker, on the actual map meshes.
    // No standalone test, simulation, game tick, script or renderer runs here.
    const auto stem=std::filesystem::path(mapName).stem().string()+"--"+std::filesystem::path(mobName).stem().string();
    std::filesystem::create_directories(reportDirectory);
    std::ofstream losses(reportDirectory/(stem+".losses.csv"));losses<<"profile,class,x,y,z,reason\n";
    std::ofstream water(reportDirectory/(stem+".water-gains.csv"));water<<"profile,x,y,z,depth,water_limit,forbidden,surface,legacy_reason\n";
    for(int type=0;type<AgentCount;++type) {
        const auto& mesh=*built[type];const int c=mesh.profile.materialClass;
        unsigned oldCount=0,lost=0,gained=0,waterGained=0,forbiddenWaterGained=0,stepCount=0,stepFailed=0,stepStalled=0;
        const float heights[]={2,.4f,1,1.6f,2.2f,1.7f,3,6};const float height=(std::min)(heights[c],ClearanceHeights(mesh.profile).back());
        for(int y=3;y<sourceMap->YSize()-3;++y)for(int x=3;x<sourceMap->XSize()-3;++x) {
            const bool oldPass=((sourceMap->map[y][x].dwTerrains>>(c*4))&15)!=0;
            if(oldPass)++oldCount;CPlot3D cp(AITile2Meter(x+.5f),AITile2Meter(y+.5f),0);
            Point p{cp.x,cp.y,c==0?sourceMap->GetTileSurface(cp):sourceMap->GetTileHeight(cp)};Location at;
            const bool onMesh=Project(mesh,p,.05f,4.f,at,height);
            if(!oldPass){if(onMesh){++gained;if(sourceMap->map[y][x].nDepth>0) {
                ++waterGained;const int depth=sourceMap->map[y][x].nDepth,limit=c==1?0:sourceMap->GetMapHeight(heights[c]);
                const bool forbidden=c!=0&&(depth==N_AI_MAX_DEPTH+1||depth>=limit-1);
                if(forbidden)++forbiddenWaterGained;
                water<<Agents[type].name<<','<<p.x<<','<<p.y<<','<<p.z<<','<<depth<<','<<limit<<','<<forbidden<<','<<at.surface<<','<<sourceMap->NavigationLegacyRejection(c,x,y)<<'\n';
            }}continue;}
            if(!onMesh) {++lost;losses<<Agents[type].name<<','<<c<<','<<p.x<<','<<p.y<<','<<p.z<<",no_polygon_or_clearance\n";continue;}
            if((x%16)==0&&(y%16)==0) {
                Point to=at.position;to.x+=.02f;Location target;
                if(Project(mesh,to,.001f,1.f,target,height)) {
                    ++stepCount;Location next;
                    const char* reason=nullptr;
                    if(!Constrain(mesh,at.position,to,next,height,&reason)) {
                        ++stepFailed;
                        printf("QUALITY_STEP_FAILED profile=%s class=%d from=(%.6f,%.6f,%.6f) to=(%.6f,%.6f,%.6f) reason=%s\n",Agents[type].name,c,at.position.x,at.position.y,at.position.z,to.x,to.y,to.z,reason?reason:"unknown");
                    }
                    else if(std::hypot(next.position.x-at.position.x,next.position.y-at.position.y)<.001f)++stepStalled;
                }
            }
        }
        const float cellArea=AITile2Meter(1.f)*AITile2Meter(1.f);
        printf("QUALITY mpr=%s mob=%s profile=%s class=%d legacy_cells=%u lost=%u lost_m2=%.2f gained=%u gained_m2=%.2f gained_water_m2=%.2f gained_forbidden_water_m2=%.2f step_samples=%u step_failed=%u step_stalled=%u\n",mapName.c_str(),mobName.c_str(),Agents[type].name,c,oldCount,lost,lost*cellArea,gained,gained*cellArea,waterGained*cellArea,forbiddenWaterGained*cellArea,stepCount,stepFailed,stepStalled);fflush(stdout);
    }
    std::ofstream connections(reportDirectory/(stem+".connections.csv"));
    connections<<"profile,id,model,surface,x,y,z,out_x,out_y,status,length\n";
    for(const auto& entry:objects) {
        std::vector<Point> samples;Point low{FLT_MAX,FLT_MAX,FLT_MAX},high{-FLT_MAX,-FLT_MAX,-FLT_MAX};
        std::uint64_t surface=0;
        for(const auto& t:entry.second.ground)if(t.area&&t.surface>1) {
            for(auto p:{t.a,t.b,t.c}) {low.x=(std::min)(low.x,p.x);low.y=(std::min)(low.y,p.y);low.z=(std::min)(low.z,p.z);high.x=(std::max)(high.x,p.x);high.y=(std::max)(high.y,p.y);high.z=(std::max)(high.z,p.z);}
            samples.push_back({(t.a.x+t.b.x+t.c.x)/3,(t.a.y+t.b.y+t.c.y)/3,(t.a.z+t.b.z+t.c.z)/3});surface=t.surface;
        }
        if(samples.empty())continue;
        auto* object=sourceWorld->GetMapObject(entry.first);const char* model=object?static_cast<LPCSTR>(object->GetMaskName()):"unknown";
        for(int type=0;type<AgentCount;++type) {
            const auto& mesh=*built[type];Location start;bool placed=false;
            const float classHeights[]={2,.4f,1,1.6f,2.2f,1.7f,3,6};const float height=classHeights[mesh.profile.materialClass];
            for(size_t k=0;k<samples.size();k+= (std::max)(size_t(1),samples.size()/32))if(Project(mesh,samples[k],.05f,.3f,start,height)&&(start.surface>>32)==unsigned(entry.first)){placed=true;break;}
            Route best;Point destination{};
            if(placed)for(Point outside:std::vector<Point>{{low.x-1,(low.y+high.y)*.5f,0},{high.x+1,(low.y+high.y)*.5f,0},{(low.x+high.x)*.5f,low.y-1,0},{(low.x+high.x)*.5f,high.y+1,0}}) {
                outside.z=sourceMap->GetTileHeight(CPlot3D(outside.x,outside.y,0));Location goal;
                if(!Project(mesh,outside,1.f,4.f,goal,height)||goal.surface!=1)continue;
                Request r;r.source=start.position;r.goal=goal.position;r.height=height;r.maxLength=100;
                auto route=Find(mesh,r);best=std::move(route);destination=goal.position;if(best.status==Status::Complete)break;
            }
            connections<<Agents[type].name<<','<<entry.first<<','<<model<<','<<(placed?start.surface:surface)<<','<<start.position.x<<','<<start.position.y<<','<<start.position.z<<','<<destination.x<<','<<destination.y<<','<<(placed?StatusName(best.status):"no_surface")<<','<<best.length<<'\n';
        }
    }
    std::ofstream units(reportDirectory/(stem+".placements.csv"));units<<"id,model,profile,x,y,z,distance,reason\n";
    for(const auto& entry:sourceWorld->mapObjects)if(entry.second->WhoAmI()==HPC_UNIT) {
        auto* unit=static_cast<CUnit*>(entry.second);auto cp=unit->GetCP();const auto& shape=unit->GetComplection();
        const auto size=bodyTable.Lookup(ResourceName(unit->GetMaskName()),shape.strength,shape.grace,shape.tall);
        const int type=unit->GetUserUnitStats()->cLocomotion==3?Flying:ProfileForRadius(size.radius,unit->GetAIClass()==0);const auto& mesh=*built[type];
        Point p{cp.x,cp.y,unit->GetAIClass()==0?sourceMap->GetTileSurface(cp):sourceMap->GetTileHeight(cp)};Location at;
        const float height=(std::min)(size.height[0],ClearanceHeights(mesh.profile).back());
        if(!Project(mesh,p,.05f,4.f,at,height)) {
            const bool withinPlacement=Project(mesh,p,1.f,4.f,at,height);const float distance=withinPlacement?std::hypot(at.position.x-p.x,at.position.y-p.y):-1;
            units<<unit->GetID()<<','<<unit->GetMaskName()<<','<<Agents[type].name<<','<<p.x<<','<<p.y<<','<<p.z<<','<<distance<<','<<(withinPlacement?"placement_within_1m":"no_polygon_within_1m")<<'\n';
        }
    }
    for(const auto& entry:normalizedBounds)printf("BOUNDS_NORMALIZED mpr=%s mob=%s %s\n",mapName.c_str(),mobName.c_str(),entry.c_str());
    const auto writeStart=std::chrono::steady_clock::now();
    const auto qualityMs=std::chrono::duration_cast<std::chrono::milliseconds>(writeStart-qualityStart).count();
    for(int type=0;type<AgentCount;++type) {
        const auto& mesh=*built[type];const auto& bytes=outputs[type];
        const auto writeStart=std::chrono::steady_clock::now();
        if(!retained[type])
            Atomic(directory/Navigation::MeshFilename(mapName,mobName,type),bytes);
        writeMs[type]=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-writeStart).count();
        unsigned polygons=0;const dtNavMesh& data=*mesh.data;
        for(int i=0;i<data.getMaxTiles();++i)if(const auto* h=data.getTile(i)->header)polygons+=h->polyCount;
        printf("PROFILE map=%s mob=%s type=%s radius=%.2f tiles=%u rebuilt=%u ms=%llu raster_cpu_ms=%llu polygons_cpu_ms=%llu bytes=%zu polygons=%u recast_raster_ms=%llu filters_compact_ms=%llu erosion_ms=%llu regions_contours_polys_ms=%llu detail_ms=%llu serialize_compress_ms=%llu write_ms=%llu height_outliers=0\n",
            mapName.c_str(),mobName.c_str(),Agents[type].name,mesh.profile.radius,mesh.tiles,mesh.rebuilt,
            static_cast<unsigned long long>(mesh.buildMs),static_cast<unsigned long long>(mesh.rasterMs),
            static_cast<unsigned long long>(mesh.polygonMs),bytes.size(),polygons,
            mesh.stagesMs[0],mesh.stagesMs[1],mesh.stagesMs[2],mesh.stagesMs[3],mesh.stagesMs[4],storeMs[type],writeMs[type]);
        fflush(stdout);
    }
    const auto decoded=LoadSources(sourceBytes);
    if(decoded->ground.size()!=geometry.ground.size()||decoded->doors.size()!=geometry.doors.size())throw std::runtime_error("Nav64: source serialization mismatch");
    Atomic(directory/(stem+".navgeom"),sourceBytes);totalBytes+=sourceBytes.size();
    printf("SOURCES triangles=%zu doors=%zu offmesh=%zu bytes=%zu\n",geometry.ground.size(),geometry.doors.size(),geometry.connections.size(),sourceBytes.size());fflush(stdout);
    nlohmann::json catalogue={{"version",4},{"static",staticIds},{"dynamic",nlohmann::json::array()},{"flight_dynamic",nlohmann::json::array()}};
    for(const auto& entry:initialObjects) {
        nlohmann::json boxes=nlohmann::json::array();
        for(const auto& box:entry.second) {
            nlohmann::json corners=nlohmann::json::array();
            for(auto p:box.corners)corners.push_back({p.x,p.y,p.z});
            boxes.push_back(std::move(corners));
        }
        catalogue["dynamic"].push_back({{"id",entry.first},{"boxes",std::move(boxes)}});
    }
    for(const auto& entry:initialFlightObjects) {
        nlohmann::json boxes=nlohmann::json::array();
        for(const auto& box:entry.second) {
            nlohmann::json corners=nlohmann::json::array();for(auto p:box.corners)corners.push_back({p.x,p.y,p.z});
            boxes.push_back(std::move(corners));
        }
        catalogue["flight_dynamic"].push_back({{"id",entry.first},{"boxes",std::move(boxes)}});
    }
    const auto text=catalogue.dump();Navigation::Bytes bytes(text.begin(),text.end());
    Atomic(directory/(std::filesystem::path(mapName).stem().string()+"--"+std::filesystem::path(mobName).stem().string()+".navworld"),bytes);
    nlohmann::json cache={{"version",1},{"algorithm",AlgorithmVersion},{"sources",Digest(sourceBytes)},{"source_keys",sourceReuse.keys},
        {"world",Digest(bytes)},{"profiles",nlohmann::json::array()}};
    for(int type=0;type<AgentCount;++type) {
        unsigned polygons=0;const dtNavMesh& data=*built[type]->data;
        for(int i=0;i<data.getMaxTiles();++i)if(const auto* h=data.getTile(i)->header)polygons+=h->polyCount;
        cache["profiles"].push_back({{"mesh",Digest(outputs[type])},{"keys",built[type]->tileKeys},{"tiles",built[type]->tiles},{"polygons",polygons}});
    }
    const auto unsignedText=cache.dump();cache["checksum"]=Digest(Bytes(unsignedText.begin(),unsignedText.end()));
    const auto cacheText=cache.dump();Atomic(CacheDirectory()/(stem+".navcache"),Bytes(cacheText.begin(),cacheText.end()));
    printf("PHASE_TIMING map=%s source_ms=%lld source_serialize_compress_ms=%lld tiles_ms=%lld serialize_compress_ms=%lld quality_ms=%lld write_ms=%lld\n",
        mapName.c_str(),sourceMs,sourceStoreMs,tilesMs,serializeMs,qualityMs,
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-writeStart).count());fflush(stdout);
    return totalBytes+bytes.size();
}
