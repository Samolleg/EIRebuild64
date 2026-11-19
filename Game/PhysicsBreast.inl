// Per-character GPU scene sharing the debris SDK and CUDA context.
using namespace owned::gltf;
namespace {
PxVec3 pv(const SecondaryPoint& p){return {p[0],p[1],p[2]};}
SecondaryPoint point3(const PxVec3& p){return {p.x,p.y,p.z};}
constexpr float radians=PxPi/180.f;
struct Drive {float spring,massBlend,dampingPercent;};
struct JointPreset {
    float density,low,high,swingY,swingZ,targetAngle,travel;
    Drive x,z,twist,swing;
};
constexpr JointPreset presets[]={
    {.66f,-14,18,25,25,2,.85f,{350,.6f,.41250002f},{335,.575f,.41250002f},{4.3500004f,.9f,.41250002f},{4.3500004f,.9f,.41250002f}},
    {.1446192f,-18,18,18,18,3,.40f,{},{500,.98f,2.5f},{.5f,.98f,1.5f},{.5f,.98f,1.5f}},
    {.0041712f,0,0,0,0,0,.55f,{},{500,.98f,4.5f},{},{}},
    {4.5f,-27.5f,45,23,9,12,0,{},{},{2.6f,.9f,.075f},{2.9f,.9f,.075f}}
};
PxTransform segmentFrame(const SecondarySegment& s){
    auto z=pv(s.end)-pv(s.pivot);
    auto y=z.cross(pv(s.lateral));
    if(z.normalize()<1e-6f || y.normalize()<1e-6f)throw std::runtime_error("Degenerate Nala joint frame");
    return PxTransform(pv(s.pivot),PxQuat(PxMat33(y.cross(z),y,z)).getNormalized());
}
PxD6JointDrive drive(Drive d,float mass){
    const float k=d.spring*(1+d.massBlend*(mass-1));
    return PxD6JointDrive(k,k*d.dampingPercent*.01f,PX_MAX_F32);
}
bool jump(const SecondaryTargets& a,const SecondaryTargets& b){
    for(unsigned i=0;i<body_parts;++i)
        if((pv(a.body[i].pivot)-pv(b.body[i].pivot)).magnitudeSquared()>.25f)return true;
    return a.has_hair!=b.has_hair;
}
class CharacterSolver final: public SecondaryPhysics {
    std::shared_ptr<Runtime> sdk;
    Owned<PxScene> scene;
    std::array<Owned<PxRigidDynamic>,4> anchors;
    std::array<Owned<PxRigidDynamic>,body_parts> bodies;
    std::array<Owned<PxD6Joint>,body_parts> joints;
    std::array<PxVec3,hair_groups*hair_points> particles{},oldParticles{};
    SecondaryTargets previous{};
    SecondaryPose result{};
    std::array<float,body_parts> travel{};
    double last=0;
    std::string failure;
    bool started=false;
    static unsigned anchorIndex(unsigned i){return 2*(i/4)+(i%4==3);}
    static bool anchored(unsigned i){return i%4==0||i%4==3;}
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
        for(auto& a:anchors){
            a.reset(sdk->physics->createRigidDynamic(PxTransform(PxIdentity)));
            if(!a)throw std::runtime_error("Nala anchor allocation");
            a->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC,true);scene->addActor(*a);
        }
        for(unsigned i=0;i<body_parts;++i){
            auto& b=bodies[i];b.reset(sdk->physics->createRigidDynamic(PxTransform(PxIdentity)));
            if(!b)throw std::runtime_error("Nala body allocation");
            b->setAngularDamping(.05f);b->setSolverIterationCounts(21,1);b->setSleepThreshold(0);
            if(i%4!=3)b->setMaxDepenetrationVelocity(3.f);
            scene->addActor(*b);
            auto* parent=anchored(i)?anchors[anchorIndex(i)].get():bodies[i-1].get();
            joints[i].reset(PxD6JointCreate(*sdk->physics,parent,PxTransform(PxIdentity),b.get(),PxTransform(PxIdentity)));
            if(!joints[i])throw std::runtime_error("Nala joint allocation");
        }
    }
    void configure(const SecondaryTargets& targets){
        // Density coefficients scale Nala's reference volumes. The old 450 g
        // per side calibrates the neutral three-part breast, not each segment.
        constexpr float referenceLength[]={.0545f,.0457f,.0423f,.0636f};
        std::array<float,body_parts> masses{},lengths{};
        for(unsigned i=0;i<body_parts;++i){
            unsigned k=i%4;
            float length=(pv(targets.body[i].end)-pv(targets.body[i].pivot)).magnitude();
            lengths[i]=length;
            float scale=std::clamp(length/referenceLength[k],.5f,2.f);
            masses[i]=.45f*presets[k].density/(.66f+.1446192f+.0041712f)*scale*scale*scale;
        }
        for(unsigned i=0;i<body_parts;++i){
            const unsigned k=i%4;
            const auto& preset=presets[k];auto& b=*bodies[i];auto& j=*joints[i];
            const float length=lengths[i],mass=masses[i];
            float effectiveMass=mass;
            if(k<2)for(unsigned n=i+1;n<=i+(2-k);++n)effectiveMass+=masses[n];
            b.setMass(mass);b.setCMassLocalPose(PxTransform(PxVec3(0,0,length*.5f)));
            b.setMassSpaceInertiaTensor(PxVec3((std::max)(1e-7f,mass*length*length/6.f)));
            const float limit=travel[i]=length*preset.travel;
            for(auto axis:{PxD6Axis::eX,PxD6Axis::eZ}){
                if(k<3&&(axis==PxD6Axis::eZ||k==0)){
                    j.setMotion(axis,PxD6Motion::eLIMITED);
                    j.setLinearLimit(axis,PxJointLinearLimitPair(sdk->physics->getTolerancesScale(),-limit*1.25f,limit*2.f));
                }
            }
            if(k!=2){
                for(auto axis:{PxD6Axis::eTWIST,PxD6Axis::eSWING1,PxD6Axis::eSWING2})j.setMotion(axis,PxD6Motion::eLIMITED);
                j.setTwistLimit(PxJointAngularLimitPair(preset.low*radians,preset.high*radians));
                j.setSwingLimit(PxJointLimitCone(preset.swingY*radians,preset.swingZ*radians));
            }
            j.setDrive(PxD6Drive::eX,drive(preset.x,effectiveMass));j.setDrive(PxD6Drive::eZ,drive(preset.z,effectiveMass));
            j.setDrive(PxD6Drive::eTWIST,drive(preset.twist,effectiveMass));
            j.setDrive(PxD6Drive::eSWING1,drive(preset.swing,effectiveMass)); j.setDrive(PxD6Drive::eSWING2,drive(preset.swing,effectiveMass));
            j.setDrivePosition(PxTransform(PxVec3(0,0,k==0?-.45f*length:0.f),PxQuat(preset.targetAngle*radians,PxVec3(1,0,0))));
        }
    }
    void resetAt(double now,const SecondaryTargets& target){
        configure(target);
        for(unsigned i=0;i<body_parts;++i){
            auto frame=segmentFrame(target.body[i]);
            if(anchored(i))anchors[anchorIndex(i)]->setGlobalPose(frame);
            bodies[i]->setGlobalPose(frame);bodies[i]->setLinearVelocity(PxVec3(0));bodies[i]->setAngularVelocity(PxVec3(0));
        }
        for(unsigned i=0;i<particles.size();++i)oldParticles[i]=particles[i]=pv(target.hair[i]);
        previous=target;started=true;last=now;result={};
    }
    void constrainBreastTravel(){
        // The reference adds four position-only passes after the D6 solve.
        // Work in the current parent joint frame, including locked axes.
        for(unsigned pass=0;pass<4;++pass){
            bool corrected=false;
            for(unsigned i=0;i<body_parts;++i){
                const unsigned k=i%4;
                if(k==3)continue;
                const auto parent=anchored(i)?anchors[anchorIndex(i)]->getGlobalPose():
                    bodies[i-1]->getGlobalPose()*joints[i]->getLocalPose(PxJointActorIndex::eACTOR0);
                auto pose=bodies[i]->getGlobalPose();
                auto local=parent.transformInv(pose.p);
                auto next=local;
                for(unsigned axis=0;axis<3;++axis){
                    const float limit=(axis==2||(axis==0&&k==0))?travel[i]:0.f;
                    const float clipped=std::clamp(local[axis],-limit*1.25f,limit*2.f);
                    if(std::abs(clipped-local[axis])>1e-5f)next[axis]=clipped*.988f;
                }
                if(next!=local){pose.p=parent.transform(next);bodies[i]->setGlobalPose(pose);corrected=true;}
            }
            if(!corrected)break;
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
                std::array<PxTransform,body_parts> from,to,frame;
                for(unsigned i=0;i<body_parts;++i){from[i]=segmentFrame(previous.body[i]);to[i]=segmentFrame(target.body[i]);}
                const int steps=int(std::floor((elapsed+1e-9)/secondary_step));
                float alpha=0;
                for(int step=1;step<=steps;++step){
                    alpha=float((std::min)(1.,step*secondary_step/elapsed));
                    for(unsigned i=0;i<body_parts;++i){
                        frame[i]=PxTransform(from[i].p*(1-alpha)+to[i].p*alpha,PxSlerp(alpha,from[i].q,to[i].q));
                        if(anchored(i))anchors[anchorIndex(i)]->setKinematicTarget(frame[i]);
                        else joints[i]->setLocalPose(PxJointActorIndex::eACTOR0,frame[i-1].transformInv(frame[i]));
                    }
                    scene->simulate(float(secondary_step));PxU32 error=0;
                    if(!scene->fetchResults(true,&error)||error)throw std::runtime_error("Nala GPU step failed");
                    constrainBreastTravel();
                    if(target.has_hair)hairStep(target,alpha);
                }
                for(unsigned i=0;i<body_parts;++i){
                    auto pose=bodies[i]->getGlobalPose();
                    if(!pose.isValid())throw std::runtime_error("Invalid Nala physics pose");
                    auto q=(pose.q*frame[i].q.getConjugate()).getNormalized();
                    result.body[i]={point3(pose.p-frame[i].p),{q.x,q.y,q.z,q.w}};
                }
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
