#include "StdAfx.h"
#include "NavigationBake.h"
#include "NavigationObject.h"
#include "../Navigation/Profiles.h"
#include "../Navigation/Query.h"
#include <fstream>
#include <future>
#include "aiMap.h"
#include "Unit.h"
#include "WorldServer.h"
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
    const auto idValue=object->GetID();
    const auto kind=object->WhoAmI();
    if(authored.doors.count(idValue)||(kind==HPC_LEVER&&static_cast<CLeverObject*>(object)->IsNavigationDoor())) {
        if(remove){doors.erase(idValue);return;}
        if(authored.doors.count(idValue))doors[idValue]=authored.doors.at(idValue);
        else {
            object->CreateCurrentPlacement();Door d;d.object=idValue;
            d.minimum={FLT_MAX,FLT_MAX,FLT_MAX};d.maximum={-FLT_MAX,-FLT_MAX,-FLT_MAX};
            auto base=object->GetCP();base.z+=map.GetOriginalTileHeight(base);bool solid=false;
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
    for(INT_PTR n=0;n<parts.GetSize();++n){int i=object->GetFigureIndexByName(parts[n]);if(i<0)continue;
        const auto& bv=object->fbvs[i];
        if(bv.IsIgnored())continue;
        const auto partName=Navigation::ResourceName(parts[n]);
        const bool walkable=bv.IsBridge()||(authored.walkable.count(id)&&authored.walkable.at(id).count(partName));
        if(!walkable&&bv.GetPassability()!=AI_PASS_NO)continue;
        {
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
                for(size_t j=0;j<figure->indices.size();j+=3) {
                    Triangle t{vertices[figure->indices[j]],vertices[figure->indices[j+1]],vertices[figure->indices[j+2]],walkable?map.NavigationBridgeArea():BYTE(0)};
                    t.object=id;t.surface=walkable?(std::uint64_t(unsigned(id))<<32)|unsigned(i+2):0;
                    input.ground.push_back(t);
                }
            }catch(const std::exception& e) {
                Navigation::Solid failure{};failure.error="Nav64: invalid walkable geometry "+label.str()+" reason="+e.what();input.solids.push_back(std::move(failure));
            }
        }
    }
    if(input.solids.empty()&&input.ground.empty())objects.erase(id);else objects[id]=std::move(input);
}

