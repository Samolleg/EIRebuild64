#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "PhysicsFxCore.h"
#include "../external/owned-renderer/Source/secondary_physics.h"
#include <PxPhysicsAPI.h>
#include <gpu/PxGpu.h>
#include <cudamanager/PxCudaContextManager.h>
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <thread>
#include <cmath>
#include <filesystem>
#include <map>
#include <mutex>
#include <stdexcept>

namespace PhysicsFx {
namespace {
using namespace physx;
template<class T> struct Release { void operator()(T* p) const {if(p)p->release();} };
template<class T> using Owned=std::unique_ptr<T,Release<T>>;
struct Errors final:PxErrorCallback {
    std::atomic<unsigned> count{0};std::mutex lock;std::string last;
    void reportError(PxErrorCode::Enum code,const char* message,const char*,int) override {
        if(code==PxErrorCode::eDEBUG_INFO)return;
        std::lock_guard<std::mutex> guard(lock);last=message?message:"PhysX error";
        // Warnings also fail this GPU path: never hide a CPU fallback.
        ++count;
    }
    std::string Message(){std::lock_guard<std::mutex> guard(lock);return last;}
};
struct Runtime {
    PxDefaultAllocator allocator;Errors errors;
    Owned<PxFoundation> foundation;Owned<PxPhysics> physics;
    Owned<PxCudaContextManager> cuda;Owned<PxDefaultCpuDispatcher> dispatcher;
    HMODULE module=nullptr,driver=nullptr;
    ~Runtime(){dispatcher.reset();physics.reset();cuda.reset();foundation.reset();if(module)FreeLibrary(module);if(driver)FreeLibrary(driver);}
    void Initialize(){
        wchar_t executable[32768]{};auto length=GetModuleFileNameW(nullptr,executable,32768);
        if(!length||length>=32768)throw std::runtime_error("Cannot resolve PhysX GPU runtime directory");
        const auto path=std::filesystem::path(executable).parent_path()/L"PhysXGpu_64.dll";
        driver=LoadLibraryExW(L"nvcuda.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!driver)throw std::runtime_error("PhysX GPU requires an NVIDIA CUDA driver; CPU fallback disabled");
        module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!module)throw std::runtime_error("Cannot load PhysXGpu_64.dll beside executable; CPU fallback disabled");
        wchar_t actual[32768]{};
        if(!GetModuleFileNameW(module,actual,32768)||_wcsicmp(path.c_str(),actual)!=0||GetModuleHandleW(L"PhysXGpu_64.dll")!=module)
            throw std::runtime_error("PhysX GPU module identity mismatch");
        if(!GetProcAddress(module,"PxCreatePhysXGpu")||!GetProcAddress(module,"PxCreateCudaContextManager"))
            throw std::runtime_error("Invalid PhysX GPU runtime exports");
        foundation.reset(PxCreateFoundation(PX_PHYSICS_VERSION,allocator,errors));
        if(!foundation)throw std::runtime_error("Cannot create PhysX foundation");
        physics.reset(PxCreatePhysics(PX_PHYSICS_VERSION,*foundation,PxTolerancesScale(),false));
        if(!physics)throw std::runtime_error("Cannot create PhysX SDK");
        PxCudaContextManagerDesc desc;cuda.reset(PxCreateCudaContextManager(*foundation,desc));
        if(!cuda||!cuda->contextIsValid()||errors.count)throw std::runtime_error("PhysX CUDA context unavailable: "+errors.Message());
        dispatcher.reset(PxDefaultCpuDispatcherCreate(1)); // SDK coordination; dynamics/broadphase run on CUDA.
        if(!dispatcher)throw std::runtime_error("Cannot create PhysX task dispatcher");
    }
};
std::shared_ptr<Runtime> runtime(){
    // One process-wide SDK. Shared leases serialize the final teardown with
    // acquisition on a different physics worker.
    struct Pool {std::mutex lock;std::unique_ptr<Runtime> sdk;unsigned users=0;};
    static const auto pool=std::make_shared<Pool>();
    struct Lease {
        std::shared_ptr<Pool> owner;
        explicit Lease(std::shared_ptr<Pool> value):owner(std::move(value)) {
            auto& pool=*owner;
            std::lock_guard guard(pool.lock);
            if(!pool.sdk) {
                auto value=std::make_unique<Runtime>();value->Initialize();
                pool.sdk=std::move(value);
            }
            ++pool.users;
        }
        ~Lease(){std::lock_guard guard(owner->lock);if(!--owner->users)owner->sdk.reset();}
    };
    auto lease=std::make_shared<Lease>(pool);
    return std::shared_ptr<Runtime>(lease,pool->sdk.get());
}
PxVec3 px(Vec p){return {p.x,p.y,p.z};}
Vec vec(const PxVec3& p){return {p.x,p.y,p.z};}
float random01(std::uint32_t& seed){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return float(seed&0xffffff)/16777216.f;}
bool finite(Vec p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
Pose pose(const PxRigidDynamic& body){const auto p=body.getGlobalPose();return {vec(p.p),{p.q.x,p.q.y,p.q.z,p.q.w}};}
PxFilterFlags filter(PxFilterObjectAttributes a,PxFilterData fa,PxFilterObjectAttributes b,PxFilterData fb,PxPairFlags& pair,const void*,PxU32){
    // Each overlapping local patch only collides with its own stones.
    if(fa.word0!=fb.word0||PxFilterObjectIsKinematic(a)||PxFilterObjectIsKinematic(b)||fa.word1==fb.word1)return PxFilterFlag::eSUPPRESS;
    pair=PxPairFlag::eCONTACT_DEFAULT;return PxFilterFlag::eDEFAULT;
}
std::array<float,4> interpolate(const std::array<float,4>& a,const std::array<float,4>& input,float t){
    auto b=input;float dot=0;for(unsigned i=0;i<4;++i)dot+=a[i]*b[i];
    if(dot<0){for(float& v:b)v=-v;dot=-dot;}
    float x=1-t,y=t;
    if(dot<.9995f){const float angle=std::acos(std::clamp(dot,-1.f,1.f)),s=std::sin(angle);x=std::sin((1-t)*angle)/s;y=std::sin(t*angle)/s;}
    std::array<float,4> result{};float length=0;for(unsigned i=0;i<4;++i){result[i]=a[i]*x+b[i]*y;length+=result[i]*result[i];}
    length=std::sqrt(length);for(float& v:result)v/=length;return result;
}
}

#include "PhysicsHair.inl"

std::array<Vec,6> StoneVertices(unsigned shape,float r){
    const float k=float(shape%4);
    return {{{r,0,.13f*r},{-.83f*r,.08f*r,0},{0,(.72f+.06f*k)*r,0},
        {.09f*r,-r,-.07f*r},{.1f*r,.1f*r,(.78f+.07f*k)*r},{-.1f*r,0,-.7f*r}}};
}
bool Events::Admit(int id,int start,int now,bool ready){
    seen.erase(std::remove_if(seen.begin(),seen.end(),[now](const Entry& e){return std::int64_t(now)-e.start>8;}),seen.end());
    for(auto e:seen)if(e.id==id&&e.start==start)return false;
    if(seen.size()>=1024)return false;
    seen.push_back({id,start});const auto age=std::int64_t(now)-start;
    return ready&&now>readyAfter&&start>readyAfter&&age>=-1&&age<=2;
}
struct World::Impl {
    std::shared_ptr<Runtime> sdk=runtime();
    Owned<PxScene> scene;Owned<PxMaterial> material;
    std::array<Owned<PxConvexMesh>,4> hulls;
    struct Patch {Owned<PxRigidStatic> actor;Owned<PxTriangleMesh> mesh;Vec center;unsigned users=0,slot=0;};
    struct Body {Owned<PxRigidDynamic> actor;std::uint64_t patch;unsigned shape;float radius,age=0,life;std::uint32_t color;std::array<Pose,2> poses;};
    std::map<std::uint64_t,Patch> patches;std::vector<Body> stones;Stats stats;
    Impl(){
        PxSceneDesc desc(sdk->physics->getTolerancesScale());
        desc.gravity={0,0,-9.81f};desc.cpuDispatcher=sdk->dispatcher.get();desc.filterShader=filter;
        desc.cudaContextManager=sdk->cuda.get();desc.broadPhaseType=PxBroadPhaseType::eGPU;
        desc.flags|=PxSceneFlag::eENABLE_GPU_DYNAMICS|PxSceneFlag::eENABLE_PCM|PxSceneFlag::eENABLE_STABILIZATION;
        desc.solverType=PxSolverType::eTGS;desc.gpuMaxNumPartitions=8;
        auto& memory=desc.gpuDynamicsConfig;
        memory.tempBufferCapacity=8*1024*1024;memory.heapCapacity=16*1024*1024;
        memory.maxRigidContactCount=8192;memory.maxRigidPatchCount=4096;
        // Broadphase sees pairs before our filter: budget all 128 stones + 16 patches.
        memory.foundLostPairsCapacity=32768;memory.foundLostAggregatePairsCapacity=1024;memory.totalAggregatePairsCapacity=1024;
        memory.collisionStackSize=8*1024*1024;
        scene.reset(sdk->physics->createScene(desc));
        if(!scene||scene->getCudaContextManager()!=sdk->cuda.get()||!scene->getFlags().isSet(PxSceneFlag::eENABLE_GPU_DYNAMICS)||scene->getBroadPhaseType()!=PxBroadPhaseType::eGPU||sdk->errors.count)
            throw std::runtime_error("PhysX GPU scene creation failed: "+sdk->errors.Message());
        material.reset(sdk->physics->createMaterial(.75f,.65f,.18f));
        if(!material)throw std::runtime_error("PhysX material allocation failed");
        PxCookingParams cooking(sdk->physics->getTolerancesScale());cooking.buildGPUData=true;
        for(unsigned kind=0;kind<hulls.size();++kind){
            auto vertices=StoneVertices(kind,1);PxConvexMeshDesc convex;
            convex.points.count=PxU32(vertices.size());convex.points.stride=sizeof(Vec);convex.points.data=vertices.data();
            convex.flags=PxConvexFlag::eCOMPUTE_CONVEX;convex.vertexLimit=64;
            hulls[kind].reset(PxCreateConvexMesh(cooking,convex,sdk->physics->getPhysicsInsertionCallback()));
            if(!hulls[kind]||!hulls[kind]->isGpuCompatible())throw std::runtime_error("Stone hull is not GPU compatible");
        }
        if(sdk->errors.count)throw std::runtime_error("PhysX GPU preparation failed: "+sdk->errors.Message());
        stats.gpuDynamics=stats.gpuBroadPhase=true;stones.reserve(MaxStones);
    }
    ~Impl(){stones.clear();patches.clear();}
    void retireUnused(){for(auto p=patches.begin();p!=patches.end();)if(!p->second.users)p=patches.erase(p);else ++p;}
};
World::World()=default;World::~World()=default;
bool World::Initialize(){
    if(attempted)return impl&&failure.empty();attempted=true;
    try{impl=std::make_unique<Impl>();return true;}catch(const std::exception& e){failure=e.what();return false;}
}
std::string World::Device() const{return impl?impl->sdk->cuda->getDeviceName():std::string();}
void World::Clear(){impl.reset();failure.clear();attempted=false;}
bool World::HasPatch(std::uint64_t key) const{return impl&&impl->patches.count(key);}
bool World::AddPatch(std::uint64_t key,Vec center,const std::vector<Triangle>& triangles){
    if(HasPatch(key))return true;
    if(!finite(center)||triangles.empty()||triangles.size()>16384||!Initialize())return false;
    impl->retireUnused();if(impl->patches.size()>=16)return false;
    std::vector<PxVec3> vertices;vertices.reserve(triangles.size()*3);std::vector<PxU32> indices;indices.reserve(triangles.size()*3);
    for(const auto& t:triangles){if(!finite(t.a)||!finite(t.b)||!finite(t.c))return false;for(auto v:{t.a,t.b,t.c}){indices.push_back(PxU32(vertices.size()));vertices.push_back(px(v));}}
    PxCookingParams cooking(impl->sdk->physics->getTolerancesScale());cooking.buildGPUData=true;cooking.meshPreprocessParams|=PxMeshPreprocessingFlag::eWELD_VERTICES;cooking.meshWeldTolerance=.0001f;
    PxTriangleMeshDesc desc;desc.points.count=PxU32(vertices.size());desc.points.stride=sizeof(PxVec3);desc.points.data=vertices.data();
    desc.triangles.count=PxU32(triangles.size());desc.triangles.stride=3*sizeof(PxU32);desc.triangles.data=indices.data();
    Owned<PxTriangleMesh> mesh(PxCreateTriangleMesh(cooking,desc,impl->sdk->physics->getPhysicsInsertionCallback()));if(!mesh)return false;
    unsigned slot=0;for(;slot<16;++slot){bool used=false;for(auto& p:impl->patches)used|=p.second.slot==slot;if(!used)break;}
    Owned<PxRigidStatic> actor(impl->sdk->physics->createRigidStatic(PxTransform(PxIdentity)));
    Owned<PxShape> shape(impl->sdk->physics->createShape(PxTriangleMeshGeometry(mesh.get()),*impl->material,true));
    if(!actor||!shape)return false;
    shape->setSimulationFilterData(PxFilterData(slot,0,0,0));shape->setContactOffset(.002f);shape->setRestOffset(0);
    actor->attachShape(*shape);impl->scene->addActor(*actor);
    impl->patches.emplace(key,Impl::Patch{std::move(actor),std::move(mesh),center,0,slot});return true;
}
unsigned World::Burst(std::uint64_t key,Vec point,Vec normal,float power,std::uint32_t seed){
    if(!HasPatch(key)||!finite(point)||!finite(normal)||!std::isfinite(power)||!failure.empty())return 0;
    auto& patch=impl->patches.at(key);PxVec3 n=px(normal);if(n.normalize()<.0001f)n={0,0,1};
    if(!seed)seed=0x95d1a73u;
    const unsigned count=std::min(unsigned(48+std::clamp(power,0.f,1.f)*16),MaxStones-unsigned(impl->stones.size()));unsigned made=0;
    for(unsigned i=0;i<count;++i){
        const unsigned kind=unsigned(random01(seed)*4);
        // A dense chip spray plus 6-8 readable chunks that survive the flash.
        const bool large=(i%8)==0;
        const float radius=large?.08f+random01(seed)*.05f:.025f+random01(seed)*.035f;
        PxVec3 outward(random01(seed)*2-1,random01(seed)*2-1,random01(seed)*.5f);if(outward.normalize()<.0001f)outward={0,0,1};
        const PxVec3 p=px(point)+n*(radius+.012f)+outward*.04f;
        Owned<PxRigidDynamic> actor(impl->sdk->physics->createRigidDynamic(PxTransform(p)));
        Owned<PxShape> shape(impl->sdk->physics->createShape(PxConvexMeshGeometry(impl->hulls[kind].get(),PxMeshScale(radius)),*impl->material,true));
        if(!actor||!shape)break;
        shape->setSimulationFilterData(PxFilterData(patch.slot,1,0,0));shape->setContactOffset(.002f);shape->setRestOffset(0);
        actor->attachShape(*shape);PxRigidBodyExt::updateMassAndInertia(*actor,1800.f);
        actor->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_SPECULATIVE_CCD,true);
        actor->setLinearDamping(.12f);actor->setAngularDamping(.4f);actor->setMaxAngularVelocity(30.f);actor->setSolverIterationCounts(4,2);
        actor->setLinearVelocity(outward*(1.4f+random01(seed)*2.6f)+n*(2.6f+random01(seed)*2.4f)+PxVec3(0,0,1.2f+random01(seed)*1.2f));
        actor->setAngularVelocity({random01(seed)*20-10,random01(seed)*20-10,random01(seed)*20-10});impl->scene->addActor(*actor);
        Impl::Body body{};body.actor=std::move(actor);body.patch=key;body.shape=kind;body.radius=radius;body.life=3.f+random01(seed)*2;
        const unsigned shade=110+unsigned(random01(seed)*55);body.color=0xff000000u|(shade<<16)|((shade-3)<<8)|(shade-12);
        body.poses.fill(pose(*body.actor));impl->stones.push_back(std::move(body));++patch.users;++made;
    }return made;
}
void World::Tick(bool paused){
    if(paused||!impl||!failure.empty())return;auto& w=*impl;
    const auto begin=std::chrono::steady_clock::now();for(auto& b:w.stones)b.poses.front()=b.poses.back();
    const bool awake=std::any_of(w.stones.begin(),w.stones.end(),
        [](const auto& stone){return !stone.actor->isSleeping();});
    w.stats.substeps=awake?1:0;
    if(awake){
        w.scene->simulate(TickSeconds);PxU32 error=0;bool complete=w.scene->fetchResults(true,&error);
        if(!complete||error||w.sdk->errors.count){failure="PhysX GPU step failed: "+w.sdk->errors.Message();++w.stats.errors;w.stats.gpuDynamics=w.stats.gpuBroadPhase=false;w.stones.clear();w.patches.clear();return;}
        for(auto& b:w.stones)b.poses.back()=pose(*b.actor);
    }
    if(awake){
        PxSimulationStatistics values{};w.scene->getSimulationStatistics(values);
        w.stats.gpuHeapBytes=values.gpuMemHeap;w.stats.gpuBroadPhaseBytes=values.gpuMemHeapBroadPhase;w.stats.gpuSolverBytes=values.gpuMemHeapSolver;
        w.stats.gpuContacts=values.gpuDynamicsMemoryConfigStatistics.rigidContactCount;
    }else w.stats.gpuContacts=0;
    for(auto b=w.stones.begin();b!=w.stones.end();){
        b->age+=TickSeconds;const auto p=b->poses.back().position,c=w.patches.at(b->patch).center;
        if(b->age>=b->life||!finite(p)||std::abs(p.x-c.x)>PatchRadius-1||std::abs(p.y-c.y)>PatchRadius-1||p.z<c.z-20){--w.patches.at(b->patch).users;b=w.stones.erase(b);}else ++b;
    }
    w.retireUnused();w.stats.stepMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
}
StoneTracks World::Snapshot() const {
    StoneTracks result;
    if(!impl || !failure.empty())return result;
    result.reserve(impl->stones.size());
    for(const auto& b:impl->stones)result.push_back({b.poses,b.radius,b.age,b.life,b.shape,b.color});
    return result;
}
std::vector<Stone> World::Sample(float fraction) const { return SampleTracks(Snapshot(),fraction); }
std::vector<Stone> SampleTracks(const StoneTracks& tracks,float fraction) {
    std::vector<Stone> result;
    const float t=std::clamp(fraction,0.f,1.f);
    result.reserve(tracks.size());for(const auto& b:tracks){
        const auto& a=b.poses.front();const auto& z=b.poses.back();Stone s;
        s.pose.position={a.position.x+(z.position.x-a.position.x)*t,a.position.y+(z.position.y-a.position.y)*t,a.position.z+(z.position.z-a.position.z)*t};
        s.pose.rotation=interpolate(a.rotation,z.rotation,t);s.radius=b.radius;s.shape=b.shape;s.color=b.color;
        s.alpha=std::clamp((b.life-b.age+(1-t)*TickSeconds)/.6f,0.f,1.f);result.push_back(s);
    }return result;
}
Stats World::Statistics() const{
    if(!impl)return {};auto s=impl->stats;s.bodies=unsigned(impl->stones.size());s.patches=unsigned(impl->patches.size());s.active=0;
    for(auto& b:impl->stones)if(!b.actor->isSleeping())++s.active;return s;
}
}
