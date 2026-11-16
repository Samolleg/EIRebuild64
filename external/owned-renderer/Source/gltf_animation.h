#pragma once
// Immutable glTF animation data, CPU reference and the shared D3D11/D3D12
// byte-buffer contract. No engine objects, device handles or model names.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace owned::gltf {
using Float4 = std::array<float,4>;
using Affine = std::array<float,16>; // row-major, column vectors
inline void animation_require(bool value, const char* why) {
    if (!value) throw std::runtime_error(std::string("glTF animation: ") + why);
}
enum class Interpolation : std::uint32_t { step, linear, cubic };
enum class Path : std::uint32_t { translation, rotation, scale, weights };
struct Track {
    std::uint32_t node = 0, width = 0;
    Path path = Path::translation;
    Interpolation interpolation = Interpolation::linear;
    std::vector<float> times, values;
};
struct Event {
    std::string id, name, payload;
    float time = 0;
};
struct Clip {
    std::string name;
    float duration = 0;
    std::vector<Track> tracks;
    std::vector<Event> events;
};
struct Node {
    std::int32_t parent = -1;
    Float4 translation{0,0,0,0}, rotation{0,0,0,1}, scale{1,1,1,0};
    std::vector<float> weights;
};
struct MorphVertex {
    std::uint32_t node = 0;
    // One position/normal/tangent delta per target; applied before skinning.
    std::vector<std::array<float,9>> deltas;
};
struct Binding {
    std::string legacy;
    std::uint32_t flags = 0, clip = 0;
};
struct Animation {
    std::vector<Node> nodes;
    std::vector<Clip> clips;
    std::vector<MorphVertex> morphs;
    std::vector<Binding> bindings;
};
struct Playback {
    std::uint32_t clip = UINT32_MAX, previous = UINT32_MAX;
    float time = 0, previous_time = 0, blend = 1;
};
struct GpuProgram {
    std::string key;
    std::vector<std::uint8_t> bytes;
    std::uint32_t nodes = 0, morphs = 0;
};
inline float dot4(const Float4& a, const Float4& b) {
    return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]+a[3]*b[3];
}
inline Float4 normalized(Float4 q) {
    const float length = std::sqrt(dot4(q,q));
    animation_require(std::isfinite(length) && length > 1e-8f, "zero/nonfinite quaternion");
    for (auto& v:q) v /= length;
    return q;
}
inline Float4 slerp(Float4 a, Float4 b, float t) {
    a=normalized(a); b=normalized(b);
    float d=dot4(a,b);
    if(d<0){for(auto& v:b)v=-v;d=-d;}
    d=(std::clamp)(d,0.f,1.f);
    float x=1-t,y=t;
    if(d<.9995f){const float angle=std::acos(d),s=std::sin(angle);
        x=std::sin((1-t)*angle)/s;y=std::sin(t*angle)/s;}
    Float4 out{};for(unsigned c=0;c<4;++c)out[c]=x*a[c]+y*b[c];
    return normalized(out);
}
struct Interval { std::size_t a=0,b=0; float t=0,dt=0; };
inline Interval interval(const Track& track,float time) {
    animation_require(!track.times.empty() && std::isfinite(time),"empty track/nonfinite time");
    if(time<=track.times.front())return {};
    const auto last=track.times.size()-1;
    if(time>=track.times.back())return {last,last,0,0};
    const auto b=std::size_t(std::upper_bound(track.times.begin(),track.times.end(),time)-track.times.begin());
    const float dt=track.times[b]-track.times[b-1];
    return {b-1,b,(time-track.times[b-1])/dt,dt};
}
inline float component(const Track& track,const Interval& i,unsigned c) {
    const auto width=track.width;
    const bool cubic=track.interpolation==Interpolation::cubic;
    const auto stride=width*(cubic?3u:1u);
    const float a=track.values[i.a*stride+(cubic?width:0)+c];
    if(i.a==i.b||track.interpolation==Interpolation::step)return a;
    const float b=track.values[i.b*stride+(cubic?width:0)+c];
    if(!cubic)return a+(b-a)*i.t;
    const float t=i.t,t2=t*t,t3=t2*t;
    const float out=track.values[i.a*stride+2*width+c]*i.dt;
    const float in=track.values[i.b*stride+c]*i.dt;
    return (2*t3-3*t2+1)*a+(t3-2*t2+t)*out+(-2*t3+3*t2)*b+(t3-t2)*in;
}
inline Float4 sample(const Track& track,float time) {
    const auto i=interval(track,time);Float4 out{};
    if(track.path==Path::rotation && track.interpolation==Interpolation::linear && i.a!=i.b){
        Float4 a{},b{};std::copy_n(track.values.data()+i.a*4,4,a.begin());
        std::copy_n(track.values.data()+i.b*4,4,b.begin());return slerp(a,b,i.t);
    }
    for(unsigned c=0;c<(std::min)(4u,track.width);++c)out[c]=component(track,i,c);
    return track.path==Path::rotation?normalized(out):out;
}
inline Affine multiply(const Affine& a,const Affine& b) {
    Affine r{};for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)
        for(unsigned k=0;k<4;++k)r[i*4+j]+=a[i*4+k]*b[k*4+j];return r;
}
inline Affine trs(const Node& n) {
    const auto q=normalized(n.rotation);const float x=q[0],y=q[1],z=q[2],w=q[3];
    Affine m{1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w),n.translation[0],
        2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w),n.translation[1],
        2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y),n.translation[2],0,0,0,1};
    for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)m[r*4+c]*=n.scale[c];return m;
}
inline std::vector<Node> sample_local(const Animation& animation,std::uint32_t clip,float time) {
    auto out=animation.nodes;if(clip==UINT32_MAX)return out;
    animation_require(clip<animation.clips.size(),"clip index");
    for(const auto& track:animation.clips[clip].tracks){
        auto& n=out.at(track.node);
        if(track.path==Path::weights){const auto i=interval(track,time);n.weights.resize(track.width);
            for(unsigned c=0;c<track.width;++c)n.weights[c]=component(track,i,c);}
        else if(track.path==Path::translation)n.translation=sample(track,time);
        else if(track.path==Path::rotation)n.rotation=sample(track,time);
        else n.scale=sample(track,time);
    }
    return out;
}
struct SampledPose { std::vector<Affine> joints; std::vector<std::array<float,9>> morphs; };
inline SampledPose evaluate(const Animation& animation,const Playback& playback,
    const Affine& root={1,0,0,0,0,0,-1,0,0,1,0,0,0,0,0,1}) {
    auto nodes=sample_local(animation,playback.clip,playback.time);
    if(playback.blend<1){const auto prior=sample_local(animation,playback.previous,playback.previous_time);
        for(std::size_t i=0;i<nodes.size();++i){auto& n=nodes[i];const auto& p=prior[i];
            n.rotation=slerp(p.rotation,n.rotation,playback.blend);
            for(unsigned c=0;c<3;++c){n.translation[c]=p.translation[c]+playback.blend*(n.translation[c]-p.translation[c]);
                n.scale[c]=p.scale[c]+playback.blend*(n.scale[c]-p.scale[c]);}
            for(std::size_t c=0;c<n.weights.size();++c)n.weights[c]=p.weights[c]+playback.blend*(n.weights[c]-p.weights[c]);}}
    SampledPose out;out.joints.resize(nodes.size());out.morphs.resize(animation.morphs.size());
    for(std::size_t i=0;i<nodes.size();++i)out.joints[i]=multiply(nodes[i].parent<0?root:out.joints.at(nodes[i].parent),trs(nodes[i]));
    for(std::size_t i=0;i<animation.morphs.size();++i){const auto& m=animation.morphs[i];
        for(std::size_t t=0;t<m.deltas.size();++t)for(unsigned c=0;c<9;++c)
            out.morphs[i][c]+=nodes.at(m.node).weights.at(t)*m.deltas[t][c];}
    return out;
}
// A cubic Hermite segment lies in the convex hull of these Bezier controls.
// This bound includes extrema between keys and remains safe during crossfades.
inline std::array<double,2> component_bounds(const Track& t,unsigned c) {
    double lo=std::numeric_limits<double>::infinity(),hi=-lo;
    const auto include=[&](double v){lo=(std::min)(lo,v);hi=(std::max)(hi,v);};
    const auto stride=t.width*(t.interpolation==Interpolation::cubic?3:1);
    for(std::size_t k=0;k<t.times.size();++k){
        const double v=t.values[k*stride+(t.interpolation==Interpolation::cubic?t.width:0)+c];include(v);
        if(t.interpolation==Interpolation::cubic){
            if(k)include(v-double(t.values[k*stride+c])*(t.times[k]-t.times[k-1])/3);
            if(k+1<t.times.size())include(v+double(t.values[k*stride+2*t.width+c])*(t.times[k+1]-t.times[k])/3);
        }
    }
    return {lo,hi};
}
inline bool nonzero_cubic(const std::array<Float4,4>& p,unsigned depth=0) {
    for(unsigned c=0;c<4;++c){float lo=p[0][c],hi=lo;for(unsigned k=1;k<4;++k){lo=(std::min)(lo,p[k][c]);hi=(std::max)(hi,p[k][c]);}
        if(lo>1e-7f||hi<-1e-7f)return true;}
    if(depth==16)return false;
    auto midpoint=[](const Float4& a,const Float4& b){Float4 r{};for(unsigned c=0;c<4;++c)r[c]=(a[c]+b[c])*.5f;return r;};
    auto a=midpoint(p[0],p[1]),b=midpoint(p[1],p[2]),c=midpoint(p[2],p[3]);
    auto d=midpoint(a,b),e=midpoint(b,c),f=midpoint(d,e);
    return nonzero_cubic({p[0],a,d,f},depth+1)&&nonzero_cubic({f,e,c,p[3]},depth+1);
}
inline void validate(const Animation& a) {
    animation_require(!a.nodes.empty()&&a.nodes.size()<65535,"node limit");
    animation_require(a.clips.size()<65535,"clip limit");
    std::map<std::string,bool> names;
    for(std::size_t i=0;i<a.nodes.size();++i){const auto& n=a.nodes[i];
        animation_require(n.parent>=-1&&n.parent<std::int64_t(i),"parent order");
        normalized(n.rotation);animation_require(n.weights.size()<=256,"morph target limit");
        for(auto v:n.translation)animation_require(std::isfinite(v),"node translation");
        for(auto v:n.scale)animation_require(std::isfinite(v),"node scale");
        for(unsigned c=0;c<3;++c)animation_require(n.scale[c]>1e-8f,"nonpositive node scale");
        for(auto v:n.weights)animation_require(std::isfinite(v),"node weight");}
    for(const auto& clip:a.clips){
        animation_require(!clip.name.empty()&&names.emplace(clip.name,true).second,"duplicate/empty clip");
        animation_require(std::isfinite(clip.duration)&&clip.duration>=0,"clip duration");
        std::map<std::pair<unsigned,unsigned>,bool> channels;
        for(const auto& t:clip.tracks){
            animation_require(t.node<a.nodes.size()&&unsigned(t.path)<=3&&unsigned(t.interpolation)<=2,"channel target/mode");
            animation_require(channels.emplace(std::make_pair(t.node,unsigned(t.path)),true).second,"duplicate node/path");
            const unsigned width=t.path==Path::rotation?4:t.path==Path::weights?unsigned(a.nodes[t.node].weights.size()):3;
            animation_require(width&&t.width==width&&!t.times.empty()&&t.times.size()<=16777216,"channel width/keys");
            animation_require(t.values.size()==t.times.size()*t.width*(t.interpolation==Interpolation::cubic?3:1),"channel sample count");
            for(std::size_t k=0;k<t.times.size();++k)animation_require(std::isfinite(t.times[k])&&t.times[k]>=0&&
                (k==0||t.times[k]>t.times[k-1])&&t.times[k]<=clip.duration,"channel time");
            for(float value:t.values)animation_require(std::isfinite(value),"nonfinite sample");
            if(t.path==Path::rotation)for(float time:t.times)sample(t,time);
            if(t.path==Path::scale)for(unsigned c=0;c<3;++c)
                animation_require(component_bounds(t,c)[0]>1e-8,"scale curve crosses singularity or has an unsafe cubic hull");
            if(t.path==Path::rotation&&t.interpolation==Interpolation::cubic)
                for(std::size_t k=1;k<t.times.size();++k){std::array<Float4,4> p{};const float dt=(t.times[k]-t.times[k-1])/3;
                    for(unsigned c=0;c<4;++c){p[0][c]=t.values[(k-1)*12+4+c];p[3][c]=t.values[k*12+4+c];
                        p[1][c]=p[0][c]+dt*t.values[(k-1)*12+8+c];p[2][c]=p[3][c]-dt*t.values[k*12+c];}
                    animation_require(nonzero_cubic(p),"cubic quaternion approaches zero");}
        }
        std::map<std::string,bool> ids;float last=-1;
        for(const auto& e:clip.events){animation_require(!e.id.empty()&&!e.name.empty()&&ids.emplace(e.id,true).second,"event identity");
            animation_require(std::isfinite(e.time)&&e.time>=0&&e.time<=clip.duration&&e.time>=last,"event time/order");last=e.time;}
    }
    for(const auto& m:a.morphs){animation_require(m.node<a.nodes.size()&&m.deltas.size()==a.nodes[m.node].weights.size(),"morph dimensions");
        for(const auto& d:m.deltas)for(float v:d)animation_require(std::isfinite(v),"morph delta");}
    for(const auto& b:a.bindings)animation_require(b.clip<a.clips.size(),"binding clip");
}

