#pragma once
#include "WorldBackgroundStreaming.h"
#include <cmath>

#pragma pack(push,8)
namespace WorldBackground {
// Worker-only prediction. Never changes authoritative camera coordinates.
class CameraPrefetch {
    Point last;double stamp=0;uint64_t epoch=0;bool valid=false;
public:
    void Update(SectorView& view,uint64_t nextEpoch,double seconds){
        view.anticipate=false;view.anticipated=view.camera;
        if(valid&&epoch==nextEpoch){
            const double dt=seconds-stamp,dx=view.camera.x-last.x,dy=view.camera.y-last.y;
            const double distance=std::hypot(dx,dy);
            // No prediction through teleports, pauses or stale samples.
            if(dt>=.02&&dt<=1&&distance>.1&&distance<=64){
                const double lead=(std::min)(32.0,distance*.5/dt);
                view.anticipated={view.camera.x+dx*lead/distance,view.camera.y+dy*lead/distance};view.anticipate=true;
            }
        }
        last=view.camera;stamp=seconds;epoch=nextEpoch;valid=true;
    }
};
}
#pragma pack(pop)
