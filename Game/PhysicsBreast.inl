// Same PhysX foundation/CUDA context as debris, separate per-actor GPU scene.
// Included inside namespace PhysicsFx after the shared runtime helpers.
class BreastPhysics final : public owned::gltf::SecondaryPhysics {
    std::shared_ptr<Runtime> sdk;
    Owned<PxScene> scene;
    std::array<Owned<PxRigidDynamic>,2> anchors, bodies;
    std::array<Owned<PxD6Joint>,2> joints;
    owned::gltf::SecondaryPoints previous{}, offsets{};
    double last = 0, accumulated = 0;
    bool started = false;
    std::string failure;
    static PxVec3 point(const std::array<float,3>& p) { return {p[0],p[1],p[2]}; }
    void initialize() {
        sdk=runtime();
        PxSceneDesc desc(sdk->physics->getTolerancesScale());
        desc.gravity=PxVec3(0); // authored rest shape already includes gravity
        desc.cpuDispatcher=sdk->dispatcher.get();desc.filterShader=filter;
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
        if(!scene || !scene->getFlags().isSet(PxSceneFlag::eENABLE_GPU_DYNAMICS) ||
           scene->getBroadPhaseType()!=PxBroadPhaseType::eGPU)
            throw std::runtime_error("Breast PhysX GPU scene unavailable");
        for(unsigned i=0;i<2;++i) {
            anchors[i].reset(sdk->physics->createRigidDynamic(PxTransform(PxIdentity)));
            bodies[i].reset(sdk->physics->createRigidDynamic(PxTransform(PxIdentity)));
            if(!anchors[i]||!bodies[i])throw std::runtime_error("Breast actor allocation failed");
            anchors[i]->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC,true);
            bodies[i]->setMass(.35f);bodies[i]->setMassSpaceInertiaTensor(PxVec3(.003f));
            bodies[i]->setSolverIterationCounts(8,2);
            bodies[i]->setSleepThreshold(0);
            scene->addActor(*anchors[i]);scene->addActor(*bodies[i]);
            joints[i].reset(PxD6JointCreate(*sdk->physics,anchors[i].get(),PxTransform(PxIdentity),
                bodies[i].get(),PxTransform(PxIdentity)));
            if(!joints[i])throw std::runtime_error("Breast spring allocation failed");
            for(auto axis:{PxD6Axis::eX,PxD6Axis::eY,PxD6Axis::eZ})joints[i]->setMotion(axis,PxD6Motion::eLIMITED);
            joints[i]->setDistanceLimit(PxJointLinearLimit(owned::gltf::secondary_limit));
            for(auto axis:{PxD6Drive::eX,PxD6Drive::eY,PxD6Drive::eZ})
                joints[i]->setDrive(axis,PxD6JointDrive(160.f,6.f,PX_MAX_F32));
        }
        if(sdk->errors.count)throw std::runtime_error(sdk->errors.Message());
    }
public:
    ~BreastPhysics() override {
        for(auto& j:joints)j.reset();
        for(auto& b:bodies)b.reset();for(auto& a:anchors)a.reset();scene.reset();
    }
    const std::string& error() const override {return failure;}
    owned::gltf::SecondaryPoints advance(double clock,const owned::gltf::SecondaryPoints& targets,bool reset) override {
        if(!failure.empty())return {};
        try {
            if(!std::isfinite(clock))throw std::runtime_error("Nonfinite breast clock");
            for(const auto& p:targets)if(!finite(vec(point(p))))throw std::runtime_error("Nonfinite breast target");
            if(!sdk)initialize();
            const double elapsed=clock-last;
            for(unsigned i=0;i<2;++i)reset|=(point(targets[i])-point(previous[i])).magnitudeSquared()>.25f;
            if(!started||reset||elapsed<0||elapsed>.25) {
                for(unsigned i=0;i<2;++i) {
                    const PxTransform pose(point(targets[i]));
                    anchors[i]->setGlobalPose(pose);bodies[i]->setGlobalPose(pose);
                    bodies[i]->setLinearVelocity(PxVec3(0));bodies[i]->setAngularVelocity(PxVec3(0));
                }
                started=true;last=clock;previous=targets;accumulated=0;offsets={};return offsets;
            }
            if(elapsed==0)return offsets; // pause, shadow pass, attachment query
            constexpr double step=1./120.;
            accumulated+=elapsed;
            while(accumulated+1e-10>=step) {
                const float fraction=float(std::clamp((elapsed-accumulated+step)/elapsed,0.,1.));
                for(unsigned i=0;i<2;++i) {
                    const auto p=point(previous[i])+(point(targets[i])-point(previous[i]))*fraction;
                    anchors[i]->setKinematicTarget(PxTransform(p));
                }
                scene->simulate(float(step));scene->fetchResults(true);accumulated-=step;
                if(sdk->errors.count)throw std::runtime_error(sdk->errors.Message());
                for(unsigned i=0;i<2;++i) {
                    auto delta=bodies[i]->getGlobalPose().p-anchors[i]->getGlobalPose().p;
                    const float length=delta.magnitude();
                    if(length>owned::gltf::secondary_limit)delta*=owned::gltf::secondary_limit/length;
                    offsets[i]={delta.x,delta.y,delta.z};
                }
            }
            last=clock;previous=targets;return offsets;
        }catch(const std::exception& e){failure=e.what();offsets={};return {};}
    }
};
void InstallCharacterPhysics() {
    owned::gltf::secondary_factory=[]()->std::shared_ptr<owned::gltf::SecondaryPhysics>{return std::make_shared<BreastPhysics>();};
}
