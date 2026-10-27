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
    loaded=false;error.clear();failures=stepFailures=0;nextStepLog=0;++generation;queues.clear();queryObstacles=nullptr;
    if(sourceJob.valid())sourceJob.get();sources.reset();absentStatic.clear();sourceDirty.clear();liveDoors.clear();
    liveObjects.clear();appliedObjects.clear();bakedStatic.clear();dirtyObjects.clear();
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
bool NavigationWorld::Load(const std::filesystem::path& inputDirectory) {
    if(loaded)return true;
    const auto start=std::chrono::steady_clock::now();
    try {
        const auto* mod=ModRuntime::Resources();if(!mod)throw std::runtime_error("Nav64: no loaded mod");
        const auto directory=inputDirectory.empty()?std::filesystem::path(mod->Directory())/"Maps"/"Nav64":inputDirectory;
        auto bodiesPending=std::async(std::launch::async,[&] {
            auto bytes=Read(directory/Navigation::BodyFilename);const auto size=bytes.size();
            return std::make_pair(Navigation::LoadBodies(bytes),size);
        });
        auto cataloguePending=std::async(std::launch::async,[&] {
            const auto bytes=Read(directory/(std::filesystem::path(mapName).stem().string()+"--"+std::filesystem::path(mobName).stem().string()+".navworld"));
            return nlohmann::json::parse(bytes.begin(),bytes.end());
        });
        // Independent ready meshes and lossless source planes are decoded
        // together; neither depends on the other until IndexSurfaces below.
        auto sourcePending=std::async(std::launch::async,[&] {
            return LoadSources(Read(directory/(std::filesystem::path(mapName).stem().string()+"--"+std::filesystem::path(mobName).stem().string()+".navgeom")));
        });
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
        auto bodies=bodiesPending.get();bodyTable=std::move(bodies.first);size_t bytes=bodies.second;
        for(int type=0;type<AgentCount;++type){auto result=pending[type].get();bytes+=result.bytes;meshes[type]=std::move(result.mesh);}
        const auto json=cataloguePending.get();
        if(json.at("version").get<int>()!=3)throw std::runtime_error("Nav64: incompatible world catalogue");
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
        for(int id:json.at("static")){bakedStatic.insert(id);dirtyObjects.insert(id);}
        sources=sourcePending.get();
        for(auto& mesh:meshes){mesh->sources=sources;IndexSurfaces(*mesh);}
        loaded=true;SyncObjects(true);
        const auto initialTiles=sourceDirty.size();UpdateSources(true);
        if(ModCliTool::CurrentKind()==ModCliTool::Kind::NavBake) {
            printf("LOAD_INITIAL_UPDATES profile_tiles=%zu\n",initialTiles);fflush(stdout);
        }
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
    liveObjects[id]=NavigationBoxes(object,map);
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
            record["find_raw"]=StatusName(Navigation::Find(mesh,request).status);
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
        auto movementRows=nlohmann::json::array();unsigned requiredFailures=0;
        const auto oldTime=world.GetSegmentTimeCounter();
        for(const auto& record:records) {
            if(record.at("scene").at(0)!=mapName||record.at("scene").at(1)!=mobName)continue;
            const bool required=record.value("required",false);
            if(record.at("kind")!="walk"&&!required)continue;
            if(!human)throw std::runtime_error("Nav64: requested movement release has no standing small body");
            const auto a=record.at("source").get<std::array<float,3>>(),b=record.at("goal").get<std::array<float,3>>();
            const Point from{a[0],a[1],a[2]};auto* unit=human;
            const auto cp=unit->GetCP();unit->navLocation={};unit->navLocation.position=from;unit->navLocation.surface=1;
            world.aiMap.MoveObject(unit,CPlot3D(from.x,from.y,cp.z));
            bool ok=Place(unit,world.aiMap);world.aiMap.AddObject(unit);
            SAIPath path;unsigned wait=0,ticks=0,waitTicks=0,avoidTicks=0,avoidAttempts=0;float maxDelta=0;double detourMaxMs=0;std::string failure="no_path";
            const CPlot3D goal(b[0],b[1],0);
            ok=ok&&world.aiMap.NewPath(unit,goal,path);
            if(ok)startWalker(unit,path);
            const bool beside=record.value("neighbour",false);
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
                if(movement.replanned){++avoidTicks;continue;}
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
            if(beside&&(!waitTicks||!avoidAttempts)){ok=false;failure="avoidance_not_exercised";}
            if(record.value("require_detour",false)&&!avoidTicks){ok=false;failure="standing_detour_not_exercised";}
            if(beside&&avoidAttempts>1){ok=false;failure="unchanged_obstacle_retried";}
            if(required&&!ok)++requiredFailures;
            auto result=record;result["complete"]=ok;result["failure"]=failure;result["ticks"]=ticks;result["wait_ticks"]=waitTicks;result["avoid_ticks"]=avoidTicks;result["avoidance_attempts"]=avoidAttempts;result["detour_max_ms"]=detourMaxMs;result["max_delta"]=maxDelta;
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
    Navigation::Request request;request.start=unit?&unit->navLocation:nullptr;request.source=from;request.goal=goal;request.maxLength=maxLength;request.goalRadius=goalRadius;request.height=Height(unit,aiClass);
    if(Navigation::Levitates(mesh->profile)&&unit) {
        auto& map=static_cast<CWorldServer*>(unit->GetWorld())->aiMap;
        request.source.z=map.GetTileSurface(CPlot3D(from.x,from.y,0));
        request.goal.z=map.GetTileSurface(CPlot3D(goal.x,goal.y,0));
    }
    request.obstacles=queryObstacles;
    if(costs&&!Navigation::Levitates(mesh->profile))std::copy(costs,costs+16,request.areaCosts);
    auto route=Navigation::Find(*mesh,request);
    if(route.status!=Navigation::Status::Complete&&failures++<16)GameJournal::Event("NAV","query_failed","unit=%d reason=%s from=(%.2f,%.2f,%.2f) goal=(%.2f,%.2f,%.2f) profile=%s height=%.2f max_length=%.3f route_length=%.3f",unit?unit->GetID():-1,Navigation::StatusName(route.status),from.x,from.y,from.z,goal.x,goal.y,goal.z,mesh->profileId.c_str(),request.height,maxLength,route.length);
    return route;
}
bool NavigationWorld::Support(CUnit* unit,Navigation::Point point,Navigation::Location& result) {
    auto* mesh=ForUnit(unit);
    return mesh&&Navigation::Project(*mesh,point,.001f,4.f,result,Height(unit));
}
bool NavigationWorld::CanPlace(CUnit* unit,Navigation::Point point,int aiClass) {
    CorePerf::Scope total(CorePerf::NavigationTotal),timing(CorePerf::NavigationPlace);
    auto* mesh=ForUnit(unit,aiClass);if(!mesh)return false;
    if(Navigation::Levitates(mesh->profile)&&unit)point.z=static_cast<CWorldServer*>(unit->GetWorld())->aiMap.GetTileSurface(CPlot3D(point.x,point.y,0));
    Navigation::Location location;return Navigation::Project(*mesh,point,.05f,4.f,location,Height(unit,aiClass));
}
bool NavigationWorld::Step(CUnit* unit,Navigation::Point from,Navigation::Point desired,Navigation::Location& result,const CAIRoute* route) {
    CorePerf::Scope total(CorePerf::NavigationTotal),timing(CorePerf::NavigationStep);
    auto* mesh=ForUnit(unit);
    const char* reason=mesh?"clearance":"no_mesh";
    std::vector<Point> corners;
    if(route) {
        corners.reserve(route->route.GetCount());
        for(POSITION i=route->route.GetHeadPosition();i;) {
            const auto& node=route->route.GetNext(i);corners.push_back({AITile2Meter(node.point.x),AITile2Meter(node.point.y),node.supportZ});
        }
    }
    const auto* address=unit?&unit->navLocation:nullptr;
    const bool accepted=mesh&&(route?Navigation::Follow(*mesh,from,desired,corners,result,Height(unit),&reason,address):Navigation::Constrain(*mesh,from,desired,result,Height(unit),&reason,address));
    if(!accepted) {
        const auto now=GetTickCount64();
        if(stepFailures++<16||now>=nextStepLog) {
            nextStepLog=now+2000;
            GameJournal::Event("NAV","step_rejected","unit=%d reason=%s from=(%.3f,%.3f,%.3f) desired=(%.3f,%.3f,%.3f)",unit?unit->GetID():-1,reason,from.x,from.y,from.z,desired.x,desired.y,desired.z);
        }
        return false;
    }
    return true;
}
bool NavigationWorld::Move(CUnit* unit,SAIPath& path,float time,unsigned& wait,Movement& move) {
    auto& world=*static_cast<CWorldServer*>(unit->GetWorld());auto& map=world.aiMap;
    auto& segment=path.curSegment;
    if(!wait)queues.erase(unit->GetID());
    auto pos=unit->GetCP();pos.x=Meter2AITile(pos.x);pos.y=Meter2AITile(pos.y);
    CPlot3D direction;int x=-1,y=-1;float z=0;
    move.finished=!unit->walker.GetPosition(unit->GetWorldBase(),segment,time,move.rotating,pos,direction,move.speed,x,y,&z);
    const auto from=Position(unit,map);
    move.desired={AITile2Meter(pos.x),AITile2Meter(pos.y),segment.IsMoving()?z:from.z};
    move.direction={direction.x,direction.y,0};
    // Script/patrol turns do not move the body. A clearance projection must
    // neither reject them nor replace their heading with a corridor tangent.
    if(!segment.IsMoving()) {
        move.desired=from;move.support=unit->navLocation;
        move.support.position=from;wait=0;queues.erase(unit->GetID());
        return true;
    }
    if(!Step(unit,from,move.desired,move.support,&segment.route))return false;
    pos.x=move.support.position.x;pos.y=move.support.position.y;
    if(std::hypot(pos.x-from.x,pos.y-from.y)<.00001f)return true;
    auto* neighbour=map.CheckCircleCollision(unit,pos);
    if(neighbour) {
        wait=(std::min)(wait+1,1000000u);
        auto& queue=queues[unit->GetID()];
        const Point goal{path.ptDestination.x,path.ptDestination.y,path.ptDestination.z};
        if(Distance(queue.goal,goal)>.001f||queue.blocker!=neighbour->GetID()){queue={goal,{},neighbour->GetID(),false};wait=1;}
        // Once blocked, retry only after an actual change of the saved standing
        // obstacles. A failed search never turns into another search each tick.
        const auto* mesh=ForUnit(unit);const float clearance=Height(unit);
        if(queue.attempted&&(queue.mesh!=mesh||queue.generation!=mesh->generation||queue.height!=clearance))queue.attempted=false;
        if(queue.attempted)for(const auto& saved:queue.blockers) {
            auto* other=world.GetUnit(saved.id);
            if(!other||other->IsMoving()||Distance(Position(other,map),saved.position)>.02f||
                std::abs(Radius(other)+Radius(unit)+.02f-saved.radius)>.001f||
                std::abs(Height(other)-saved.height)>.001f) {queue.attempted=false;break;}
        }
        // 27 game ticks at 55 ms: bounded Q1/Q2 wait, then one detour query.
        if(wait>=27&&!move.rotating&&!neighbour->IsMoving()&&!queue.attempted) {
            queue.blockers.clear();queue.attempted=true;++move.avoidanceAttempts;
            queue.mesh=mesh;queue.generation=mesh->generation;queue.height=clearance;
            for(const auto& entry:world.mapObjects)if(auto* other=dynamic_cast<CUnit*>(entry.second)) {
                if(other==unit||other->IsMoving()||other->IsDead())continue;
                const auto at=Position(other,map);
                if(std::hypot(at.x-from.x,at.y-from.y)>24.f)continue;
                queue.blockers.push_back({at,Radius(other)+Radius(unit)+.02f,Height(other),other->GetID()});
            }
            SAIPath alternate;
            struct QueryScope {
                NavigationWorld& owner;
                ~QueryScope(){owner.queryObstacles=nullptr;}
            } scope{*this};
            queryObstacles=&queue.blockers;
            const auto start=std::chrono::steady_clock::now();
            const bool found=map.NewPath(unit,path.ptDestination,alternate);
            move.detourMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            GameJournal::Event("NAV","queue_detour","unit=%d blocker=%d result=%s obstacles=%zu ms=%.3f",unit->GetID(),neighbour->GetID(),found?"ready":"waiting",queue.blockers.size(),move.detourMs);
            if(found) {
                path.curSegment=alternate.curSegment;path.toGo=alternate.toGo;
                path.ptFinish=alternate.ptFinish;path.bFinished=alternate.bFinished;
                path.bRouteFailed=alternate.bRouteFailed;path.nPathCost=alternate.nPathCost;
                float speed=0,angular=0;unit->GetSpeeds(speed,angular);
                unit->walker.SetSpeeds(speed,angular);unit->walker.CreateWalkerRoute(path.curSegment);
                move.replanned=true;move.finished=false;move.support=unit->navLocation;
                wait=0;queues.erase(unit->GetID());return true;
            }
        }
        move.waiting=true;move.blocker=neighbour->GetID();return true;
    }
    wait=0;queues.erase(unit->GetID());
    if(!move.rotating) {
        const float dx=move.support.position.x-from.x,dy=move.support.position.y-from.y,length=std::hypot(dx,dy);
        if(length>.00001f)move.direction={dx/length,dy/length,0};
    }
    return true;
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
                for(const auto& tile:sources->objectTiles[id])for(int type=0;type<AgentCount;++type)sourceDirty.emplace(type,tile.first,tile.second);
                GameJournal::Event("NAV","static_changed","NID=%d absent=%d queued_profile_tiles=%zu",id,int(absent),sourceDirty.size());
            }
        } else {
            auto old=appliedObjects.find(id),live=liveObjects.find(id);
            bool same=old!=appliedObjects.end()&&live!=liveObjects.end()&&old->second.size()==live->second.size();
            if(same)for(size_t i=0;i<old->second.size();++i)for(size_t j=0;j<8;++j)
                if(Distance(old->second[i].corners[j],live->second[i].corners[j])>.00001f)same=false;
            if(same)continue;
            if(old!=appliedObjects.end())for(const auto& box:old->second)QueueBox(box);
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
    if(!Navigation::ResolveLocation(*mesh,from,&unit->navLocation,location,Height(unit))) {
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
        const auto triangles=sources->tileTriangles[{x,z}];
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
