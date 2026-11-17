#include "PhysicsFxCore.h"
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <stdexcept>

namespace PhysicsFx {
namespace {
using namespace JPH;
struct Runtime {
    Runtime() { RegisterDefaultAllocator(); Factory::sInstance=new Factory; RegisterTypes(); }
    ~Runtime() { UnregisterTypes(); delete Factory::sInstance; Factory::sInstance=nullptr; }
};
std::shared_ptr<Runtime> runtime() {
    static std::weak_ptr<Runtime> current;
    auto p=current.lock();if(!p){p=std::make_shared<Runtime>();current=p;}return p;
}
class Broad final:public BroadPhaseLayerInterface {
public:
    uint GetNumBroadPhaseLayers() const override {return 2;}
    BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer layer) const override {return BroadPhaseLayer(uint8(layer<16?0:1));}
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(BroadPhaseLayer layer) const override {return uint8(layer)==0?"terrain":"debris";}
#endif
};
class Pairs final:public ObjectLayerPairFilter {
public: bool ShouldCollide(ObjectLayer a,ObjectLayer b) const override {return (a^b)==16;}
};
class BroadPairs final:public ObjectVsBroadPhaseLayerFilter {
public: bool ShouldCollide(ObjectLayer a,BroadPhaseLayer b) const override {return (a<16?0:1)!=uint8(b);}
};
Vec3 j(Vec p){return {p.x,p.y,p.z};}
Vec vec(Vec3Arg p){return {p.GetX(),p.GetY(),p.GetZ()};}
float random01(std::uint32_t& seed){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return float(seed&0xffffff)/16777216.f;}
bool finite(Vec p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
Pose pose(BodyInterface& bodies,BodyID id){RVec3 p;Quat q;bodies.GetPositionAndRotation(id,p,q);return {vec(p),{q.GetX(),q.GetY(),q.GetZ(),q.GetW()}};}
}
std::array<Vec,6> StoneVertices(unsigned shape,float r) {
    const float k=float(shape%4);
    return {{{r,0,.13f*r},{-.83f*r,.08f*r,0},{0,(.72f+.06f*k)*r,0},
        {.09f*r,-r,-.07f*r},{.1f*r,.1f*r,(.78f+.07f*k)*r},{-.1f*r,0,-.7f*r}}};
}
bool Events::Admit(int id,int start,int now,bool ready) {
    seen.erase(std::remove_if(seen.begin(),seen.end(),[now](const Entry& e){return std::int64_t(now)-e.start>8;}),seen.end());
    for(auto e:seen)if(e.id==id&&e.start==start)return false;
    if(seen.size()>=1024)return false;
    seen.push_back({id,start});
    const auto age=std::int64_t(now)-start;
    return ready&&now>readyAfter&&start>readyAfter&&age>=-1&&age<=2;
}
struct World::Impl {
    // Runtime dies last, after shapes, bodies and allocator.
    std::shared_ptr<Runtime> registration=runtime();
    Broad broad;Pairs pairs;BroadPairs broadPairs;
    TempAllocatorImpl temporary{4*1024*1024};
    JobSystemSingleThreaded jobs{2048};
    PhysicsSystem physics;
    struct Patch {BodyID id;Vec center;unsigned users=0,slot=0;};
    struct Body {BodyID id;std::uint64_t patch;unsigned shape;float radius,age=0,life;std::uint32_t color;std::array<Pose,5> poses;};
    std::map<std::uint64_t,Patch> patches;
    std::vector<Body> stones;
    Stats stats;
    Impl(){physics.Init(MaxStones+16,0,2048,2048,broad,broadPairs,pairs);physics.SetGravity({0,0,-9.81f});stones.reserve(MaxStones);}
    ~Impl(){clear();}
    void remove(BodyID id){auto& api=physics.GetBodyInterface();api.RemoveBody(id);api.DestroyBody(id);}
    void clear(){for(auto& b:stones)remove(b.id);stones.clear();for(auto& p:patches)remove(p.second.id);patches.clear();}
    void retireUnused(){for(auto p=patches.begin();p!=patches.end();)if(!p->second.users){remove(p->second.id);p=patches.erase(p);}else ++p;}
};
World::World()=default;
World::~World()=default;
void World::Clear(){impl.reset();}
bool World::HasPatch(std::uint64_t key) const {return impl&&impl->patches.count(key);}
bool World::AddPatch(std::uint64_t key,Vec center,const std::vector<Triangle>& triangles){
    if(HasPatch(key))return true;
    if(!finite(center)||triangles.empty()||triangles.size()>16384)return false;
    if(!impl)impl=std::make_unique<Impl>();
    impl->retireUnused();if(impl->patches.size()>=16)return false;
    TriangleList list;list.reserve(triangles.size());
    for(const auto& t:triangles){if(!finite(t.a)||!finite(t.b)||!finite(t.c))return false;list.emplace_back(Float3(t.a.x,t.a.y,t.a.z),Float3(t.b.x,t.b.y,t.b.z),Float3(t.c.x,t.c.y,t.c.z));}
    MeshShapeSettings settings(list);auto shape=settings.Create();if(shape.HasError())return false;
    unsigned slot=0;for(;slot<16;++slot){bool used=false;for(auto& p:impl->patches)used|=p.second.slot==slot;if(!used)break;}
    BodyCreationSettings creation(shape.Get(),RVec3::sZero(),Quat::sIdentity(),EMotionType::Static,ObjectLayer(slot));
    creation.mFriction=.75f;creation.mRestitution=.18f;
    auto& api=impl->physics.GetBodyInterface();const auto id=api.CreateAndAddBody(creation,EActivation::DontActivate);
    if(id.IsInvalid())return false;impl->patches.emplace(key,Impl::Patch{id,center,0,slot});return true;
}
unsigned World::Burst(std::uint64_t key,Vec point,Vec normal,float power,std::uint32_t seed){
    if(!HasPatch(key)||!finite(point)||!finite(normal)||!std::isfinite(power))return 0;
    auto& patch=impl->patches.at(key);auto& api=impl->physics.GetBodyInterface();
    const Vec3 n=j(normal).NormalizedOr(Vec3::sAxisZ());
    if(!seed)seed=0x95d1a73u;
    const unsigned count=(std::min)(unsigned(8+std::clamp(power,0.f,1.f)*4),MaxStones-unsigned(impl->stones.size()));
    unsigned made=0;
    for(unsigned i=0;i<count;++i){
        const unsigned kind=unsigned(random01(seed)*4);const float radius=.018f+random01(seed)*.042f;
        ConvexHullShapeSettings shape;shape.mMaxConvexRadius=.001f;
        for(auto p:StoneVertices(kind,radius))shape.mPoints.push_back(j(p));
        auto result=shape.Create();if(result.HasError())continue;
        const Vec3 outward=Vec3(random01(seed)*2-1,random01(seed)*2-1,random01(seed)*.5f).NormalizedOr(Vec3::sAxisZ());
        const Vec3 p=j(point)+n*(radius+.012f)+outward*.04f;
        BodyCreationSettings creation(result.Get(),p,Quat::sIdentity(),EMotionType::Dynamic,ObjectLayer(patch.slot+16));
        creation.mMotionQuality=EMotionQuality::LinearCast;creation.mFriction=.75f;creation.mRestitution=.18f;
        creation.mLinearDamping=.12f;creation.mAngularDamping=.4f;
        creation.mLinearVelocity=outward*(.7f+random01(seed)*1.6f)+n*(1.4f+random01(seed)*1.8f);
        creation.mAngularVelocity={random01(seed)*20-10,random01(seed)*20-10,random01(seed)*20-10};
        const auto id=api.CreateAndAddBody(creation,EActivation::Activate);if(id.IsInvalid())break;
        Impl::Body body{};body.id=id;body.patch=key;body.shape=kind;body.radius=radius;body.life=3.f+random01(seed)*2;
        const unsigned shade=110+unsigned(random01(seed)*55);body.color=0xff000000u|(shade<<16)|((shade-3)<<8)|(shade-12);
        body.poses.fill(pose(api,id));impl->stones.push_back(body);++patch.users;++made;
    }
    return made;
}
void World::Tick(bool paused){
    if(paused||!impl)return;
    auto& w=*impl;auto& api=w.physics.GetBodyInterface();
    const auto begin=std::chrono::steady_clock::now();
    for(auto& b:w.stones)b.poses[0]=b.poses[4];
    if(!w.stones.empty())for(unsigned step=1;step<=4;++step){
        if(w.physics.Update(StepSeconds,1,&w.temporary,&w.jobs)!=EPhysicsUpdateError::None)++w.stats.errors;
        for(auto& b:w.stones)b.poses[step]=pose(api,b.id);
    }
    for(auto b=w.stones.begin();b!=w.stones.end();){
        b->age+=TickSeconds;const auto p=b->poses[4].position;const auto c=w.patches.at(b->patch).center;
        if(b->age>=b->life||!finite(p)||std::abs(p.x-c.x)>PatchRadius-1||std::abs(p.y-c.y)>PatchRadius-1||p.z<c.z-20){
            --w.patches.at(b->patch).users;w.remove(b->id);b=w.stones.erase(b);
        }else ++b;
    }
    w.retireUnused();w.stats.stepMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
}
std::vector<Stone> World::Sample(float fraction) const {
    std::vector<Stone> result;if(!impl)return result;
    fraction=std::clamp(fraction,0.f,1.f)*4;unsigned at=(std::min)(unsigned(fraction),3u);float t=fraction-at;
    result.reserve(impl->stones.size());
    for(const auto& b:impl->stones){
        const auto& a=b.poses[at];const auto& z=b.poses[at+1];Stone s;
        s.pose.position={a.position.x+(z.position.x-a.position.x)*t,a.position.y+(z.position.y-a.position.y)*t,a.position.z+(z.position.z-a.position.z)*t};
        const Quat q=Quat(a.rotation[0],a.rotation[1],a.rotation[2],a.rotation[3]).SLERP(Quat(z.rotation[0],z.rotation[1],z.rotation[2],z.rotation[3]),t);
        s.pose.rotation={q.GetX(),q.GetY(),q.GetZ(),q.GetW()};s.radius=b.radius;s.shape=b.shape;s.color=b.color;
        s.alpha=std::clamp((b.life-b.age+(1-fraction/4)*TickSeconds)/.6f,0.f,1.f);result.push_back(s);
    }return result;
}
Stats World::Statistics() const {
    if(!impl)return {};auto s=impl->stats;s.bodies=unsigned(impl->stones.size());s.patches=unsigned(impl->patches.size());
    s.active=impl->physics.GetNumActiveBodies(EBodyType::RigidBody);return s;
}
}
