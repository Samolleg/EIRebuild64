#pragma once
#include "Geometry.h"
#include <algorithm>
#include <queue>
#include <stdexcept>

namespace Navigation {
inline constexpr int FlightBlockCells=8;
// Automatic flight is an upper envelope, not a second walkable ground layer.
// A one-metre lattice conservatively covers each source footprint. Propagating
// its maxima gives continuous approach/departure ramps, irrespective of the
// source slope, step height, material or availability of a walkable roof.
// The ramp gradient shapes altitude only: it never rejects an obstacle.
inline Geometry FlightGeometry(const Geometry& input,const Profile& profile,int onlyX=-1,int onlyZ=-1) {
    constexpr float cell=1.f,clearance=.5f;
    const int nx=int(std::ceil((input.maximum.x-input.minimum.x)/cell))+1;
    const int nz=int(std::ceil((input.maximum.z-input.minimum.z)/cell))+1;
    if(nx<2||nz<2||std::uint64_t(nx)*nz>4000000)
        throw std::runtime_error("Nav64: flight envelope outside supported bounds");
    float top=input.maximum.y;
    for(const auto& t:input.ground)top=std::max({top,t.a.z+t.waterDepth.x,t.b.z+t.waterDepth.y,t.c.z+t.waterDepth.z});
    for(const auto& box:input.flightBoxes)top=std::max(top,box.maximum.z);
    for(const auto* boxes:{&input.solids,&input.flightSolids})for(const auto& box:*boxes)for(auto p:box.corners)top=std::max(top,p.z);
    for(const auto& door:input.doors)top=std::max(top,door.maximum.z);
    int firstX=0,firstZ=0,lastX=nx-1,lastZ=nz-1;
    int emitX0=0,emitZ0=0,emitX1=nx-1,emitZ1=nz-1;
    if(onlyX>=0&&onlyZ>=0) {
        // Match the conservative polygon blocks, including the source triangle
        // beyond their far edge, when rebuilding an isolated runtime tile.
        const float width=profile.tileCells*profile.cellSize,
            border=std::max(1.3f+cell,FlightBlockCells*profile.cellSize+cell);
        emitX0=std::max(0,int(std::floor((onlyX*width-border)/cell)));
        emitZ0=std::max(0,int(std::floor((onlyZ*width-border)/cell)));
        emitX1=std::min(nx-1,int(std::ceil(((onlyX+1)*width+border)/cell)));
        emitZ1=std::min(nz-1,int(std::ceil(((onlyZ+1)*width+border)/cell)));
        // A height maximum falls by at least one metre per metre travelled.
        // Beyond this halo no source can raise even the lowest world support.
        const int halo=int(std::ceil(std::max(0.f,top+clearance-input.minimum.y)+profile.radius+cell))+2;
        firstX=std::max(0,emitX0-halo);firstZ=std::max(0,emitZ0-halo);
        lastX=std::min(nx-1,emitX1+halo);lastZ=std::min(nz-1,emitZ1+halo);
    }
    const int width=lastX-firstX+1,depth=lastZ-firstZ+1;
    std::vector<float> heights(size_t(width)*depth,input.minimum.y);
    auto cover=[&](float x0,float z0,float x1,float z1,float top) {
        const float padding=profile.radius+cell;
        const int a=std::max(firstX,int(std::floor((x0-padding-input.minimum.x)/cell)));
        const int b=std::min(lastX,int(std::ceil((x1+padding-input.minimum.x)/cell)));
        const int c=std::max(firstZ,int(std::floor((z0-padding-input.minimum.z)/cell)));
        const int d=std::min(lastZ,int(std::ceil((z1+padding-input.minimum.z)/cell)));
        for(int z=c;z<=d;++z)for(int x=a;x<=b;++x)
            heights[size_t(z-firstZ)*width+x-firstX]=std::max(heights[size_t(z-firstZ)*width+x-firstX],top+clearance);
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
        const int x=int(index%width),z=int(index/width);
        for(int dz=-1;dz<=1;++dz)for(int dx=-1;dx<=1;++dx) {
            if((!dx&&!dz)||x+dx<0||x+dx>=width||z+dz<0||z+dz>=depth)continue;
            const auto next=size_t(z+dz)*width+x+dx;
            const float value=height-cell*std::hypot(float(dx),float(dz));
            if(value>heights[next]){heights[next]=value;queue.emplace(value,next);}
        }
    }
    Geometry sky;sky.minimum=input.minimum;sky.maximum=input.maximum;sky.maximum.y=std::max(sky.maximum.y,top+clearance);
    auto point=[&](int x,int z) {
        return FromNav({std::min(input.maximum.x,input.minimum.x+x*cell),heights[size_t(z-firstZ)*width+x-firstX],
            std::min(input.maximum.z,input.minimum.z+z*cell)});
    };
    sky.ground.reserve(size_t(emitX1-emitX0)*(emitZ1-emitZ0)*2);
    for(int z=emitZ0;z<emitZ1;++z)for(int x=emitX0;x<emitX1;++x) {
        const auto a=point(x,z),b=point(x+1,z),c=point(x,z+1),d=point(x+1,z+1);
        // Recast Y-up winding; Surface does not apply ground class policies.
        sky.ground.push_back({a,c,b});sky.ground.push_back({b,c,d});
        sky.maximum.y=std::max({sky.maximum.y,a.z,b.z,c.z,d.z});
    }
    return sky;
}
}
