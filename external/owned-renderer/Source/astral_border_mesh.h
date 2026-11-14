#pragma once
#include "owned_asset_loader.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace owned {
// The authored rear curtain is a 59 x 7 grid. Its vertices survive in the
// FIG, but 33 cells were deliberately omitted. Close those cells without
// fitting a new surface, moving vertices or touching the three front ribbons.
inline std::shared_ptr<const FigureCpuAsset> close_astral_border_cuts(
    const std::shared_ptr<const FigureCpuAsset>& source) {
    constexpr unsigned columns=59, rows=7, count=columns*rows;
    if(!source || source->shape_count!=1 || source->positions.size()*4<count) return source;
    const auto point=[&](unsigned i){const auto& p=source->positions[i/4];const auto k=i%4;return std::array<float,3>{p.x[k],p.y[k],p.z[k]};};
    // Establish this specific authored grid before interpreting its indices.
    for(unsigned c=0;c<columns;++c) {
        const auto p=point(c*rows);
        if(std::abs(p[0]-(-18.f+36.f*c/(columns-1)))>.001f || std::abs(p[2]-.15f)>.001f) return source;
    }
    std::set<std::array<unsigned,3>> faces;
    std::array<unsigned,count> component;component.fill(UINT_MAX);
    for(unsigned i=0;i<source->components.size();++i) {
        const auto& v=source->components[i];
        if(v.position<count && component[v.position]==UINT_MAX)component[v.position]=i;
    }
    for(std::size_t i=0;i+2<source->indices.size();i+=3) {
        std::array<unsigned,3> face;
        for(unsigned k=0;k<3;++k) {
            if(source->indices[i+k]>=source->components.size())return source;
            face[k]=source->components[source->indices[i+k]].position;
        }
        if(*std::max_element(face.begin(),face.end())>=count)continue;
        std::sort(face.begin(),face.end());faces.insert(face);
    }
    std::shared_ptr<FigureCpuAsset> result;
    for(unsigned c=0;c+1<columns;++c)for(unsigned r=0;r+1<rows;++r) {
        const unsigned a=c*rows+r,b=a+rows;
        for(const auto face:{std::array<unsigned,3>{a,b,a+1},std::array<unsigned,3>{a+1,b,b+1}}) {
            auto key=face;std::sort(key.begin(),key.end());if(faces.count(key))continue;
            if(!result)result=std::make_shared<FigureCpuAsset>(*source);
            auto& g=*result;
            if(g.components.size()+6>65536 || (g.normals.size()+2)*4>65536)return source;
            const auto p=point(face[0]),q=point(face[1]),s=point(face[2]);
            std::array<float,3> u,v,n;
            for(unsigned k=0;k<3;++k){u[k]=q[k]-p[k];v[k]=s[k]-p[k];}
            n={u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]};
            const float length=std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
            if(!std::isfinite(length)||length<1e-8f)return source;
            for(unsigned side=0;side<2;++side) {
                NativePackedGroup normal{};
                for(unsigned k=0;k<4;++k){const float sign=side?-1.f:1.f;normal.x[k]=sign*n[0]/length;normal.y[k]=sign*n[1]/length;normal.z[k]=sign*n[2]/length;}
                const auto normalIndex=static_cast<std::uint16_t>(g.normals.size()*4);g.normals.push_back(normal);
                for(unsigned k=0;k<3;++k) {
                    const auto position=face[side?2-k:k];
                    unsigned nearest=position;
                    if(component[nearest]==UINT_MAX) {
                        float distance=INFINITY;
                        for(unsigned j=0;j<count;++j)if(component[j]!=UINT_MAX) {
                            const auto x=point(position),y=point(j);float d=0;for(unsigned axis=0;axis<3;++axis)d+=(x[axis]-y[axis])*(x[axis]-y[axis]);
                            if(d<distance){distance=d;nearest=j;}
                        }
                    }
                    if(component[nearest]==UINT_MAX)return source;
                    const auto uv=source->components[component[nearest]].texture;
                    g.indices.push_back(static_cast<std::uint16_t>(g.components.size()));
                    g.components.push_back({static_cast<std::uint16_t>(position),normalIndex,uv});
                }
            }
        }
    }
    if(!result)return source;
    result->normal_groups=static_cast<unsigned>(result->normals.size());
    result->identity.canonical_name+="/border-closed-grid-v1";
    return result;
}
}
