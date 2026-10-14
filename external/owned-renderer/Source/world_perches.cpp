// Algorithms shared by the retail bridge and the embedded renderer.
#include "world_scene_algorithms.h"
#include "owned_terrain_front_end.h"
#include "owned_terrain_decode.h"
#include "retained_static_scene.h"
#include "retained_static_submission.h"
#include "ground_cover.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace owned {
// One FIG vertex (component) at a complection, posed by its node: model space.
static std::array<double,3> perch_model_point(const FigureCpuAsset& figure,const VisualNodeSample& node,
    const std::array<float,3>& shape,std::uint32_t component) {
    const std::uint32_t index=figure.components[component].position;
    double local[3]{};
    for(std::uint32_t endpoint=0;endpoint<figure.shape_count;++endpoint) {
        double weight=1;
        if(figure.shape_count==8)for(unsigned axis=0;axis<3;++axis)
            weight*=(endpoint&(1u<<axis))?shape[axis]:1.0-shape[axis];
        const auto& group=figure.positions[std::size_t(endpoint)*figure.position_groups+index/4];
        local[0]+=weight*group.x[index%4];local[1]+=weight*group.y[index%4];local[2]+=weight*group.z[index%4];
    }
    if(index<node.morphs.size())for(unsigned axis=0;axis<3;++axis)local[axis]+=node.morphs[index][axis];
    const double w=node.rotation[0],qx=node.rotation[1],qy=node.rotation[2],qz=node.rotation[3];
    const double tx=2*(qy*local[2]-qz*local[1]),ty=2*(qz*local[0]-qx*local[2]),tz=2*(qx*local[1]-qy*local[0]);
    return {local[0]+w*tx+(qy*tz-qz*ty)+node.translation[0],local[1]+w*ty+(qz*tx-qx*tz)+node.translation[1],
            local[2]+w*tz+(qx*ty-qy*tx)+node.translation[2]};
}
static bool perch_figure_valid(const FigureCpuAsset& figure) {
    if((figure.shape_count!=1&&figure.shape_count!=8)||
       figure.positions.size()!=std::size_t(figure.position_groups)*figure.shape_count)return false;
    for(const auto& c:figure.components)
        if(c.position>=figure.position_groups*4u||c.texture>=figure.texture_coordinates.size()||
           c.normal>=figure.normal_groups*4u)return false;
    for(const auto i:figure.indices)if(i>=figure.components.size())return false;
    return true;
}
// A DXT texel's colour is a branch's (brown: red over green and blue), not a
// leaf's (green) or snow's (white). Packed formats carry no such evidence here.
static bool perch_branch_texel(const TextureCpuAsset& texture,std::uint32_t x,std::uint32_t y) {
    if((texture.format!=MmpFormat::dxt1&&texture.format!=MmpFormat::dxt3)||texture.mips.empty()||!texture.source)return false;
    const auto& mip=texture.mips[0];
    const std::uint32_t block=texture.format==MmpFormat::dxt1?8u:16u;
    const std::size_t offset=mip.byte_offset+(std::size_t(y/4)*((mip.width+3)/4)+x/4)*block+(block-8);
    if(x>=mip.width||y>=mip.height||offset+8>texture.source->size()||offset+8>mip.byte_offset+mip.bytes)return false;
    const std::uint8_t* b=texture.source->data()+offset;
    const unsigned c0=b[0]|(b[1]<<8),c1=b[2]|(b[3]<<8),sel=(b[4+(y%4)]>>(2*(x%4)))&3u;
    const auto channel=[](unsigned c,int k){return k==0?((c>>11)&31)*255/31:k==1?((c>>5)&63)*255/63:(c&31)*255/31;};
    int rgb[3];
    for(int k=0;k<3;++k) {
        const int a=int(channel(c0,k)),e=int(channel(c1,k));
        rgb[k]=sel==0?a:sel==1?e:c0>c1?(sel==2?(2*a+e)/3:(a+2*e)/3):(sel==2?(a+e)/2:0);
    }
    return rgb[0]>rgb[1]+6&&rgb[0]>rgb[2]+12;
}
// A model's perches as triangle samples of named parts (the 45 interior
// barycentric samples of the foliage classification; -1 the centroid).
static void perch_barycentric(int sample,double& a,double& b) {
    a=b=1.0/3;
    if(sample<0)return;
    for(unsigned i=0;i<9;++i)for(unsigned j=0;j<9-i;++j)if(sample--==0){a=(i+.5)/9.5;b=(j+.5)/9.5;return;}
}
// Chosen once per model (drawn part set and textures), at the first
// complection endpoint and the rest pose.
// Parts drawn from a completed proxy surface carry its alpha facts but no
// pixels; their branch colour is read from the shared tree atlas (tree02, the
// map's ground-cover bark art), whose layout the tree textures share.
std::vector<PerchRef> choose_model_perches(const RetainedVisualSample& sample,bool cactus,const TextureCpuAsset* atlas) {
    const auto& assembly=*sample.assembly;
    std::vector<PerchRef> refs;
    std::vector<VisualNodeSample> nodes;
    const std::array<float,3> rest_shape{};
    if(!sample_visual_skeleton(*assembly.skeleton,sample.selection,rest_shape,assembly.center_shift,0,nodes,true))return refs;
    std::vector<critters::PerchCandidate> candidates;
    std::vector<PerchRef> sources;
    bool solid=true;   // no cut-out texel on any drawn triangle: a stump or a log
    const auto texel=[](double coordinate,std::uint32_t size) {
        const double wrapped=coordinate-std::floor(coordinate);   // WRAP addressing
        return (std::min)(static_cast<std::uint32_t>(wrapped*size),size-1);
    };
    for(const auto& part:assembly.parts) {
        if(!part.geometry||!(part.texture||part.surface)||part.skeleton_node>=nodes.size()||!perch_figure_valid(*part.geometry))continue;
        const auto& figure=*part.geometry;const auto& node=nodes[part.skeleton_node];
        TextureAlphaPixels alpha;
        const bool readable=part.texture?texture_alpha_pixels(*part.texture,0,alpha):visual_surface_alpha_pixels(*part.surface,alpha);
        if(!readable||!alpha.width||!alpha.height)continue;
        const TextureCpuAsset* colour=part.texture?part.texture.get():atlas;
        const std::uint32_t colour_w=colour&&!colour->mips.empty()?colour->mips[0].width:0,colour_h=colour&&!colour->mips.empty()?colour->mips[0].height:0;
        std::vector<std::array<double,3>> points(figure.components.size());
        float top=-INFINITY;
        for(std::uint32_t c=0;c<figure.components.size();++c){points[c]=perch_model_point(figure,node,rest_shape,c);top=(std::max)(top,float(points[c][2]));}
        for(std::size_t t=0;t+2<figure.indices.size();t+=3) {
            const std::uint16_t k[3]{figure.indices[t],figure.indices[t+1],figure.indices[t+2]};
            NativeTextureCoordinate uv[3];
            for(int v=0;v<3;++v)uv[v]=figure.texture_coordinates[figure.components[k[v]].texture];
            bool cut=false;int best=-1,branch=-1;double best_z=-INFINITY,branch_z=-INFINITY;
            int s=-1;
            for(unsigned i=0;i<9;++i)for(unsigned j=0;j<9-i;++j) {
                ++s;
                const double a=(i+.5)/9.5,b=(j+.5)/9.5,c=1-a-b;
                const std::uint32_t x=texel(a*uv[0].u+b*uv[1].u+c*uv[2].u,alpha.width),y=texel(a*uv[0].v+b*uv[1].v+c*uv[2].v,alpha.height);
                if(texture_alpha_texel_cut(alpha,x,y)){cut=true;continue;}
                const double z=a*points[k[0]][2]+b*points[k[1]][2]+c*points[k[2]][2];
                if(z>best_z){best_z=z;best=s;}
                if(z>branch_z&&colour_w&&colour_h&&perch_branch_texel(*colour,texel(a*uv[0].u+b*uv[1].u+c*uv[2].u,colour_w),
                    texel(a*uv[0].v+b*uv[1].v+c*uv[2].v,colour_h))){branch_z=z;branch=s;}
            }
            if(cut)solid=false;
            critters::PerchCandidate candidate;candidate.part_top=top;
            int chosen=-1;
            if(!cut) {
                double n[3]{};
                for(int v=0;v<3;++v){const auto idx=figure.components[k[v]].normal;const auto& g=figure.normals[idx/4];
                    n[0]+=g.x[idx%4];n[1]+=g.y[idx%4];n[2]+=g.z[idx%4];}
                // the normal turns with its node as the position does
                const double w=node.rotation[0],qx=node.rotation[1],qy=node.rotation[2],qz=node.rotation[3];
                const double tx=2*(qy*n[2]-qz*n[1]),ty=2*(qz*n[0]-qx*n[2]),tz=2*(qx*n[1]-qy*n[0]);
                const double r[3]{n[0]+w*tx+(qy*tz-qz*ty),n[1]+w*ty+(qz*tx-qx*tz),n[2]+w*tz+(qx*ty-qy*tx)};
                const double l=std::sqrt(r[0]*r[0]+r[1]*r[1]+r[2]*r[2]);
                candidate.bark=true;candidate.up=l>0&&r[2]/l>.6;
            } else if(branch>=0){chosen=branch;candidate.bark=candidate.up=true;}
            else if(best>=0)chosen=best;
            else continue;   // a fully cut-out sliver
            double a,b;perch_barycentric(chosen,a,b);const double c=1-a-b;
            for(int axis=0;axis<3;++axis)candidate.p[axis]=float(a*points[k[0]][axis]+b*points[k[1]][axis]+c*points[k[2]][axis]);
            candidates.push_back(candidate);
            sources.push_back({part.name,std::uint32_t(t/3),chosen,critters::perch_fill});
        }
    }
    const auto picks=critters::select_perches(candidates,cactus,solid&&!cactus);
    std::string line=assembly.skeleton->visual_reference+(assembly.parts.empty()?std::string():" "+
        (assembly.parts[0].texture?assembly.parts[0].texture_source_name:std::string(atlas?"surface+tree02":"surface")))+
        (solid&&!cactus?" solid":"")+" candidates="+std::to_string(candidates.size())+":";
    for(const auto& pick:picks) {
        refs.push_back(sources[pick.index]);refs.back().kind=pick.kind;
        const auto& p=candidates[pick.index].p;
        char text[128];
        std::snprintf(text,sizeof(text)," %s %s#%u@%d (%.3f %.3f %.3f);",
            pick.kind==critters::perch_branch?"branch":pick.kind==critters::perch_edge?"edge":"fill",
            refs.back().part.c_str(),refs.back().triangle,refs.back().sample,p[0],p[1],p[2]);
        line+=text;
    }
    bridge_append_probe_event("critter_perches",line.c_str());   // once per model: what was picked
    return refs;
}
// This object's perches in the world, at its own pose and complection.
bool place_perches(const RetainedVisualSample& sample,const std::vector<PerchRef>& refs,std::vector<std::array<float,4>>& output, std::uint64_t* preparation_count) {
    output.clear();
    if(!prepare_world_visual_pose(sample, preparation_count))return false;
    const auto& assembly=*sample.assembly;
    const auto& root=sample.prepared->gpu_pose.root_to_world;
    // Perches are chosen on surfaces facing up in model space. Yaw and scale
    // carry over exactly; an object placed tilted over 25 degrees would put
    // them on its sides, so it gets none.
    const double up_x=root[2],up_y=root[6],up_z=root[10];
    const double up_length=std::sqrt(up_x*up_x+up_y*up_y+up_z*up_z);
    if(!(up_length>0)||up_z<std::cos(25*3.14159265358979/180)*up_length)return true;
    const auto& shape=retained_visual_shape(sample);
    std::vector<VisualNodeSample> nodes;
    if(!sample_visual_skeleton(*assembly.skeleton,sample.selection,shape,assembly.center_shift,0,nodes,true))return false;
    for(const auto& ref:refs) {
        const VisualPartAsset* part=nullptr;
        for(const auto& candidate:assembly.parts)if(candidate.name==ref.part){part=&candidate;break;}
        if(!part||!part->geometry||part->skeleton_node>=nodes.size()||!retained_visual_part_visible(sample,*part,false)||
           !(retained_visual_effective_alpha(sample,*part)>0)||!perch_figure_valid(*part->geometry))continue;
        const auto& figure=*part->geometry;
        if(std::size_t(ref.triangle)*3+2>=figure.indices.size())continue;
        double a,b;perch_barycentric(ref.sample,a,b);const double weights[3]{a,b,1-a-b};
        double model[3]{};
        for(int v=0;v<3;++v) {
            const auto p=perch_model_point(figure,nodes[part->skeleton_node],shape,figure.indices[std::size_t(ref.triangle)*3+v]);
            for(int axis=0;axis<3;++axis)model[axis]+=weights[v]*p[axis];
        }
        std::array<float,4> world{};
        for(unsigned row=0;row<3;++row)
            world[row]=float(root[row*4]*model[0]+root[row*4+1]*model[1]+root[row*4+2]*model[2]+root[row*4+3]);
        if(!std::isfinite(world[0])||!std::isfinite(world[1])||!std::isfinite(world[2]))continue;
        world[3]=float(ref.kind);
        output.push_back(world);
    }
    return true;
}

} // namespace owned
