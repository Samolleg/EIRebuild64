// The bridge's scene half: pure consumers of the prepared world frame the
// renderer pulls (liquid ripples, ground cover units, corpses and object
// contacts, critter obstacles and perches). Reads only the frame copies
// (owned_world_frame.h); host-buildable.
#include "owned_world_frame.h"
#include "world_scene_algorithms.h"
#include "embedded_perf06.h"
#include "critters.h"
#include "hosted_plants.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <future>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace owned {


// Snow trail identity: the visual incarnation (never an address) the
// footprint markers record as their creator, so live rows and prints join.
#if !defined(EID3D11_EMBEDDED)
bool bridge_ground_cover_units(float center_x,float center_y,float radius,
    std::vector<std::array<float,4>>& output,std::vector<GroundUnitDetail>* details,std::vector<GroundUnitTrail>* trails) noexcept {
    output.clear();
    if(details)details->clear();
    if(trails)trails->clear();
    const auto& visual=g_owned_visual_world_frame;
    const auto& terrain=g_owned_terrain_frame;
    if(!visual.active||!visual.values||!terrain.world_serial||!terrain.source_values.terrain)return false;
    build_ground_cover_units(*visual.values,*terrain.source_values.terrain,center_x,center_y,radius,
        visual.draw_undetected_units,output,details,trails);
    return true;
}

void bridge_ground_cover_corpses(float center_x,float center_y,float radius,
    std::vector<ground_cover::FootprintSplat>& output) noexcept {
    const auto& visual=g_owned_visual_world_frame;
    const auto& terrain=g_owned_terrain_frame;
    if(!visual.active||!visual.values||!terrain.world_serial||!terrain.source_values.terrain)return;
    build_ground_cover_corpses(*visual.values,*terrain.source_values.terrain,center_x,center_y,radius,
        visual.draw_undetected_units,output);
}


bool bridge_liquid_ripples(NativeLiquidRipples& output) noexcept {
    output={};
    const auto& visual=g_owned_visual_world_frame;
    const auto& terrain=g_owned_terrain_frame;
    if(!visual.active||!visual.values||!terrain.world_serial||
       !terrain.source_values.terrain||!terrain.source_values.geometry)return false;
    build_liquid_ripples(*visual.values,*terrain.source_values.terrain,*terrain.source_values.geometry,
        visual.draw_undetected_units,output);
    output.reach[0]=terrain.source_values.geometry->animation.maximum_wind_multiplier;
    return true;
}

bool bridge_ambient_world_source(AmbientWorldSource& output) noexcept {
    output={};
    // World composition runs after the outer draw releases its frame borrows.
    // Use the retained map owner and its completed geometry publication; map reset
    // retires these together without extending any draw-frame lifetime.
    const auto source=retained_visual_scene().terrain_source_snapshot();
    if(!source.terrain||!source.geometry)return false;
    const auto& animation=source.geometry->animation;
    output.wind_direction[0]=animation.wind_direction[0];output.wind_direction[1]=animation.wind_direction[1];
    output.wind_force_normalized=animation.wind_force_normalized;
    output.terrain=source.terrain;output.map=source.map;output.geometry=source.geometry;
    return true;
}

