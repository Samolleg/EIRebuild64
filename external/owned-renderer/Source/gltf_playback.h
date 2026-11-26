#pragma once
#include "gltf_animation.h"
#include "riding_idle.h"
#include <set>

namespace owned::gltf {
struct Request {
    std::string legacy;
    std::uint32_t flags=0,frames=0,event_flags=0,hit=0,sound=0;
    std::array<std::uint32_t,4> steps{};
    std::array<std::uint32_t,2> hides{};
};
inline bool tuple_matches(std::uint32_t rule,std::uint32_t query) {
    const auto weapon=query&0x7fffu;
    return (weapon?((rule&weapon)==weapon):(rule&0x7fffu)==0)&&
        (rule&~0x7fffu)==(query&~0x7fffu);
}
inline unsigned named_clip(const Animation& a,const std::string& name) {
    for(unsigned i=0;i<a.clips.size();++i)if(a.clips[i].name==name)return i;
    return UINT32_MAX;
}
inline unsigned resolve_clip(const Animation& a,const Request& request) {
    const auto find=[&](bool legacy,unsigned flags){unsigned found=UINT32_MAX;
        for(const auto& b:a.bindings){const bool match=legacy?!b.legacy.empty()&&b.legacy==request.legacy:
                b.legacy.empty()&&tuple_matches(b.flags,flags);
            if(match){animation_require(found==UINT32_MAX,"ambiguous binding priority");found=b.clip;}}
        return found;};
    unsigned clip=find(true,request.flags);if(clip!=UINT32_MAX)return clip;
    clip=find(false,request.flags);if(clip!=UINT32_MAX)return clip;
    const unsigned action=request.flags&0x3c0000u;
    if(action==0x80000u||action==0xc0000u){
        clip=find(false,(request.flags&~0x3c00000u)|0x2800000u);if(clip!=UINT32_MAX)return clip;}
    clip=find(false,request.flags&~0x7fffu);if(clip!=UINT32_MAX)return clip;
    const auto state=request.flags&0x38000u;
    animation_require(action==0x1c0000u||(state!=0x8000u&&state!=0x30000u),"lie/rest requires an explicit binding");
    animation_require(!((action==0x40000u||action==0x240000u)&&(request.flags&0x3fc00000u)),"special/cross code requires an explicit binding");
    const char* name=nullptr;
    switch(action){case 0x40000u:name="special";break;case 0x80000u:name="attack_unarmed";break;
    case 0xc0000u:name="cast";break;case 0x100000u:name="run";break;case 0x140000u:name="walk";break;
    case 0x180000u:name="idle";break;case 0x1c0000u:name="death";break;case 0x200000u:name="suffer";break;
    case 0x240000u:name="cross";break;default:break;}
    clip=name?named_clip(a,name):UINT32_MAX;
    if(clip==UINT32_MAX)throw std::runtime_error("glTF unresolved animation legacy="+request.legacy+" flags="+std::to_string(request.flags));
    return clip;
}
inline bool loop_request(const Request& r){return (r.flags&0xc0000000u)==0x80000000u;}
inline float clip_time(const Clip& clip,const Request& r,double phase){
    animation_require(r.frames&&std::isfinite(phase)&&phase>=0,"source duration/phase");
    double normalized=phase/r.frames;
    normalized=loop_request(r)?normalized-std::floor(normalized):(std::min)(normalized,1.0);
    return float(normalized*clip.duration);
}
inline void validate_event_phase(const Clip& clip,const Request& r){
    animation_require(r.frames>0,"event source duration");
    const auto match=[&](const char* name,unsigned frame){
        animation_require(frame<=r.frames,"ADB event outside action");
        bool found=false;for(const auto& e:clip.events)if(e.name==name){
            const double sourceFrame=clip.duration?double(e.time)/clip.duration*r.frames:0;
            if(std::abs(sourceFrame-frame)<=1.00001)found=true;}
        if(!found)throw std::runtime_error(std::string("glTF event phase mismatch: ")+name+" in "+clip.name+" for "+r.legacy);
    };
    const auto action=r.flags&0x3c0000u;
    if((r.event_flags&4)&&(action==0x80000u||action==0xc0000u))
        match(action==0xc0000u||(r.flags&0x60u)?"release":"hit",r.hit);
    if(r.event_flags&2)for(auto frame:r.steps){if(!frame)break;match("footstep",frame);}
    if((r.event_flags&8)&&r.sound)match("sound",r.sound);
    if(r.event_flags&1)for(auto frame:r.hides)match("attachment_visibility",frame);
    // Every authored gameplay-relevant marker must have a corresponding ADB
    // phase as well; no extra phantom hits/steps on the visual timeline.
    for(const auto& e:clip.events){
        if(e.name=="action_end"){animation_require(std::abs(e.time-clip.duration)<=1e-5f,"action_end phase");continue;}
        const double frame=clip.duration?double(e.time)/clip.duration*r.frames:0;bool matched=false;
        if(e.name=="hit"||e.name=="release")matched=(r.event_flags&4)&&(action==0x80000u||action==0xc0000u)&&
            e.name==(action==0xc0000u||(r.flags&0x60u)?"release":"hit")&&std::abs(frame-r.hit)<=1.00001;
        else if(e.name=="footstep"){if(r.event_flags&2)for(auto s:r.steps){if(!s)break;matched|=std::abs(frame-s)<=1.00001;}}
        else if(e.name=="sound")matched=(r.event_flags&8)&&r.sound&&std::abs(frame-r.sound)<=1.00001;
        else if(e.name=="attachment_visibility"){if(r.event_flags&1)for(auto h:r.hides)matched|=std::abs(frame-h)<=1.00001;}
        else throw std::runtime_error("glTF unsupported animation event "+e.name);
        animation_require(matched,"authored event has no matching ADB phase");
    }
}
struct ActionToken {
    std::uint64_t intent=0,selection=0;
    std::uint32_t entry=0;
    bool operator==(const ActionToken& b)const{return intent==b.intent&&selection==b.selection&&entry==b.entry;}
};
struct EventCursor {
    bool active=false;
    ActionToken action;
    double absolute=0;
    template<class Deliver>
    void Advance(const Clip& clip,const ActionToken& next,double at,bool loop,bool seek,Deliver&& deliver){
        animation_require(std::isfinite(at)&&at>=0&&at<9007199254740991.0,"event position");
        const bool changed=active&&!(next==action);
        const bool reset=!active||seek||(!changed&&at<absolute);
        if(!reset){const double from=changed?0:absolute;
            if(!loop)at=(std::min)(at,1.0);
            // A visual stall may span many loops. Fail explicitly instead of
            // hanging or silently replaying an unbounded event history.
            animation_require(at-from<=1024,"event catch-up exceeds 1024 cycles");
            const auto first=loop?std::uint64_t(std::floor(from)):0;
            const auto last=loop?std::uint64_t(std::floor(at)):0;
            for(auto cycle=first;cycle<=last;++cycle)for(const auto& e:clip.events){
                const double fraction=clip.duration?double(e.time)/clip.duration:0;
                const double position=cycle+fraction;
                if((position>from||(changed&&position==0))&&position<=at)deliver(cycle,e);
            }
        }
        active=true;action=next;absolute=at;
    }
};
struct PlaybackState {
    bool active=false;
    ActionToken action;
    Playback value;
    double last_clock=0,transition_start=0,transition_duration=.12;
    EventCursor events;
    RidingIdleState riding_idle;
    Playback Update(unsigned clip,float time,const ActionToken& next,double clock,bool seek,bool instant=false,float fadeSeconds=.12f){
        animation_require(std::isfinite(clock)&&std::isfinite(time),"playback clock");
        const bool reset=!active||seek||clock<last_clock;
        if(reset)value={clip,UINT32_MAX,time,0,1};
        else if(!(next==action)||clip!=value.clip){
            // Locomotion changes crossfade for 120 ms. An interrupted fade,
            // explicit seek or terminal action cuts immediately; it never
            // replays a previous action or needs an unbounded blend stack.
            const bool fade=!instant&&value.blend>=1;
            value={clip,fade?value.clip:UINT32_MAX,time,value.time,fade?0.f:1.f};transition_start=clock;
            transition_duration=(std::max)(.01,double(fadeSeconds));
        }else value.time=time;
        if(value.blend<1)value.blend=float((std::clamp)((clock-transition_start)/transition_duration,0.0,1.0));
        action=next;active=true;last_clock=clock;return value;
    }
};
} // namespace owned::gltf