Navigation::Geometry NavigationBake::Geometry() {
    Navigation::Geometry input=terrain;
    for(const auto& door:doors)input.doors.push_back(door.second);
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
std::uint64_t NavigationBake::Bake(const std::string& mapName,const std::string& mobName,const std::filesystem::path& directory,const Navigation::Bytes& table,const Navigation::Bytes& authored) {
    bodyTable=Navigation::LoadBodies(table);
    auto geometry=Geometry();
    std::vector<Navigation::BuildRequest> requests;
    for(int type=0;type<AgentCount;++type){requests.push_back({Agents[type].profile});}
    auto built=Navigation::BuildProfiles(geometry,requests);
    auto sources=std::make_shared<Navigation::Geometry>(geometry);
    for(auto& mesh:built){mesh->sources=sources;IndexSurfaces(*mesh);}
    auto sourceOutput=std::async(std::launch::async,[&]{return StoreSources(geometry);});
    std::uint64_t totalBytes=0;std::vector<Navigation::Bytes> outputs(AgentCount);
    for(int type=0;type<AgentCount;++type) {
        auto& mesh=*built[type];mesh.mapName=mapName;mesh.mobName=mobName;mesh.profileId=Agents[type].name;CheckReleaseHeights(mesh);
    }
    auto serialize=[&](int first){for(int type=first;type<AgentCount;type+=2)outputs[type]=Navigation::Store(*built[type]);};
    auto parallel=std::async(std::launch::async,[&]{serialize(1);});serialize(0);parallel.get();
    for(int type=0;type<AgentCount;++type)totalBytes+=outputs[type].size();
    std::filesystem::create_directories(directory);
    // Release-data quality is part of the baker, on the actual map meshes.
    // No standalone test, simulation, game tick, script or renderer runs here.
    const auto stem=std::filesystem::path(mapName).stem().string()+"--"+std::filesystem::path(mobName).stem().string();
    std::ofstream losses(directory/(stem+".losses.csv"));losses<<"profile,class,x,y,z,reason\n";
    std::ofstream water(directory/(stem+".water-gains.csv"));water<<"profile,x,y,z,depth,water_limit,forbidden,surface\n";
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
                water<<Agents[type].name<<','<<p.x<<','<<p.y<<','<<p.z<<','<<depth<<','<<limit<<','<<forbidden<<','<<at.surface<<'\n';
            }}continue;}
            if(!onMesh) {++lost;losses<<Agents[type].name<<','<<c<<','<<p.x<<','<<p.y<<','<<p.z<<",no_polygon_or_clearance\n";continue;}
            if((x%16)==0&&(y%16)==0) {
                Point to=at.position;to.x+=.02f;Location target;
                if(Project(mesh,to,.001f,1.f,target,height)) {
                    ++stepCount;Location next;
                    if(!Constrain(mesh,at.position,to,next,height))++stepFailed;
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
        // Anchors are actual zone1 sites, not synthetic corridor fixtures.
        struct Passage {const char* name;Point a,b;int first,last;};
        const Passage passages[]={
            {"start_exit",{55.55f,138.78f,19.24f},{78.f,145.f,14.f},Small,Human},
            {"main_gate",{188.f,108.f,6.2f},{188.f,116.f,6.2f},Small,Giant},
            {"elder_entrance",{197.98f,143.7f,6.3f},{197.98f,145.5f,6.3f},Small,Human},
            {"samvlrmp01",{192.5f,113.9f,6.f},{197.3f,113.8f,7.85f},Small,Human}
        };
        for(const auto& passage:passages)for(int type=passage.first;type<=passage.last;++type) {
            const auto& candidate=*built[type];Location a,b;const float height=type==Giant?6.f:type==Large?3.f:1.6f;
            const bool source=Project(candidate,passage.a,.5f,4.f,a,height),goal=Project(candidate,passage.b,.5f,4.f,b,height);
            Request r;r.source=a.position;r.goal=b.position;r.height=height;r.maxLength=100;
            Route path;if(source&&goal)path=Find(candidate,r);
            printf("PASSAGE name=%s profile=%s source=%d goal=%d route=%s length=%.3f from=(%.3f,%.3f,%.3f) to=(%.3f,%.3f,%.3f)\n",passage.name,Agents[type].name,int(source),int(goal),StatusName(path.status),path.length,a.position.x,a.position.y,a.position.z,b.position.x,b.position.y,b.position.z);fflush(stdout);
        }

    }
    std::ofstream connections(directory/(stem+".connections.csv"));
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
    std::ofstream units(directory/(stem+".placements.csv"));units<<"id,model,profile,x,y,z,distance,reason\n";
    for(const auto& entry:sourceWorld->mapObjects)if(entry.second->WhoAmI()==HPC_UNIT) {
        auto* unit=static_cast<CUnit*>(entry.second);auto cp=unit->GetCP();const auto& shape=unit->GetComplection();
        const auto size=bodyTable.Lookup(ResourceName(unit->GetMaskName()),shape.strength,shape.grace,shape.tall);
        const int type=ProfileForRadius(size.radius,unit->GetAIClass()==0);const auto& mesh=*built[type];
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
    auto sourceBytes=sourceOutput.get();const auto decoded=LoadSources(sourceBytes);
    if(decoded->ground.size()!=geometry.ground.size()||decoded->doors.size()!=geometry.doors.size())throw std::runtime_error("Nav64: source serialization mismatch");
    Atomic(directory/(stem+".navgeom"),sourceBytes);totalBytes+=sourceBytes.size();
    printf("SOURCES triangles=%zu doors=%zu offmesh=%zu bytes=%zu\n",geometry.ground.size(),geometry.doors.size(),geometry.connections.size(),sourceBytes.size());fflush(stdout);
    nlohmann::json catalogue={{"version",2},{"static",staticIds}};
    const auto text=catalogue.dump();Navigation::Bytes bytes(text.begin(),text.end());
    Atomic(directory/(std::filesystem::path(mapName).stem().string()+"--"+std::filesystem::path(mobName).stem().string()+".navworld"),bytes);
    return totalBytes+bytes.size();
}