bool bridge_critter_obstacles(float x,float y,float radius,
    std::vector<std::array<float,6>>& output) noexcept {
    output.clear();
    const auto& visual=g_owned_visual_world_frame;
    return visual.active&&visual.values&&build_critter_obstacles(*visual.values,x,y,radius,output);
}
#endif // !EID3D11_EMBEDDED
namespace {
// One FIG vertex (component) at a complection, posed by its node: model space.
std::array<double,3> perch_model_point(const FigureCpuAsset& figure,const VisualNodeSample& node,
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
bool perch_figure_valid(const FigureCpuAsset& figure) {
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
bool perch_branch_texel(const TextureCpuAsset& texture,std::uint32_t x,std::uint32_t y) {
    if((texture.format!=MmpFormat::dxt1&&texture.format!=MmpFormat::dxt3&&texture.format!=MmpFormat::dxt5)||texture.mips.empty()||!texture.source)return false;
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
struct PerchRef { std::string part; std::uint32_t triangle=0; int sample=-1; critters::PerchKind kind=critters::perch_fill; };
void perch_barycentric(int sample,double& a,double& b) {
    a=b=1.0/3;
    if(sample<0)return;
    for(unsigned i=0;i<9;++i)for(unsigned j=0;j<9-i;++j)if(sample--==0){a=(i+.5)/9.5;b=(j+.5)/9.5;return;}
}
// Chosen once per model (drawn part set and textures), at the first
// complection endpoint and the rest pose.
// Parts drawn from a completed proxy surface carry its alpha facts but no
// pixels; their branch colour is read from the shared tree atlas (tree02, the
// map's ground-cover bark art), whose layout the tree textures share.
std::vector<PerchRef> choose_model_perches_c56(const RetainedVisualSample& sample,bool cactus,const TextureCpuAsset* atlas) {
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
bool place_perches_c56(const RetainedVisualSample& sample,const std::vector<PerchRef>& refs,std::vector<std::array<float,4>>& output) {
    output.clear();
    if(!prepare_owned_visual_pose(sample))return false;
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

// Perches chosen per model (perch_model_key), kept for the world
// (bridge_release_world_caches empties it).
std::unordered_map<std::string,std::vector<PerchRef>> g_perch_models;

const TextureCpuAsset* perch_tree_atlas() noexcept {
    AmbientWorldSource source;
    if(bridge_ambient_world_source(source)&&source.map) {
        const auto found=source.map->textures.find("tree02");
        if(found!=source.map->textures.end()&&found->second&&found->second->source)return found->second.get();
    }
    return nullptr;
}

// A tree or cactus sample whose perches can be chosen (bridge_critter_perches).
bool perch_sample(const RetainedVisualSample& sample,bool& cactus) noexcept {
    if(sample.values.unit||sample.values.hidden||(sample.values.object_flags&0x40u)||
       sample.status!=RetainedVisualStatus::ready||!sample.height_valid||!sample.assembly||
       !sample.assembly->skeleton||!sample.prepared)return false;
    const char* model=sample.assembly->skeleton->visual_reference.c_str();
    cactus=critters::cactus_model(model);
    return cactus||ambient::tree_kind(model)!=ambient::tree_none;
}

std::string perch_model_key(const RetainedVisualSample& sample,bool atlas) {
    std::string model_key=sample.assembly->skeleton->visual_reference;
    for(const auto& part:sample.assembly->parts)model_key+='|'+part.name+':'+part.texture_source_name;
    if(atlas)model_key+="|atlas";
    return model_key;
}
} // namespace

bool bridge_prepare_critter_perch_models(unsigned budget_us) noexcept try {
    const auto& visual=g_owned_visual_world_frame;
    if(!visual.active||!visual.values)return true;
    const TextureCpuAsset* atlas=perch_tree_atlas();
    const auto start=std::chrono::steady_clock::now();
    for(const auto& sample:visual.values->entities) {
        bool cactus=false;
        if(!perch_sample(sample,cactus))continue;
        std::string key=perch_model_key(sample,atlas!=nullptr);
        if(g_perch_models.count(key))continue;
        if(std::chrono::steady_clock::now()-start>=std::chrono::microseconds(budget_us))return false;
        g_perch_models.emplace(std::move(key),choose_model_perches_c56(sample,cactus,atlas));
    }
    return true;
} catch(...) {return true;}

#if !defined(EID3D11_EMBEDDED)
bool bridge_critter_perches(float x,float y,float radius,std::vector<CritterPerchTree>& output) noexcept try {
    output.clear();
    const auto& visual=g_owned_visual_world_frame;
    if(!visual.active||!visual.values)return false;
    auto& models=g_perch_models;
    const TextureCpuAsset* atlas=perch_tree_atlas();
    // Perches are placed once per object placement; a burst spreads over calls.
    const auto start=std::chrono::steady_clock::now();
    bool building=true;
    for(const auto& sample:visual.values->entities) {
        bool cactus=false;
        if(!perch_sample(sample,cactus))continue;
        NativeWorldBounds b{};
        if(!prepare_owned_visual_world_bounds(sample,b))continue;
        if(b.maximum[0]<x-radius||b.minimum[0]>x+radius||b.maximum[1]<y-radius||b.minimum[1]>y+radius)continue;
        auto& prepared=*sample.prepared;
        const double still=0;
        if(!prepared.perches_valid||!owned_visual_pose_key_matches(prepared.perches_key,sample,&still)) {
            if(!building)continue;
            auto key=owned_visual_pose_key(sample);key.phase=0;
            prepared.perches_valid=false;
            const std::string model_key=perch_model_key(sample,atlas!=nullptr);
            auto found=models.find(model_key);
            if(found==models.end())found=models.emplace(model_key,choose_model_perches_c56(sample,cactus,atlas)).first;
            const bool placed=place_perches_c56(sample,found->second,prepared.perches);
            building=std::chrono::steady_clock::now()-start<std::chrono::microseconds(1500);
            if(!placed)continue;
            prepared.perches_key=key;prepared.perches_valid=true;
        }
        if(prepared.perches.empty())continue;
        CritterPerchTree tree;
        const auto& root=prepared.gpu_pose.root_to_world;
        tree.x=root[3];tree.y=root[7];tree.top=b.maximum[2];tree.cactus=cactus;
        for(const auto& p:prepared.perches) {
            if(tree.count>=6)break;
            for(int axis=0;axis<3;++axis)tree.points[tree.count][axis]=p[axis];
            ++tree.count;
        }
        output.push_back(tree);
    }
    return true;
} catch(...) {output.clear();return false;}
#endif // !EID3D11_EMBEDDED

namespace {
// Ground-cover object contacts (#164). The store holds every eligible non-unit
// scene object, filed in a 16 m XY grid by its contact rectangles (or, until
// those are built, its envelope). It changes only through the scene's own
// per-ID change journal (consumer ground_contacts: values, status, lifecycle
// add/remove) and is refilled whole on a journal reset, a static terrain
// change or a new terrain source. Contacts are built once per placement,
// nearest the eye first, under kGroundContactBudgetMs a frame; while the load
// reveal hold covers the world every one in the window is built, so the
// revealed frame has them all. An object not yet built has no footprint
// (grass shows through it).
// The answer for a window is the rectangles of the grid cells it covers. It
// is kept and handed out again while those cells and the store are unchanged
// (825f42fe measured the per-frame read of every in-window object's contacts
// at the cost of the former walk: 0.23 ms median on Suslanger).
constexpr float kGroundContactCell = 16.f;
constexpr double kGroundContactBudgetMs = .3;
struct GroundContactStore {
    struct Entry {
        LifecycleIdentity lifecycle{};
        VisualResourceIncarnation resource{};
        std::shared_ptr<VisualPreparedResources> prepared; // holds the built contacts
        std::array<float,4> box{}; // XY min/max filed in the grid
        bool built = false, bounded = true;
        std::uint64_t seen = 0;
    };
    std::unordered_map<std::int32_t,Entry> entries;
    std::unordered_map<std::int64_t,std::vector<std::int32_t>> cells;
    std::vector<std::int32_t> unbounded; // no envelope: offered to every window
    std::vector<float> sector_top;
    std::uint64_t terrain_generation = 0, terrain_revision = 0, stamp = 0;
    std::uint64_t revision = 1; // any filing change
    std::vector<ground_cover::FootprintSplat> answer;
    std::array<std::int32_t,4> answer_cells{};
    std::uint64_t answer_revision = 0; // 0: no complete answer kept
    bool valid = false;
    void clear() {
        entries.clear();cells.clear();unbounded.clear();answer.clear();
        answer_revision=0;valid=false;++revision;
    }
};
// Never destroyed: it holds scene resources whose owners are gone at static
// destruction. Released with the cover's world (bridge_release_ground_contacts).
GroundContactStore& g_ground_contacts=*new GroundContactStore;

std::int64_t ground_contact_cell_key(std::int32_t x,std::int32_t y) noexcept {
    return (std::int64_t(x)<<32)|std::uint32_t(y);
}
std::array<std::int32_t,4> ground_contact_cell_range(const std::array<float,4>& box) noexcept {
    return {std::int32_t(std::floor(box[0]/kGroundContactCell)),std::int32_t(std::floor(box[1]/kGroundContactCell)),
            std::int32_t(std::floor(box[2]/kGroundContactCell)),std::int32_t(std::floor(box[3]/kGroundContactCell))};
}
template<class F> void ground_contact_cells(const std::array<std::int32_t,4>& range,F&& visit) {
    for(auto cy=range[1];cy<=range[3];++cy)for(auto cx=range[0];cx<=range[2];++cx)visit(ground_contact_cell_key(cx,cy));
}
std::array<float,4> ground_contact_box(const std::vector<std::array<float,4>>& rectangles) noexcept {
    std::array<float,4> box{INFINITY,INFINITY,-INFINITY,-INFINITY};
    for(const auto& r:rectangles) {
        box[0]=std::min(box[0],r[0]-r[2]);box[1]=std::min(box[1],r[1]-r[3]);
        box[2]=std::max(box[2],r[0]+r[2]);box[3]=std::max(box[3],r[1]+r[3]);
    }
    return box;
}
void ground_contact_unfile(GroundContactStore& store,std::int32_t id,const GroundContactStore::Entry& entry) {
    ++store.revision;
    if(!entry.bounded) {
        store.unbounded.erase(std::remove(store.unbounded.begin(),store.unbounded.end(),id),store.unbounded.end());
        return;
    }
    if(!(entry.box[0]<=entry.box[2]))return; // built with no contacts: never filed
    ground_contact_cells(ground_contact_cell_range(entry.box),[&](std::int64_t key){
        const auto cell=store.cells.find(key);
        if(cell==store.cells.end())return;
        auto& ids=cell->second;
        ids.erase(std::remove(ids.begin(),ids.end(),id),ids.end());
        if(ids.empty())store.cells.erase(cell);
    });
}
void ground_contact_file(GroundContactStore& store,std::int32_t id,const GroundContactStore::Entry& entry) {
    ++store.revision;
    if(!entry.bounded){store.unbounded.push_back(id);return;}
    if(!(entry.box[0]<=entry.box[2]))return;
    ground_contact_cells(ground_contact_cell_range(entry.box),[&](std::int64_t key){store.cells[key].push_back(id);});
}
bool ground_contacts_current(const RetainedVisualSample& sample,const RetainedTerrainSourceSnapshot& source) {
    const auto& prepared=*sample.prepared;
    const double still=0;
    return prepared.ground_contacts_valid&&owned_visual_pose_key_matches(prepared.ground_contacts_key,sample,&still)&&
        prepared.ground_contacts_terrain_generation==source.source_generation&&
        prepared.ground_contacts_terrain_revision==source.source_revision;
}
// The frame's samples stay in signed-ID order with one sample per ID
// (RetainedStaticScene::visual_frame_unlocked).
const RetainedVisualSample* ground_contact_sample(const std::vector<RetainedVisualSample>& entities,std::int32_t id) noexcept {
    const auto found=std::lower_bound(entities.begin(),entities.end(),id,
        [](const RetainedVisualSample& sample,std::int32_t value){return sample.resource.signed_id<value;});
    return found!=entities.end()&&found->resource.signed_id==id ? &*found : nullptr;
}
// One sample's entry under the exact rule of the former per-frame walk;
// false: not a contact source.
bool ground_contact_entry(const RetainedVisualSample& sample,const RetainedTerrainSourceSnapshot& source,
    GroundContactStore::Entry& entry) {
    if(sample.values.unit||sample.values.hidden||(sample.values.object_flags&0x40u)||
       sample.status!=RetainedVisualStatus::ready||!sample.height_valid||!sample.assembly||
       !sample.assembly->skeleton||!sample.prepared)return false;
    entry={};
    entry.lifecycle=sample.lifecycle;entry.resource=sample.resource;entry.prepared=sample.prepared;
    if(ground_contacts_current(sample,source)) {
        entry.built=true;entry.box=ground_contact_box(sample.prepared->ground_contacts);
    } else {
        // The published entity envelope contains every part envelope.
        NativeWorldBounds envelope{};
        if(prepare_owned_visual_world_bounds(sample,envelope)&&
           std::isfinite(envelope.minimum[0])&&std::isfinite(envelope.minimum[1])&&
           std::isfinite(envelope.maximum[0])&&std::isfinite(envelope.maximum[1]))
            entry.box={envelope.minimum[0],envelope.minimum[1],envelope.maximum[0],envelope.maximum[1]};
        else entry.bounded=false;
    }
    return true;
}
bool same_ground_contact_entry(const GroundContactStore::Entry& left,const GroundContactStore::Entry& right) noexcept {
    return same_lifecycle_identity(left.lifecycle,right.lifecycle)&&same_visual_resource(left.resource,right.resource)&&
        left.prepared==right.prepared&&left.built==right.built&&left.bounded==right.bounded&&left.box==right.box;
}
} // namespace

#if !defined(EID3D11_EMBEDDED)
void bridge_release_ground_contacts() noexcept {
    g_ground_contacts.clear();
    std::vector<float>().swap(g_ground_contacts.sector_top);
}

bool bridge_ground_cover_objects(float x,float y,float radius,
    std::vector<ground_cover::FootprintSplat>& output) noexcept try {
    output.clear();
    const auto& visual=g_owned_visual_world_frame;
    const auto& terrain=g_owned_terrain_frame;
    if(!visual.active||!visual.values||!terrain.world_serial||!terrain.source_values.terrain)return false;
    const auto& source=terrain.source_values;
    const auto& ground=*source.terrain;
    const auto& entities=visual.values->entities;
    auto& store=g_ground_contacts;
    auto& scene=retained_visual_scene();
    const auto changes=scene.visual_source_changes(RetainedShadowConsumer::ground_contacts);
    const bool full=!store.valid||changes.full||(changes.terrain_dirty&&!changes.terrain_time_only)||
        store.terrain_generation!=source.source_generation||
        store.terrain_revision!=source.source_revision;
    if(full) {
        store.clear();
        store.sector_top.assign(std::size_t(ground.sector_count_x)*ground.sector_count_y,-INFINITY);
        for(const auto& sector:ground.sectors)
            if(sector.map_x<ground.sector_count_x&&sector.map_y<ground.sector_count_y) {
                auto& value=store.sector_top[std::size_t(sector.map_y)*ground.sector_count_x+sector.map_x];
                value=std::max(value,sector.bounds.maximum[2]);
            }
        store.terrain_generation=source.source_generation;store.terrain_revision=source.source_revision;
        GroundContactStore::Entry entry;
        for(const auto& sample:entities)
            if(ground_contact_entry(sample,source,entry)) {
                ground_contact_file(store,sample.resource.signed_id,entry);
                store.entries[sample.resource.signed_id]=std::move(entry);
            }
        store.valid=true;
    } else {
        GroundContactStore::Entry entry;
        for(const auto id:changes.dirty_visuals) {
            const auto* sample=ground_contact_sample(entities,id);
            const bool source_now=sample&&ground_contact_entry(*sample,source,entry);
            const auto found=store.entries.find(id);
            // An unchanged entry (an animated prop's phase, a flag that does
            // not concern cover) keeps its filing and the kept answer.
            if(found!=store.entries.end()&&source_now&&same_ground_contact_entry(found->second,entry))continue;
            if(found!=store.entries.end()){ground_contact_unfile(store,id,found->second);store.entries.erase(found);}
            if(source_now){ground_contact_file(store,id,entry);store.entries[id]=std::move(entry);}
        }
    }
    // The scene thread's frame may predate the newest changes: they stay
    // unacknowledged and are read again next frame.
    scene.acknowledge_shadow_source_changes(RetainedShadowConsumer::ground_contacts,
        owned_scene_ack_revision(changes.revision));

    // The cells the cover window touches; the answer covers them whole.
    const auto range=ground_contact_cell_range({x-radius,y-radius,x+radius,y+radius});
    if(store.answer_revision==store.revision&&store.answer_cells==range) {
        output=store.answer;
        return true;
    }
    const std::array<float,4> area{range[0]*kGroundContactCell,range[1]*kGroundContactCell,
        (range[2]+1)*kGroundContactCell,(range[3]+1)*kGroundContactCell};
    const auto overlaps=[&](const std::array<float,4>& box){
        return !(box[2]<area[0]||box[0]>area[2]||box[3]<area[1]||box[1]>area[3]);
    };
    const auto emit=[&](const GroundContactStore::Entry& entry){
        for(const auto& rectangle:entry.prepared->ground_contacts) {
            if(rectangle[0]+rectangle[2]<area[0]||rectangle[0]-rectangle[2]>area[2]||
               rectangle[1]+rectangle[3]<area[1]||rectangle[1]-rectangle[3]>area[3])continue;
            output.push_back(ground_cover::object_splat(rectangle));
        }
    };
    static std::vector<std::pair<float,std::int32_t>> pending;
    pending.clear();
    ++store.stamp;
    const auto offer=[&](std::int32_t id){
        auto& entry=store.entries.at(id);
        if(entry.seen==store.stamp)return;
        entry.seen=store.stamp;
        if(entry.built){emit(entry);return;}
        if(entry.bounded&&!overlaps(entry.box))return;
        const float dx=.5f*(entry.box[0]+entry.box[2])-x,dy=.5f*(entry.box[1]+entry.box[3])-y;
        pending.push_back({entry.bounded?dx*dx+dy*dy:0.f,id});
    };
    ground_contact_cells(range,[&](std::int64_t key){
        const auto cell=store.cells.find(key);
        if(cell!=store.cells.end())for(const auto id:cell->second)offer(id);
    });
    for(const auto id:store.unbounded)offer(id);
    // Builds re-file their entry, so they run after the grid walk.
    std::sort(pending.begin(),pending.end());
    const bool held=load_reveal_held();
    const auto start=frame_acct::now();
    const auto budget=static_cast<std::int64_t>(kGroundContactBudgetMs/frame_acct::ms_per_tick());
    std::size_t done=0;
    for(;done<pending.size();++done) {
        if(!held&&frame_acct::now()-start>=budget)break;
        const auto id=pending[done].second;
        auto& entry=store.entries.at(id);
        const auto* sample=ground_contact_sample(entities,id);
        // A sample of another incarnation is the journal's next change.
        if(!sample||!same_lifecycle_identity(sample->lifecycle,entry.lifecycle)||
           !same_visual_resource(sample->resource,entry.resource)||sample->prepared!=entry.prepared)continue;
        auto& prepared=*entry.prepared;
        auto key=owned_visual_pose_key(*sample);key.phase=0;
        prepared.ground_contacts_valid=false;
        // A failed build (deterministic for this placement) leaves no contacts
        // and is retried when the journal reports the object changed.
        const bool built=build_ground_contacts(*sample,ground,store.sector_top,prepared.ground_contacts);
        if(built) {
            prepared.ground_contacts_key=key;prepared.ground_contacts_valid=true;
            prepared.ground_contacts_terrain_generation=source.source_generation;
            prepared.ground_contacts_terrain_revision=source.source_revision;
        } else prepared.ground_contacts.clear();
        ground_contact_unfile(store,id,entry);
        entry.built=true;entry.bounded=true;entry.box=ground_contact_box(prepared.ground_contacts);
        ground_contact_file(store,id,entry);
        emit(entry);
    }
    // Keep the answer only when nothing in the window still waits for a build.
    store.answer=output;store.answer_cells=range;
    store.answer_revision=done==pending.size()?store.revision:0;
    return true;
} catch(...) {output.clear();g_ground_contacts.clear();return false;}

#endif // !EID3D11_EMBEDDED
// Critter perches per model (bridge_release_world_caches).
void release_critter_perch_models() noexcept {
    decltype(g_perch_models)().swap(g_perch_models);
}

// ---- Hosted plants (issue #251 3c; hosted_plants.h) ------------------------
namespace {
using ground_cover::HostedPlantData;
using ground_cover::HostedPlantRecord;
using ground_cover::HostedModelRecord;

std::string hosted_lower(std::string text) {
    for(char& c:text)if(c>='A'&&c<='Z')c=char(c+32);
    return text;
}
// A fixed-size name field of the exported data.
std::string hosted_name(const char* field,std::size_t size) {
    return std::string(field,strnlen(field,size));
}
// A part's texture name without folder and extension, folded.
std::string hosted_texture_stem(const VisualPartAsset& part) {
    std::string name=hosted_lower(part.texture_source_name.empty()?part.material_name:part.texture_source_name);
    const auto slash=name.find_last_of("/\\");if(slash!=std::string::npos)name.erase(0,slash+1);
    const auto dot=name.find('.');if(dot!=std::string::npos)name.erase(dot);
    return name;
}
// The exported host a placed object stands for (its model and first part's
// texture), or -1: a model whose texture is not the grown-on one is no host.
int hosted_host_of(const HostedPlantData& data,const RetainedVisualSample& sample) {
    static const auto names=[&data]{
        std::vector<std::pair<std::string,std::string>> folded;
        for(const auto& host:data.hosts)
            folded.push_back({hosted_lower(hosted_name(host.model,sizeof(host.model))),hosted_lower(hosted_name(host.texture,sizeof(host.texture)))});
        return folded;
    }();
    const std::string model=hosted_lower(sample.assembly->skeleton->visual_reference);
    std::string texture;
    int first=-1;
    for(std::uint32_t h=0;h<names.size();++h) {
        if(model!=names[h].first)continue;
        if(texture.empty()&&!sample.assembly->parts.empty())texture=hosted_texture_stem(sample.assembly->parts[0]);
        if(texture!=names[h].second)continue;
        if(first<0)first=int(h);
        // A model drawn with one of several alternative parts (Dejavu's
        // gravestone stst46: rip1 or rip2, each the whole stone) has one host a
        // part: the host whose parts the drawn model all has.
        const auto& host=data.hosts[h];bool all=true;
        for(std::uint32_t k=0;k<host.parts&&all;++k) {
            const std::string want=hosted_lower(hosted_name(data.parts[host.first_part+k].name,sizeof(data.parts[host.first_part+k].name)));
            all=std::any_of(sample.assembly->parts.begin(),sample.assembly->parts.end(),[&](const auto& part){return hosted_lower(part.name)==want;});
        }
        if(all)return int(h);
    }
    if(first<0&&!texture.empty()) {
        // A host model drawn with another texture is no host: named once per model and texture.
        static std::unordered_set<std::string> named;
        if(named.size()<64&&named.insert(model+"|"+texture).second) {
            std::string want;
            for(const auto& n:names)if(n.first==model)want+=(want.empty()?"":",")+n.second;
            if(!want.empty())bridge_append_probe_event("hosted_plants_omitted",(model+": drawn with texture "+texture+", grown on "+want+"; its hosted plants are not drawn").c_str());
        }
    }
    return first; // none matches whole: its check names the missing part
}
// Posed model-space points of every exported part (FIG components, in the
// exported part order) at one complection: what perch_model_point gives.
bool hosted_pose(const RetainedVisualSample& sample,const std::vector<std::uint32_t>& part_map,const std::array<float,3>& shape,
    std::vector<std::vector<std::array<double,3>>>& points) {
    const auto& assembly=*sample.assembly;
    std::vector<VisualNodeSample> nodes;
    if(!sample_visual_skeleton(*assembly.skeleton,sample.selection,shape,assembly.center_shift,0,nodes,true))return false;
    points.assign(part_map.size(),{});
    for(std::size_t k=0;k<part_map.size();++k) {
        const auto& part=assembly.parts[part_map[k]];
        if(part.skeleton_node>=nodes.size())return false;
        const auto& figure=*part.geometry;
        points[k].resize(figure.components.size());
        for(std::uint32_t c=0;c<figure.components.size();++c)points[k][c]=perch_model_point(figure,nodes[part.skeleton_node],shape,c);
    }
    return true;
}
// Once per model: the installed model is the one the plants were grown on.
struct HostedModel { bool ok=false; std::vector<std::uint32_t> part_map; };
std::unordered_map<std::string,HostedModel> g_hosted_models;
std::string hosted_model_key(const RetainedVisualSample& sample) {
    std::string key=sample.assembly->skeleton->visual_reference;
    for(const auto& part:sample.assembly->parts)key+='|'+part.name+':'+part.texture_source_name;
    return key;
}
HostedModel hosted_check_model(const HostedPlantData& data,const RetainedVisualSample& sample,int host) {
    HostedModel result;
    const auto& h=data.hosts[host];
    const std::string model=hosted_name(h.model,sizeof(h.model)),texture=hosted_name(h.texture,sizeof(h.texture));
    const auto& assembly=*sample.assembly;
    const auto omit=[&](const std::string& why) {
        bridge_append_probe_event("hosted_plants_omitted",(model+": "+why+"; its hosted plants are not drawn").c_str());
        return result;
    };
    for(std::uint32_t k=0;k<h.parts;++k) {
        const auto& want=data.parts[h.first_part+k];
        const std::string name=hosted_name(want.name,sizeof(want.name));
        std::uint32_t found=UINT32_MAX;
        for(std::uint32_t p=0;p<assembly.parts.size();++p)
            if(hosted_lower(assembly.parts[p].name)==hosted_lower(name)){found=p;break;}
        if(found==UINT32_MAX) {
            std::string have;
            for(const auto& part:assembly.parts)have+=(have.empty()?"":",")+part.name+":"+std::to_string(part.geometry?part.geometry->indices.size()/3:0);
            return omit("no part "+name+" (the drawn model "+assembly.skeleton->visual_reference+" has "+have+")");
        }
        const auto& part=assembly.parts[found];
        if(!part.geometry||!perch_figure_valid(*part.geometry))return omit("part "+name+" has no readable geometry");
        if(part.geometry->indices.size()!=std::size_t(want.triangles)*3)
            return omit("part "+name+" has "+std::to_string(part.geometry->indices.size()/3)+" triangles, not "+std::to_string(want.triangles));
        if(hosted_texture_stem(part)!=hosted_lower(texture))return omit("part "+name+" draws "+hosted_texture_stem(part)+", not "+texture);
        result.part_map.push_back(found);
    }
    // The mean shape (every complection endpoint weighted alike) the plants were grown on.
    std::vector<std::vector<std::array<double,3>>> mean;
    if(!hosted_pose(sample,result.part_map,{.5f,.5f,.5f},mean))return omit("its rest pose cannot be sampled");
    double lo[3]{DBL_MAX,DBL_MAX,DBL_MAX},hi[3]{-DBL_MAX,-DBL_MAX,-DBL_MAX};
    for(const auto& part:mean)for(const auto& p:part)for(int a=0;a<3;++a){lo[a]=(std::min)(lo[a],p[a]);hi[a]=(std::max)(hi[a],p[a]);}
    double off=0;for(int a=0;a<3;++a)off=(std::max)({off,std::abs(lo[a]-h.lo[a]),std::abs(hi[a]-h.hi[a])});
    if(!(off<.01)) {
        char text[200];
        std::snprintf(text,sizeof(text),"mean-shape bounds (%.3f %.3f %.3f)-(%.3f %.3f %.3f) differ from the grown-on model's by %.3f m",
            lo[0],lo[1],lo[2],hi[0],hi[1],hi[2],off);
        return omit(text);
    }
    result.ok=true;
    return result;
}

// Per host placement: the incarnation it was built for, its record and, for
// a rebound host, its own vertices.
struct HostedEntry {
    LifecycleIdentity lifecycle{};
    VisualResourceIncarnation resource{};
    VisualPreparedResources::PoseKey key;
    std::shared_ptr<VisualPreparedResources> prepared;
    bool ok=false;
    HostedPlantRecord record{};
    // 8 floats a vertex (position in mesh units, normal), shared by every placement
    // whose posed host shape is the same (hosted_rebind_key): one copy per shape.
    std::shared_ptr<const std::vector<float>> rebound;
    std::shared_future<std::shared_ptr<const std::vector<float>>> pending;  // the rebind, off the frame thread
    std::uint64_t rebind_key=0;
};
// Rebinds by model and posed host shape, shared between placements; dropped with the scene.
std::unordered_map<std::uint64_t,std::shared_future<std::shared_ptr<const std::vector<float>>>> g_hosted_rebinds;
// The scene's host placements, kept between frames and changed only by the
// retained scene's change journal (RetainedShadowConsumer::hosted_plants).
struct HostedScene {
    std::map<std::uint64_t,HostedEntry> entries; // by host object id (high word) and model
    std::vector<std::int32_t> todo;              // objects to visit: journal changes, the budget's rest (sorted)
    std::vector<std::uint64_t> pending;          // entries whose rebind runs
    int zone=-1;
    bool valid=false;
};
HostedScene g_hosted_scene;
std::uint64_t g_hosted_revision=0;
std::uint64_t hosted_entry_key(std::int32_t id,std::uint32_t model) noexcept { return std::uint64_t(std::uint32_t(id))<<32|model; }

// One model's vertices re-expressed on a posed host (the binding words).
void hosted_rebind(const HostedPlantData& data,const HostedModelRecord& model,const std::vector<std::vector<std::array<double,3>>>& points,
    const std::vector<std::vector<std::uint16_t>>& indices,const std::vector<bool>& flips,std::vector<float>& out) {
    const auto s16=[](std::uint32_t x){return double(std::int16_t(std::uint16_t(x&0xffffu)))/16384.0;};
    const auto s8=[](std::uint32_t x){return double(std::int8_t(std::uint8_t(x&0xffu)))/127.0;};
    std::vector<std::uint32_t> words;
    if(!ground_cover::hosted_read_binds(data,model,words)){out.clear();return;}
    out.resize(std::size_t(model.vertices)*8);
    for(std::uint32_t v=0;v<model.vertices;++v) {
        const std::uint32_t* w=&words[std::size_t(v)*5];
        const std::uint32_t part=w[0]>>24,tri=w[0]&0xffffffu;
        const auto& pts=points.at(part);const auto& idx=indices.at(part);
        std::array<double,3> A=pts.at(idx.at(tri*3)),B=pts.at(idx.at(tri*3+1)),C=pts.at(idx.at(tri*3+2));
        if(flips[part])std::swap(B,C);
        const double wa=double(w[1]&0xffffu)/65535.0,wb=double(w[1]>>16)/65535.0,wc=1-wa-wb;
        double e1[3],e2[3],n[3],t1[3],t2[3];
        for(int a=0;a<3;++a){e1[a]=B[a]-A[a];e2[a]=C[a]-A[a];}
        n[0]=e1[1]*e2[2]-e1[2]*e2[1];n[1]=e1[2]*e2[0]-e1[0]*e2[2];n[2]=e1[0]*e2[1]-e1[1]*e2[0];
        const double ln=std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]),l1=std::sqrt(e1[0]*e1[0]+e1[1]*e1[1]+e1[2]*e1[2]);
        for(int a=0;a<3;++a){n[a]/=ln>0?ln:1;t1[a]=e1[a]/(l1>0?l1:1);}
        t2[0]=n[1]*t1[2]-n[2]*t1[1];t2[1]=n[2]*t1[0]-n[0]*t1[2];t2[2]=n[0]*t1[1]-n[1]*t1[0];
        const double o1=s16(w[2]),o2=s16(w[2]>>16),on=s16(w[3]),n1=s8(w[3]>>16),n2=s8(w[3]>>24),n3=s8(w[4]);
        double p[3],q[3];
        for(int a=0;a<3;++a){
            p[a]=wa*A[a]+wb*B[a]+wc*C[a]+o1*t1[a]+o2*t2[a]+on*n[a];
            q[a]=n1*t1[a]+n2*t2[a]+n3*n[a];
        }
        const double lq=std::sqrt(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]);
        float* o=&out[std::size_t(v)*8];
        for(int a=0;a<3;++a){o[a]=float(p[a]/model.scale);o[4+a]=float(q[a]/(lq>0?lq:1));}
        o[3]=o[7]=0;
    }
}

// The cover frame of a linear map L (columns: the model's x, y, z axes in the
// world): rotation with up = L z, yaw inside the tilted frame, per-axis scale.
bool hosted_frame(const double L[3][3],HostedPlantRecord& record,double scale[3]) {
    double R[3][3];
    for(int c=0;c<3;++c) {
        scale[c]=std::sqrt(L[0][c]*L[0][c]+L[1][c]*L[1][c]+L[2][c]*L[2][c]);
        if(!(scale[c]>1e-6))return false;
        for(int r=0;r<3;++r)R[r][c]=L[r][c]/scale[c];
    }
    const auto dot=[&](int a,int b){return R[0][a]*R[0][b]+R[1][a]*R[1][b]+R[2][a]*R[2][b];};
    if(std::abs(dot(0,1))>2e-3||std::abs(dot(0,2))>2e-3||std::abs(dot(1,2))>2e-3)return false; // a sheared placement
    const double det=R[0][0]*(R[1][1]*R[2][2]-R[1][2]*R[2][1])-R[0][1]*(R[1][0]*R[2][2]-R[1][2]*R[2][0])+R[0][2]*(R[1][0]*R[2][1]-R[1][1]*R[2][0]);
    if(!(det>0))return false;   // mirrored
    const double up[3]{R[0][2],R[1][2],R[2][2]};
    if(!(up[2]>.05))return false; // the cover frame's tilt keeps up above the horizon
    // tx = x - up * up.x, ty = up x tx (gcVertexFrameAt)
    double tx[3]{1-up[0]*up[0],-up[1]*up[0],-up[2]*up[0]};
    const double lt=std::sqrt(tx[0]*tx[0]+tx[1]*tx[1]+tx[2]*tx[2]);for(double& v:tx)v/=lt;
    const double ty[3]{up[1]*tx[2]-up[2]*tx[1],up[2]*tx[0]-up[0]*tx[2],up[0]*tx[1]-up[1]*tx[0]};
    const double c=R[0][0]*tx[0]+R[1][0]*tx[1]+R[2][0]*tx[2],s=R[0][0]*ty[0]+R[1][0]*ty[1]+R[2][0]*ty[2];
    const double ls=std::sqrt(c*c+s*s);if(!(ls>.5))return false;
    record.frame[0]=float(c/ls);record.frame[1]=float(s/ls);
    record.up[0]=float(up[0]);record.up[1]=float(up[1]);record.up[2]=float(up[2]);
    return true;
}
} // namespace

// The exported hosted plants (eid3d_hosted\hosted_plants.bin beside this
// module: ddraw.dll, or the x64 host for itself), tables read once.
namespace {
bool hosted_read_at(const std::wstring& path,std::uint64_t offset,std::size_t bytes,void* out) noexcept {
    FILE* file=nullptr;
    if(_wfopen_s(&file,path.c_str(),L"rb")||!file)return false;
    const bool ok=_fseeki64(file,std::int64_t(offset),SEEK_SET)==0&&(bytes==0||std::fread(out,1,bytes,file)==bytes);
    std::fclose(file);
    return ok;
}
void hosted_read_failed() noexcept {
    static bool named=false;
    if(!named){named=true;bridge_append_probe_event("hosted_plants_omitted","eid3d_hosted\\hosted_plants.bin could not be read; hosted plants are not drawn");}
}
} // namespace
const ground_cover::HostedPlantData* ground_cover::hosted_plant_data() noexcept {
    static const HostedPlantData* parsed=[]() -> const HostedPlantData* {
        static HostedPlantData data;
        const auto fail=[](const char* why){bridge_append_probe_event("hosted_plants_omitted",why);return nullptr;};
        HMODULE module=nullptr;wchar_t path[MAX_PATH]{};
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&ground_cover::hosted_plant_data),&module)||!GetModuleFileNameW(module,path,MAX_PATH))
            return fail("module path unknown; no hosted plants");
        std::wstring folder=path;const auto slash=folder.find_last_of(L"\\/");
        if(slash==std::wstring::npos)return fail("module path unknown; no hosted plants");
        data.path=folder.substr(0,slash+1)+L"eid3d_hosted\\hosted_plants.bin";
        FILE* file=nullptr;
        if(_wfopen_s(&file,data.path.c_str(),L"rb")||!file)return fail("eid3d_hosted\\hosted_plants.bin is missing; no hosted plants");
        _fseeki64(file,0,SEEK_END);const std::int64_t size=_ftelli64(file);std::fclose(file);
        ground_cover::HostedFileHeader header{};
        if(size<std::int64_t(sizeof(header))||!hosted_read_at(data.path,0,sizeof(header),&header))return fail("eid3d_hosted\\hosted_plants.bin is damaged; no hosted plants");
        const std::uint64_t tables=std::uint64_t(header.hosts)*sizeof(ground_cover::HostedHostRecord)+
            std::uint64_t(header.parts)*sizeof(ground_cover::HostedPartRecord)+std::uint64_t(header.models)*sizeof(HostedModelRecord)+
            std::uint64_t(header.textures)*sizeof(ground_cover::HostedTextureRecord);
        const std::uint64_t need=sizeof(header)+tables+(std::uint64_t(header.vertex_words)+header.bind_words+header.indices)*4+header.texture_bytes;
        if(std::memcmp(header.magic,"HPL2",4)||header.version!=ground_cover::kHostedFileVersion||std::uint64_t(size)!=need||tables>(1u<<20))
            return fail("eid3d_hosted\\hosted_plants.bin is from another build or damaged; no hosted plants");
        data.hosts.resize(header.hosts);data.parts.resize(header.parts);data.models.resize(header.models);data.textures.resize(header.textures);
        std::uint64_t at=sizeof(header);
        const auto table=[&](auto& vector){
            const std::size_t bytes=vector.size()*sizeof(vector[0]);
            const bool ok=hosted_read_at(data.path,at,bytes,vector.data());at+=bytes;return ok;};
        if(!table(data.hosts)||!table(data.parts)||!table(data.models)||!table(data.textures))
            return fail("eid3d_hosted\\hosted_plants.bin could not be read; no hosted plants");
        data.vertex_at=at;at+=std::uint64_t(header.vertex_words)*4;
        data.bind_at=at;at+=std::uint64_t(header.bind_words)*4;
        data.index_at=at;at+=std::uint64_t(header.indices)*4;
        data.texture_at=at;
        // Every range inside the file (validated once: the tables are then trusted).
        for(const auto& texture:data.textures) {
            std::uint64_t bytes=0;
            for(std::uint32_t level=0;level<texture.mips;++level){
                const std::uint32_t w=texture.width>>level,h=texture.height>>level;
                if(w<4||h<4||w%4||h%4)return fail("a hosted plant texture's levels are damaged; no hosted plants");
                bytes+=std::uint64_t(w/4)*(h/4)*8;
            }
            if(!texture.mips||texture.mips>16||bytes!=texture.bytes||std::uint64_t(texture.first_byte)+texture.bytes>header.texture_bytes)
                return fail("a hosted plant texture's ranges are damaged; no hosted plants");
        }
        for(const auto& model:data.models) {
            const bool colony=model.kind==ground_cover::kCaveColonyKind;
            if((colony?model.host!=ground_cover::kNoHost:(model.kind>=std::size(ground_cover::kHostedKinds)||model.host>=data.hosts.size()))||
               !(model.scale>0)||std::uint64_t(model.first_vertex)+model.vertices>header.vertex_words/6||
               std::uint64_t(model.first_index)+model.indices>header.indices||(model.binds&&(model.binds!=model.vertices||
               std::uint64_t(model.first_bind)+model.binds>header.bind_words/5))||model.texture>data.textures.size())
                return fail("a hosted plant model's ranges are damaged; no hosted plants");
        }
        for(const auto& host:data.hosts)
            if(std::uint64_t(host.first_part)+host.parts>data.parts.size())return fail("a hosted plant host's parts are damaged; no hosted plants");
        return &data;
    }();
    return parsed;
}
bool ground_cover::hosted_read_vertices(const HostedPlantData& data,const HostedModelRecord& model,std::vector<std::uint32_t>& words) noexcept try {
    words.resize(std::size_t(model.vertices)*6);
    if(hosted_read_at(data.path,data.vertex_at+std::uint64_t(model.first_vertex)*24,words.size()*4,words.data()))return true;
    hosted_read_failed();words.clear();return false;
} catch(...) {words.clear();return false;}
bool ground_cover::hosted_read_binds(const HostedPlantData& data,const HostedModelRecord& model,std::vector<std::uint32_t>& words) noexcept try {
    words.resize(std::size_t(model.binds)*5);
    if(hosted_read_at(data.path,data.bind_at+std::uint64_t(model.first_bind)*20,words.size()*4,words.data()))return true;
    hosted_read_failed();words.clear();return false;
} catch(...) {words.clear();return false;}
bool ground_cover::hosted_read_indices(const HostedPlantData& data,const HostedModelRecord& model,std::vector<std::uint32_t>& indices) noexcept try {
    indices.resize(model.indices);
    if(!hosted_read_at(data.path,data.index_at+std::uint64_t(model.first_index)*4,indices.size()*4,indices.data())){hosted_read_failed();indices.clear();return false;}
    for(const auto index:indices)if(index>=model.vertices){
        bridge_append_probe_event("hosted_plants_omitted","a hosted plant model's indices are damaged; it is not drawn");indices.clear();return false;}
    return true;
} catch(...) {indices.clear();return false;}
bool ground_cover::hosted_read_texture(const HostedPlantData& data,const HostedTextureRecord& texture,std::vector<std::uint8_t>& bytes) noexcept try {
    bytes.resize(texture.bytes);
    if(hosted_read_at(data.path,data.texture_at+texture.first_byte,bytes.size(),bytes.data()))return true;
    hosted_read_failed();bytes.clear();return false;
} catch(...) {bytes.clear();return false;}

