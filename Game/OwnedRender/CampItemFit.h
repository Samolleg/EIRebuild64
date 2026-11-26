#pragma once
#include <DirectXCollision.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

// Fit a rest mesh to the item's original inventory FIG (shape zero). Area
// samples keep the fit independent of vertex seams and triangle subdivision.
namespace CampItemFit {
using Point=std::array<float,3>;
using Basis=std::array<Point,3>;
inline float dot(const Point& a,const Point& b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
inline Point delta(const Point& a,const Point& b) { return {a[0]-b[0],a[1]-b[1],a[2]-b[2]}; }
inline Point rotate(const Basis& m,const Point& p) { return {dot(m[0],p),dot(m[1],p),dot(m[2],p)}; }
inline Basis identity() { return {Point{1,0,0},Point{0,1,0},Point{0,0,1}}; }
struct Triangle {Point a,b,c;};
struct Cloud {
    static constexpr unsigned count=192;
    std::array<Point,count> samples{};
    Basis axes{};
    Point center{};
    float rms=0;
    bool prepare(const std::vector<Triangle>& triangles) {
        std::vector<double> cumulative;
        cumulative.reserve(triangles.size());
        double total=0;
        for(const auto& t:triangles) {
            const auto ab=delta(t.b,t.a), ac=delta(t.c,t.a);
            const Point cross{ab[1]*ac[2]-ab[2]*ac[1],ab[2]*ac[0]-ab[0]*ac[2],ab[0]*ac[1]-ab[1]*ac[0]};
            const double area=std::sqrt(dot(cross,cross));
            total+=area; cumulative.push_back(total);
            for(unsigned k=0;k<3;++k) center[k]+=float(area*((t.a[k]+t.b[k]+t.c[k])/3.));
        }
        if (!std::isfinite(total) || total<=0) return false;
        for(auto& x:center) x/=float(total);
        std::array<DirectX::XMFLOAT3,count> points;
        for(unsigned i=0;i<count;++i) {
            const auto n=std::lower_bound(cumulative.begin(),cumulative.end(),(i+.5)*total/count)-cumulative.begin();
            const auto& t=triangles[n];
            const float r=std::sqrt(float(std::fmod(.5+i*.7548776662466927,1.)));
            const float s=float(std::fmod(.5+i*.5698402909980532,1.));
            auto& v=samples[i];
            for(unsigned k=0;k<3;++k)
                v[k]=(1-r)*t.a[k]+r*(1-s)*t.b[k]+r*s*t.c[k]-center[k];
            rms+=dot(v,v);
            points[i]={v[0],v[1],v[2]};
        }
        rms=std::sqrt(rms/count);
        if(!std::isfinite(rms) || rms<1.e-7f) return false;
        for(auto& v:samples) for(float& x:v) x/=rms;
        DirectX::BoundingOrientedBox box;
        DirectX::BoundingOrientedBox::CreateFromPoints(box,count,points.data(),sizeof(points[0]));
        DirectX::XMFLOAT4X4 m;
        DirectX::XMStoreFloat4x4(&m,DirectX::XMMatrixRotationQuaternion(DirectX::XMLoadFloat4(&box.Orientation)));
        for(unsigned i=0;i<3;++i) for(unsigned j=0;j<3;++j) axes[i][j]=m.m[i][j];
        return true;
    }
};
struct Placement {
    Basis basis=identity();
    Point sourceCenter{},targetCenter{};
    float scale=1;
};
// Compare probable rigid rotations in the two principal frames. A proper
// rotation preserves handedness; the surface distances distinguish tip from grip.
inline Placement fit(const Cloud& source,const Cloud& target,bool weapon) {
    Placement result;
    result.sourceCenter=source.center; result.targetCenter=target.center;
    result.scale=target.rms/source.rms;
    float best=(std::numeric_limits<float>::max)();
    std::array<unsigned,3> order{0,1,2};
    do {
        const int parity=((order[0]>order[1])+(order[0]>order[2])+(order[1]>order[2]))%2?-1:1;
        for(int x:{-1,1}) for(int y:{-1,1}) {
            const int sign[3]={x,y,parity*x*y};
            Basis rotation{};
            for(unsigned r=0;r<3;++r) for(unsigned c=0;c<3;++c)
                for(unsigned k=0;k<3;++k) rotation[r][c]+=target.axes[order[k]][r]*sign[k]*source.axes[k][c];
            // Garments are authored upright. Favor that pose for a newly made
            // GLB whose cut differs substantially from the old inventory art.
            float error=(weapon?.0001f:.035f)*(1-rotation[1][1])*Cloud::count;
            for(const auto& p:source.samples) {
                const auto v=rotate(rotation,p);
                float nearest=(std::numeric_limits<float>::max)();
                for(const auto& q:target.samples) {
                    const auto d=delta(v,q); nearest=(std::min)(nearest,dot(d,d));
                }
                error+=nearest;
                if(error>=best) break;
            }
            if(error<best) {best=error;result.basis=rotation;}
        }
    } while(std::next_permutation(order.begin(),order.end()));
    return result;
}
}
