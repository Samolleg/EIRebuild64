#pragma once
#include "PhysicsFxCore.h"
#include <algorithm>
#include <cmath>
#include <deque>

namespace PhysicsFx {
struct DebrisPlayback {
    std::shared_ptr<const StoneTracks> visible;
    double position=0, lastClock=-1;
    float fraction=0;
};

// One track set covers [end-1,end] for every stone surviving that step.
// Only interpolate within it: across a birth or retirement indices can change.
class DebrisHistory {
    struct Frame {std::uint64_t end; std::shared_ptr<const StoneTracks> tracks;};
    std::deque<Frame> frames;
public:
    void publish(std::uint64_t end,std::shared_ptr<const StoneTracks> tracks) {
        if(!frames.empty() && end!=frames.back().end+1)frames.clear();
        frames.push_back({end,std::move(tracks)});
        if(frames.size()>8)frames.pop_front();
    }
    bool hasStones() const {return !frames.empty() && !frames.back().tracks->empty();}
    void sample(double clock,DebrisPlayback& cursor) const {
        if(frames.empty() || clock==cursor.lastClock)return;
        const double first=double(frames.front().end)-1, last=double(frames.back().end);
        if(cursor.lastClock<0 || cursor.position<first || clock<cursor.lastClock)
            cursor.position=(std::max)(first,last-2.);
        else {
            // Keep a short interpolation buffer. Consume a late publication
            // across render frames and catch up gradually when the worker recovers.
            const double speed=std::clamp((last-cursor.position)/2.,.5,1.25);
            cursor.position=(std::min)(last,cursor.position+(clock-cursor.lastClock)*speed);
        }
        cursor.lastClock=clock;
        const auto index=(std::min)(size_t(cursor.position-first),frames.size()-1);
        const auto& frame=frames[index];
        cursor.visible=frame.tracks;
        cursor.fraction=float(std::clamp(cursor.position-(double(frame.end)-1),0.,1.));
    }
};
}