namespace {
// The object a host sample stands for may host: the published, drawn, placed
// non-unit objects (the former per-frame walk's rule).
bool hosted_candidate(const RetainedVisualSample& sample) noexcept {
    return !(sample.values.unit||sample.values.hidden||(sample.values.object_flags&0x40u)||
             sample.status!=RetainedVisualStatus::ready||!sample.height_valid||!sample.assembly||
             !sample.assembly->skeleton||!sample.prepared);
}
bool hosted_entry_current(const HostedEntry& entry,const RetainedVisualSample& sample) {
    const double still=0;
    return same_lifecycle_identity(entry.lifecycle,sample.lifecycle)&&same_visual_resource(entry.resource,sample.resource)&&
        entry.prepared==sample.prepared&&owned_visual_pose_key_matches(entry.key,sample,&still);
}
// One new or changed placement: its record and, for a host that is no
// per-axis scale of the grown-on shape, its rebind (started off the frame
// thread). The entry keeps the incarnation it was built for, failed or not.
void hosted_build(const HostedPlantData& data,const RetainedVisualSample& sample,int host,std::uint32_t model_id,
    std::uint64_t entry_key,HostedEntry& entry) {
    const auto& model=data.models[model_id];
    const std::int32_t id=sample.resource.signed_id;
    entry=HostedEntry{};
    entry.lifecycle=sample.lifecycle;entry.resource=sample.resource;
    entry.key=owned_visual_pose_key(sample);entry.key.phase=0;entry.prepared=sample.prepared;
    const std::string model_key=hosted_model_key(sample);
    auto found=g_hosted_models.find(model_key);
    if(found==g_hosted_models.end())found=g_hosted_models.emplace(model_key,hosted_check_model(data,sample,host)).first;
    if(!found->second.ok||!prepare_owned_visual_pose(sample))return;
    const auto& part_map=found->second.part_map;
    std::vector<std::vector<std::array<double,3>>> mean,posed;
    if(!hosted_pose(sample,part_map,{.5f,.5f,.5f},mean)||!hosted_pose(sample,part_map,retained_visual_shape(sample),posed))return;
    // Per-axis scale of the placement's complection against the mean shape.
    double sm[3]{},mm[3]{};
    for(std::size_t k=0;k<mean.size();++k)for(std::size_t c=0;c<mean[k].size();++c)for(int a=0;a<3;++a){
        sm[a]+=posed[k][c][a]*mean[k][c][a];mm[a]+=mean[k][c][a]*mean[k][c][a];}
    double s[3];for(int a=0;a<3;++a)s[a]=mm[a]>1e-12?sm[a]/mm[a]:1;
    double residual=0,lo[3]{DBL_MAX,DBL_MAX,DBL_MAX},hi[3]{-DBL_MAX,-DBL_MAX,-DBL_MAX};
    for(std::size_t k=0;k<mean.size();++k)for(std::size_t c=0;c<mean[k].size();++c)for(int a=0;a<3;++a){
        residual=(std::max)(residual,std::abs(posed[k][c][a]-s[a]*mean[k][c][a]));
        lo[a]=(std::min)(lo[a],posed[k][c][a]);hi[a]=(std::max)(hi[a],posed[k][c][a]);}
    const bool scaled=residual<.02;
    const std::string host_name=hosted_name(data.hosts[host].model,sizeof(data.hosts[host].model));
    if(!scaled&&!model.binds) {
        char text[160];std::snprintf(text,sizeof(text),"%s placement %d: complection is no per-axis scale of the grown-on shape (%.3f m off)",
            host_name.c_str(),id,residual);
        bridge_append_probe_event("hosted_plants_omitted",text);
        return;
    }
    const auto& root=sample.prepared->gpu_pose.root_to_world;
    double L[3][3];
    for(int r=0;r<3;++r)for(int c=0;c<3;++c)L[r][c]=double(root[r*4+c])*(scaled?s[c]:1.0);
    HostedPlantRecord& record=entry.record;double axis[3];
    if(!hosted_frame(L,record,axis)) {
        char text[160];std::snprintf(text,sizeof(text),"%s placement %d: its rotation is sheared, mirrored or tilted past the horizon",
            host_name.c_str(),id);
        bridge_append_probe_event("hosted_plants_omitted",text);
        return;
    }
    record.model=model_id;
    record.root[0]=root[3];record.root[1]=root[7];record.root[2]=root[11];
    record.root[3]=float(model.scale*axis[1]);
    record.frame[2]=float(axis[0]/axis[1]);record.frame[3]=float(axis[2]/axis[1]);
    // Bounds: the posed host's model-space box through the placement.
    double centre[3],half=0;
    for(int a=0;a<3;++a)centre[a]=.5*(lo[a]+hi[a]);
    for(int r=0;r<3;++r)record.bounds[r]=float(root[r*4]*centre[0]+root[r*4+1]*centre[1]+root[r*4+2]*centre[2]+root[r*4+3]);
    double rs=0;for(int c=0;c<3;++c)rs=(std::max)(rs,std::sqrt(double(root[c])*root[c]+double(root[4+c])*root[4+c]+double(root[8+c])*root[8+c]));
    for(int a=0;a<3;++a)half+=(hi[a]-lo[a])*(hi[a]-lo[a]);
    record.bounds[3]=float(.5*std::sqrt(half)*rs+.3);  // spurs and drapers stand off the surface
    std::uint32_t hash=std::uint32_t(id)*0x9e3779b1u;hash^=hash>>15;hash*=0x2c1b3c6du;hash^=hash>>12;record.hash=hash;
    record.rebound=0;
    if(scaled){entry.ok=true;return;}
    // Rebind off the frame thread: the posed parts and their triangles go with it.
    std::vector<std::vector<std::uint16_t>> indices;std::vector<bool> flips;
    const auto& h=data.hosts[host];
    for(std::size_t k=0;k<part_map.size();++k){
        indices.push_back(sample.assembly->parts[part_map[k]].geometry->indices);flips.push_back(data.parts[h.first_part+k].flip!=0);}
    // Placements of one host model with the same posed shape (their complection
    // and parts, to 0.1 mm) share one rebind and one vertex copy.
    std::uint64_t rk=0xcbf29ce484222325ull^(std::uint64_t(model_id)*0x9e3779b97f4a7c15ull);
    for(const auto& part:posed)for(const auto& p:part)for(int a=0;a<3;++a){
        rk^=std::uint64_t(std::int64_t(std::llround(p[a]*1e4)));rk*=0x100000001b3ull;}
    entry.rebind_key=rk;
    auto cached=g_hosted_rebinds.find(rk);
    if(cached==g_hosted_rebinds.end()) {
        const HostedPlantData* source=&data;
        cached=g_hosted_rebinds.emplace(rk,std::async(std::launch::async,[source,model,points=std::move(posed),indices=std::move(indices),flips=std::move(flips)]() {
            auto out=std::make_shared<std::vector<float>>();
            try{hosted_rebind(*source,model,points,indices,flips,*out);}catch(...){out->clear();}
            return std::shared_ptr<const std::vector<float>>(std::move(out));
        }).share()).first;
    }
    entry.pending=cached->second;
    g_hosted_scene.pending.push_back(entry_key);
}
// One object's entries against its sample (nullptr: it left the world).
// False: an entry still waits (a running rebind, or the frame's budget ran
// out before its build), so the object stays queued.
// gipat5's shade rule (ground_cover.h HostedShade): the canopy is the sum of
// a smoothstep falloff to 6 m from each swaying tree taller than 3 m (the
// tree influence map knows only the shipped tree models, not Dejavu's samt
// trees), saturating at 1; the object hash varies the amount and leaves many
// sunny hosts bare. Static map objects: their shade never changes.
struct HostedShadeAt { double canopy=-1,hash=0; };
bool hosted_shade_selects(std::uint32_t shade,const std::vector<RetainedVisualSample>& entities,const RetainedVisualSample& sample,
    HostedShadeAt& at) {
    if(shade==ground_cover::hs_none)return true;
    if(at.canopy<0) {   // once a visit
        double canopy=0;
        const auto& root=sample.pose.root;
        for(const auto& tree:entities) {
            if(tree.values.unit||!tree.assembly||!tree.assembly->sways||((tree.assembly->sway_shape>>22)&63u)<12u)continue;
            const double t=(std::min)(std::hypot(double(tree.pose.root[0])-root[0],double(tree.pose.root[1])-root[1])/6.0,1.0);
            canopy+=1-t*t*(3-2*t);
            if(canopy>=1)break;
        }
        at.canopy=(std::min)(canopy,1.0);
        std::uint32_t h=std::uint32_t(sample.resource.signed_id)*0x9e3779b1u;h^=h>>15;h*=0x2c1b3c6du;h^=h>>12;
        at.hash=(h&0xffffu)/65536.0;
    }
    const double amount=at.canopy<.25&&at.hash<.45?0:(.12+.95*at.canopy)*(.55+.7*at.hash);
    if(shade==ground_cover::hs_moss_light)return amount>=.2&&amount<.75;
    if(shade==ground_cover::hs_moss_full)return amount>=.75;
    return at.canopy>=.25||at.hash>=.5;   // hs_vines
}
// Dead vines vary per placement (user 2026-11-15: the same pattern on every
// grave): a hash of the placement's world position picks none (25%), the
// host's first dead-vine model "few" (35%) or its last "many" (40%); a host
// exported with one model shows it or none.
bool hosted_amount_selects(const HostedPlantData& data,std::uint32_t model_id,const RetainedVisualSample& sample) {
    const auto& model=data.models[model_id];
    if(model.kind!=0u)return true; // kHostedKinds[0]: dead vines
    std::uint32_t rank=0,count=0;
    for(std::uint32_t m=0;m<data.models.size();++m)
        if(data.models[m].host==model.host&&data.models[m].kind==model.kind){if(m<model_id)++rank;++count;}
    const auto cell=[](float v){return std::uint32_t(std::int32_t(std::floor(v*4.0f)));};
    std::uint32_t h=cell(sample.pose.root[0])*0x9e3779b1u^cell(sample.pose.root[1])*0x85ebca77u;h^=h>>15;h*=0x2c1b3c6du;h^=h>>12;
    const double u=(h&0xffffu)/65536.0;
    if(u<.25)return false;
    if(count<2)return true;
    return rank==(u<.6?0u:count-1);
}
bool hosted_visit(const HostedPlantData& data,int zone,std::int32_t id,const RetainedVisualSample* sample,
    bool budget_left,bool& changed,const std::vector<RetainedVisualSample>& entities) {
    auto& entries=g_hosted_scene.entries;
    const auto first=entries.lower_bound(hosted_entry_key(id,0)),last=entries.upper_bound(hosted_entry_key(id,UINT32_MAX));
    const int host=sample&&hosted_candidate(*sample)?hosted_host_of(data,*sample):-1;
    bool done=true;
    // Entries of models this object no longer hosts (or of an object that left).
    for(auto it=first;it!=last;) {
        const std::uint32_t model_id=std::uint32_t(it->first);
        const bool hosted=host>=0&&data.models[model_id].host==std::uint32_t(host);
        if(hosted){++it;continue;}
        if(it->second.pending.valid()){done=false;++it;continue;} // dropped when its rebind returns
        changed|=it->second.ok;it=entries.erase(it);
    }
    if(host<0)return done;
    HostedShadeAt shade_at;
    // Every model grown on this host for a kind of this zone (the lichen's
    // rosettes and cards): one entry each.
    for(std::uint32_t model_id=0;model_id<data.models.size();++model_id) {
        const auto& model=data.models[model_id];
        if(model.host!=std::uint32_t(host)||model.kind==ground_cover::kCaveColonyKind||
           !((ground_cover::kHostedKinds[model.kind].zones>>zone)&1u))continue;
        const std::uint64_t key=hosted_entry_key(id,model_id);
        auto found=entries.find(key);
        if(!hosted_shade_selects(ground_cover::kHostedKinds[model.kind].shade,entities,*sample,shade_at)||
           !hosted_amount_selects(data,model_id,*sample)) {
            if(found!=entries.end()&&!found->second.pending.valid()){changed|=found->second.ok;entries.erase(found);}
            else if(found!=entries.end())done=false;
            continue;
        }
        if(found!=entries.end()) {
            if(found->second.pending.valid()){done=false;continue;}
            if(hosted_entry_current(found->second,*sample))continue;
        }
        if(!budget_left){done=false;continue;}
        auto& entry=found!=entries.end()?found->second:entries[key];
        changed|=entry.ok;
        hosted_build(data,*sample,host,model_id,key,entry);
        changed|=entry.ok;
        if(entry.pending.valid())done=false;
    }
    return done;
}
} // namespace

