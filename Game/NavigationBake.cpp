#include "StdAfx.h"
#include "NavigationBake.h"
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
    auto figure=std::make_shared<owned::FigureCpuAsset>();owned::ResourceFailure failure;
    if(!owned::parse_figure_asset(blob,*figure,failure))throw std::runtime_error("Nav64: "+key+": "+failure.message);
    figures.emplace(key,figure);return figure;
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
    constexpr float canopyCell=.3f;
    std::map<std::pair<int,int>,float> canopy;
    for(INT_PTR n=0;n<parts.GetSize();++n){int i=object->GetFigureIndexByName(parts[n]);if(i<0)continue;
        const auto& bv=object->fbvs[i];
        const bool groundPart=!bv.IsIgnored();
        const auto partName=Navigation::ResourceName(parts[n]);
        const bool walkable=bv.IsBridge()||(authored.walkable.count(id)&&authored.walkable.at(id).count(partName));
        const bool foliage=!walkable&&bv.GetPassability()!=AI_PASS_NO;
        const bool groundFoliage=!foliage||(kind==HPC_TREE&&hasTrunk);
        {
            std::ostringstream label;label<<"object NID="<<id<<" model="<<object->GetMaskName()<<" part="<<parts[n].GetString();
            try {
                const auto figure=Figure(*static_cast<CFigureManager*>(object->GetWorld()->GetFigureManager()),object->GetMaskName(),parts[n]);const auto& c=object->GetComplection();
                const float shape[3]={c.strength,c.grace,c.tall};float weights[8]{};
                for(unsigned s=0;s<figure->shape_count;++s) {
                    weights[s]=1;
                    if(figure->shape_count!=1)for(int axis=0;axis<3;++axis)weights[s]*=(s&(1u<<axis))?shape[axis]:1-shape[axis];
                }
                auto base=object->GetCP();base.z+=GroundHeight(GamePoint(base));
                std::vector<Navigation::Point> vertices(figure->components.size());
                for(size_t j=0;j<vertices.size();++j) {
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
            const int height=int(std::floor((cell.second-grid.base)/Agents[Small].profile.cellHeight));
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
    auto input=Geometry();IndexSources(input,tileCells*.1f);auto sourceBytes=StoreSources(input);
    auto sources=LoadSources(sourceBytes);const auto& geometry=*sources;
    std::vector<Navigation::BuildRequest> requests;
    for(int type=0;type<AgentCount;++type){
        auto p=Agents[type].profile;p.tileCells=tileCells;BuildRequest request{p,{}};
        if(mapName=="zone1.mpr"&&mobName=="zone1.mob"&&(type==Small||type==Human))
            request.columns={{198.75f,112.36f,0},{198.825f,112.36f,0},{198.9f,112.36f,0},{202.75f,112.5f,0},{202.825f,112.5f,0},{202.9f,112.5f,0}};
        requests.push_back(std::move(request));
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
    auto built=Navigation::BuildProfiles(initial,requests);
    for(auto& mesh:built){mesh->sources=sources;IndexSurfaces(*mesh);}
    std::uint64_t totalBytes=0;std::vector<Navigation::Bytes> outputs(AgentCount);
    for(int type=0;type<AgentCount;++type) {
        auto& mesh=*built[type];mesh.mapName=mapName;mesh.mobName=mobName;mesh.profileId=Agents[type].name;CheckReleaseHeights(mesh);
    }
    auto serialize=[&](int first){for(int type=first;type<AgentCount;type+=2)outputs[type]=Navigation::Store(*built[type]);};
    auto parallel=std::async(std::launch::async,[&]{serialize(1);});serialize(0);parallel.get();
    for(int type=0;type<AgentCount;++type) {
        auto ready=Navigation::Load(outputs[type],built[type]->profile,mapName,mobName,Agents[type].name);
        ready->buildMs=built[type]->buildMs;ready->rasterMs=built[type]->rasterMs;ready->polygonMs=built[type]->polygonMs;
        ready->sources=sources;IndexSurfaces(*ready);built[type]=std::move(ready);
        totalBytes+=outputs[type].size();
    }
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
    if(mapName=="zone1.mpr"&&mobName=="zone1.mob") {
        auto& mesh=*built[Small];Point start{55.55f,138.78f,19.24f};Location placed,next;
        const bool place=Project(mesh,start,1.f,4.f,placed,1.6f);
        Request request;request.source=place?placed.position:start;request.goal={58.9822f,139.653f,19.24f};request.height=1.6f;
        const auto route=Find(mesh,request);bool step=false;
        if(place&&route.status==Status::Complete&&route.points.size()>1) {
            auto to=route.points[1].position;const float length=std::hypot(to.x-placed.position.x,to.y-placed.position.y);
            if(length>0){to.x=placed.position.x+(to.x-placed.position.x)*.02f/length;to.y=placed.position.y+(to.y-placed.position.y)*.02f/length;step=Constrain(mesh,placed.position,to,next,1.6f);}
        }
        printf("HERO_START profile=small original=(55.55,138.78,19.24) place=%d placed=(%.5f,%.5f,%.5f) goal=(58.9822,139.653) route=%s step=%d moved=%.6f\n",int(place),placed.position.x,placed.position.y,placed.position.z,StatusName(route.status),int(step),step?std::hypot(next.position.x-placed.position.x,next.position.y-placed.position.y):0);fflush(stdout);
        if(!step||std::hypot(next.position.x-placed.position.x,next.position.y-placed.position.y)<.019f)throw std::runtime_error("Nav64: HERO_START does not advance");
        const float width=mesh.profile.tileCells*mesh.profile.cellSize;
        const int tx=int((start.x-geometry.minimum.x)/width),tz=int((-start.y-geometry.minimum.z)/width);
        const auto begin=std::chrono::steady_clock::now();
        auto rebuild=std::async(std::launch::async,[&]{
            Navigation::Geometry input;input.minimum=geometry.minimum;input.maximum=geometry.maximum;input.doors=geometry.doors;
            const float border=1.3f,lx=geometry.minimum.x+tx*width-border,lz=geometry.minimum.z+tz*width-border;
            for(const auto& t:geometry.ground){auto a=ToNav(t.a),b=ToNav(t.b),c=ToNav(t.c);
                if((std::max)({a.x,b.x,c.x})>=lx&&(std::min)({a.x,b.x,c.x})<=lx+width+2*border&&(std::max)({a.z,b.z,c.z})>=lz&&(std::min)({a.z,b.z,c.z})<=lz+width+2*border)input.ground.push_back(t);}
            return RebuildTile(input,mesh.profile,tx,tz);
        });
        auto replacement=rebuild.get();
        const double workerMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
        const auto apply=std::chrono::steady_clock::now();ReplaceTile(mesh,*replacement,tx,tz);
        const double applyMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-apply).count();
        printf("REBUILD tile_cells=%u tile=%d,%d worker_ms=%.3f apply_ms=%.3f\n",tileCells,tx,tz,workerMs,applyMs);fflush(stdout);
        // Anchors are actual zone1 sites, not synthetic corridor fixtures.
        struct Passage {const char* name;Point a,b;int first,last;};
        const Passage passages[]={
            {"start_exit",{55.55f,138.78f,19.24f},{78.f,145.f,14.f},Small,Human},
            {"main_gate",{188.f,108.f,6.2f},{188.f,116.f,6.2f},Small,Giant},
            {"elder_entrance",{197.98f,143.7f,6.3f},{197.98f,145.5f,6.3f},Small,Human},
            {"samvlrmp01",{197.592580f,118.819145f,6.199420f},{197.467460f,114.019735f,7.875580f},Small,Human}
        };
        for(const auto& passage:passages)for(int type=passage.first;type<=passage.last;++type) {
            const auto& candidate=*built[type];Location a,b;const float height=type==Giant?6.f:type==Large?3.f:1.6f;
            const bool source=Project(candidate,passage.a,.5f,4.f,a,height),goal=Project(candidate,passage.b,.5f,4.f,b,height);
            Request r;r.source=a.position;r.goal=b.position;r.height=height;r.maxLength=100;
            Route path;if(source&&goal)path=Find(candidate,r);
            printf("PASSAGE name=%s profile=%s source=%d goal=%d route=%s length=%.3f from=(%.3f,%.3f,%.3f) to=(%.3f,%.3f,%.3f)\n",passage.name,Agents[type].name,int(source),int(goal),StatusName(path.status),path.length,a.position.x,a.position.y,a.position.z,b.position.x,b.position.y,b.position.z);fflush(stdout);
        }

    }
    if(mapName=="zone1.mpr") {
        std::ofstream identities(reportDirectory/(stem+".objects.csv"));
        identities<<std::setprecision(9)<<"id,kind,model,x,y,z,owner,name,prototype,class,hero,collision_radius,body_radius,pose\n";
        for(const auto& entry:sourceWorld->mapObjects) {
            auto* o=entry.second;const auto p=o->GetCP();
            identities<<entry.first<<','<<int(o->WhoAmI())<<','<<o->GetMaskName()<<','<<p.x<<','<<p.y<<','<<p.z<<',';
            if(o->WhoAmI()==HPC_UNIT) {auto* u=static_cast<CUnit*>(o);identities<<u->GetOwnerNum()<<','<<u->GetName()<<','<<u->GetPrototype()->szName<<','<<u->GetAIClass()<<','<<u->IsHero()<<','<<u->GetCollisionDistance();const auto& c=u->GetComplection();identities<<','<<bodyTable.Lookup(ResourceName(u->GetMaskName()),c.strength,c.grace,c.tall).radius<<','<<int(u->GetCurrentPose());}
            identities<<'\n';
        }
        CBridgeManager legacyBridges;
        for(const auto& entry:sourceWorld->mapObjects) {
            auto* object=entry.second;if(object->WhoAmI()!=HPC_BUILDING&&object->WhoAmI()!=HPC_LEVER)continue;
            auto cp=object->GetCP();cp.z+=sourceMap->GetOriginalTileHeight(cp);
            for(INT_PTR i=0;i<object->fbvs.GetSize();++i)if(object->fbvs[i].IsBridge())
                legacyBridges.AddBridge(entry.first,object->fbvs[i],cp,object->GetOrientation());
        }
        for(auto point:{Point{66.8446f,140.352f,14.3403f},Point{66.7652f,140.422f,14.4166f},Point{66.8539f,140.519f,14.3892f},Point{66.9009f,140.942f,14.4771f},Point{66.5964f,140.981f,14.5257f},Point{67.1842f,140.672f,14.3528f}}) {
            Location exact;const bool placed=Project(*built[Small],point,.001f,4.f,exact,1.6f);
            const float old=legacyBridges.CalcZ(CPlot3D(point.x,point.y,0),point.z);
            printf("WALL_SUPPORT x=%.6f y=%.6f placed=%d source_z=%.6f legacy_client_z=%.6f excess=%.6f surface=%llu\n",point.x,point.y,int(placed),placed?exact.position.z:point.z,old,old-(placed?exact.position.z:point.z),static_cast<unsigned long long>(exact.surface));fflush(stdout);
        }
        std::ofstream wall(reportDirectory/(stem+".wall-parts.csv"));
        wall<<"id,model,part,bridge,passability,triangles,walkable,min_x,max_x,min_y,max_y,min_z,max_z\n";
        for(const auto& entry:objects) {
            auto* object=sourceWorld->GetMapObject(entry.first);if(!object)continue;
            std::map<unsigned,std::vector<const Triangle*>> parts;
            for(const auto& t:entry.second.ground) {
                const float x0=(std::min)({t.a.x,t.b.x,t.c.x}),x1=(std::max)({t.a.x,t.b.x,t.c.x}),
                    y0=(std::min)({t.a.y,t.b.y,t.c.y}),y1=(std::max)({t.a.y,t.b.y,t.c.y});
                if((x1<65.5f||x0>68.f||y1<139.5f||y0>142.f)&&(x1<190.f||x0>208.f||y1<111.f||y0>115.f))continue;
                const auto label=entry.second.groundSources.upper_bound(size_t(&t-entry.second.ground.data()));
                unsigned part=0;
                if(t.surface>1)part=unsigned(t.surface)-2;
                else if(label!=entry.second.groundSources.begin()) {
                    const auto& text=std::prev(label)->second;const auto at=text.find(" part=");
                    if(at!=std::string::npos)part=unsigned(object->GetFigureIndexByName(text.substr(at+6).c_str()));
                }
                parts[part].push_back(&t);
            }
            for(const auto& group:parts) {
                const auto part=group.first;if(part>=unsigned(object->fbvs.GetSize()))continue;
                Point low{FLT_MAX,FLT_MAX,FLT_MAX},high{-FLT_MAX,-FLT_MAX,-FLT_MAX};unsigned walkable=0;
                for(auto* t:group.second){if(t->area)++walkable;for(auto v:{t->a,t->b,t->c}){low.x=(std::min)(low.x,v.x);low.y=(std::min)(low.y,v.y);low.z=(std::min)(low.z,v.z);high.x=(std::max)(high.x,v.x);high.y=(std::max)(high.y,v.y);high.z=(std::max)(high.z,v.z);}}
                wall<<entry.first<<','<<object->GetMaskName()<<','<<object->GetBodyPartNames()[part].GetString()<<','<<object->fbvs[part].IsBridge()<<','<<object->fbvs[part].GetPassability()<<','<<group.second.size()<<','<<walkable<<','<<low.x<<','<<high.x<<','<<low.y<<','<<high.y<<','<<low.z<<','<<high.z<<'\n';
            }
        }
        std::ofstream vegetation(reportDirectory/(stem+".vegetation.csv"));
        vegetation<<"id,model,triangles,walkable,surfaces\n";
        std::ofstream foliage(reportDirectory/(stem+".foliage-parts.csv"));foliage<<"id,model,part,ignored,passability,min_z,max_z\n";
        for(const auto& entry:sourceWorld->mapObjects) {
            auto* object=entry.second;if(object->WhoAmI()!=HPC_TREE)continue;
            for(INT_PTR i=0;i<object->fbvs.GetSize();++i){const auto& bv=object->fbvs[i];foliage<<entry.first<<','<<object->GetMaskName()<<','<<object->GetBodyPartNames()[i].GetString()<<','<<bv.IsIgnored()<<','<<bv.GetPassability()<<','<<bv.ptMin.z+bv.ptCenter.z+bv.ptBone.z<<','<<bv.ptMax.z+bv.ptCenter.z+bv.ptBone.z<<'\n';}
        }
        for(const auto& entry:objects) {
            auto* object=sourceWorld->GetMapObject(entry.first);if(!object||object->WhoAmI()!=HPC_TREE)continue;
            const auto p=object->GetCP();(void)p;
            unsigned walkable=0;std::set<unsigned long long> surfaces;
            for(const auto& t:entry.second.ground)if(t.area){++walkable;surfaces.insert(t.surface);}
            vegetation<<entry.first<<','<<object->GetMaskName()<<','<<entry.second.ground.size()<<','<<walkable<<',';
            for(auto s:surfaces)vegetation<<s<<' ';vegetation<<'\n';
        }
        nlohmann::json occupancy=nlohmann::json::array();
        for(const auto& entry:initialObjects)for(const auto& box:entry.second) {
            nlohmann::json points=nlohmann::json::array();for(auto p:box.corners)points.push_back({p.x,p.y,p.z});
            occupancy.push_back({{"id",entry.first},{"corners",points}});
        }
        std::ofstream(reportDirectory/(stem+".occupancy.json"))<<occupancy.dump(2);
        struct Walk {const char* name;Point a,b;int first,last;};
        const Walk walks[]={
            {"r15_video_a",{67.7441f,142.534f,14.5f},{66.4061f,142.845f,14.5f},Small,Human},
            {"r15_video_b",{65.7843f,143.878f,14.5f},{65.2427f,143.248f,14.5f},Small,Human},
            {"wallwalk_east",{193.487f,112.524f,7.76405f},{205.953f,112.161f,7.7f},Small,Human},
            {"wallwalk_west",{205.953f,112.161f,7.7f},{193.487f,112.524f,7.76405f},Small,Human},
            {"forest_video_1",{81.0518f,134.829f,0},{80.1159f,136.388f,0},Small,Human},
            {"forest_video_2",{80.0643f,134.072f,0},{82.925f,137.092f,0},Small,Human},
            {"forest_video_3",{79.7675f,135.033f,0},{83.0217f,137.281f,0},Small,Human},
            {"forest_video_4",{80.3086f,136.03f,0},{76.733f,137.009f,0},Small,Human},
            {"forest_video_5",{79.0708f,133.304f,0},{70.5251f,128.469f,0},Small,Human},
            {"forest_video_6",{70.5251f,128.469f,0},{74.236f,129.858f,0},Small,Human},
            {"unit1945",{141.485f,47.941f,6.039f},{142.5f,49.5f,6.f},Levitate,Levitate},
            {"unit1458",{126.582f,188.528f,8.362f},{126.637f,188.413f,8.362f},Small,Human},
            {"start_exit",{55.55f,138.78f,19.24f},{78.f,145.f,14.f},Small,Human},
            {"settlement",{188.f,116.f,0},{192.f,135.f,0},Small,Human},
            {"main_gate",{188.f,108.f,6.2f},{188.f,116.f,6.2f},Small,Giant},
            {"samvlrmp01",{197.592580f,118.819145f,6.199420f},{197.467460f,114.019735f,7.875580f},Small,Human},
            {"ruin_wall_video2_a",{66.8446f,140.352f,0},{66.7652f,140.422f,0},Small,Human},
            {"ruin_wall_video2_b",{66.8539f,140.519f,0},{66.9009f,140.942f,0},Small,Human},
            {"ruin_wall_video2_c",{66.5964f,140.981f,0},{67.1842f,140.672f,0},Small,Human},
            {"mountain_193425",{68.5084f,125.304f,0},{66.1163f,121.391f,0},Small,Human},
            {"mountain_193427",{65.8008f,125.409f,0},{65.0339f,121.688f,0},Small,Human},
            {"mountain_193452",{83.398f,131.061f,0},{85.7246f,129.781f,0},Small,Human},
            {"flight_gate",{188.f,108.f,0},{188.f,116.f,0},Flying,Flying},
            {"flight_forest",{79.0708f,133.304f,0},{70.5251f,128.469f,0},Flying,Flying},
            {"flight_mountain",{83.398f,131.061f,0},{85.7246f,129.781f,0},Flying,Flying},
            {"npc1365_height",{153.076f,111.926f,6.229f},{153.118f,111.925f,7.307f},Small,Human}
        };
        unsigned failed=0,continuitySteps=0,continuityFailures=0;
        float maxJump=0,maxSupportError=0;
        auto continuity=[&](const Mesh& mesh,const Location& previous,const Location& next,Point interpolated,const char* name) {
            ++continuitySteps;
            if(Flies(mesh.profile)) {
                Location displayed;
                const bool valid=Project(mesh,interpolated,.001f,1.e6f,displayed,next.height)&&Distance(displayed.position,next.position)<.003f;
                if(!valid)++continuityFailures;
                return valid;
            }
            const auto a=previous.position,b=next.position;
            const float delta=std::abs(b.z-a.z),limit=mesh.profile.climb+std::tan(mesh.profile.slope*FP_PI/180.f)*std::hypot(b.x-a.x,b.y-a.y);
            maxJump=(std::max)(maxJump,delta);
            bool supported=false;float supportError=FLT_MAX,terrainZ=-FLT_MAX;
            const auto* params=mesh.data->getParams();const float width=params->tileWidth;
            const auto found=geometry.surfaceTriangles.find({int(std::floor((b.x-params->orig[0])/width)),int(std::floor((-b.y-params->orig[2])/width))});
            if(found!=geometry.surfaceTriangles.end())for(auto ti:found->second) {
                const auto t=Surface(input.ground[ti],mesh.profile);
                if(!t.area)continue;
                const float ax=t.b.x-t.a.x,ay=t.b.y-t.a.y,bx=t.c.x-t.a.x,by=t.c.y-t.a.y,det=ax*by-ay*bx;
                if(det<=1.e-8f)continue;
                const float u=((b.x-t.a.x)*by-(b.y-t.a.y)*bx)/det,v=(ax*(b.y-t.a.y)-ay*(b.x-t.a.x))/det;
                if(t.terrain) {if(u<-.001f||v<-.001f||u+v>1.001f)continue;}
                else {
                    const float nx=ay*(t.c.z-t.a.z)-(t.b.z-t.a.z)*by,ny=(t.b.z-t.a.z)*bx-ax*(t.c.z-t.a.z);
                    if(det/std::sqrt(nx*nx+ny*ny+det*det)<=std::cos(mesh.profile.slope*FP_PI/180.f))continue;
                    const float distance=(std::max)({(std::max)(0.f,-u)*det/std::hypot(bx,by),(std::max)(0.f,-v)*det/std::hypot(ax,ay),(std::max)(0.f,u+v-1)*det/std::hypot(bx-ax,by-ay)});
                    if(distance>mesh.profile.cellSize*.25f)continue;
                }
                const float z=t.a.z+u*(t.b.z-t.a.z)+v*(t.c.z-t.a.z);
                if(t.terrain)terrainZ=(std::max)(terrainZ,z);
                if(t.surface==next.surface){supported=true;supportError=(std::min)(supportError,std::abs(z-b.z));}
            }
            Location displayed;
            const bool client=Project(mesh,interpolated,.001f,4.f,displayed,next.height)&&
                displayed.surface==next.surface&&Distance(displayed.position,b)<.003f;
            if(supported)maxSupportError=(std::max)(maxSupportError,supportError);
            if(delta>limit+.001f||!supported||supportError>.025f||b.z<terrainZ-.025f||!client) {
                ++continuityFailures;
                printf("HEIGHT_DISCONTINUITY name=%s profile=%s from=(%.6f,%.6f,%.6f) to=(%.6f,%.6f,%.6f) delta=%.6f limit=%.6f surface=%llu support_error=%.6f terrain=%.6f client=%d client_surface=%llu client_z=%.6f\n",name,mesh.profileId.c_str(),a.x,a.y,a.z,b.x,b.y,b.z,delta,limit,static_cast<unsigned long long>(next.surface),supportError,terrainZ,int(client),static_cast<unsigned long long>(displayed.surface),displayed.position.z);fflush(stdout);
                return false;
            }
            return true;
        };
        if(mobName=="zone1.mob") {
            // Exact support sites. The first three were moved to the nearest
            // points of the same terrain platform after GPJUN02 geometry
            // changed (REVIEW 14); projection/continuity tolerances stay fixed.
            struct SupportSite { Point point; int type; };
            const SupportSite sites[]={
                {{70.252174f,142.300003f,14.332336f},Small},
                {{70.253365f,142.300003f,14.331959f},Small},
                {{74.453560f,143.699066f,14.370000f},Human},
                {{70.249107f,142.399994f,14.343869f},Human},
                {{197.505427f,115.760385f,7.256626f},Small},
                {{197.505427f,115.760385f,7.256626f},Human},
                {{197.533046f,116.729691f,6.870812f},Small},
                {{197.533092f,116.731232f,6.868850f},Human}
            };
            for(const auto& site:sites) {
                const auto& mesh=*built[site.type];Location at,previous;previous.position=site.point;
                const bool placed=Project(mesh,site.point,.001f,mesh.profile.climb,at,1.6f);
                const bool valid=placed&&continuity(mesh,previous,at,site.point,"source_seam");
                printf("SUPPORT_REGRESSION profile=%s at=(%.6f,%.6f,%.6f) placed=%d valid=%d surface=%llu z=%.6f\n",mesh.profileId.c_str(),site.point.x,site.point.y,site.point.z,int(placed),int(valid),static_cast<unsigned long long>(at.surface),at.position.z);fflush(stdout);
                if(!valid) {
                    Location nearest;const bool found=Project(mesh,site.point,.001f,4.f,nearest,1.6f);
                    printf("SUPPORT_NEAREST found=%d surface=%llu at=(%.6f,%.6f,%.6f)\n",int(found),static_cast<unsigned long long>(nearest.surface),nearest.position.x,nearest.position.y,nearest.position.z);fflush(stdout);
                    ++failed;
                }
            }
        }
        for(const auto& walk:walks)for(int type=walk.first;type<=walk.last;++type) {
            const auto& mesh=*built[type];const float height=type==Large?3.f:type==Giant?6.f:1.6f;
            auto a=walk.a,b=walk.b;
            if(!a.z)a.z=sourceMap->GetTileHeight(CPlot3D(a.x,a.y,0));
            if(!b.z)b.z=sourceMap->GetTileHeight(CPlot3D(b.x,b.y,0));
            Location at,goal;bool ok=Project(mesh,a,1.f,4.f,at,height)&&Project(mesh,b,1.f,4.f,goal,height);
            Request request;request.source=at.position;request.goal=goal.position;request.height=height;
            auto route=ok?Find(mesh,request):Route{};ok=ok&&route.status==Status::Complete;
            const char* reason=ok?"none":StatusName(route.status);unsigned steps=0;Point desired=at.position;
            for(size_t i=1;ok&&i<route.points.size();++i) {
                const auto start=at.position,end=route.points[i].position;
                const auto n=(std::max)(1,int(std::ceil(std::hypot(end.x-start.x,end.y-start.y)/.05f)));
                for(int k=1;ok&&k<=n;++k) {
                    const float f=float(k)/n;desired={start.x+(end.x-start.x)*f,start.y+(end.y-start.y)*f,start.z+(end.z-start.z)*f};
                    Location next;++steps;ok=Constrain(mesh,at.position,desired,next,height,&reason,&at);
                    if(ok)ok=continuity(mesh,at,next,desired,walk.name);
                    if(ok)at=next;

                }
            }
            if((route.status==Status::Complete||Flies(mesh.profile))&&!ok)++failed;
            const char* stepReason=reason;
            if(route.status==Status::Complete&&!ok) {
                nlohmann::json dump={{"name",walk.name},{"profile",mesh.profileId},{"from",{at.position.x,at.position.y,at.position.z}},{"desired",{desired.x,desired.y,desired.z}},{"polygon",at.polygon},{"polygons",nlohmann::json::array()}};
                for(auto ref:route.corridor) {
                    const dtMeshTile* tile=nullptr;const dtPoly* poly=nullptr;
                    if(dtStatusFailed(mesh.data->getTileAndPolyByRef(ref,&tile,&poly)))continue;
                    float distance=FLT_MAX;nlohmann::json vertices=nlohmann::json::array(),links=nlohmann::json::array();
                    for(int i=0;i<poly->vertCount;++i){const auto* v=tile->verts+poly->verts[i]*3;vertices.push_back({v[0],-v[2],v[1]});distance=(std::min)(distance,std::hypot(v[0]-desired.x,-v[2]-desired.y));}
                    if(distance>1)continue;
                    for(auto i=poly->firstLink;i!=DT_NULL_LINK;i=tile->links[i].next){const auto& l=tile->links[i];links.push_back({l.ref,l.edge,l.side,l.bmin,l.bmax});}
                    dump["polygons"].push_back({{"ref",ref},{"vertices",vertices},{"links",links},{"supports",mesh.polygonSurfaces.at({tile->header->x,tile->header->y})[size_t(poly-tile->polys)]}});
                }
                std::ofstream(reportDirectory/(stem+"."+walk.name+"."+mesh.profileId+".failure.json"))<<dump.dump(2);
            }
            if(route.status==Status::Complete) {
                std::vector<Point> corners;for(const auto& location:route.points)corners.push_back(location.position);
                Location current=route.points.front();size_t segment=1;float offset=0;unsigned ticks=0;bool followed=true;
                while(segment<corners.size()&&followed) {
                    float budget=.17f;Point target=current.position;
                    while(segment<corners.size()) {
                        const auto start=corners[segment-1],end=corners[segment];const float length=std::hypot(end.x-start.x,end.y-start.y);
                        const float distance=(std::min)(budget,(std::max)(0.f,length-offset));offset+=distance;budget-=distance;
                        const float f=length>0?offset/length:1;target={start.x+(end.x-start.x)*f,start.y+(end.y-start.y)*f,start.z+(end.z-start.z)*f};
                        if(offset<length-.00001f)break;offset=0;++segment;if(budget<=.00001f)break;
                    }
                    Location next;++ticks;followed=Follow(mesh,current.position,target,corners,next,height,&reason,&current);
                    if(followed)followed=continuity(mesh,current,next,target,walk.name);
                    if(followed)current=next;
                }
                printf("FOLLOW_STEPS name=%s profile=%s ticks=%u complete=%d reason=%s at=(%.6f,%.6f,%.6f)\n",walk.name,Agents[type].name,ticks,int(followed),reason,current.position.x,current.position.y,current.position.z);fflush(stdout);
                if(!followed)++failed;
            }
            printf("ROUTE_STEPS name=%s profile=%s route=%s steps=%u complete=%d reason=%s at=(%.6f,%.6f,%.6f) desired=(%.6f,%.6f,%.6f)\n",walk.name,Agents[type].name,StatusName(route.status),steps,int(ok),stepReason,at.position.x,at.position.y,at.position.z,desired.x,desired.y,desired.z);fflush(stdout);
        }
        printf("HEIGHT_CONTINUITY routes=%zu steps=%u failures=%u max_delta=%.6f max_source_error=%.6f\n",std::size(walks),continuitySteps,continuityFailures,maxJump,maxSupportError);fflush(stdout);
        if(failed||continuityFailures) {
            Atomic(reportDirectory/(stem+".failure.navgeom"),sourceBytes);
            throw std::runtime_error("Nav64: route-to-step release quality failures="+std::to_string(failed));
        }
    }
    if(mapName=="zonemainmenunew.mpr") {
        auto* object=sourceWorld->GetMapObject(1);
        if(object&&object->WhoAmI()==HPC_UNIT) {
            auto* unit=static_cast<CUnit*>(object);const auto shape=unit->GetComplection();
            const auto body=bodyTable.Lookup(ResourceName(unit->GetMaskName()),shape.strength,shape.grace,shape.tall);
            const auto& mesh=*built[ProfileForRadius(body.radius,unit->GetAIClass()==0)];
            Location at,next;const float height=(std::min)(body.height[0],2.f);
            const bool placed=Project(mesh,{103.291f,51.737f,3.112f},1.f,4.f,at,height);
            const char* reason="no_start";
            const bool step=placed&&Constrain(mesh,at.position,{103.106f,51.830f,at.position.z},next,height,&reason,&at);
            printf("MENU_STEP unit=1 model=%s profile=%s placed=%d step=%d reason=%s\n",unit->GetMaskName(),mesh.profileId.c_str(),int(placed),int(step),reason);fflush(stdout);
            if(!step)throw std::runtime_error("Nav64: menu route-step regression");
        }
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
    for(int type=0;type<AgentCount;++type) {
        const auto& mesh=*built[type];const auto& bytes=outputs[type];
        Atomic(directory/Navigation::MeshFilename(mapName,mobName,type),bytes);
        printf("PROFILE map=%s mob=%s type=%s radius=%.2f tiles=%u ms=%llu raster_cpu_ms=%llu polygons_cpu_ms=%llu bytes=%zu height_outliers=0\n",
            mapName.c_str(),mobName.c_str(),Agents[type].name,mesh.profile.radius,mesh.tiles,
            static_cast<unsigned long long>(mesh.buildMs),static_cast<unsigned long long>(mesh.rasterMs),
            static_cast<unsigned long long>(mesh.polygonMs),bytes.size());
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
    return totalBytes+bytes.size();
}
