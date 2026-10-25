// The bridge's scene half: pure consumers of the prepared world frame the
// renderer pulls (liquid ripples, ground cover units, corpses and object
// contacts, critter obstacles and perches). Reads only the frame copies
// (owned_world_frame.h); host-buildable.
#include "owned_world_frame.h"
#include "world_scene_algorithms.h"
#include "embedded_perf06.h"
#include "critters.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
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

} // namespace owned
