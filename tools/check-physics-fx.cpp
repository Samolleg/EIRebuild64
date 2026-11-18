#include "../Game/PhysicsFxCore.h"
#include "../Game/PhysicsFxPlayback.h"
#include "../Game/PhysicsFxTerrain.h"
#include "../Game/OwnedRender/E1PhysicsDebris.h"
#include "../external/owned-renderer/Source/secondary_physics.h"
#include <xmmintrin.h>
#include <algorithm>
#include <chrono>
#include <thread>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace PhysicsFx;
unsigned checks=0;
void check(bool ok,const char* name){++checks;if(!ok)throw std::runtime_error(name);std::cout<<"PASS "<<name<<'\n';}
void check_debris_playback(){
    DebrisHistory history;DebrisPlayback cursor;
    auto publish=[&](unsigned tick){
        auto tracks=std::make_shared<StoneTracks>(1);auto& stone=tracks->front();
        stone.poses[0].position.x=float(tick-1);stone.poses[1].position.x=float(tick);
        stone.life=5;history.publish(tick,tracks);
    };
    publish(1);publish(2);publish(3);
    history.sample(3.,cursor);
    float x=SampleTracks(*cursor.visible,cursor.fraction)[0].pose.position.x;
    for(unsigned i=1;i<=8;++i){
        double now=3.+i*.25;
        if(i==6){publish(4);publish(5);}
        history.sample(now,cursor);
        const float next=SampleTracks(*cursor.visible,cursor.fraction)[0].pose.position.x;
        check(next>x && next-x<=.313f,"late publications interpolate at every draw");
        const auto previous=cursor.position;
        history.sample(now,cursor);
        check(cursor.position==previous,"identical render clock preserves pose");
        x=next;
    }
    publish(25);history.sample(25.,cursor);
    check(cursor.position==24.,"discarded simulation interval resets history");
}
std::vector<Triangle> plane(float slope=0){return {{{-14,-14,-14*slope},{14,-14,14*slope},{14,14,14*slope}},{{-14,-14,-14*slope},{14,14,14*slope},{-14,14,-14*slope}}};}
owned::TerrainCpuAsset fixture(unsigned material){
    owned::TerrainCpuAsset t;t.tile_types={material};t.sector_count_x=t.sector_count_y=1;t.sectors.resize(1);
    auto& s=t.sectors[0];s.bounds.minimum={0,0,0};s.bounds.maximum={2,2,1};
    for(unsigned y=0;y<3;++y)for(unsigned x=0;x<3;++x){owned::TerrainVertex v;v.x=float(x);v.y=float(y);v.z=.2f*x;s.vertices.push_back(v);}
    owned::TerrainTileDescriptor tile;for(unsigned i=0;i<9;++i)tile.vertex_indices[i]=std::uint16_t(i);s.tiles.push_back(tile);return t;
}
#include "check-breast-physics.inl"
int main(int argc,char** argv)try{
    if(argc>1&&std::string(argv[1])=="--expect-gpu-unavailable"){
        check_breast_physics(true);
        World missing;check(!missing.Initialize()&&!missing.Error().empty(),"missing runtime rejected");
        check(!missing.AddPatch(1,{0,0,0},plane())&&missing.Burst(1,{0,0,0},{0,0,1},1,1)==0,"no CPU fallback");
        std::cout<<missing.Error()<<'\n';return 0;
    }
    check_debris_playback();
    check_breast_physics();
    Events e;e.Reset(100);
    check(!e.Admit(1,100,100,false),"initial sync suppressed");
    check(e.Admit(2,103,103,true),"fresh event admitted");
    check(!e.Admit(2,103,104,true),"duplicate event suppressed");
    check(!e.Admit(3,105,110,true),"late visibility suppressed");
    check(!e.Admit(4,120,110,true),"future event suppressed");
    e.Reset(200);check(!e.Admit(2,103,203,true),"restore does not replay old event");
    std::vector<Triangle> triangles;auto ground=fixture(2);auto hit=ExtractTerrain(ground,{1,1,0},{.25f,.7f,1},triangles);
    check(hit.found&&triangles.size()==8&&std::abs(hit.point.z-.05f)<1e-5f&&hit.normal.z>.97f,"authored slope/topology and material");
    for(unsigned type:{6u,13u,14u,8u}){auto t=fixture(type);ExtractTerrain(t,{1,1,0},{1,1,1},triangles);check(triangles.empty(),"liquid/unknown excluded");}
    ground.astral_slots={1};ExtractTerrain(ground,{1,1,0},{1,1,1},triangles);check(triangles.empty(),"astral excluded");
    ground=fixture(2);ground.sectors[0].tiles[0].object_id=0;ExtractTerrain(ground,{1,1,0},{1,1,1},triangles);check(triangles.empty(),"upper layer excluded");
    check(StoneTerrain(2)&&StoneTerrain(4)&&StoneTerrain(15)&&!StoneTerrain(0)&&!StoneTerrain(6),"stone emission classification");
    for(unsigned material:{0u,1u,9u,11u}){
        auto terrain=fixture(material);
        auto gentle=ExtractTerrain(terrain,{1,1,0},{.25f,.7f,1},triangles);
        check(!StoneSurface(gentle),"gentle natural cover does not emit stones");
        for(auto& v:terrain.sectors[0].vertices)v.z=2.f*v.x;
        auto cliff=ExtractTerrain(terrain,{1,1,0},{.25f,.7f,1},triangles);
        check(StoneSurface(cliff),"steep covered cliff emits stones");
    }
    for(unsigned material:{3u,6u,8u,10u,12u,13u,14u}){
        GroundHit excluded;excluded.found=true;excluded.material=material;excluded.normal={.9f,0,.3f};
        check(!StoneSurface(excluded),"sand ice snowballs liquids unknown are not rock cliffs");
    }
    GroundHit boundary;boundary.found=true;boundary.material=9;boundary.normal={0,0,.76604444f};
    check(!StoneSurface(boundary),"walk limit itself is not a cliff");
    boundary.normal.z=.765f;check(StoneSurface(boundary),"beyond walk limit is cliff");
    boundary.found=false;check(!StoneSurface(boundary),"missing ground never emits stones");
    // Optional private source cases: query XY, expected emission and nine actual
    // source vertices. Run the production terrain extraction, not a copied rule.
    if(argc>2){
        std::ifstream cases(argv[2]);if(!cases)throw std::runtime_error("source cases missing");
        unsigned count=0,material,expected;float x,y;
        while(cases>>material>>x>>y>>expected){
            auto terrain=fixture(material);
            for(auto& v:terrain.sectors[0].vertices)if(!(cases>>v.x>>v.y>>v.z))throw std::runtime_error("truncated source case");
            terrain.sectors[0].bounds.minimum={x-3,y-3,-100};terrain.sectors[0].bounds.maximum={x+3,y+3,100};
            auto actual=ExtractTerrain(terrain,{x,y,0},{x,y,0},triangles);
            check(actual.found&&StoneSurface(actual)==bool(expected),"actual terrain impact source classification");++count;
        }
        check(count==8,"all eight owner impact coordinates checked");
    }
    // Two worlds with the same events must match despite different render sample counts.
    const auto controlBefore=_mm_getcsr()&~0x3fu;
    World a,b;if(!a.Initialize())throw std::runtime_error(a.Error());
    check(a.Statistics().gpuDynamics&&a.Statistics().gpuBroadPhase,"GPU dynamics and GPU broadphase enabled");
    std::cout<<"GPU_DEVICE "<<a.Device()<<'\n';
    check(a.AddPatch(1,{0,0,0},plane())&&b.AddPatch(1,{0,0,0},plane()),"PhysX GPU terrain created");
    check(a.Burst(1,{0,0,0},{0,0,1},1,123)==64&&b.Burst(1,{0,0,0},{0,0,1},1,123)==64,"64 convex stones per full impact");
    const auto burst=a.Sample(1);
    check(std::count_if(burst.begin(),burst.end(),[](const Stone& s){return s.radius>=.08f;})==8,"eight large readable chunks");
    check(std::all_of(burst.begin(),burst.end(),[](const Stone& s){return s.radius>=.025f&&s.radius<=.13f;}),"bounded chip and chunk size");
    auto paused=a.Sample(1);for(int i=0;i<20;++i)a.Tick(true);auto after=a.Sample(1);
    check(after.size()==paused.size()&&after[0].pose.position.z==paused[0].pose.position.z,"pause freezes pose and lifetime");
    float highest=0,lowest=100;bool gpuContacts=false,gpuMemory=false;std::ofstream trajectory;
    if(argc>1){trajectory.open(argv[1]);trajectory<<"tick,index,x,y,z,radius,alpha\n";}
    for(int tick=0;tick<95;++tick){
        a.Tick(false);b.Tick(false);for(float phase:{0.f,.13f,.4f,.9f})a.Sample(phase);
        if(!a.Error().empty()||!b.Error().empty())throw std::runtime_error(a.Error()+b.Error());
        const auto stats=a.Statistics();gpuContacts|=stats.gpuContacts>0;
        gpuMemory|=stats.gpuHeapBytes>0&&stats.gpuBroadPhaseBytes>0&&stats.gpuSolverBytes>0;
        const auto sa=a.Sample(1),sb=b.Sample(1);check(sa.size()==sb.size(),"render FPS independent count");
        for(unsigned i=0;i<sa.size();++i){
            const auto p=sa[i].pose.position,q=sb[i].pose.position;
            if(std::abs(p.z-q.z)>1e-6f||std::abs(p.x-q.x)>1e-6f)throw std::runtime_error("render affected simulation");
            highest=(std::max)(highest,p.z);lowest=(std::min)(lowest,p.z);
            if(trajectory)trajectory<<tick<<','<<i<<','<<p.x<<','<<p.y<<','<<p.z<<','<<sa[i].radius<<','<<sa[i].alpha<<'\n';
        }
    }
    check(highest>.12f&&lowest>-.01f,"flight and terrain contact without tunnelling");
    check(a.Statistics().bodies==0&&a.Statistics().patches==0,"expiry releases bodies and terrain");
    check(gpuContacts&&gpuMemory,"GPU contact buffers and solver/broadphase allocations used");
    check((_mm_getcsr()&~0x3fu)==controlBefore,"PhysX preserves floating-point control");
    check(a.AddPatch(2,{0,0,0},plane(.4f)),"sloped terrain created");
    for(unsigned i=0;i<20;++i)a.Burst(2,{0,0,.01f},{-.37139f,0,.928477f},1,27+i);
    check(a.Statistics().bodies==128,"hard body budget");
    float worstGap=0;
    for(int tick=0;tick<55;++tick){a.Tick(false);if(!a.Error().empty())throw std::runtime_error(a.Error());for(const auto& stone:a.Sample(1))worstGap=(std::min)(worstGap,stone.pose.position.z-.4f*stone.pose.position.x);}
    check(worstGap>-.015f&&a.Statistics().errors==0,"128 CCD bodies on slope");
    a.Clear();check(a.Statistics().bodies==0&&a.Statistics().patches==0,"world generation cleanup");
    b.Clear();check(a.Initialize(),"GPU runtime reinitializes after all worlds released");
    // The render mesh must describe the same convex hull as the physics shape.
    for(unsigned kind=0;kind<4;++kind){OwnedRender::DebrisMesh mesh(kind);auto points=StoneVertices(kind,1);
        for(unsigned i=0;i<6;++i)check(mesh.positions[i/4].x[i%4]==points[i].x,"render/physics shape agreement");
        for(auto index:mesh.indices)if(index>=mesh.components.size())throw std::runtime_error("invalid triangle index");
        for(unsigned face=0;face<8;++face){
            const auto& a=mesh.uv[face*3];const auto& b=mesh.uv[face*3+1];const auto& c=mesh.uv[face*3+2];
            check(std::abs((b.u-a.u)*(c.v-a.v)-(b.v-a.v)*(c.u-a.u))>.05f,"nondegenerate texture facet");
            for(unsigned j=0;j<3;++j){const unsigned index=face*3+j;check(mesh.components[index].texture==index,"facet UV component binding");}
        }
    }
    Stone stone;stone.radius=.1f;stone.pose.position={1,2,3};stone.pose.rotation={0,0,.70710678f,.70710678f};
    const auto m=OwnedRender::DebrisMatrix(stone);check(std::abs(m[4]-.1f)<1e-6f&&m[3]==1&&m[7]==2&&m[11]==3,"quaternion/scale/translation render matrix");
    std::cout<<"RESULT PASS checks="<<checks<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