struct NodeEnvelope { double translation=0,scale=1;std::vector<double> weights; };
inline std::vector<NodeEnvelope> envelope(const Animation& a) {
    std::vector<NodeEnvelope> out(a.nodes.size());
    for(unsigned i=0;i<a.nodes.size();++i){const auto& n=a.nodes[i];std::array<double,3> t{},s{};
        auto& e=out[i];e.weights.assign(n.weights.begin(),n.weights.end());for(auto& w:e.weights)w=std::abs(w);
        for(unsigned c=0;c<3;++c){t[c]=std::abs(n.translation[c]);s[c]=std::abs(n.scale[c]);}
        for(const auto& clip:a.clips)for(const auto& track:clip.tracks)if(track.node==i&&track.path!=Path::rotation)
            for(unsigned c=0;c<track.width;++c){const auto range=component_bounds(track,c);const auto bound=(std::max)(std::abs(range[0]),std::abs(range[1]));
                if(track.path==Path::translation)t[c]=(std::max)(t[c],bound);
                else if(track.path==Path::scale)s[c]=(std::max)(s[c],bound);
                else e.weights[c]=(std::max)(e.weights[c],bound);}
        e.translation=std::sqrt(t[0]*t[0]+t[1]*t[1]+t[2]*t[2]);e.scale=(std::max)({s[0],s[1],s[2]});
        if(n.parent>=0){e.translation=out[n.parent].translation+out[n.parent].scale*e.translation;e.scale*=out[n.parent].scale;}
        animation_require(std::isfinite(e.translation)&&std::isfinite(e.scale),"animation envelope overflow");
    }
    return out;
}

