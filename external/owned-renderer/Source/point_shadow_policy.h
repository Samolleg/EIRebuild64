#pragma once
#include "retained_static_scene.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <map>

namespace owned {
// Work/quality policy only. Residency has no lamp-count or byte ceiling.
struct PointShadowPolicy {
    static constexpr unsigned lamp_work_budget_per_frame=4; // cube side comes from point_shadow_quality
    static constexpr float fade_seconds=.25f, hold_seconds=2.f;
    // A focus jump or a long frame gap is a new view: shadows take their state at once.
    static constexpr float jump_distance=8.f, jump_seconds=.5f;
    static constexpr float score_hysteresis=1.25f, distance_hysteresis=4.f, anchor_grid=4.f;
    static constexpr unsigned empty_backoff=30;
    // Light shape (visual tuning; the game supplies only center and range).
    // Each lamp is treated as a small sphere so penumbrae widen with distance
    // from the blocker; a small part that encloses the light is its fixture.
    static constexpr float source_radius_fraction=.03f, source_radius_min=.15f, source_radius_max=.6f;
    static constexpr float fixture_extent_fraction=.3f, fixture_extent_max=2.5f;
    static constexpr float move_settle_seconds=.75f;
    // Caster movement below these bounds deltas (swinging lanterns, idle
    // animation of static props) does not make the caster dynamic.
    static constexpr float caster_move_translation=.1f, caster_move_extent=.2f;
};
// How much a lamp's shadow matters: its luminance times how strongly its range
// reaches the view focus, r^2/(r^2+d^2). A small fire at the focus outranks a
// large lamp at the edge of the view.
inline float point_shadow_importance(const RetainedWorldPointLight& l,float distance) noexcept {
    const float r2=l.radius*l.radius,reach=r2>0?r2/(r2+distance*distance):0.f;
    const float luminance=.2126f*l.color[0]+.7152f*l.color[1]+.0722f*l.color[2];
    return std::max(luminance,0.f)*reach;
}
inline float point_shadow_source_radius(float range) noexcept {
    return std::clamp(range*PointShadowPolicy::source_radius_fraction,
        PointShadowPolicy::source_radius_min,PointShadowPolicy::source_radius_max);
}
// A fixture-sized caster part (lantern box, brazier bowl, lamp head) whose
// bounds enclose the light's source sphere does not shadow that light. Larger
// parts such as walls always cast, so an enclosed interior light cannot leak.
inline bool point_shadow_fixture(const NativeWorldBounds& b,const RetainedWorldPointLight& l) noexcept {
    if(!b.valid)return false;
    const float limit=std::min(l.radius*PointShadowPolicy::fixture_extent_fraction,PointShadowPolicy::fixture_extent_max);
    const float source=point_shadow_source_radius(l.radius);
    for(unsigned a=0;a<3;++a){
        if(b.maximum[a]-b.minimum[a]>limit)return false;
        if(l.center[a]<b.minimum[a]-source||l.center[a]>b.maximum[a]+source)return false;
    }
    return true;
}
inline bool point_shadow_intersects(const NativeWorldBounds& b,const RetainedWorldPointLight& l) noexcept {
    if(!b.valid)return false;
    const auto center=retained_point_shadow_center(l);
    double distance=0;
    for(unsigned a=0;a<3;++a){const double d=std::max(double(b.minimum[a])-center[a],
        std::max(0.,double(center[a])-b.maximum[a]));distance+=d*d;}
    return distance<=double(l.radius)*l.radius;
}
inline bool point_shadow_local_change(const NativeWorldBounds& before,
    const NativeWorldBounds& after,const RetainedWorldPointLight& light) noexcept {
    return point_shadow_intersects(before,light)||point_shadow_intersects(after,light);
}
// Content equality only: source_id is not proven to identify a lamp lifetime.
// 1/1024 world-unit bins absorb capture noise without camera-dependent identity.
struct PointShadowContentKey {
    std::int32_t source_id=0;
    std::array<double,4> geometry{};
    bool casts=false;
    bool operator<(const PointShadowContentKey& b) const noexcept {
        if(source_id!=b.source_id)return source_id<b.source_id;
        if(geometry!=b.geometry)return geometry<b.geometry;
        return casts<b.casts;
    }
};
inline PointShadowContentKey point_shadow_key(const RetainedWorldPointLight& l) noexcept {
    PointShadowContentKey k;k.source_id=l.source_id;k.casts=l.casts_shadow;
    // Range is excluded: animated ranges are absorbed by the grow-only bake
    // radius instead of turning the same lamp into a new one every frame.
    for(unsigned a=0;a<3;++a)k.geometry[a]=std::round(double(l.center[a])*1024.0);
    return k;
}
// Same id and casts flag, centre within a quarter of the range: the same lamp
// that moved (carried, torch-borne or animated lights), not a new one.
inline bool point_shadow_nearby(const RetainedWorldPointLight& a,const RetainedWorldPointLight& b) noexcept {
    if(a.source_id!=b.source_id||a.casts_shadow!=b.casts_shadow)return false;
    double d=0;for(unsigned i=0;i<3;++i){const double t=double(a.center[i])-b.center[i];d+=t*t;}
    const double limit=.25*std::max(a.radius,b.radius);
    return d<=limit*limit;
}
inline bool point_shadow_caster_moved(const NativeWorldBounds& a,const NativeWorldBounds& b) noexcept {
    if(!a.valid||!b.valid)return a.valid!=b.valid;
    for(unsigned i=0;i<3;++i){
        const float centre=.5f*((b.minimum[i]+b.maximum[i])-(a.minimum[i]+a.maximum[i]));
        const float extent=(b.maximum[i]-b.minimum[i])-(a.maximum[i]-a.minimum[i]);
        if(std::abs(centre)>PointShadowPolicy::caster_move_translation||
           std::abs(extent)>PointShadowPolicy::caster_move_extent)return true;
    }
    return false;
}
inline bool point_shadow_moved(const RetainedWorldPointLight& a,const RetainedWorldPointLight& b) noexcept {
    return point_shadow_key(a).geometry!=point_shadow_key(b).geometry;
}
// Grow-only quantized cube range; small range animation never re-bakes.
inline float point_shadow_bake_radius(float current,float range) noexcept {
    if(current>0&&range<=current&&range>=current*.6f)return current;
    return std::ceil(range*1.15f*2.f)*.5f;
}
inline std::vector<std::size_t> point_shadow_matches(const std::vector<RetainedWorldPointLight>& old,
    const std::vector<RetainedWorldPointLight>& next) {
    std::map<PointShadowContentKey,std::vector<std::size_t>> available;
    for(std::size_t i=old.size();i>0;--i)available[point_shadow_key(old[i-1])].push_back(i-1);
    std::vector<std::size_t> matches(next.size(),old.size());
    std::vector<bool> used(old.size(),false);
    for(std::size_t i=0;i<next.size();++i){auto found=available.find(point_shadow_key(next[i]));
        if(found!=available.end()&&!found->second.empty()){
            matches[i]=found->second.back();found->second.pop_back();used[matches[i]]=true;}}
    for(std::size_t i=0;i<next.size();++i)if(matches[i]==old.size())
        for(std::size_t j=0;j<old.size();++j)if(!used[j]&&point_shadow_nearby(old[j],next[i])){
            matches[i]=j;used[j]=true;break;}
    return matches;
}
// Move state and caster membership with the same one-to-one content mapping.
// Identical duplicate values consume separate old entries; pointers never identify lamps.
template<class T,class Retire>
void remap_point_shadow_values(std::vector<T>& values,const std::vector<std::size_t>& matches,Retire retire){
    std::vector<T> next(matches.size());std::vector<bool> used(values.size(),false);
    for(std::size_t i=0;i<matches.size();++i)if(matches[i]<values.size()){
        used[matches[i]]=true;next[i]=std::move(values[matches[i]]);}
    for(std::size_t i=0;i<values.size();++i)if(!used[i])retire(values[i]);
    values=std::move(next);
}
// Shadow-only merging of nearby casting lights (tunable distance, 0 = off).
// Each single-link cluster casts from one cube at the strength-weighted
// centre whose range covers every member; members keep their own
// illumination and sample the cluster cube. leader[i] is the index whose
// cube light i samples (-1 when i casts from its own cube).
inline void point_shadow_merge(const std::vector<RetainedWorldPointLight>& in,float distance,
    std::vector<RetainedWorldPointLight>& out,std::vector<int>& leader){
    out=in;leader.assign(in.size(),-1);
    if(!(distance>0)||in.size()<2)return;
    std::vector<std::size_t> parent(in.size());std::iota(parent.begin(),parent.end(),std::size_t{0});
    const auto root=[&](std::size_t i){while(parent[i]!=i)i=parent[i]=parent[parent[i]];return i;};
    const double limit=double(distance)*distance;
    for(std::size_t i=0;i<in.size();++i)if(in[i].casts_shadow)
        for(std::size_t j=i+1;j<in.size();++j)if(in[j].casts_shadow){
            double d=0;for(unsigned a=0;a<3;++a){const double v=double(in[i].center[a])-in[j].center[a];d+=v*v;}
            if(d<=limit)parent[root(j)]=root(i);}
    const auto weight=[&](std::size_t i){return double(in[i].radius)*std::max({in[i].color[0],in[i].color[1],in[i].color[2],1e-3f});};
    std::map<std::size_t,std::vector<std::size_t>> clusters;
    for(std::size_t i=0;i<in.size();++i)if(in[i].casts_shadow)clusters[root(i)].push_back(i);
    for(const auto& item:clusters){const auto& m=item.second;if(m.size()<2)continue;
        std::size_t head=m.front();double total=0;std::array<double,3> centre{};
        for(auto i:m){const double w=weight(i);total+=w;for(unsigned a=0;a<3;++a)centre[a]+=w*in[i].center[a];
            if(w>weight(head))head=i;}
        for(auto& c:centre)c/=total;
        double reach=0;
        for(auto i:m){double d=0;for(unsigned a=0;a<3;++a){const double v=in[i].center[a]-centre[a];d+=v*v;}
            reach=std::max(reach,std::sqrt(d)+in[i].radius);}
        for(unsigned a=0;a<3;++a)out[head].center[a]=float(centre[a]);
        out[head].radius=float(reach);
        for(auto i:m)if(i!=head){out[i].casts_shadow=false;leader[i]=int(head);}
    }
}
// Chosen policy: camera focus anchor; not a gameplay-state read. Unproject the
// verified clip centre and intersect owned receiver bounds, then quantize pans.
inline std::array<float,3> point_shadow_focus(const std::array<float,16>& clip,
    const std::vector<NativeWorldBounds>& receivers) noexcept {
    double m[4][8]{};
    for(unsigned r=0;r<4;++r){for(unsigned c=0;c<4;++c)m[r][c]=clip[r*4+c];m[r][4+r]=1;}
    for(unsigned c=0;c<4;++c){
        unsigned pivot=c;for(unsigned r=c+1;r<4;++r)if(std::abs(m[r][c])>std::abs(m[pivot][c]))pivot=r;
        if(std::abs(m[pivot][c])<1e-20)return {};
        for(unsigned k=0;k<8;++k)std::swap(m[c][k],m[pivot][k]);
        const double scale=m[c][c];for(double& x:m[c])x/=scale;
        for(unsigned r=0;r<4;++r)if(r!=c){const double f=m[r][c];for(unsigned k=0;k<8;++k)m[r][k]-=f*m[c][k];}
    }
    std::array<double,3> origin{},direction{};
    const double w0=m[3][7],w1=m[3][6]+m[3][7];
    if(std::abs(w0)<1e-20||std::abs(w1)<1e-20)return {};
    double length=0;
    for(unsigned a=0;a<3;++a){origin[a]=m[a][7]/w0;direction[a]=(m[a][6]+m[a][7])/w1-origin[a];length+=direction[a]*direction[a];}
    length=std::sqrt(length);if(!(length>0))return {};
    for(auto& d:direction)d/=length;
    double nearest=std::numeric_limits<double>::infinity(),min_depth=nearest,max_depth=-nearest;
    for(const auto& b:receivers)if(b.valid){
        double enter=0,leave=std::numeric_limits<double>::infinity();bool hit=true;
        for(unsigned a=0;a<3;++a){
            if(std::abs(direction[a])<1e-12){if(origin[a]<b.minimum[a]||origin[a]>b.maximum[a])hit=false;}
            else{double lo=(b.minimum[a]-origin[a])/direction[a],hi=(b.maximum[a]-origin[a])/direction[a];
                if(lo>hi)std::swap(lo,hi);enter=std::max(enter,lo);leave=std::min(leave,hi);}
        }
        if(hit&&enter<=leave)nearest=std::min(nearest,enter);
        for(unsigned corner=0;corner<8;++corner){double t=0;
            for(unsigned a=0;a<3;++a)t+=((corner&(1u<<a)?b.maximum[a]:b.minimum[a])-origin[a])*direction[a];
            min_depth=std::min(min_depth,t);max_depth=std::max(max_depth,t);}
    }
    if(!std::isfinite(nearest))nearest=std::isfinite(min_depth)?std::max(0.,(min_depth+max_depth)*.5):length*.5;
    std::array<float,3> result{};
    for(unsigned a=0;a<3;++a)result[a]=float(std::round((origin[a]+nearest*direction[a])/PointShadowPolicy::anchor_grid)*PointShadowPolicy::anchor_grid);
    return result;
}

inline bool build_owned_point_shadow_cube(const RetainedWorldPointLight& light,
    NativePointShadowFrame& result) noexcept {
    if(!(light.radius>0) || !std::isfinite(light.radius))return false;
    const float near_plane=light.radius*.02f;
    const float depth_scale=light.radius/(light.radius-near_plane);
    const float depth_offset=-near_plane*depth_scale;
    const auto shadow_center=retained_point_shadow_center(light);
    // Right, up and forward form a left-handed frame for each 90-degree face.
    static constexpr std::array<std::array<std::array<float,3>,3>,6> axes{{
        {{{0,0,-1},{0,1,0},{1,0,0}}},
        {{{0,0,1},{0,1,0},{-1,0,0}}},
        {{{1,0,0},{0,0,-1},{0,1,0}}},
        {{{1,0,0},{0,0,1},{0,-1,0}}},
        {{{1,0,0},{0,1,0},{0,0,1}}},
        {{{-1,0,0},{0,1,0},{0,0,-1}}}
    }};
    result={};
    for(unsigned face=0;face<6;++face){
        auto& slice=result.slices[face];const auto& basis=axes[face];
        auto& matrix=slice.world_to_light;
        for(unsigned row=0;row<4;++row){
            if(row==3) {
                for(unsigned axis=0;axis<3;++axis)matrix[12+axis]=basis[2][axis];
                matrix[15]=-std::inner_product(basis[2].begin(),basis[2].end(),
                    shadow_center.begin(),0.f);
            }else{
                const float scale=row==2?depth_scale:1.f;
                for(unsigned axis=0;axis<3;++axis)matrix[row*4+axis]=basis[row][axis]*scale;
                matrix[row*4+3]=-scale*std::inner_product(basis[row].begin(),basis[row].end(),
                    shadow_center.begin(),0.f)+(row==2?depth_offset:0.f);
            }
        }
        slice.camera_slice=matrix;slice.bias={light.radius/256.f,1.f/light.radius,0,0};
        slice.enabled=true;
        for(float value:matrix)if(!std::isfinite(value))return false;
    }
    return true;
}


struct PointShadowLamp {
    int static_slot=-1,overlay_slot=-1,replacement_slot=-1;
    bool stale=true,eligible=false,dynamic=false,valid=false,overlay_valid=false,retiring=false,ever_baked=false,suppressed=false;
    float strength=0,overlay_strength=0,score=0,distance=0;
    double acquired=0,retry_after=0,empty_since=-1,last_move=-1;
    float bake_radius=0;
    bool moving=false; // fades out once while moving; re-bakes after settling
    bool fast=false; // in the full-rate overlay tier
    std::uint64_t waited=0,next_overlay=0;
};
// List ordinals are presentation indices; content matching transfers retained state.
// source_id alone is deliberately NOT treated as a persistent light identity.
class PointShadowScheduler {
public:
    std::vector<PointShadowLamp> lamps;
    std::vector<unsigned> work;
    std::uint64_t allocation_failures=0,pressure_drops=0,strength_zeroed=0;
    std::array<float,3> last_anchor{};bool has_anchor=false;
    void reset(std::size_t count){lamps.assign(count,{});work.clear();has_anchor=false;}
    void invalidate(unsigned index){lamps[index].stale=true;}
    bool observe_casters(unsigned index,bool has,double now){auto& s=lamps[index];
        if(has){s.empty_since=-1;s.eligible=true;if(!s.suppressed)s.retiring=false;return false;}
        if(s.empty_since<0)s.empty_since=now;
        if(s.eligible&&now-s.empty_since>=PointShadowPolicy::hold_seconds){s.eligible=false;return true;}
        return false;
    }
    template<class Allocate> int bake_slot(unsigned index,Allocate allocate){auto& s=lamps[index];
        if(!s.valid)return s.static_slot;
        if(s.replacement_slot<0&&!allocate(false,s.replacement_slot)){++allocation_failures;return -1;}
        return s.replacement_slot;
    }
    // Pressure overrides minimum residence, but never chooses an arbitrary index.
    // A retiring committed cube remains published until its fade reaches zero.
    void drop_least_important(double now){
        for(const auto& s:lamps)if(s.suppressed&&s.retiring)return;
        unsigned worst=UINT_MAX;
        for(unsigned i=0;i<lamps.size();++i){const auto& s=lamps[i];
            if(s.eligible&&!s.suppressed&&(worst==UINT_MAX||s.score<lamps[worst].score))worst=i;}
        if(worst==UINT_MAX)return;
        auto& s=lamps[worst];s.suppressed=true;s.retiring=true;
        s.retry_after=now+PointShadowPolicy::hold_seconds;++pressure_drops;
    }
    template<class Allocate,class Release>
    void select(const std::vector<RetainedWorldPointLight>& lights,const std::array<float,3>& anchor,
        double now,float dt,std::uint64_t frame,Allocate allocate,Release release,bool pressure=false) {
        work.clear();allocation_failures=pressure_drops=strength_zeroed=0;
        bool jump=has_anchor&&dt>PointShadowPolicy::jump_seconds;
        if(has_anchor){double moved=0;for(unsigned a=0;a<3;++a){const double d=double(anchor[a])-last_anchor[a];moved+=d*d;}
            jump|=moved>double(PointShadowPolicy::jump_distance)*PointShadowPolicy::jump_distance;}
        last_anchor=anchor;has_anchor=true;
        const float step=jump?1.f:dt/PointShadowPolicy::fade_seconds;
        for(std::size_t i=0;i<lamps.size();++i){auto& s=lamps[i];const auto& l=lights[i];const float prior_strength=s.strength;
            double distance=0;for(unsigned a=0;a<3;++a){const double d=l.center[a]-anchor[a];distance+=d*d;}
            const float d=float(std::sqrt(distance));
            if(std::abs(d-s.distance)>PointShadowPolicy::distance_hysteresis)s.distance=d;
            const float score=point_shadow_importance(l,s.distance);
            if(s.score==0||score>s.score*PointShadowPolicy::score_hysteresis||score*PointShadowPolicy::score_hysteresis<s.score)s.score=score;
            if(s.suppressed&&!pressure&&!s.retiring&&now>=s.retry_after)s.suppressed=false;
            if(!s.eligible)s.retiring=true;
            if(s.retiring){s.strength=std::max(0.f,s.strength-step);
                if(s.strength==0){
                    if(s.static_slot>=0)release(s.static_slot);
                    if(s.overlay_slot>=0)release(s.overlay_slot);
                    if(s.replacement_slot>=0)release(s.replacement_slot);
                    s.static_slot=s.overlay_slot=s.replacement_slot=-1;s.stale=true;s.valid=s.overlay_valid=false;s.retiring=false;s.overlay_strength=0;
                }
            }else if(s.moving)s.strength=std::max(0.f,s.strength-step);
            else if(s.valid)s.strength=std::min(1.f,s.strength+step);
            if(s.overlay_valid)s.overlay_strength=std::min(1.f,s.overlay_strength+step);
            if(!s.dynamic){
                if(s.overlay_slot>=0)release(s.overlay_slot);
                s.overlay_valid=false;s.overlay_strength=0;s.overlay_slot=-1;s.next_overlay=frame+PointShadowPolicy::empty_backoff;
            }
            if(prior_strength>0&&s.strength==0)++strength_zeroed;
            ++s.waited;
        }
        if(pressure)drop_least_important(now);
        std::vector<unsigned> order;
        for(unsigned i=0;i<lamps.size();++i)if(lamps[i].eligible)order.push_back(i);
        std::stable_sort(order.begin(),order.end(),[&](unsigned a,unsigned b){return lamps[a].score>lamps[b].score;});
        // Allocation is demand-driven. Importance orders requests; it is not admission.
        if(!pressure)for(unsigned i:order){auto& s=lamps[i];if(s.retiring||s.suppressed)continue;
            if(s.static_slot<0){
                if(!allocate(false,s.static_slot)){++allocation_failures;drop_least_important(now);break;}
                s.valid=false;s.stale=true;s.strength=0;s.acquired=now;
            }
            if(s.dynamic&&s.overlay_slot<0){
                if(!allocate(true,s.overlay_slot)){++allocation_failures;drop_least_important(now);break;}
                s.overlay_valid=false;s.overlay_strength=0;
            }
        }
        const auto wants=[&](unsigned i){const auto& s=lamps[i];return s.eligible&&!s.retiring&&!s.suppressed&&!s.moving&&s.static_slot>=0&&s.empty_since<0&&
            (s.stale||(s.dynamic&&s.overlay_slot>=0&&frame>=s.next_overlay));};
        order.erase(std::remove_if(order.begin(),order.end(),[&](unsigned i){return !wants(i);}),order.end());
        std::stable_sort(order.begin(),order.end(),[&](unsigned a,unsigned b){
            const auto& x=lamps[a];const auto& y=lamps[b];
            if(x.ever_baked!=y.ever_baked)return !x.ever_baked;
            if(x.waited!=y.waited)return x.waited>y.waited;
            const auto bucket=[&](const PointShadowLamp& s){return (std::min(2u,unsigned(s.distance/32.f))+3u-unsigned(frame%3))%3u;};
            if(bucket(x)!=bucket(y))return bucket(x)<bucket(y);
            return x.score>y.score;
        });
        // The work budget limits static (re)bakes only. Overlays hold moving
        // casters and must update every frame they are due, or those shadows
        // stutter and blink while waiting for budget.
        unsigned bakes=0;work_bakes.clear();
        for(unsigned i:order){const auto& s=lamps[i];
            const bool bake=s.stale&&bakes<PointShadowPolicy::lamp_work_budget_per_frame;
            const bool overlay_due=s.dynamic&&s.overlay_slot>=0&&frame>=s.next_overlay;
            if(!bake&&!overlay_due)continue;
            bakes+=bake;work.push_back(i);work_bakes.push_back(bake);
        }
    }
    std::vector<bool> work_bakes; // parallel to work: whether that entry re-bakes its static tier
    // Full-rate overlay tier: the `count` most important lamps with moving
    // casters (0 = all). A member keeps its place until another lamp is
    // score_hysteresis times more important, so near-equal lamps do not swap
    // redraw cadence every frame.
    void rank_fast_overlays(unsigned count){
        std::vector<std::pair<float,unsigned>> ranked;
        for(unsigned i=0;i<lamps.size();++i){auto& s=lamps[i];const bool candidate=s.dynamic&&s.overlay_slot>=0;
            if(candidate&&count)ranked.emplace_back(-s.score*(s.fast?PointShadowPolicy::score_hysteresis:1.f),i);
            s.fast=candidate&&!count;}
        std::stable_sort(ranked.begin(),ranked.end());
        for(std::size_t k=0;k<ranked.size()&&k<count;++k)lamps[ranked[k].second].fast=true;
    }
    template<class Release> void commit(unsigned i,bool baked,bool overlay,Release release){auto& s=lamps[i];
        if(baked){if(s.replacement_slot>=0){release(s.static_slot);s.static_slot=s.replacement_slot;s.replacement_slot=-1;}
            s.valid=true;s.stale=false;s.ever_baked=true;}if(overlay){s.overlay_valid=true;s.next_overlay=0;}s.waited=0;}
};
} // namespace owned