// The hosted plants' host records: kept between frames, revisited only for
// the objects the retained scene's change journal names (a full read: every
// object), new placements within a 1.5 ms budget a frame.
bool bridge_hosted_plants(int zone,ground_cover::HostedPlantFrame& frame) noexcept try {
    const auto& visual=g_owned_visual_world_frame;
    const HostedPlantData* data=ground_cover::hosted_plant_data();
    if(!visual.active||!visual.values||!data)return false;
    const auto& entities=visual.values->entities;
    auto& store=g_hosted_scene;
    auto& scene=retained_visual_scene();
    const auto changes=scene.visual_source_changes(RetainedShadowConsumer::hosted_plants);
    bool changed=false;
    if(store.zone!=zone) {
        // Another zone hosts other kinds.
        changed=!store.entries.empty();
        store.entries.clear();store.pending.clear();store.todo.clear();g_hosted_rebinds.clear();
        store.zone=zone;store.valid=false;
    }
    std::vector<std::int32_t> visit;
    if(!store.valid||changes.full||(changes.terrain_dirty&&!changes.terrain_time_only)) {
        // Every object, and every object an entry is kept for.
        visit.reserve(entities.size()+store.entries.size());
        for(const auto& sample:entities)visit.push_back(sample.resource.signed_id);
        for(const auto& entry:store.entries)visit.push_back(std::int32_t(std::uint32_t(entry.first>>32)));
        store.valid=true;
    } else visit=changes.dirty_visuals;
    // The scene thread's frame may predate the newest changes: they stay
    // unacknowledged and are read again next frame.
    scene.acknowledge_shadow_source_changes(RetainedShadowConsumer::hosted_plants,owned_scene_ack_revision(changes.revision));
    // Finished rebinds.
    for(std::size_t k=0;k<store.pending.size();) {
        const auto found=store.entries.find(store.pending[k]);
        if(found==store.entries.end()){store.pending[k]=store.pending.back();store.pending.pop_back();continue;}
        auto& entry=found->second;
        if(entry.pending.wait_for(std::chrono::seconds(0))!=std::future_status::ready){++k;continue;}
        entry.rebound=entry.pending.get();entry.pending={};entry.ok=entry.rebound&&!entry.rebound->empty();changed|=entry.ok;
        visit.push_back(std::int32_t(std::uint32_t(found->first>>32))); // its object as it is now
        store.pending[k]=store.pending.back();store.pending.pop_back();
    }
    if(!visit.empty()||!store.todo.empty()) {
        visit.insert(visit.end(),store.todo.begin(),store.todo.end());
        std::sort(visit.begin(),visit.end());visit.erase(std::unique(visit.begin(),visit.end()),visit.end());
        store.todo.clear();
        const auto start=std::chrono::steady_clock::now();
        for(const auto id:visit) {
            const bool budget_left=std::chrono::steady_clock::now()-start<std::chrono::microseconds(1500);
            if(!hosted_visit(*data,zone,id,ground_contact_sample(entities,id),budget_left,changed,entities))store.todo.push_back(id);
        }
    }
    if(changed)++g_hosted_revision;
    if(frame.revision!=g_hosted_revision) {
        frame.records.clear();frame.rebound.clear();
        std::unordered_map<const std::vector<float>*,std::uint32_t> copies; // one copy a shared rebind
        for(const auto& item:store.entries) {
            const auto& entry=item.second;
            if(!entry.ok)continue;
            frame.records.push_back(entry.record);
            if(entry.rebound&&!entry.rebound->empty()) {
                auto [at,fresh]=copies.emplace(entry.rebound.get(),std::uint32_t(1+frame.rebound.size()/8));
                frame.records.back().rebound=at->second;
                if(fresh)frame.rebound.insert(frame.rebound.end(),entry.rebound->begin(),entry.rebound->end());
            }
        }
        frame.revision=g_hosted_revision;
    }
    return true;
} catch(...) {return false;}

void release_hosted_plants() noexcept {
    g_hosted_scene=HostedScene{};                         // waits for any rebind still running
    g_hosted_rebinds.clear();
    decltype(g_hosted_models)().swap(g_hosted_models);
    ++g_hosted_revision;
}

} // namespace owned
