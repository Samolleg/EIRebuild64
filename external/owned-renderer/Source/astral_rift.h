#pragma once
#include "retained_static_scene.h"
#include <algorithm>
#include <cmath>

namespace owned {
// Exact semantic selection. Never infer the effect from purple texels or a
// shared texture: the same atlas may be used by unrelated static models.
inline bool astral_name(const std::string& a,const char* b) {
    if(a.size()!=std::char_traits<char>::length(b))return false;
    for(std::size_t i=0;i<a.size();++i) {
        const char c=a[i]>='A'&&a[i]<='Z'?a[i]+('a'-'A'):a[i];
        if(c!=b[i])return false;
    }
    return true;
}
inline bool astral_model(const VisualAssemblyAsset& a) {
    if(!a.skeleton)return false;
    for(const auto* name:{"samastrift01","samastrift01.mod","samastrift02","samastrift02.mod",
        "samastrift03","samastrift03.mod","samastrift04","samastrift04.mod",
        "samastrborder01","samastrborder01.mod"})
        if(astral_name(a.skeleton->visual_reference,name))return true;
    return false;
}
inline unsigned astral_part(const VisualAssemblyAsset& a,const VisualPartAsset& p) {
    if(!astral_model(a))return 0;
    if(astral_name(a.skeleton->visual_reference,"samastrborder01") ||
        astral_name(a.skeleton->visual_reference,"samastrborder01.mod"))
        return astral_name(p.name,"empty_boundaryveils")?1u:0u;
    const bool original=astral_name(a.skeleton->visual_reference,"samastrift01") ||
        astral_name(a.skeleton->visual_reference,"samastrift01.mod");
    if(!original)return astral_name(p.name,"empty_veils")?1u:0u;
    const char* names[]={"empty_veillow","empty_veilcrown","empty_veilcross","empty_veilwake"};
    for(unsigned i=0;i<4;++i)if(astral_name(p.name,names[i]))return i+1;
    return 0;
}
inline bool astral_clumps(const VisualAssemblyAsset& a,const VisualPartAsset& p) {
    return astral_model(a) && p.name.size()==15 && p.name.rfind("astral_clumps_",0)==0 &&
        p.name.back()>='1' && p.name.back()<='4';
}
inline bool astral_dynamic(const VisualAssemblyAsset& a,const VisualPartAsset& p) {
    return astral_part(a,p)!=0 || astral_clumps(a,p);
}
// 02--04 and the border put multiple disconnected ribbons into one FIG. Partition by
// triangle connectivity, not spatial proximity or UV (all ribbons reuse UV).
// 01 retains the exact original component order and emitter selection.
inline std::vector<std::vector<const NativeVertexComponent*>> astral_ribbons(
    const VisualPartAsset& part) {
    const auto& f=*part.geometry;
    std::vector<std::vector<const NativeVertexComponent*>> result;
    if(!astral_name(part.name,"empty_veils") && !astral_name(part.name,"empty_boundaryveils")) {
        result.emplace_back();for(const auto& c:f.components)result.back().push_back(&c);
        return result;
    }
    const std::size_t count=f.positions.size()*4;
    std::vector<std::size_t> parent(count);
    for(std::size_t i=0;i<count;++i)parent[i]=i;
    const auto root=[&](std::size_t i){
        while(parent[i]!=i){parent[i]=parent[parent[i]];i=parent[i];}return i;
    };
    for(std::size_t i=0;i+2<f.indices.size();i+=3) {
        std::size_t v[3];bool valid=true;
        for(unsigned k=0;k<3;++k){
            const auto c=f.indices[i+k];
            if(c>=f.components.size()||f.components[c].position>=count){valid=false;break;}
            v[k]=f.components[c].position;
        }
        if(valid){parent[root(v[1])]=root(v[0]);parent[root(v[2])]=root(v[0]);}
    }
    std::vector<std::size_t> group(count,count);
    for(const auto index:f.indices) {
        if(index>=f.components.size())continue;
        const auto& c=f.components[index];if(c.position>=count||c.texture>=f.texture_coordinates.size())continue;
        const auto r=root(c.position);
        if(group[r]==count){group[r]=result.size();result.emplace_back();}
        result[group[r]].push_back(&c);
    }
    return result;
}
// Built once with the immutable appearance, then serialized/uploaded like
// other parts. Lifetime, pose and removal are owned by that same appearance.
inline void add_astral_clumps(VisualAssemblyAsset& a) {
    if(!astral_model(a))return;
    for(const auto& p:a.parts)if(astral_clumps(a,p))return;
    const auto count=a.parts.size();
    for(std::size_t pi=0;pi<count;++pi) {
        const auto source=a.parts[pi]; const auto id=astral_part(a,source);
        if(!id||!source.geometry||source.geometry->shape_count!=1)continue;
        a.parts[pi].casts_shadow=false;
        const auto& f=*source.geometry;
        auto g=std::make_shared<FigureCpuAsset>();
        g->identity=f.identity;g->identity.canonical_name+="/astral-clumps-v1";
        g->signature=f.signature;g->shape_count=1;g->material_group=f.material_group;g->texture_number=f.texture_number;
        // Eight crossed clumps per connected ribbon. A clump has 8 vertices;
        // cap only malformed/pathological inputs before uint16 index overflow.
        const auto ribbons=astral_ribbons(source);
        for(std::size_t ribbon=0;ribbon<ribbons.size() && ribbon<1024;++ribbon)
        for(unsigned k=0;k<8;++k) {
            const float wanted=.0166015625f+(.4833984375f-.0166015625f)*(k+1)/9.f;
            const NativeVertexComponent* chosen=nullptr;float best=1e30f;
            for(const auto* candidate:ribbons[ribbon]) {
                const auto& c=*candidate;
                if(c.texture>=f.texture_coordinates.size()||c.position/4>=f.positions.size())continue;
                const auto uv=f.texture_coordinates[c.texture];
                const float score=std::abs(uv.u-wanted)+.2f*std::abs(uv.v-(k%2?.5166015625f:.9833984375f));
                if(score<best){best=score;chosen=&c;}
            }
            if(!chosen)continue;
            const auto& packed=f.positions[chosen->position/4];const auto lane=chosen->position%4;
            const float center[]={packed.x[lane],packed.y[lane],packed.z[lane]};
            for(unsigned plane=0;plane<2;++plane) {
                const auto start=static_cast<std::uint16_t>(g->components.size());
                NativePackedGroup p{},n{};
                for(unsigned v=0;v<4;++v) {
                    const float x=v==1||v==2?1.f:0.f,y=v>=2?1.f:0.f;
                    p.x[v]=center[0]+(x-.5f)*1.4f;p.y[v]=center[1]+(plane==0?(y-.5f)*.6f:0);
                    p.z[v]=center[2]+(plane==1?(y-.5f)*.6f:0);p.w[v]=1;n.z[v]=1;
                    const auto ix=static_cast<std::uint16_t>(start+v);
                    g->components.push_back({ix,ix,ix});
                    g->texture_coordinates.push_back({float((ribbon*8+k)*2)+x,y});
                    g->shape_position_radii[0]=(std::max)(g->shape_position_radii[0],std::sqrt(p.x[v]*p.x[v]+p.y[v]*p.y[v]+p.z[v]*p.z[v]));
                }
                g->positions.push_back(p);g->normals.push_back(n);
                for(unsigned v:{0u,1u,2u,0u,2u,3u,2u,1u,0u,3u,2u,0u})g->indices.push_back(static_cast<std::uint16_t>(start+v));
            }
        }
        if(g->indices.empty())continue;
        g->position_groups=static_cast<unsigned>(g->positions.size());g->normal_groups=g->position_groups;
        g->bounds=f.bounds;
        VisualPartAsset p=source;p.name="astral_clumps_"+std::to_string(id);p.geometry=std::move(g);p.casts_shadow=false;
        a.parts.push_back(std::move(p));
    }
}
} // namespace owned
