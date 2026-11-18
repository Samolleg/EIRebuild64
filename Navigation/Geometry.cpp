#include "Geometry.h"
#include <algorithm>
#include <stdexcept>

namespace Navigation {
int Geometry::SurfaceCell(float coordinate, float origin, int tile) const {
    const float local = std::clamp((coordinate - origin) / tileWidth - tile, 0.f, 1.f);
    return std::min(int(local * surfaceDivisions), surfaceDivisions - 1);
}

void Geometry::IndexSurfaceCells() {
    surfaceCells.clear();
    for (const auto& [tile, candidates] : surfaceTriangles) {
        if (candidates.size() < 64) continue;
        SurfaceCells cells;
        std::size_t references = 0;
        for (const auto index : candidates) {
            const auto& t = ground[index];
            // Largest quarter-cell tolerance in the supported ground profiles.
            // Pad in the same XY space as Address, then convert Y to nav Z.
            constexpr float margin = .3f * .25f;
            const auto x0 = SurfaceCell(std::min({t.a.x,t.b.x,t.c.x}) - margin, minimum.x, tile.first);
            const auto x1 = SurfaceCell(std::max({t.a.x,t.b.x,t.c.x}) + margin, minimum.x, tile.first);
            const auto z0 = SurfaceCell(-(std::max({t.a.y,t.b.y,t.c.y}) + margin), minimum.z, tile.second);
            const auto z1 = SurfaceCell(-(std::min({t.a.y,t.b.y,t.c.y}) - margin), minimum.z, tile.second);
            references += (x1 - x0 + 1) * (z1 - z0 + 1);
            // Very large overlapping faces benefit little from subdivision.
            if (references > 4 * candidates.size()) break;
            for (int z = z0; z <= z1; ++z)
                for (int x = x0; x <= x1; ++x)
                    cells[z * surfaceDivisions + x].push_back(index);
        }
        if (references <= 4 * candidates.size()) surfaceCells.emplace(tile, std::move(cells));
    }
}

const std::vector<std::uint32_t>* Geometry::SurfaceCandidates(Point navPoint) const {
    const int x = int(std::floor((navPoint.x - minimum.x) / tileWidth));
    const int z = int(std::floor((navPoint.z - minimum.z) / tileWidth));
    const auto fine = surfaceCells.find({x,z});
    if (fine != surfaceCells.end())
        return &fine->second[SurfaceCell(navPoint.z, minimum.z, z) * surfaceDivisions +
                             SurfaceCell(navPoint.x, minimum.x, x)];
    const auto coarse = surfaceTriangles.find({x,z});
    return coarse == surfaceTriangles.end() ? nullptr : &coarse->second;
}

void Geometry::Bounds() {
    if(ground.empty()) throw std::runtime_error("Nav64: MPR contains no ground triangles");
    minimum=maximum=ToNav(ground.front().a);
    auto add=[&](Point p) {
        if(!Finite(p)) throw std::runtime_error("Nav64: non-finite source geometry");
        p=ToNav(p);
        minimum.x=std::min(minimum.x,p.x); minimum.y=std::min(minimum.y,p.y); minimum.z=std::min(minimum.z,p.z);
        maximum.x=std::max(maximum.x,p.x); maximum.y=std::max(maximum.y,p.y); maximum.z=std::max(maximum.z,p.z);
    };
    for(size_t i=0;i<ground.size();++i) {
        const auto& t=ground[i];
        if(!Finite(t.a)||!Finite(t.b)||!Finite(t.c)) {
            auto source=groundSources.upper_bound(i);
            std::string name="unknown sector";size_t first=0;
            if(source!=groundSources.begin()){--source;name=source->second;first=source->first;}
            throw std::runtime_error("Nav64: non-finite surface geometry source="+name+" triangle="+std::to_string(i-first));
        }
        add(t.a);add(t.b);add(t.c);
        if(!Finite(t.waterDepth)||t.waterDepth.x<0||t.waterDepth.y<0||t.waterDepth.z<0)
            throw std::runtime_error("Nav64: invalid MPR water depth");
        add({t.a.x,t.a.y,t.a.z+t.waterDepth.x});
        add({t.b.x,t.b.y,t.b.z+t.waterDepth.y});
        add({t.c.x,t.c.y,t.c.z+t.waterDepth.z});
    }
    for(const auto& box:flightBoxes) {
        if(!Finite(box.minimum)||!Finite(box.maximum))throw std::runtime_error("Nav64: non-finite flight bounds");
        maximum.y=std::max(maximum.y,box.maximum.z+.6f);
    }
    for(const auto& s:solids) {
        if(!s.error.empty())throw std::runtime_error(s.error);
        for(auto p:s.corners)add(p);
    }
}
std::vector<float> Footprint(const Solid& solid,float& low,float& high) {
    std::vector<Point> p;
    low=1.e30f;high=-1.e30f;
    for(auto v:solid.corners) {v=ToNav(v);p.push_back(v);low=std::min(low,v.y);high=std::max(high,v.y);}
    std::sort(p.begin(),p.end(),[](Point a,Point b){return a.x<b.x || (a.x==b.x && a.z<b.z);});
    p.erase(std::unique(p.begin(),p.end(),[](Point a,Point b){return a.x==b.x && a.z==b.z;}),p.end());
    auto cross=[](Point a,Point b,Point c){return (b.x-a.x)*(c.z-a.z)-(b.z-a.z)*(c.x-a.x);};
    std::vector<Point> hull;
    for(auto v:p) {while(hull.size()>1 && cross(hull[hull.size()-2],hull.back(),v)<=0)hull.pop_back();hull.push_back(v);}
    const auto lower=hull.size();
    for(int i=int(p.size())-2;i>=0;--i) {while(hull.size()>lower && cross(hull[hull.size()-2],hull.back(),p[i])<=0)hull.pop_back();hull.push_back(p[i]);}
    if(!hull.empty())hull.pop_back();
    std::vector<float> result;
    for(auto v:hull) {result.push_back(v.x);result.push_back(0);result.push_back(v.z);}
    return result;
}
}
