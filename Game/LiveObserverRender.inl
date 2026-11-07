struct VisualFact {
    int id=0,status=0;
    std::uint64_t generation=0;
    std::string model;
    owned::VisualObjectPose pose;
    owned::VisualTickValues values;
};
struct RenderCapture {
    std::uint64_t frame=0,epoch=0;
    CameraView camera;
    OwnedRender::E1WorldFrame world;
    size_t worldCount=0,particleCount=0,particleSources=0,particlePoints=0;
    unsigned long long drawn=0,omitted=0;
    bool visualResult=false,visualAccepted=false;
    bool visuals=false,truncated=false,arenaPresent=false,golemPresent=false,cursePresent=false;
    bool arenaPublished=false,golemPublished=false,cursePublished=false;
    HRESULT arenaResult=S_OK,golemResult=S_OK,curseResult=S_OK;
    owned::EmbeddedArenaInput arena;
    owned::EmbeddedGolemInput golem;
    owned::EmbeddedCurseInput curse;
    std::vector<VisualFact> objects;
    std::vector<LiveParticleAccess::Value> particles;
    std::map<int,unsigned> templates;
    Clock::duration cost{};
};
void FillCapturedCameraReply(Job& job) {
    job.response["camera"]=CameraJson(job.renderFrames.back()->camera);
    job.response["world_frame"]=job.renderFrames.back()->frame;
}
bool Match(const std::string&,const std::string&);
RenderCapture* Capturing() noexcept {
    if(!active||active->cancelled||active->readyToFinish||active->renderFrames.empty())return nullptr;
    if(active->method!="render_state"||active->phase!=1)return nullptr;
    auto* c=active->renderFrames.back().get();
    return c;
}
struct CaptureCost {
    RenderCapture& capture;Clock::time_point started=Clock::now();
    ~CaptureCost(){const auto cost=Clock::now()-started;capture.cost+=cost;active->captureCost+=cost;}
};
bool InRenderRadius(const std::array<float,3>& p,const RenderCapture& c) {
    const double dx=double(p[0])-c.camera.target.x,dy=double(p[1])-c.camera.target.y;
    return dx*dx+dy*dy<=active->filterRadius*active->filterRadius;
}
} // namespace

