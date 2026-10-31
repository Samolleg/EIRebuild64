#pragma once
#include "Geometry.h"
#include <algorithm>
#include <queue>
#include <stdexcept>

namespace Navigation {
// Automatic flight is an upper envelope, not a second walkable ground layer.
// A one-metre lattice conservatively covers each source footprint. Propagating
// its maxima gives continuous approach/departure ramps, irrespective of the
// source slope, step height, material or availability of a walkable roof.
// The ramp gradient shapes altitude only: it never rejects an obstacle.
inline Geometry FlightGeometry(const Geometry& input,const Profile& profile) {
    constexpr float cell=1.f,clearance=.5f;
    const int nx=int(std::ceil((input.maximum.x-input.minimum.x)/cell))+1;
    const int nz=int(std::ceil((input.maximum.z-input.minimum.z)/cell))+1;
    if(nx<2||nz<2||std::uint64_t(nx)*nz>4000000)
        throw std::runtime_error("Nav64: flight envelope outside supported bounds");
    std::vector<float> heights(size_t(nx)*nz,input.minimum.y);
    auto cover=[&](float x0,float z0,float x1,float z1,float top) {
        const float padding=profile.radius+cell;
        const int a=std::max(0,int(std::floor((x0-padding-input.minimum.x)/cell)));
        const int b=std::min(nx-1,int(std::ceil((x1+padding-input.minimum.x)/cell)));
        const int c=std::max(0,int(std::floor((z0-padding-input.minimum.z)/cell)));
        const int d=std::min(nz-1,int(std::ceil((z1+padding-input.minimum.z)/cell)));
        for(int z=c;z<=d;++z)for(int x=a;x<=b;++x)
            heights[size_t(z)*nx+x]=std::max(heights[size_t(z)*nx+x],top+clearance);
    };
    for(const auto& triangle:input.ground) {
        const auto a=ToNav(triangle.a),b=ToNav(triangle.b),c=ToNav(triangle.c);
        cover(std::min({a.x,b.x,c.x}),std::min({a.z,b.z,c.z}),
            std::max({a.x,b.x,c.x}),std::max({a.z,b.z,c.z}),
            std::max({a.y+triangle.waterDepth.x,b.y+triangle.waterDepth.y,c.y+triangle.waterDepth.z}));
    }
    for(const auto& box:input.flightBoxes)
        cover(box.minimum.x,-box.maximum.y,box.maximum.x,-box.minimum.y,box.maximum.z);
    for(const auto* boxes:{&input.solids,&input.flightSolids})for(const auto& box:*boxes) {
        if(!box.error.empty())throw std::runtime_error(box.error);
        Point low{1.e30f,1.e30f,1.e30f},high{-1.e30f,-1.e30f,-1.e30f};
        for(auto corner:box.corners) {
            const auto p=ToNav(corner);
            low.x=std::min(low.x,p.x);low.z=std::min(low.z,p.z);
            high.x=std::max(high.x,p.x);high.y=std::max(high.y,p.y);high.z=std::max(high.z,p.z);
        }
        cover(low.x,low.z,high.x,high.z,high.y);
    }
    // Door state affects ground passage, never the existence of sky above it.
    for(const auto& door:input.doors)
        cover(door.minimum.x,-door.maximum.y,door.maximum.x,-door.minimum.y,door.maximum.z);
    using Entry=std::pair<float,size_t>;
    std::priority_queue<Entry> queue;
    for(size_t i=0;i<heights.size();++i)queue.emplace(heights[i],i);
    while(!queue.empty()) {
        const auto [height,index]=queue.top();queue.pop();
        if(height<heights[index])continue;
        const int x=int(index%nx),z=int(index/nx);
        for(int dz=-1;dz<=1;++dz)for(int dx=-1;dx<=1;++dx) {
            if((!dx&&!dz)||x+dx<0||x+dx>=nx||z+dz<0||z+dz>=nz)continue;
            const auto next=size_t(z+dz)*nx+x+dx;
            const float value=height-cell*std::hypot(float(dx),float(dz));
            if(value>heights[next]){heights[next]=value;queue.emplace(value,next);}
        }
    }
    Geometry sky;sky.minimum=input.minimum;sky.maximum=input.maximum;
    auto point=[&](int x,int z) {
        return FromNav({std::min(input.maximum.x,input.minimum.x+x*cell),heights[size_t(z)*nx+x],
            std::min(input.maximum.z,input.minimum.z+z*cell)});
    };
    sky.ground.reserve(size_t(nx-1)*(nz-1)*2);
    for(int z=0;z<nz-1;++z)for(int x=0;x<nx-1;++x) {
        const auto a=point(x,z),b=point(x+1,z),c=point(x,z+1),d=point(x+1,z+1);
        // Recast Y-up winding; Surface does not apply ground class policies.
        sky.ground.push_back({a,c,b});sky.ground.push_back({b,c,d});
        sky.maximum.y=std::max({sky.maximum.y,a.z,b.z,c.z,d.z});
    }
    return sky;
}
}
