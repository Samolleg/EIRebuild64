#pragma once

// Far world-lamp memory (bridge side, before publication).
//
// The game activates world lamps within ~144 units of the camera and drops
// them from its light list beyond ~149 (measured list membership, zone18).
// Lamps it drops far away are kept lit at their last values without shadows
// (the shadow scheduler fades their cubes out); lamps entering far away fade
// in; lamps removed near the camera (spell lights ending) fade out.
//
// Only a lamp that never moved may be remembered at its last position. A
// lamp whose centre changes, or a quest light (it follows its unit), is
// mobile: its frame-to-frame successor takes over its entry, and once the
// game stops listing it it fades out at any distance. Entries are keyed by
// content (id + half-unit centre), so without this a light moving far from
// the camera left every old position behind as a remembered lamp until the
// camera came within the cull distance. A lightning strike light is
// transient too: it is never remembered, or a strike landing beyond the cull
// distance would stay lit after the game removed it.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <tuple>
#include <vector>

#include "sky_clouds.h"

namespace owned {

constexpr double kWorldLampCullDistance=130.0; // below the game's ~142-149 activation band
constexpr double kWorldLampFadeSeconds=.8;
constexpr double kWorldLampShadowStart=85.0,kWorldLampShadowEnd=135.0; // camera distance

template<class Light> struct WorldLampMemory {
    struct Entry { Light light; float fade=0,shadow_fade=1; bool present=false,was_present=false,mobile=false; };
    std::map<std::tuple<std::int32_t,int,int,int>,Entry> lamps;
    std::uint64_t world_epoch=0; double clock=-1;
    std::array<float,3> camera{}; bool camera_valid=false;
};

// Same id and casts flag, centre within a quarter of the range: the same lamp
// one frame later (the point-shadow scheduler's move rule).
template<class Light> bool world_lamp_successor(const Light& a,const Light& b) noexcept {
    if(a.source_id!=b.source_id||a.casts_shadow!=b.casts_shadow)return false;
    double d=0;for(unsigned i=0;i<3;++i){const double t=double(a.center[i])-b.center[i];d+=t*t;}
    const double limit=.25*std::max(a.radius,b.radius);
    return d<=limit*limit;
}

// lights: this frame's game light list in, the lights to publish out.
// step: elapsed game seconds since the previous call.
template<class Light> void retain_world_lamps(WorldLampMemory<Light>& memory,std::vector<Light>& lights,double step) {
    const auto distance=[&](const Light& light){
        if(!memory.camera_valid)return 0.0;
        double sum=0;for(unsigned axis=0;axis<3;++axis){const double d=light.center[axis]-memory.camera[axis];sum+=d*d;}
        return std::sqrt(sum);
    };
    const auto key=[](const Light& light){
        return std::make_tuple(light.source_id,int(std::lround(light.center[0]*2)),
            int(std::lround(light.center[1]*2)),int(std::lround(light.center[2]*2)));
    };
    for(auto& [id,entry]:memory.lamps){entry.was_present=entry.present;entry.present=false;}
    std::vector<typename decltype(memory.lamps)::iterator> arrived;
    for(const auto& light:lights){
        auto [found,inserted]=memory.lamps.try_emplace(key(light));
        auto& entry=found->second;
        if(inserted){entry.fade=distance(light)>=kWorldLampCullDistance?0.f:1.f;arrived.push_back(found);}
        else if(entry.was_present&&entry.light.center!=light.center)entry.mobile=true;
        if(light.attached||sky_strike_light(light))entry.mobile=true;
        entry.light=light;entry.present=true;
    }
    // A new entry continuing a lamp listed last frame and missing now is that
    // lamp moved: it inherits the fade state and the old position is dropped.
    for(auto found:arrived){
        auto& entry=found->second;
        for(auto old=memory.lamps.begin();old!=memory.lamps.end();++old){
            if(old->second.present||!old->second.was_present||
               !world_lamp_successor(old->second.light,entry.light))continue;
            entry.fade=old->second.fade;entry.shadow_fade=old->second.shadow_fade;entry.mobile=true;
            memory.lamps.erase(old);
            break;
        }
    }
    lights.clear();
    const float delta=static_cast<float>(step/kWorldLampFadeSeconds);
    for(auto it=memory.lamps.begin();it!=memory.lamps.end();){
        auto& entry=it->second;
        const bool keep=entry.present||(!entry.mobile&&distance(entry.light)>=kWorldLampCullDistance);
        entry.fade=keep?std::min(1.f,entry.fade+delta):std::max(0.f,entry.fade-delta);
        if(!keep&&entry.fade<=0){it=memory.lamps.erase(it);continue;}
        auto value=entry.light;
        for(auto& channel:value.color)channel*=entry.fade;
        // Shadows fade with camera distance, fully gone by the game's cull
        // band, so moving away never cuts them; a lamp the game dropped
        // also fades on time in case it left the list while still close.
        const float by_distance=static_cast<float>(std::clamp(
            (kWorldLampShadowEnd-distance(entry.light))/(kWorldLampShadowEnd-kWorldLampShadowStart),0.0,1.0));
        entry.shadow_fade=entry.present?by_distance:std::min(by_distance,std::max(0.f,entry.shadow_fade-delta));
        value.shadow_fade=entry.shadow_fade;
        if(entry.shadow_fade<=0)value.casts_shadow=false;
        lights.push_back(value);
        ++it;
    }
}

}  // namespace owned
