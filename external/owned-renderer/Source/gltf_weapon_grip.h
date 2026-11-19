#pragma once
#include "gltf_asset.h"

namespace owned::gltf {
inline float grip_dot(const Point3& a,const Point3& b) {return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
inline Point3 grip_cross(const Point3& a,const Point3& b) {
    return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
}
inline Point3 grip_origin(const Affine& a) {return {a[3],a[7],a[11]};}
inline bool grip_normalize(Point3& a) {
    const float n=weapon_length(a);if(!std::isfinite(n)||n<1e-6f)return false;
    for(auto& v:a)v/=n;return true;
}

// Interpolate the rotation along its shortest quaternion arc. Any remaining
// affine component is interpolated separately, preserving both exact endpoints.
inline bool grip_blend(Affine& result,const Affine& from,const Affine& to,float weight) {
    if(weight<=0){result=from;return true;}
    if(weight>=1){result=to;return true;}
    const float a=from[0],b=from[1],c=from[2],d=from[4],e=from[5],f=from[6],
                g=from[8],h=from[9],i=from[10];
    const float det=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);
    if(!std::isfinite(det)||std::abs(det)<1e-12f)return false;
    const std::array<float,9> inv{(e*i-f*h)/det,(c*h-b*i)/det,(b*f-c*e)/det,
        (f*g-d*i)/det,(a*i-c*g)/det,(c*d-a*f)/det,(d*h-e*g)/det,
        (b*g-a*h)/det,(a*e-b*d)/det};
    float m[9]{};
    for(unsigned r=0;r<3;++r)for(unsigned col=0;col<3;++col)
        for(unsigned k=0;k<3;++k)m[r*3+col]+=to[r*4+k]*inv[k*3+col];
    Point3 x{m[0],m[3],m[6]},y{m[1],m[4],m[7]},z0{m[2],m[5],m[8]};
    if(!grip_normalize(x))return false;
    const float projection=grip_dot(x,y);for(unsigned k=0;k<3;++k)y[k]-=x[k]*projection;
    if(!grip_normalize(y))return false;
    auto z=grip_cross(x,y);if(grip_dot(z,z0)<0){for(auto& v:y)v=-v;for(auto& v:z)v=-v;}
    const float r00=x[0],r01=y[0],r02=z[0],r10=x[1],r11=y[1],r12=z[1],
                r20=x[2],r21=y[2],r22=z[2];
    Float4 q{};const float trace=r00+r11+r22;
    if(trace>0) {
        const float s=std::sqrt(trace+1.f)*2;q={(r21-r12)/s,(r02-r20)/s,(r10-r01)/s,.25f*s};
    } else if(r00>r11 && r00>r22) {
        const float s=std::sqrt(1.f+r00-r11-r22)*2;q={.25f*s,(r01+r10)/s,(r02+r20)/s,(r21-r12)/s};
    } else if(r11>r22) {
        const float s=std::sqrt(1.f+r11-r00-r22)*2;q={(r01+r10)/s,.25f*s,(r12+r21)/s,(r02-r20)/s};
    } else {
        const float s=std::sqrt(1.f+r22-r00-r11)*2;q={(r02+r20)/s,(r12+r21)/s,.25f*s,(r10-r01)/s};
    }
    Node partial{},full{};partial.rotation=slerp({0,0,0,1},q,weight);full.rotation=normalized(q);
    const auto rp=trs(partial),rf=trs(full);result=from;
    for(unsigned row=0;row<3;++row) {
        for(unsigned col=0;col<3;++col) {
            float p=0,v=0;
            for(unsigned k=0;k<3;++k){p+=rp[row*4+k]*from[k*4+col];v+=rf[row*4+k]*from[k*4+col];}
            result[row*4+col]=p+weight*(to[row*4+col]-v);
        }
        result[row*4+3]=from[row*4+3]+weight*(to[row*4+3]-from[row*4+3]);
    }
    return true;
}

struct GripFrame {
    Affine reflection{};
    Point3 axis{},grip{},base_wrist{},shoulder{};
    float separation=0,along=0,perpendicular=0,max_reach=0;
};

inline bool grip_frame(GripFrame& f,const WeaponArmPlan& plan,const WeaponTwoHandSupport& support,
                       const std::array<Affine,256>& arm,const Affine& weapon) {
    f.grip=weapon_point(weapon,support.grip);f.axis=weapon_direction(weapon,support.axis);
    if(!grip_normalize(f.axis))return false;
    const auto radial=[&](Point3 p){p=weapon_subtract(p,f.grip);const float d=grip_dot(p,f.axis);
        for(unsigned k=0;k<3;++k)p[k]-=d*f.axis[k];return p;};
    auto primary=radial(grip_origin(arm[plan.primary_hand]));
    auto toward=radial(grip_origin(arm[plan.hand]));
    f.shoulder=grip_origin(arm[plan.upper]);
    if(!grip_normalize(primary))return false;
    if(!grip_normalize(toward)){toward=radial(f.shoulder);if(!grip_normalize(toward))return false;}
    auto normal=weapon_subtract(primary,toward);
    if(!grip_normalize(normal)){normal=grip_cross(f.axis,primary);if(!grip_normalize(normal))return false;}
    f.reflection={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    for(unsigned r=0;r<3;++r) {
        for(unsigned c=0;c<3;++c)f.reflection[r*4+c]-=2*normal[r]*normal[c];
        f.reflection[r*4+3]=2*normal[r]*grip_dot(normal,f.grip);
    }
    const auto& hand=plan.nodes[plan.hand];
    if(hand.mirror<0 || unsigned(hand.mirror)!=plan.primary_hand)return false;
    f.base_wrist=grip_origin(multiply(multiply(f.reflection,arm[plan.primary_hand]),hand.mirror_bind));
    Point3 left{},right{};
    for(unsigned j=0;j<4;++j)for(unsigned k=0;k<3;++k) {
        left[k]+=.25f*arm[plan.markers[j]][k*4+3];
        right[k]+=.25f*arm[plan.primary_markers[j]][k*4+3];
    }
    const float wanted=grip_dot(weapon_subtract(left,right),f.axis);
    const float width=weapon_length(weapon_subtract(grip_origin(arm[plan.width_bones[0]]),
                                                     grip_origin(arm[plan.width_bones[1]])));
    if(!std::isfinite(width)||width<1e-6f)return false;
    f.separation=std::copysign((std::max)(std::abs(wanted),1.5f*width),wanted);
    const auto elbow=grip_origin(arm[plan.forearm]),wrist=grip_origin(arm[plan.hand]);
    f.max_reach=weapon_length(weapon_subtract(elbow,f.shoulder))+weapon_length(weapon_subtract(wrist,elbow))-1e-5f;
    auto direction=weapon_subtract(f.shoulder,f.base_wrist);f.along=grip_dot(direction,f.axis);
    for(unsigned k=0;k<3;++k)direction[k]-=f.along*f.axis[k];
    f.perpendicular=weapon_length(direction);
    return std::isfinite(f.perpendicular)&&std::isfinite(f.max_reach)&&f.max_reach>1e-5f;
}

// Applies only to profiles with an explicit matched-hand contract. Keep the
// primary hand rigidly attached to the weapon; move its arm only where the
// supporting hand cannot otherwise reach the shaft.
inline bool weapon_mirrored_grip(std::array<Affine,256>& result,bool& primary_changed,
                                 const WeaponArmPlan& plan,const WeaponTwoHandSupport& support,
                                 const Affine& initial_weapon,float transition) {
    if(!support.mirror || plan.primary_begin>=plan.nodes.size() ||
       !std::isfinite(transition)||transition<=0||transition>1)return false;
    auto arm=result;auto weapon=initial_weapon;primary_changed=false;GripFrame frame{};
    for(unsigned iteration=0;iteration<3;++iteration) {
        if(!grip_frame(frame,plan,support,arm,weapon))return false;
        if(frame.perpendicular<=frame.max_reach-.015f)break;
        auto correction=weapon_subtract(frame.shoulder,frame.base_wrist);
        for(unsigned k=0;k<3;++k)correction[k]-=frame.along*frame.axis[k];
        const float factor=(frame.perpendicular-frame.max_reach+.015f)/frame.perpendicular*transition;
        for(auto& v:correction)v*=factor;
        if(weapon_length(correction)>support.max_correction)return false;
        const auto shoulder=grip_origin(arm[plan.primary_upper]),elbow=grip_origin(arm[plan.primary_forearm]),
                   wrist=grip_origin(arm[plan.primary_hand]);
        Point3 target{};for(unsigned k=0;k<3;++k)target[k]=wrist[k]+correction[k];
        WeaponArmSolution primary;
        if(!weapon_arm_target(primary,shoulder,elbow,wrist,target))return false;
        const auto shift=weapon_subtract(primary.new_wrist,wrist);
        if(weapon_length(shift)<1e-6f)break;
        for(unsigned j=plan.primary_begin;j<plan.nodes.size();++j) {
            const auto& node=plan.nodes[j];
            if(node.hand)for(unsigned k=0;k<3;++k)arm[j][k*4+3]+=shift[k];
            else arm[j]=weapon_rotate_around(arm[j],node.forearm?primary.forearm_rotation:primary.upper_rotation,
                node.forearm?primary.elbow:primary.shoulder,node.forearm?primary.new_elbow:primary.shoulder);
        }
        for(unsigned k=0;k<3;++k)weapon[k*4+3]+=shift[k];
        primary_changed=true;
    }
    if(!grip_frame(frame,plan,support,arm,weapon))return false;
    // At a partial transition a small residual is expected; at full weight an
    // unreachable target must never stretch the supporting arm.
    if(transition>=.999f && frame.perpendicular>frame.max_reach+.005f)return false;
    if(frame.perpendicular<frame.max_reach) {
        const float a=std::sqrt((std::max)(0.f,frame.max_reach*frame.max_reach-frame.perpendicular*frame.perpendicular));
        frame.separation=(std::clamp)(frame.separation,frame.along-a,frame.along+a);
    }
    for(unsigned k=0;k<3;++k)frame.reflection[k*4+3]+=frame.separation*frame.axis[k];
    const auto target_hand=multiply(multiply(frame.reflection,arm[plan.primary_hand]),plan.nodes[plan.hand].mirror_bind);
    const auto wrist=grip_origin(arm[plan.hand]),elbow=grip_origin(arm[plan.forearm]),shoulder=grip_origin(arm[plan.upper]);
    const auto desired=grip_origin(target_hand),delta=weapon_subtract(desired,wrist);
    const float distance=weapon_length(delta);
    if(!std::isfinite(distance)||distance>=support.max_correction)return false;
    const float fade=distance<=support.full_correction?1.f:
        (support.max_correction-distance)/(support.max_correction-support.full_correction);
    const float weight=transition*fade;
    Point3 target{};for(unsigned k=0;k<3;++k)target[k]=wrist[k]+delta[k]*weight;
    WeaponArmSolution secondary;
    if(!weapon_arm_target(secondary,shoulder,elbow,wrist,target))return false;
    const auto shift=weapon_subtract(secondary.new_wrist,desired);
    for(unsigned j=0;j<plan.primary_begin;++j) {
        const auto& node=plan.nodes[j];
        auto value=weapon_rotate_around(arm[j],node.forearm?secondary.forearm_rotation:secondary.upper_rotation,
            node.forearm?secondary.elbow:secondary.shoulder,node.forearm?secondary.new_elbow:secondary.shoulder);
        if(node.mirror>=0) {
            auto mirrored=multiply(multiply(frame.reflection,arm[unsigned(node.mirror)]),node.mirror_bind);
            for(unsigned k=0;k<3;++k)mirrored[k*4+3]+=shift[k];
            Affine blended{};
            if(!grip_blend(blended,value,mirrored,weight))return false;
            value=blended;
        }
        arm[j]=value;
    }
    result=std::move(arm);return true;
}
} // namespace owned::gltf
