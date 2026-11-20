// Fixed-step hair guides; only hair anchors and offsets cross the worker boundary.
using namespace owned::gltf;
namespace {
PxVec3 pv(const SecondaryPoint& p){return {p[0],p[1],p[2]};}
SecondaryPoint point3(const PxVec3& p){return {p.x,p.y,p.z};}
bool jump(const SecondaryTargets& a,const SecondaryTargets& b){
    for(unsigned i=0;i<a.hair.size();i+=hair_points)
        if((pv(a.hair[i])-pv(b.hair[i])).magnitudeSquared()>.25f)return true;
    return false;
}
class CharacterSolver final: public SecondaryPhysics {
    std::array<PxVec3,hair_groups*hair_points> particles{},oldParticles{};
    SecondaryTargets previous{};
    SecondaryPose result{};
    double last=0;
    std::string failure;
    bool started=false;
    void resetAt(double now,const SecondaryTargets& target){
        for(unsigned i=0;i<particles.size();++i)oldParticles[i]=particles[i]=pv(target.hair[i]);
        previous=target;started=true;last=now;result={};
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
            const double elapsed=now-last;
            if(!started||reset||elapsed < -1e-9||elapsed>.25||jump(target,previous))resetAt(now,target);
            else if(elapsed+1e-9>=secondary_step){
                const int steps=int(std::floor((elapsed+1e-9)/secondary_step));
                float alpha=0;
                for(int step=1;step<=steps;++step){
                    alpha=float((std::min)(1.,step*secondary_step/elapsed));
                    hairStep(target,alpha);
                }
                // Retain the sample at the last completed fixed tick.
                auto lerp=[&](SecondaryPoint& a,const SecondaryPoint& b){a=point3(pv(a)*(1-alpha)+pv(b)*alpha);};
                for(unsigned i=0;i<particles.size();++i)lerp(previous.hair[i],target.hair[i]);
                last+=steps*secondary_step;
            }
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
