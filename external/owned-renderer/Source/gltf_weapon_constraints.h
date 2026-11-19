#pragma once
#include "gltf_animation.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <string>

namespace owned::gltf {
using Point3 = std::array<float,3>;
struct WeaponString { unsigned cord=0,limb=0; Point3 outside{},inside{},tip{}; };
struct WeaponBowConstraint {
    std::array<WeaponString,2> strings;
    std::array<std::string,4> drawing_bones,grip_bones;
};
struct WeaponProjectileAim { Point3 nock{},forward{}; };
struct WeaponGripMirror {
    std::string primary_upper,primary_forearm,primary_hand;
    std::array<std::string,4> primary_markers;
    std::array<std::string,2> width_bones;
    Point3 bind_normal{};
    std::vector<std::array<std::string,2>> pairs; // support, primary
};
struct WeaponTwoHandSupport {
    Point3 grip{},axis{};
    std::string upper,forearm,hand;
    std::array<std::string,4> markers;
    std::vector<std::string> clip_prefixes;
    std::map<std::string,std::array<float,4>> clip_windows;
    std::unique_ptr<WeaponGripMirror> mirror;
    float full_correction=.1f,max_correction=.22f;
};
struct WeaponArmSolution {
    Point3 shoulder{},elbow{},new_elbow{},new_wrist{};
    std::array<float,9> upper_rotation{},forearm_rotation{};
};

inline float weapon_support_weight(const WeaponTwoHandSupport& support,const std::string& action,
                                   float time,float duration) {
    const bool matched=std::any_of(support.clip_prefixes.begin(),support.clip_prefixes.end(),
        [&](const std::string& prefix){return action.compare(0,prefix.size(),prefix)==0;});
    if(!matched)return 0;
    if(support.clip_windows.empty())return 1;
    const auto it=support.clip_windows.find(action);
    if(it==support.clip_windows.end() || !std::isfinite(time) || !std::isfinite(duration) || duration<=0)return 0;
    const auto& w=it->second;const float phase=time/duration;
    return (std::clamp)((std::min)((phase-w[0])/(w[1]-w[0]),(w[3]-phase)/(w[3]-w[2])),0.f,1.f);
}

inline Point3 weapon_point(const Affine& a,const Point3& p) {
    Point3 result{};
    for(unsigned r=0;r<3;++r) {
        result[r]=a[r*4+3];
        for(unsigned c=0;c<3;++c)result[r]+=a[r*4+c]*p[c];
    }
    return result;
}
inline Point3 weapon_direction(const Affine& a,const Point3& p) {
    Point3 result{};
    for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)result[r]+=a[r*4+c]*p[c];
    return result;
}
inline Point3 weapon_subtract(const Point3& a,const Point3& b) {
    return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};
}
inline float weapon_length(const Point3& p) { return std::hypot(p[0],p[1],p[2]); }

// Proper shortest-arc rotation, including the antiparallel case. Returns false
// for degenerate or nonfinite inputs, leaving the caller's original pose intact.
inline bool weapon_rotate(const Point3& from,const Point3& to,std::array<float,9>& result) {
    const auto a=weapon_length(from),b=weapon_length(to);
    if(!std::isfinite(a)||!std::isfinite(b)||a<1e-6f||b<1e-6f)return false;
    Point3 x{},y{};for(unsigned i=0;i<3;++i){x[i]=from[i]/a;y[i]=to[i]/b;}
    const float cosine=(std::clamp)(x[0]*y[0]+x[1]*y[1]+x[2]*y[2],-1.f,1.f);
    if(cosine<-.9999f) {
        unsigned least=0;for(unsigned i=1;i<3;++i)if(std::abs(x[i])<std::abs(x[least]))least=i;
        Point3 axis{};axis[least]=1;
        const float projection=x[least];for(unsigned i=0;i<3;++i)axis[i]-=projection*x[i];
        const float length=weapon_length(axis);if(length<1e-6f)return false;
        for(auto& v:axis)v/=length;
        for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)
            result[r*3+c]=2*axis[r]*axis[c]-(r==c?1.f:0.f);
        return true;
    }
    const Point3 cross{x[1]*y[2]-x[2]*y[1],x[2]*y[0]-x[0]*y[2],x[0]*y[1]-x[1]*y[0]};
    const std::array<float,9> skew{0,-cross[2],cross[1],cross[2],0,-cross[0],-cross[1],cross[0],0};
    for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c) {
        float square=0;for(unsigned k=0;k<3;++k)square+=skew[r*3+k]*skew[k*3+c];
        result[r*3+c]=(r==c?1.f:0.f)+skew[r*3+c]+square/(1+cosine);
    }
    return true;
}

