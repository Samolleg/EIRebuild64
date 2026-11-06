#pragma once
#include "Geometry.h"
#include <algorithm>
#include <cfloat>
#include <climits>
#include <stdexcept>

namespace Navigation {
// Runtime-only rigid placement. Source poses come from the normal MOB reader;
// the lossless navgeom planes remain the only source of walkable geometry.
struct SceneTransform {
    Point origin;
    std::array<Point,3> axis={Point{1,0,0},Point{0,1,0},Point{0,0,1}};
    bool Same(const SceneTransform& other) const {
        if(Distance(origin,other.origin)>.00001f)return false;
        for(unsigned i=0;i<3;++i)if(Distance(axis[i],other.axis[i])>.00001f)return false;
        return true;
    }
    Point From(const SceneTransform& source,Point p) const {
        const Point d{p.x-source.origin.x,p.y-source.origin.y,p.z-source.origin.z};
        float local[3];for(unsigned i=0;i<3;++i)local[i]=d.x*source.axis[i].x+d.y*source.axis[i].y+d.z*source.axis[i].z;
        Point out=origin;for(unsigned i=0;i<3;++i){out.x+=axis[i].x*local[i];out.y+=axis[i].y*local[i];out.z+=axis[i].z*local[i];}
        return out;
    }
};

inline std::vector<Point> ClipSceneCell(const std::vector<Point>& input,unsigned axis,float edge,bool greater) {
    std::vector<Point> out;if(input.empty())return out;
    auto coordinate=[&](Point p){return axis?p.y:p.x;};
    auto previous=input.back();float a=coordinate(previous)-edge;bool inside=greater?a>=0:a<=0;
    for(auto p:input) {
        const float b=coordinate(p)-edge;const bool next=greater?b>=0:b<=0;
        if(inside!=next){const float f=a/(a-b);out.push_back({previous.x+(p.x-previous.x)*f,previous.y+(p.y-previous.y)*f,previous.z+(p.z-previous.z)*f});}
        if(next)out.push_back(p);previous=p;a=b;inside=next;
    }
    return out;
}

inline CeilingGrid TransformCeiling(const CeilingGrid& source,const SceneTransform& from,const SceneTransform& to) {
    // Reproject the already baked lower envelope, without loading textures or
    // rerasterizing models. Outward coverage and downward height rounding keep
    // the same conservative 0.3 m / 0.05 m canopy contract.
    std::map<std::pair<int,int>,float> cells;
    for(unsigned y=0;y<source.height;++y)for(unsigned x=0;x<source.width;++x) {
        const auto height=source.heights[size_t(y)*source.width+x];if(height==0xffff)continue;
        const float z=source.base+height*.05f,lx=(source.x+int(x))*.3f,ly=(source.y+int(y))*.3f;
        std::vector<Point> face={to.From(from,{lx,ly,z}),to.From(from,{lx+.3f,ly,z}),to.From(from,{lx+.3f,ly+.3f,z}),to.From(from,{lx,ly+.3f,z})};
        float ax=FLT_MAX,ay=FLT_MAX,bx=-FLT_MAX,by=-FLT_MAX;
        for(auto p:face){ax=(std::min)(ax,p.x);ay=(std::min)(ay,p.y);bx=(std::max)(bx,p.x);by=(std::max)(by,p.y);}
        const int x0=int(std::floor(ax/.3f)),x1=int(std::floor(bx/.3f)),y0=int(std::floor(ay/.3f)),y1=int(std::floor(by/.3f));
        if(std::int64_t(x1-x0+1)*(y1-y0+1)>1000000)throw std::runtime_error("Nav64: transformed canopy exceeds cell limit");
        for(int cy=y0;cy<=y1;++cy)for(int cx=x0;cx<=x1;++cx) {
            auto clipped=ClipSceneCell(face,0,cx*.3f,true);clipped=ClipSceneCell(clipped,0,(cx+1)*.3f,false);
            clipped=ClipSceneCell(clipped,1,cy*.3f,true);clipped=ClipSceneCell(clipped,1,(cy+1)*.3f,false);
            if(clipped.size()<3)continue;
            double area=0;float low=FLT_MAX;
            for(size_t i=0;i<clipped.size();++i){auto a=clipped[i],b=clipped[(i+1)%clipped.size()];area+=double(a.x-clipped[0].x)*(b.y-clipped[0].y)-double(a.y-clipped[0].y)*(b.x-clipped[0].x);low=(std::min)(low,a.z);}
            if(std::abs(area)<1.e-10)continue;
            auto inserted=cells.emplace(std::make_pair(cx,cy),low);if(!inserted.second)inserted.first->second=(std::min)(inserted.first->second,low);
        }
    }
    CeilingGrid out;out.object=source.object;if(cells.empty())return out;
    out.x=out.y=INT_MAX;int x1=INT_MIN,y1=INT_MIN;float base=FLT_MAX;
    for(const auto& cell:cells){out.x=(std::min)(out.x,cell.first.first);out.y=(std::min)(out.y,cell.first.second);x1=(std::max)(x1,cell.first.first);y1=(std::max)(y1,cell.first.second);base=(std::min)(base,cell.second);}
    out.width=unsigned(x1-out.x+1);out.height=unsigned(y1-out.y+1);out.base=std::floor(base/.05f)*.05f;
    if(std::uint64_t(out.width)*out.height>1000000)throw std::runtime_error("Nav64: transformed canopy exceeds grid limit");
    out.heights.assign(size_t(out.width)*out.height,0xffff);
    for(const auto& cell:cells) {
        const int h=(std::max)(0,int(std::floor((cell.second-out.base)/.05f)));
        if(h<0||h>=0xffff)throw std::runtime_error("Nav64: transformed canopy height exceeds grid range");
        out.heights[size_t(cell.first.second-out.y)*out.width+cell.first.first-out.x]=std::uint16_t(h);
    }
    return out;
}

inline void TransformSource(Geometry& target,const Geometry& original,int id,const SceneTransform& from,const SceneTransform& to) {
    for(size_t i=0;i<original.ground.size();++i)if(original.ground[i].object==id) {
        auto t=original.ground[i];t.a=to.From(from,t.a);t.b=to.From(from,t.b);t.c=to.From(from,t.c);target.ground[i]=t;
    }
    for(size_t i=0;i<original.ceilings.size();++i)if(original.ceilings[i].object==id)
        target.ceilings[i]=TransformCeiling(original.ceilings[i],from,to);
    for(size_t i=0;i<original.flightBoxes.size();++i)if(original.flightBoxes[i].object==id) {
        const auto& box=original.flightBoxes[i];auto& out=target.flightBoxes[i];
        out.minimum={FLT_MAX,FLT_MAX,FLT_MAX};out.maximum={-FLT_MAX,-FLT_MAX,-FLT_MAX};
        for(int c=0;c<8;++c) {
            auto p=to.From(from,{(c&1)?box.maximum.x:box.minimum.x,(c&2)?box.maximum.y:box.minimum.y,(c&4)?box.maximum.z:box.minimum.z});
            out.minimum.x=(std::min)(out.minimum.x,p.x);out.minimum.y=(std::min)(out.minimum.y,p.y);out.minimum.z=(std::min)(out.minimum.z,p.z);
            out.maximum.x=(std::max)(out.maximum.x,p.x);out.maximum.y=(std::max)(out.maximum.y,p.y);out.maximum.z=(std::max)(out.maximum.z,p.z);
        }
    }
}
}