// GPU layout: header (10 uints); nodes (parent/count/weights/reserved + TRS,
// 64 bytes); clip table points to node-count uint4 channel arrays; each track
// is count,width,interpolation,times followed by its value-array offset.
inline std::shared_ptr<GpuProgram> pack(const Animation& a,const std::string& key) {
    validate(a);auto out=std::make_shared<GpuProgram>();out->key=key;
    out->nodes=unsigned(a.nodes.size());out->morphs=unsigned(a.morphs.size());out->bytes.resize(48);
    auto& bytes=out->bytes;
    const auto append=[&](const void* data,std::size_t size){
        const auto start=(bytes.size()+15u)&~std::size_t(15);
        animation_require(size<=512u*1024u*1024u&&start<=512u*1024u*1024u-size,"GPU animation size");
        bytes.resize(start+size);if(size)std::memcpy(bytes.data()+start,data,size);return std::uint32_t(start);};
    struct GpuNode { std::int32_t parent;std::uint32_t count,weights,reserved;Float4 t,r,s; };
    static_assert(sizeof(GpuNode)==64);
    std::vector<GpuNode> nodes;
    std::vector<unsigned> levels(a.nodes.size());unsigned maxLevel=0;
    for(std::size_t i=0;i<a.nodes.size();++i){const auto& n=a.nodes[i];
        const auto w=append(n.weights.data(),n.weights.size()*4);
        nodes.push_back({n.parent,unsigned(n.weights.size()),w,0,n.translation,n.rotation,n.scale});
        levels[i]=n.parent<0?0:levels[n.parent]+1;maxLevel=(std::max)(maxLevel,levels[i]);}
    std::vector<std::uint32_t> levelOffsets{0},levelNodes;
    for(unsigned l=0;l<=maxLevel;++l){for(unsigned i=0;i<levels.size();++i)if(levels[i]==l)levelNodes.push_back(i);levelOffsets.push_back(unsigned(levelNodes.size()));}
    const auto nodeOffset=append(nodes.data(),nodes.size()*sizeof(GpuNode));
    std::vector<std::uint32_t> clips;
    for(const auto& c:a.clips){std::vector<std::array<std::uint32_t,4>> channels(a.nodes.size());
        for(const auto& t:c.tracks){const auto times=append(t.times.data(),t.times.size()*4),values=append(t.values.data(),t.values.size()*4);
            const std::uint32_t record[]{unsigned(t.times.size()),t.width,unsigned(t.interpolation),times,values};
            channels[t.node][unsigned(t.path)]=append(record,sizeof(record));}
        clips.push_back(append(channels.data(),channels.size()*16));}
    const auto clipOffset=append(clips.data(),clips.size()*4);
    std::vector<std::array<std::uint32_t,4>> morphs;
    for(const auto& m:a.morphs){const auto delta=append(m.deltas.data(),m.deltas.size()*36);morphs.push_back({m.node,unsigned(m.deltas.size()),delta,0});}
    const auto morphOffset=append(morphs.data(),morphs.size()*16);
    const auto levelOffset=append(levelOffsets.data(),levelOffsets.size()*4),levelNode=append(levelNodes.data(),levelNodes.size()*4);
    const std::uint32_t header[]{out->nodes,unsigned(clips.size()),nodeOffset,clipOffset,out->morphs,morphOffset,maxLevel+1,levelOffset,levelNode,0};
    std::memcpy(bytes.data(),header,sizeof(header));return out;
}
} // namespace owned::gltf
