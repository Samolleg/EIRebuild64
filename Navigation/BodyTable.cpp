#include "BodyTable.h"
#include "Profiles.h"
#include <cstring>
#include <stdexcept>

namespace Navigation {
namespace {
void Require(bool ok) {if(!ok)throw std::runtime_error("Nav64: invalid/incompatible body table; re-bake navigation");}
void Valid(const BodySize& body) {
    Require(std::isfinite(body.radius)&&body.radius>0);
    for(float height:body.height)Require(std::isfinite(height)&&height>0);
}
struct Wire {
    Bytes bytes;size_t at=0;
    void U(std::uint32_t n){for(int i=0;i<4;++i)bytes.push_back(std::uint8_t(n>>(8*i)));}
    void F(float f){std::uint32_t n;std::memcpy(&n,&f,4);U(n);}
    void S(const std::string& s){U(std::uint32_t(s.size()));bytes.insert(bytes.end(),s.begin(),s.end());}
    std::uint32_t U(){Require(at<=bytes.size()&&bytes.size()-at>=4);std::uint32_t n=0;for(int i=0;i<4;++i)n|=std::uint32_t(bytes[at++])<<(8*i);return n;}
    float F(){auto n=U();float f;std::memcpy(&f,&n,4);Require(std::isfinite(f));return f;}
    std::string S(){auto n=U();Require(n>0&&n<=256&&n<=bytes.size()-at);std::string s(bytes.begin()+at,bytes.begin()+at+n);at+=n;return s;}
};
}
BodySize BodyTable::Lookup(const std::string& model,float strength,float grace,float tall) const {
    Require(std::isfinite(strength)&&std::isfinite(grace)&&std::isfinite(tall));
    auto exact=samples.find({model,strength,grace,tall});if(exact!=samples.end())return exact->second;
    BodySize result;const float shape[]={strength,grace,tall};
    float low[3],weight[3];
    for(int axis=0;axis<3;++axis){low[axis]=shape[axis]>=1?1.f:0.f;weight[axis]=shape[axis]-low[axis];}
    for(unsigned corner=0;corner<8;++corner) {
        auto found=samples.find({model,low[0]+float(corner&1),low[1]+float((corner>>1)&1),low[2]+float((corner>>2)&1)});
        if(found==samples.end())throw std::runtime_error("Nav64: body table has no model "+model+"; re-bake navigation");
        float w=1;for(unsigned axis=0;axis<3;++axis)w*=(corner&(1u<<axis))?weight[axis]:1-weight[axis];
        result.radius+=found->second.radius*w;
        for(int pose=0;pose<3;++pose)result.height[pose]+=found->second.height[pose]*w;
    }
    Valid(result);return result;
}
Bytes StoreBodies(const BodyTable& table) {
    Wire w;w.U(0x59444f42);w.U(1);w.U(AlgorithmVersion);w.U(std::uint32_t(table.samples.size()));
    for(const auto& entry:table.samples) {
        const auto& key=entry.first;const auto& body=entry.second;Valid(body);
        w.S(std::get<0>(key));w.F(std::get<1>(key));w.F(std::get<2>(key));w.F(std::get<3>(key));
        w.U(ProfileForRadius(body.radius,false));w.U(ProfileForRadius(body.radius,true));
        w.F(body.radius);for(float height:body.height)w.F(height);
    }
    return std::move(w.bytes);
}
BodyTable LoadBodies(const Bytes& bytes) {
    Wire w;w.bytes=bytes;Require(w.U()==0x59444f42&&w.U()==1&&w.U()==AlgorithmVersion);
    const auto count=w.U();Require(count>0&&count<=100000);BodyTable table;
    for(unsigned i=0;i<count;++i) {
        const auto model=w.S();const float strength=w.F(),grace=w.F(),tall=w.F();
        const auto ground=w.U(),levitate=w.U();BodySize body;body.radius=w.F();for(auto& height:body.height)height=w.F();Valid(body);
        Require(ground==ProfileForRadius(body.radius,false)&&levitate==ProfileForRadius(body.radius,true));
        Require(table.samples.emplace(BodyKey{model,strength,grace,tall},body).second);
    }
    Require(w.at==bytes.size());return table;
}
}
