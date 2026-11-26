#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace owned::gltf {
struct RidingIdleChoice { unsigned variant=0; float time=0; };
struct RidingIdleState {
    bool active=false, rider=false;
    double started=0;
    unsigned episodes=0, first=0;
    RidingIdleChoice Update(bool eligible,double now,bool isRider) {
        if(!eligible || !std::isfinite(now)) { active=false; return {}; }
        if(!active || now<started || rider!=isRider) {
            active=true; rider=isRider; started=now; first=episodes++;
        }
        // The horse's source clips contain very rapid neck and ear keys.
        // Give the filtered motion time to finish and leave a quiet pause.
        constexpr double horseRate=.30;
        const double period=isRider?9.0:20.0, delay=isRider?2.0:3.0;
        const double length=isRider?4.0:(10.0/3.0)/horseRate;
        const double elapsed=(std::max)(0.0,now-started);
        const auto cycle=static_cast<std::uint64_t>(std::floor(elapsed/period));
        const double phase=elapsed-double(cycle)*period;
        if(phase<delay || phase>=delay+length)return {};
        return {1u+unsigned((first+cycle)%(isRider?3u:2u)),
            float((phase-delay)*(isRider?1.0:horseRate))};
    }
};
}