inline bool weapon_segment(Affine& result,const Affine& initial,const Point3& outside,
                           const Point3& inside,const Point3& tip,const Point3& nock) {
    const auto old_start=weapon_point(initial,outside),old_end=weapon_point(initial,inside);
    const auto from=weapon_subtract(old_end,old_start),to=weapon_subtract(nock,tip);
    const float a=weapon_length(from),b=weapon_length(to);
    std::array<float,9> rotate{};
    if(!weapon_rotate(from,to,rotate) || a<1e-6f || !std::isfinite(b/a) || b/a>8.f)return false;
    result=initial;
    for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c) {
        float value=0;for(unsigned k=0;k<3;++k)value+=rotate[r*3+k]*initial[k*4+c];
        result[r*4+c]=value*b/a;
    }
    for(unsigned r=0;r<3;++r) {
        result[r*4+3]=tip[r];
        for(unsigned c=0;c<3;++c)result[r*4+3]-=result[r*4+c]*outside[c];
    }
    return true;
}

inline bool weapon_aim(Affine& result,const Affine& initial,const Point3& nock,
                       const Point3& forward,const Point3& hand,const Point3& grip) {
    std::array<float,9> rotate{};
    if(!weapon_rotate(weapon_direction(initial,forward),weapon_subtract(grip,hand),rotate))return false;
    result=initial;
    for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c) {
        result[r*4+c]=0;for(unsigned k=0;k<3;++k)result[r*4+c]+=rotate[r*3+k]*initial[k*4+c];
    }
    for(unsigned r=0;r<3;++r) {
        result[r*4+3]=hand[r];
        for(unsigned c=0;c<3;++c)result[r*4+3]-=result[r*4+c]*nock[c];
    }
    return true;
}

inline bool weapon_bow(std::array<Affine,16>& matrices,const WeaponBowConstraint& bow,
                       const Point3& drawing,float weight) {
    if(!std::isfinite(weight) || weight<0 || weight>1)return false;
    if(weight==0)return true;
    Point3 nock{};
    for(const auto& string:bow.strings) {
        if(string.cord>=matrices.size() || string.limb>=matrices.size())return false;
        const auto p=weapon_point(matrices[string.cord],string.inside);
        for(unsigned r=0;r<3;++r)nock[r]+=.5f*p[r];
    }
    for(unsigned r=0;r<3;++r)nock[r]+=weight*(drawing[r]-nock[r]);
    std::array<Affine,2> corrected{};
    for(unsigned i=0;i<2;++i) {
        const auto& string=bow.strings[i];
        const auto tip=weapon_point(matrices[string.limb],string.tip);
        if(!weapon_segment(corrected[i],matrices[string.cord],string.outside,string.inside,tip,nock))return false;
    }
    for(unsigned i=0;i<2;++i)matrices[bow.strings[i].cord]=corrected[i];
    return true;
}

inline bool weapon_projectile(Affine& result,const Affine& socket,const WeaponProjectileAim& aim,
                              const Point3& drawing,const Point3& grip,float weight) {
    if(!std::isfinite(weight) || weight<0 || weight>1)return false;
    result=socket;
    if(weight==0)return true;
    const auto from=weapon_direction(socket,aim.forward),to=weapon_subtract(grip,drawing);
    const float a=weapon_length(from),b=weapon_length(to);
    if(!std::isfinite(a)||!std::isfinite(b)||a<1e-6f||b<1e-6f)return false;
    Point3 target=drawing;
    for(unsigned c=0;c<3;++c)target[c]+=(1-weight)*from[c]/a+weight*to[c]/b;
    return weapon_aim(result,socket,aim.nock,aim.forward,drawing,target);
}

