#include "StdAfx.h"
#include "NavigationBake.h"
#include "NavigationObject.h"
#include "../Navigation/Profiles.h"
#include "aiMap.h"
#include "Unit.h"
#include "WorldServer.h"
#include "MapObjectServer.h"
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
#pragma comment(lib,"bcrypt.lib")
using namespace Navigation;
namespace {
Navigation::Point GamePoint(const CPlot3D& p) {return {p.x,p.y,p.z};}
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
    const float tolerance=mesh.profile.cellHeight*2; // 10 cm, including Recast's +ch detail offset.
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
    if(!owned::parse_figure_asset(blob,*figure,failure))throw std::runtime_error(failure.message);
    figures.emplace(key,figure);return figure;
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
void NavigationBake::Object(CMapObject* object,bool remove,CAIMap& map) {
    if(!NavigationObject(object))return;
    auto id=object->GetID();
    // Move/orient/lever updates remove and add synchronously. Compare their
    // final contribution at the next query, not their intermediate absence.
    if(remove){objects.erase(id);return;}
    object->CreateCurrentPlacement();
    auto* specific=static_cast<CMapObjectServer*>(object->GetMapObjectSpecific());
    const CStringArray& parts=specific->szNames.GetSize()?specific->szNames:object->GetBodyPartNames();
    Navigation::Geometry input;
    for(INT_PTR n=0;n<parts.GetSize();++n){int i=object->GetFigureIndexByName(parts[n]);if(i<0)continue;
        const auto& bv=object->fbvs[i];
        if(bv.IsIgnored())continue;
        if(bv.IsBridge()) {
            std::ostringstream label;label<<"object NID="<<id<<" model="<<object->GetMaskName()<<" part="<<parts[n].GetString();
            try {
                const auto figure=Figure(*static_cast<CFigureManager*>(object->GetWorld()->GetFigureManager()),object->GetMaskName(),parts[n]);const auto& c=object->GetComplection();
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
                    p=object->RotateVector(p+bv.ptBone)+base;vertices[j]=GamePoint(p);
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
        bool valid=Navigation::Finite(GamePoint(bv.ptBone))&&Navigation::Finite(GamePoint(bv.ptCenter))&&
            Navigation::Finite(GamePoint(bv.ptMin))&&Navigation::Finite(GamePoint(bv.ptMax))&&Navigation::Finite(GamePoint(base));
        // CFigure::RecalcBoundingBoxWithRotation reduces all eight transformed
        // endpoints with Min/Max. Endpoint order is not a validity condition.
        CPlot3D low((std::min)(bv.ptMin.x,bv.ptMax.x),(std::min)(bv.ptMin.y,bv.ptMax.y),(std::min)(bv.ptMin.z,bv.ptMax.z));
        CPlot3D high((std::max)(bv.ptMin.x,bv.ptMax.x),(std::max)(bv.ptMin.y,bv.ptMax.y),(std::max)(bv.ptMin.z,bv.ptMax.z));
        if(valid&&(bv.ptMin.x>bv.ptMax.x||bv.ptMin.y>bv.ptMax.y||bv.ptMin.z>bv.ptMax.z)) {
            std::ostringstream label;label<<"NID="<<id<<" model="<<object->GetMaskName()<<" part="<<parts[n].GetString();
            normalizedBounds.insert(label.str());
        }
        for(int j=0;j<8;++j){CPlot3D p((j&1)?high.x:low.x,(j&2)?high.y:low.y,(j&4)?high.z:low.z);
            p+=bv.ptBone+bv.ptCenter;p=object->RotateVector(p);p+=base;solid.corners[j]=GamePoint(p);valid=valid&&Navigation::Finite(solid.corners[j]);}
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
    if(input.solids.empty()&&input.ground.empty())objects.erase(id);else objects[id]=std::move(input);
}

Navigation::Geometry NavigationBake::Geometry() {
    Navigation::Geometry input=terrain;
    for(const auto& entry:objects) {
        const auto& object=entry.second;const auto first=input.ground.size();
        for(const auto& source:object.groundSources)input.groundSources.emplace(first+source.first,source.second);
        input.ground.insert(input.ground.end(),object.ground.begin(),object.ground.end());
        input.solids.insert(input.solids.end(),object.solids.begin(),object.solids.end());
    }
    input.Bounds();return input;
}
Navigation::Bytes NavigationBake::Bodies(CWorldServer& world) {
    std::set<std::string> models,missing;
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
    for(const auto& mask:models)for(int strength=0;strength<=2;++strength)for(int grace=0;grace<=2;++grace)for(int tall=0;tall<=2;++tall)
        Body(world,mask,float(strength),float(grace),float(tall));
    return Navigation::StoreBodies(bodyTable);
}
std::string NavigationBake::Fingerprint(const std::string& mapName,const std::string& mobName,const Navigation::Bytes& table,const Navigation::Bytes& authored) {
    // Hash exactly the imported build streams, the measured table and authored
    // policy. No MOB/MPR bytes, object IDs, units, scripts or model catalog.
    auto geometry=Geometry();Navigation::Bytes source;
    for(const auto& bytes:{&table,&authored}) {
        const auto hash=Digest(*bytes);source.insert(source.end(),hash.begin(),hash.end());
    }
    for(const auto& agent:Agents) {
        const auto hash=Digest(Navigation::Inputs({},agent.profile,"",{}));source.insert(source.end(),hash.begin(),hash.end());
    }
    return Digest(Navigation::Inputs(geometry,Navigation::Profile{},mapName+"|"+mobName,source));
}
std::uint64_t NavigationBake::Bake(const std::string& mapName,const std::string& mobName,const std::filesystem::path& directory,const Navigation::Bytes& table,const Navigation::Bytes& authored) {
    auto geometry=Geometry();const auto started=std::chrono::steady_clock::now();const auto key=Fingerprint(mapName,mobName,table,authored);
    printf("FINGERPRINT mpr=%s mob=%s algorithm=%u key=%s ms=%.3f\n",mapName.c_str(),mobName.c_str(),AlgorithmVersion,key.c_str(),std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count());
    std::vector<Navigation::BuildRequest> requests;
    for(int type=0;type<AgentCount;++type)requests.push_back({Agents[type].profile,key});
    auto built=Navigation::BuildProfiles(geometry,requests);
    std::uint64_t totalBytes=0;std::vector<Navigation::Bytes> outputs;
    for(int type=0;type<AgentCount;++type) {
        auto& mesh=*built[type];mesh.mapName=mapName;mesh.mobName=mobName;mesh.profileId=Agents[type].name;
        CheckReleaseHeights(mesh);
        outputs.push_back(Navigation::Store(mesh));totalBytes+=outputs.back().size();
    }
    std::filesystem::create_directories(directory);
    for(const auto& entry:normalizedBounds)printf("BOUNDS_NORMALIZED mpr=%s mob=%s %s\n",mapName.c_str(),mobName.c_str(),entry.c_str());
    for(int type=0;type<AgentCount;++type) {
        const auto& mesh=*built[type];const auto& bytes=outputs[type];
        Atomic(directory/Navigation::MeshFilename(mapName,mobName,type),bytes);
        printf("PROFILE map=%s mob=%s type=%s radius=%.2f tiles=%u ms=%llu raster_cpu_ms=%llu polygons_cpu_ms=%llu bytes=%zu height_outliers=0 key=%s\n",
            mapName.c_str(),mobName.c_str(),Agents[type].name,mesh.profile.radius,mesh.tiles,
            static_cast<unsigned long long>(mesh.buildMs),static_cast<unsigned long long>(mesh.rasterMs),
            static_cast<unsigned long long>(mesh.polygonMs),bytes.size(),mesh.fingerprint.c_str());
        fflush(stdout);
    }
    return totalBytes;
}