void ResetCamera() noexcept {
    if(cameraInDraw){LiveCameraAccess::Restore(theCamera,savedCamera);cameraInDraw=false;}
    cameraControl=CameraView{};cameraControl.revision=++cameraSequence;
}
const CPlot3D& LogicObservationPosition(const CPlot3D& ordinary) noexcept {
    return cameraInDraw?logicObservation:ordinary;
}
int ParticleLogicVisibility(const SBoundingVolume& volume,int renderedVisibility) noexcept {
    return cameraInDraw?theRenderer.GetGPU()->CheckVisibility(volume,logicParticleProjection):renderedVisibility;
}
CameraDrawScope::CameraDrawScope() noexcept {
    if(cameraControl.mode=="normal"||cameraInDraw)return;
    if(cameraControl.mode=="unit") {
        auto* unit=theWorld.GetUnit(cameraControl.unit);
        if(!unit||unit->IsInvalid()||unit->IsUnreferenced()){ResetCamera();return;}
        cameraControl.target=unit->GetCP();
    }
    // CParticle::Segment consumes the shared game RNG, and the client may
    // interpolate units during Prepare3DScene. Keep their ordinary-camera
    // inputs while the render camera controls clipping/LOD/neighbour loads.
    {
        auto* gpu=theRenderer.GetGPU();
        const DWORD stacks[]={MAT_STACK_PROJECTION,MAT_STACK_VIEW2CLIP,MAT_STACK_PROJSPACE,MAT_STACK_SCRNSPACE,MAT_STACK_SCRNSPACE2};
        CModFalloutTelephotoFrame ordinary(theCamera,theCamera.IsFalloutModeActive(),theCamera.GetFalloutTelephotoMultiplier());
        CModFalloutProjectionScope projection(gpu,ordinary.IsActive(),theCamera.GetFalloutTelephotoMultiplier(),stacks,5);
        if(!projection.IsActive())ordinary.Reset();
        logicObservation=theCamera.GetObservationPosition();
        SHMatrix matrix;gpu->CreateCameraMatrix(matrix,&theCamera);
        gpu->PushMainMatrix(matrix);logicParticleProjection=gpu->GetMatrix(MAT_STACK_PROJSPACE);gpu->PopMainMatrix();
    }
    savedCamera=LiveCameraAccess::Apply(theCamera,cameraControl.target,cameraControl.azimuth,cameraControl.pitch,cameraControl.distance);
    cameraInDraw=applied=true;lastCameraView=cameraControl;
    lastCameraView.position=theCamera.GetCP();lastCameraView.orientation=theCamera.GetOrientation();
}
CameraDrawScope::~CameraDrawScope() noexcept {
    if(applied&&cameraInDraw){LiveCameraAccess::Restore(theCamera,savedCamera);cameraInDraw=false;}
}
void ObserveWorld(const OwnedRender::E1WorldFrame& world) noexcept try {
    if(!active||active->cancelled||active->readyToFinish)return;
    const bool cameraReply=active->method=="camera"&&active->phase==2;
    if((active->method!="render_state"||active->phase!=1)&&active->method!="screenshot"&&
       active->method!="status"&&!cameraReply)return;
    const auto started=Clock::now();auto frame=std::make_shared<RenderCapture>();
    frame->frame=world.camera.scene_frame_serial;frame->epoch=owned::bridge_current_world_epoch();
    frame->camera=CurrentCameraView();frame->world=world;
    if(active->method=="render_state"&&active->renderDetails){frame->objects.reserve(active->renderLimit);frame->particles.reserve(active->renderLimit);}
    active->renderFrames.push_back(std::move(frame));
    if(active->renderFrames.size()>16)active->renderFrames.pop_front();
    active->captureCost+=Clock::now()-started;
} catch(...) {if(active)active->budgetExceeded=true;}
void ObserveVisuals(const owned::RetainedVisualFrame& frame) noexcept try {
    auto* c=Capturing();if(!c)return;CaptureCost cost{*c};
    c->visuals=true;c->worldCount=frame.entities.size();
    if(!active->renderDetails)return;
    const auto deadline=Clock::now()+std::chrono::microseconds(200);
    for(const auto& sample:frame.entities) {
        if(Clock::now()>=deadline){c->truncated=true;break;}
        if(!InRenderRadius(sample.object_pose.position,*c))continue;
        const auto& model=sample.assembly&&sample.assembly->skeleton?sample.assembly->skeleton->visual_reference:std::string();
        if(!Match(model,active->filterName))continue;
        if(c->objects.size()>=active->renderLimit){c->truncated=true;break;}
        c->objects.push_back({sample.resource.signed_id,int(sample.status),sample.resource.serial,model,sample.object_pose,sample.values});
    }
} catch(...) {if(auto* c=Capturing())c->truncated=true;}
void ObserveParticle(const CParticle& particle,bool submitted) noexcept try {
    auto* c=Capturing();if(!c)return;CaptureCost cost{*c};
    ++c->particleCount;c->particleSources+=LiveParticleAccess::SourceCount(particle);c->particlePoints+=LiveParticleAccess::PointCount(particle);
    ++c->templates[int(particle.GetTypeID())];
    if(!active->renderDetails)return;
    if(c->cost>=FrameBudget/2){c->truncated=true;return;}
    const auto fact=LiveParticleAccess::Read(particle,submitted);
    if(!InRenderRadius(fact.position,*c)||!Match(fact.name,active->filterName))return;
    if(c->particles.size()>=active->renderLimit){c->truncated=true;return;}
    c->particles.push_back(fact);
} catch(...) {if(auto* c=Capturing())c->truncated=true;}
void ObserveVisualResult(bool accepted,unsigned long long drawn,unsigned long long omitted) noexcept {
    if(auto* c=Capturing()){c->visualResult=true;c->visualAccepted=accepted;c->drawn=drawn;c->omitted=omitted;}
}
void ObserveArena(const owned::EmbeddedArenaInput& input,HRESULT result,bool published) noexcept {
    if(auto* c=Capturing()){CaptureCost cost{*c};c->arena=input;c->arenaPresent=true;c->arenaPublished=published;c->arenaResult=result;}
}
void ObserveGolem(const owned::EmbeddedGolemInput& input,HRESULT result,bool published) noexcept {
    if(auto* c=Capturing()){CaptureCost cost{*c};c->golem=input;c->golem.ground=nullptr;c->golemPresent=true;c->golemPublished=published;c->golemResult=result;}
}
void ObserveCurse(const owned::EmbeddedCurseInput& input,HRESULT result,bool published) noexcept {
    if(auto* c=Capturing()){CaptureCost cost{*c};c->curse=input;c->curse.ground=nullptr;c->cursePresent=true;c->cursePublished=published;c->curseResult=result;}
}

namespace {