inline bool weapon_arm_target(WeaponArmSolution& result,const Point3& shoulder,const Point3& elbow,
                              const Point3& wrist,const Point3& target) {
    const auto upper=weapon_subtract(elbow,shoulder),lower=weapon_subtract(wrist,elbow);
    const float first=weapon_length(upper),second=weapon_length(lower);
    auto direction=weapon_subtract(target,shoulder);const float requested=weapon_length(direction);
    if(!std::isfinite(first)||!std::isfinite(second)||!std::isfinite(requested)||
       first<1e-5f||second<1e-5f||requested<1e-5f)return false;
    for(auto& v:direction)v/=requested;
    const float reach=(std::clamp)(requested,std::abs(first-second)+1e-5f,first+second-1e-5f);
    const float along_elbow=(first*first-second*second+reach*reach)/(2*reach);
    const float height=std::sqrt((std::max)(0.f,first*first-along_elbow*along_elbow));
    const float projection=upper[0]*direction[0]+upper[1]*direction[1]+upper[2]*direction[2];
    Point3 bend{};for(unsigned i=0;i<3;++i)bend[i]=upper[i]-projection*direction[i];
    float bend_length=weapon_length(bend);
    if(bend_length<1e-5f) {
        unsigned least=0;for(unsigned i=1;i<3;++i)if(std::abs(direction[i])<std::abs(direction[least]))least=i;
        bend={};bend[least]=1;
        for(unsigned i=0;i<3;++i)bend[i]-=direction[least]*direction[i];
        bend_length=weapon_length(bend);
    }
    if(!std::isfinite(bend_length)||bend_length<1e-6f)return false;
    for(auto& v:bend)v/=bend_length;
    result.shoulder=shoulder;result.elbow=elbow;
    for(unsigned i=0;i<3;++i) {
        result.new_elbow[i]=shoulder[i]+direction[i]*along_elbow+bend[i]*height;
        result.new_wrist[i]=shoulder[i]+direction[i]*reach;
    }
    return weapon_rotate(upper,weapon_subtract(result.new_elbow,shoulder),result.upper_rotation) &&
           weapon_rotate(lower,weapon_subtract(result.new_wrist,result.new_elbow),result.forearm_rotation);
}

inline bool weapon_support_ik(WeaponArmSolution& result,const Affine& weapon,const WeaponTwoHandSupport& support,
                              const Point3& shoulder,const Point3& elbow,const Point3& wrist,
                              const Point3& palm,float transition) {
    if(!std::isfinite(transition)||transition<=0||transition>1)return false;
    const auto grip=weapon_point(weapon,support.grip);
    auto axis=weapon_direction(weapon,support.axis);
    const float axis_length=weapon_length(axis);
    if(!std::isfinite(axis_length)||axis_length<1e-6f)return false;
    for(auto& v:axis)v/=axis_length;
    const auto relative=weapon_subtract(palm,grip);
    const float along=relative[0]*axis[0]+relative[1]*axis[1]+relative[2]*axis[2];
    Point3 delta{};for(unsigned i=0;i<3;++i)delta[i]=grip[i]+along*axis[i]-palm[i];
    const float distance=weapon_length(delta);
    if(!std::isfinite(distance)||distance>=support.max_correction||distance<1e-6f)return false;
    const float fade=distance<=support.full_correction?1.f:
        (support.max_correction-distance)/(support.max_correction-support.full_correction);
    Point3 target{};for(unsigned i=0;i<3;++i)target[i]=wrist[i]+delta[i]*fade*transition;
    return weapon_arm_target(result,shoulder,elbow,wrist,target);
}

inline Affine weapon_rotate_around(const Affine& original,const std::array<float,9>& rotation,
                                   const Point3& from,const Point3& to) {
    auto result=original;
    for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c) {
        result[r*4+c]=0;for(unsigned k=0;k<3;++k)result[r*4+c]+=rotation[r*3+k]*original[k*4+c];
    }
    for(unsigned r=0;r<3;++r) {
        result[r*4+3]=to[r];
        for(unsigned k=0;k<3;++k)result[r*4+3]+=rotation[r*3+k]*(original[k*4+3]-from[k]);
    }
    return result;
}

// The arrow appears while being taken from the quiver. Give it a few source
// frames to reach the bow, then join the string until shortly after release.
inline float weapon_draw_weight(float phase,unsigned frames,unsigned show,unsigned hide) {
    if(!std::isfinite(phase)||!frames||show>frames||hide>frames)return 0;
    constexpr float release=3.f;
    phase=(std::clamp)(phase,0.f,float(frames));
    if(show==hide && phase<show)return 0;
    const float length=show==hide?float(frames-show):
        (show<hide?float(hide-show):float(frames-show+hide));
    if(length<=0)return 0;
    float since=phase-float(show);
    if(since<0 && show>hide)since+=frames;
    if(since<0)return 0;
    const float lead=(std::min)(3.f,length/3.f);
    const float ramp=(std::min)(2.f,length-lead);
    if(since<length)return (std::clamp)((since-lead)/ramp,0.f,1.f);
    if(show==hide)return 1;
    return (std::clamp)((length+release-since)/release,0.f,1.f);
}
} // namespace owned::gltf
