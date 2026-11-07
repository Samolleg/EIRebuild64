// Included after Job and the observer helpers. This lease has no serialization
// hooks: it replaces only the values read inside CWorldClient::Draw.
struct CameraView {
    CPlot3D target, position;
    CQuaternion orientation;
    std::string mode="normal";
    int unit=0;
    float azimuth=0,pitch=45,distance=30;
    std::uint64_t revision=0;
};
CameraView cameraControl,lastCameraView;
std::uint64_t cameraSequence=0;
bool cameraInDraw=false;
LiveCameraAccess::Saved savedCamera;
CPlot3D logicObservation;
SHMatrix logicParticleProjection;

json CameraJson(const CameraView& view) {
    float w,x,y,z;view.orientation.GetComponents(w,x,y,z);
    return {{"mode",view.mode},{"target",json::array({view.target.x,view.target.y,view.target.z})},
        {"position",json::array({view.position.x,view.position.y,view.position.z})},
        {"orientation_wxyz",json::array({w,x,y,z})},{"azimuth",view.azimuth},
        {"pitch",view.pitch},{"distance",view.distance},{"unit_id",view.mode=="unit"?json(view.unit):json(nullptr)},
        {"revision",view.revision},{"angles","degrees"}};
}
CameraView CurrentCameraView() {
    if(cameraInDraw)return lastCameraView;
    if(cameraControl.mode!="normal"&&lastCameraView.revision==cameraControl.revision)return lastCameraView;
    CameraView view;view.position=theCamera.GetCP();view.target=theCamera.GetAnchorPoint();
    view.orientation=theCamera.GetOrientation();view.distance=theCamera.GetDistance();
    view.pitch=theCamera.GetPitch()*180.f/FP_PI;view.revision=cameraControl.revision;
    view.azimuth=theCamera.GetRotateAngle()*180.f/FP_PI;
    return view;
}
double Number(const json& params,const char* key,double minimum,double maximum) {
    const auto& value=params.at(key);
    if(!value.is_number())throw std::runtime_error(std::string(key)+" must be a finite number");
    const double n=value.get<double>();
    if(!std::isfinite(n)||n<minimum||n>maximum)
        throw std::runtime_error(std::string(key)+" is outside the allowed range");
    return n;
}
bool CameraCommand(Job& job) {
    const auto& params=job.request.at("params");
    if(!job.phase) {
        const auto mode=params.at("mode").get<std::string>();
        if(mode=="release") {
            ResetCamera();job.phase=2;job.lodExpires=Clock::now()+std::chrono::milliseconds(1000);
            return false;
        }
        if(mode!="point"&&mode!="unit")throw std::runtime_error("camera mode must be point, unit or release");
        UpdateOperationReadiness();
        {std::lock_guard<std::mutex> lock(operationMutex);
         if(operation.inFlight)throw std::runtime_error("another game operation is in progress");}
        if(!theWorldServer.bMapLoaded||!theWorld.GetTerrain())throw std::runtime_error("no loaded game world");
        CameraView next;next.mode=mode;
        next.azimuth=float(Number(params,"azimuth",-360,360));
        next.pitch=float(Number(params,"pitch",1,89));next.distance=float(Number(params,"distance",1,2000));
        if(mode=="point") {
            next.target.x=float(Number(params,"x",-2000,theWorld.GetTerrain()->GetMaxX()+2000));
            next.target.y=float(Number(params,"y",-2000,theWorld.GetTerrain()->GetMaxY()+2000));
            if(params.contains("z"))next.target.z=float(Number(params,"z",-2000,20000));
            else {
                if(next.target.x<0||next.target.y<0||next.target.x>=theWorld.GetTerrain()->GetMaxX()||next.target.y>=theWorld.GetTerrain()->GetMaxY())
                    throw std::runtime_error("z is required outside the active terrain; neighbour height is not a host input");
                next.target.z=theWorld.GetZValue(next.target.x,next.target.y);
            }
        } else {
            const auto& id=params.at("id");
            if(!id.is_number_integer()||id<1||id>INT_MAX)throw std::runtime_error("id must be a positive game unit ID");
            next.unit=id.get<int>();auto* unit=theWorld.GetUnit(next.unit);
            if(!unit||unit->IsInvalid()||unit->IsUnreferenced())throw std::runtime_error("client unit not found");
            next.target=unit->GetCP();
        }
        next.revision=++cameraSequence;cameraControl=next;
        job.worldId=theWorldServer.GetRTID();job.lodExpires=Clock::now()+std::chrono::milliseconds(3500);
        job.phase=1;return false;
    }
    if(job.phase==2){
        if(job.renderFrames.empty()&&theWorldServer.bMapLoaded&&Clock::now()<job.lodExpires)return false;
        job.response={{"ok",true},{"camera_mode","normal"},{"camera",nullptr},{"truncated",false}};
        if(!job.renderFrames.empty()){
            job.response["camera"]=CameraJson(job.renderFrames.back()->camera);job.response["world_frame"]=job.renderFrames.back()->frame;
        }else job.response["camera_reason"]="no_world_draw";
        return true;
    }
    if(!theWorldServer.bMapLoaded||job.worldId!=theWorldServer.GetRTID()||cameraControl.mode=="normal") {
        job.response=Error("camera lease was reset before a world draw");return true;
    }
    if(lastCameraView.revision!=cameraControl.revision) {
        if(Clock::now()<job.lodExpires)return false;
        job.response=Error("no world draw applied the camera before timeout");ResetCamera();return true;
    }
    job.response={{"ok",true},{"camera",CameraJson(lastCameraView)},
        {"world_frame",owned::bridge_scene_world_to_clip_snapshot().scene_frame_serial},{"truncated",false}};
    return true;
}
