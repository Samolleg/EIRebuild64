// Per-character GPU scene sharing the debris SDK and CUDA context.
using namespace owned::gltf;
namespace {
PxVec3 pv(const SecondaryPoint& p){return {p[0],p[1],p[2]};}
SecondaryPoint point3(const PxVec3& p){return {p.x,p.y,p.z};}
// One inertial point per breast and per glute. The model already describes its
// gravity-loaded rest shape. Frequencies and damping ratios are mass-normalized.
struct TissueResponse {
    PxVec3 frequencyHz;     // lateral, vertical tangent, outward
    float dampingRatio;
    PxVec3 travelFraction;  // ellipsoid radii relative to the local lever length
};
const TissueResponse breastResponse{{5.f,4.f,6.f},.95f,{.065f,.095f,.045f}};
const TissueResponse gluteResponse{{7.f,6.f,8.f},1.f,{.035f,.055f,.025f}};
constexpr unsigned tissueCount=4;
struct TissueFrame {PxTransform rest;float length;};
TissueFrame tissueFrame(const SecondaryTargets& targets,unsigned tissue){
    const bool glute=tissue%2!=0;
    const unsigned side=4*(tissue/2);
    const auto& first=targets.body[side+(glute?3:0)];
    const auto tip=pv(targets.body[side+(glute?3:2)].end);
    auto outward=tip-pv(first.pivot);
    const float length=outward.normalize();
    auto vertical=outward.cross(pv(first.lateral));
    if(length<1e-6f||vertical.normalize()<1e-6f)
        throw std::runtime_error("Degenerate Nala tissue frame");
    return {PxTransform(tip,PxQuat(PxMat33(vertical.cross(outward),vertical,outward)).getNormalized()),length};
}
bool jump(const SecondaryTargets& a,const SecondaryTargets& b){
    for(unsigned i=0;i<body_parts;++i)
        if((pv(a.body[i].pivot)-pv(b.body[i].pivot)).magnitudeSquared()>.25f)return true;
    return a.has_hair!=b.has_hair;
}
class CharacterSolver final: public SecondaryPhysics {
    std::shared_ptr<Runtime> sdk;
    Owned<PxScene> scene;
    std::array<Owned<PxRigidDynamic>,tissueCount> anchors,bodies;
    std::array<Owned<PxD6Joint>,tissueCount> joints;
    std::array<PxVec3,hair_groups*hair_points> particles{},oldParticles{};
    SecondaryTargets previous{};
    SecondaryPose result{};
    double last=0;
    std::string failure;
    bool started=false;
    void initialize(){
        sdk=runtime();
        PxSceneDesc desc(sdk->physics->getTolerancesScale());
        desc.gravity=PxVec3(0,0,-9.81f);desc.cpuDispatcher=sdk->dispatcher.get();desc.filterShader=filter;
        desc.cudaContextManager=sdk->cuda.get();desc.broadPhaseType=PxBroadPhaseType::eGPU;
        desc.flags|=PxSceneFlag::eENABLE_GPU_DYNAMICS|PxSceneFlag::eENABLE_PCM;
        desc.solverType=PxSolverType::eTGS;
        desc.gpuDynamicsConfig.tempBufferCapacity=1024*1024;
        desc.gpuDynamicsConfig.heapCapacity=4*1024*1024;
        desc.gpuDynamicsConfig.maxRigidContactCount=1024;
        desc.gpuDynamicsConfig.maxRigidPatchCount=1024;
        desc.gpuDynamicsConfig.foundLostPairsCapacity=1024;
        desc.gpuDynamicsConfig.foundLostAggregatePairsCapacity=1024;
        desc.gpuDynamicsConfig.totalAggregatePairsCapacity=1024;
        desc.gpuDynamicsConfig.collisionStackSize=1024*1024;
        scene.reset(sdk->physics->createScene(desc));
        if(!scene || !scene->getFlags().isSet(PxSceneFlag::eENABLE_GPU_DYNAMICS))
            throw std::runtime_error("Nala GPU scene unavailable");
        for(unsigned i=0;i<tissueCount;++i){
            auto& a=anchors[i];auto& b=bodies[i];
            a.reset(sdk->physics->createRigidDynamic(PxTransform(PxIdentity)));
            b.reset(sdk->physics->createRigidDynamic(PxTransform(PxIdentity)));
            if(!a||!b)throw std::runtime_error("Nala tissue actor allocation");
            a->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC,true);
            b->setActorFlag(PxActorFlag::eDISABLE_GRAVITY,true);
            b->setSleepThreshold(0);
            scene->addActor(*a);scene->addActor(*b);
            auto& joint=joints[i];
            joint.reset(PxD6JointCreate(*sdk->physics,a.get(),PxTransform(PxIdentity),b.get(),PxTransform(PxIdentity)));
            if(!joint)throw std::runtime_error("Nala tissue joint allocation");
            const auto& response=i%2?gluteResponse:breastResponse;
            for(unsigned axis=0;axis<3;++axis){
                const float omega=PxTwoPi*response.frequencyHz[axis];
                joint->setMotion(PxD6Axis::Enum(axis),PxD6Motion::eFREE);
                joint->setDrive(PxD6Drive::Enum(axis),PxD6JointDrive(omega*omega,
                    2*response.dampingRatio*omega,PX_MAX_F32,true));
            }
        }
    }
    void resetAt(double now,const SecondaryTargets& target){
        for(unsigned i=0;i<tissueCount;++i){
            const auto frame=tissueFrame(target,i).rest;
            anchors[i]->setGlobalPose(frame);bodies[i]->setGlobalPose(frame);
            bodies[i]->setLinearVelocity(PxVec3(0));bodies[i]->setAngularVelocity(PxVec3(0));
        }
        for(unsigned i=0;i<particles.size();++i)oldParticles[i]=particles[i]=pv(target.hair[i]);
        previous=target;started=true;last=now;result={};
    }
    void publishTissue(unsigned i,const PxTransform& rest,float length){
        const bool glute=i%2!=0;
        const unsigned first=4*(i/2);
        // Project the visible offset onto an anatomical envelope in the current
        // anchor frame. Do not teleport the solver body or feed the projection back.
        auto local=rest.transformInv(bodies[i]->getGlobalPose().p);
        if(!local.isFinite())throw std::runtime_error("Nonfinite Nala tissue displacement");
        const auto radii=(glute?gluteResponse:breastResponse).travelFraction*length;
        const PxVec3 normalized(local.x/radii.x,local.y/radii.y,local.z/radii.z);
        const float norm=normalized.magnitude();
        if(norm>1.f)local/=norm;
        if(glute){
            // The pelvis is the pivot. Preserve volume and the distance to the
            // marker: only the tangential component produces a small rotation.
            const auto axis=rest.q.rotate(PxVec3(0,0,1));
            const auto direction=rest.q.rotate((PxVec3(0,0,length)+local).getNormalized());
            // atan2/cross keeps small rotations below 1 degree continuous.
            auto cross=axis.cross(direction);
            const float sine=cross.normalize();
            const PxQuat q=sine>1e-7f?PxQuat(std::atan2(sine,axis.dot(direction)),cross):PxQuat(PxIdentity);
            result.body[first+3].rotation={q.x,q.y,q.z,q.w};
        }else{
            const auto shift=rest.q.rotate(local);
            constexpr float influence[]={.20f,.55f,1.f};
            for(unsigned k=0;k<3;++k)result.body[first+k].shift=point3(shift*influence[k]);
        }
    }
    void hairStep(const SecondaryTargets& target,float alpha){
        // Values of the SMA Hermite elasticity curves at 0, .25, .5, .75, 1.
        static constexpr float mobility[2][5]={{0.000000000f,0.173898976f,0.639642874f,0.906160696f,0.995000005f},{0.000000000f,0.481814149f,0.892846832f,0.987703569f,0.994000018f}};
        constexpr float dt=float(secondary_step);
        for(unsigned i=0;i<particles.size();++i){
            const unsigned k=i%hair_points;
            const auto rest=pv(previous.hair[i])*(1-alpha)+pv(target.hair[i])*alpha;
            auto next=rest;
            if(k){
                const auto prevRest=pv(previous.hair[i-1])*(1-alpha)+pv(target.hair[i-1])*alpha;
                next=particles[i]+(particles[i]-oldParticles[i])*.8666667044f+PxVec3(0,0,-40.f*dt*dt);
                next=rest+(next-rest)*mobility[i/hair_points>=2][k];
                const auto dir=next-particles[i-1];
                const float len=(rest-prevRest).magnitude();
                if(dir.magnitudeSquared()>1e-10f)next=particles[i-1]+dir.getNormalized()*len;
                // Keep the guide within the character's secondary-motion envelope.
                auto delta=next-rest;
                if(delta.magnitudeSquared()>secondary_limit*secondary_limit)next=rest+delta.getNormalized()*secondary_limit;
            }
            oldParticles[i]=particles[i];particles[i]=next;result.hair[i]=point3(next-rest);
        }
    }
public:
    const std::string& error() const override{return failure;}
    double solvedClock() const{return last;}
    SecondaryPose advance(double now,const SecondaryTargets& target,bool reset) override {
        if(!failure.empty())return {};
        try{
            if(!std::isfinite(now))throw std::runtime_error("Nonfinite Nala clock");
            if(!scene)initialize();
            const double elapsed=now-last;
            if(!started||reset||elapsed < -1e-9||elapsed>.25||jump(target,previous))resetAt(now,target);
            else if(elapsed+1e-9>=secondary_step){
                std::array<TissueFrame,tissueCount> from,to;
                std::array<PxTransform,tissueCount> frame;
                for(unsigned i=0;i<tissueCount;++i){
                    from[i]=tissueFrame(previous,i);to[i]=tissueFrame(target,i);
                }
                const int steps=int(std::floor((elapsed+1e-9)/secondary_step));
                float alpha=0;
                for(int step=1;step<=steps;++step){
                    alpha=float((std::min)(1.,step*secondary_step/elapsed));
                    for(unsigned i=0;i<tissueCount;++i){
                        const auto& a=from[i].rest;const auto& b=to[i].rest;
                        frame[i]=PxTransform(a.p*(1-alpha)+b.p*alpha,PxSlerp(alpha,a.q,b.q));
                        anchors[i]->setKinematicTarget(frame[i]);
                    }
                    scene->simulate(float(secondary_step));PxU32 error=0;
                    if(!scene->fetchResults(true,&error)||error)throw std::runtime_error("Nala GPU step failed");
                    if(target.has_hair)hairStep(target,alpha);
                }
                for(unsigned i=0;i<tissueCount;++i)
                    publishTissue(i,frame[i],from[i].length*(1-alpha)+to[i].length*alpha);
                // Retain the sample at the last completed fixed tick.
                auto lerp=[&](SecondaryPoint& a,const SecondaryPoint& b){a=point3(pv(a)*(1-alpha)+pv(b)*alpha);};
                for(unsigned i=0;i<body_parts;++i){lerp(previous.body[i].pivot,target.body[i].pivot);
                    lerp(previous.body[i].end,target.body[i].end);lerp(previous.body[i].lateral,target.body[i].lateral);}
                if(target.has_hair)for(unsigned i=0;i<particles.size();++i)lerp(previous.hair[i],target.hair[i]);
                last+=steps*secondary_step;
            }
            if(sdk->errors.count)throw std::runtime_error(sdk->errors.Message());
            return result;
        }catch(const std::exception& e){failure=e.what();return {};}
    }
};
}
std::shared_ptr<SecondaryPhysics> CreateCharacterPhysics(){return std::make_shared<CharacterSolver>();}
namespace {
class CharacterPhysicsWorker final:public SecondaryPhysics {
    struct Input {double clock=0; SecondaryTargets targets{}; std::uint64_t epoch=0;};
    Input latest;
    struct Sample {double clock=0;SecondaryPose pose{};};
    Sample older{},newer{};
    SecondaryPose visible{};
    std::string failure,visibleFailure;
    bool stopping=false,pending=false,submitted=false,initialized=false,visibleReady=false;
    std::mutex mutex;
    std::condition_variable wake;
    std::thread worker;
    void run() noexcept {
        try{
            CharacterSolver solver;std::uint64_t epoch=0;
            for(;;){
                Input input;
                {std::unique_lock lock(mutex);wake.wait(lock,[&]{return pending||stopping;});
                    if(stopping)return;input=latest;pending=false;}
                auto pose=solver.advance(input.clock,input.targets,input.epoch!=epoch);epoch=input.epoch;
                std::lock_guard lock(mutex);failure=solver.error();
                if(input.epoch==latest.epoch && failure.empty()){
                    Sample solved{solver.solvedClock(),pose};
                    if(!initialized){older=newer=solved;initialized=true;}
                    else if(solved.clock>newer.clock){older=newer;newer=solved;}
                }
            }
        }catch(const std::exception& e){std::lock_guard lock(mutex);failure=e.what();initialized=false;}
    }
public:
    CharacterPhysicsWorker():worker([this]{run();}){}
    ~CharacterPhysicsWorker() override{
        {std::lock_guard lock(mutex);stopping=true;}wake.notify_one();worker.join();
    }
    const std::string& error() const override{return visibleFailure;}
    bool ready() const override{return visibleReady;}
    SecondaryPose advance(double clock,const SecondaryTargets& targets,bool reset) override{
        std::lock_guard lock(mutex);visibleFailure=failure;
        if(!failure.empty())return {};
        reset|=!submitted||clock<latest.clock||clock-latest.clock>.25||jump(targets,latest.targets);
        if(!reset&&clock==latest.clock)return visible;
        if(reset){++latest.epoch;visible={};initialized=false;}
        latest.clock=clock;latest.targets=targets;submitted=pending=true;visibleReady=initialized;
        if(initialized){
            const double span=newer.clock-older.clock;
            const float t=span>0?float(std::clamp((clock-secondary_step-older.clock)/span,0.,1.)):1;
            for(unsigned i=0;i<body_parts;++i){
                auto& a=older.pose.body[i];auto& b=newer.pose.body[i];
                visible.body[i].shift=point3(pv(a.shift)*(1-t)+pv(b.shift)*t);
                visible.body[i].rotation=interpolate(a.rotation,b.rotation,t);
            }
            for(unsigned i=0;i<visible.hair.size();++i)
                visible.hair[i]=point3(pv(older.pose.hair[i])*(1-t)+pv(newer.pose.hair[i])*t);
        }
        wake.notify_one();return visible;
    }
};
}
void InstallCharacterPhysics(){
    secondary_factory=[]()->std::shared_ptr<SecondaryPhysics>{return std::make_shared<CharacterPhysicsWorker>();};
}
