// Algorithms shared by the retail bridge and the embedded renderer.
#include "world_scene_algorithms.h"
#include "owned_terrain_front_end.h"
#include "owned_resource_runtime.h"
#include "owned_terrain_decode.h"
#include "retained_static_scene.h"
#include "retained_static_submission.h"
#include "ground_cover.h"
#include "frame_accounting.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <chrono>

namespace owned {
static bool same_visual_resource(const VisualResourceIncarnation& left,
    const VisualResourceIncarnation& right) noexcept {
    return left.signed_id==right.signed_id && left.serial==right.serial;
}
static bool same_lifecycle_identity(const LifecycleIdentity& left,
                             const LifecycleIdentity& right) noexcept {
    return left.id == right.id && left.generation == right.generation &&
        left.positive_world_epoch == right.positive_world_epoch;
}
static bool same_visual_selection_key(const VisualClipSelection& left,
    const VisualClipSelection& right) noexcept {
    if(left.neutral||right.neutral)return left.neutral==right.neutral;
    return left.ordinal==right.ordinal && left.database==right.database && left.clip==right.clip;
}
static VisualPreparedResources::WorldBoundsKey owned_visual_world_bounds_key(
    const RetainedVisualSample& sample) noexcept {
    VisualPreparedResources::WorldBoundsKey key{};
    key.lifecycle=sample.lifecycle;key.resource=sample.resource;
    key.envelope_radius=sample.envelope_radius;
    key.unit=sample.values.unit && !sample.completed_pose;
    if(key.unit) {
        key.assembly=sample.assembly;
        key.assembly_revision=sample.assembly?sample.assembly->revision:0;
        key.selection=sample.selection;key.root=sample.pose.root;
        key.root_rotation=sample.pose.root_rotation;key.tilted_facing=sample.pose.tilted_facing;
        key.root_rotation_valid=sample.pose.root_rotation_valid;
    } else {
        key.position=sample.object_pose.position;key.quaternion=sample.object_pose.quaternion;
    }
    return key;
}

static bool same_visual_world_bounds_key(const VisualPreparedResources::WorldBoundsKey& left,
    const VisualPreparedResources::WorldBoundsKey& right) noexcept {
    if(!same_lifecycle_identity(left.lifecycle,right.lifecycle) ||
        !same_visual_resource(left.resource,right.resource) || left.unit!=right.unit)return false;
    if(!left.unit)return left.envelope_radius==right.envelope_radius &&
        left.position==right.position && left.quaternion==right.quaternion;
    return left.assembly==right.assembly && left.assembly_revision==right.assembly_revision &&
        same_visual_selection_key(left.selection,right.selection) && left.root==right.root &&
        left.tilted_facing==right.tilted_facing &&
        (!left.tilted_facing || (left.root_rotation_valid==right.root_rotation_valid &&
            left.root_rotation==right.root_rotation));
}

static bool owned_visual_world_bounds_key_matches(const VisualPreparedResources::WorldBoundsKey& left,
    const RetainedVisualSample& sample) noexcept {
    const bool unit=sample.values.unit && !sample.completed_pose;
    if(!same_lifecycle_identity(left.lifecycle,sample.lifecycle) ||
        !same_visual_resource(left.resource,sample.resource) || left.unit!=unit)return false;
    if(!unit)return left.envelope_radius==static_cast<decltype(left.envelope_radius)>(sample.envelope_radius) &&
        left.position==sample.object_pose.position && left.quaternion==sample.object_pose.quaternion;
    return left.assembly==sample.assembly &&
        left.assembly_revision==(sample.assembly?sample.assembly->revision:0) &&
        same_visual_selection_key(left.selection,sample.selection) && left.root==sample.pose.root &&
        left.tilted_facing==sample.pose.tilted_facing &&
        (!left.tilted_facing || (left.root_rotation_valid==sample.pose.root_rotation_valid &&
            left.root_rotation==sample.pose.root_rotation));
}

bool prepare_world_visual_bounds(const RetainedVisualSample& sample,
    NativeWorldBounds& bounds) {
    auto& prepared=*sample.prepared;
    if(prepared.derived_world_bounds_valid &&
        owned_visual_world_bounds_key_matches(prepared.derived_world_bounds_key,sample)) {
        bounds=prepared.derived_world_bounds;
#ifdef EID3D11_TESTING
        ++prepared.derived_world_bounds_reuses;
#endif
        return true;
    }
    NativeWorldBounds value{};
    if(!retained_visual_world_bounds(sample,value))return false;
    prepared.derived_world_bounds_key=owned_visual_world_bounds_key(sample);prepared.derived_world_bounds=value;
    prepared.derived_world_bounds_valid=true;bounds=value;
#ifdef EID3D11_TESTING
    ++prepared.derived_world_bounds_builds;
#endif
    return true;
}

VisualPreparedResources::PoseKey owned_visual_pose_key(const RetainedVisualSample& sample) noexcept {
    VisualPreparedResources::PoseKey key{};
    key.lifecycle=sample.lifecycle;key.resource=sample.resource;key.assembly=sample.assembly;
    key.assembly_revision=sample.assembly?sample.assembly->revision:0;
    key.unit=sample.values.unit && !sample.completed_pose;key.selection=sample.selection;key.phase=sample.pose.phase;
    if(key.unit) {
        key.root=sample.pose.root;key.root_rotation=sample.pose.root_rotation;
        key.facing=sample.pose.facing;key.tilted_facing=sample.pose.tilted_facing;
        key.root_rotation_valid=sample.pose.root_rotation_valid;
    } else {
        key.position=sample.object_pose.position;key.quaternion=sample.object_pose.quaternion;
        key.shape=sample.object_pose.shape;
    }
    return key;
}

bool same_visual_pose_key(const VisualPreparedResources::PoseKey& left,
    const VisualPreparedResources::PoseKey& right) noexcept {
    if(!same_lifecycle_identity(left.lifecycle,right.lifecycle) ||
        !same_visual_resource(left.resource,right.resource) || left.assembly!=right.assembly ||
        left.assembly_revision!=right.assembly_revision || left.unit!=right.unit || left.phase!=right.phase ||
        !same_visual_selection_key(left.selection,right.selection))return false;
    if(!left.unit)return left.position==right.position && left.quaternion==right.quaternion &&
        left.shape==right.shape;
    if(left.root!=right.root || left.tilted_facing!=right.tilted_facing)return false;
    if(left.tilted_facing)return left.root_rotation_valid==right.root_rotation_valid &&
        left.root_rotation==right.root_rotation;
    return left.facing==right.facing;
}


bool owned_visual_pose_key_matches(const VisualPreparedResources::PoseKey& left,
    const RetainedVisualSample& sample,const double* phase) noexcept {
    const bool unit=sample.values.unit && !sample.completed_pose;
    const decltype(left.phase) current=phase?*phase:sample.pose.phase;
    if(!same_lifecycle_identity(left.lifecycle,sample.lifecycle) ||
        !same_visual_resource(left.resource,sample.resource) || left.assembly!=sample.assembly ||
        left.assembly_revision!=(sample.assembly?sample.assembly->revision:0) || left.unit!=unit ||
        left.phase!=current || !same_visual_selection_key(left.selection,sample.selection))return false;
    if(!unit)return left.position==sample.object_pose.position &&
        left.quaternion==sample.object_pose.quaternion && left.shape==sample.object_pose.shape;
    if(left.root!=sample.pose.root || left.tilted_facing!=sample.pose.tilted_facing)return false;
    if(left.tilted_facing)return left.root_rotation_valid==sample.pose.root_rotation_valid &&
        left.root_rotation==sample.pose.root_rotation;
    return left.facing==static_cast<decltype(left.facing)>(sample.pose.facing);
}

bool prepare_world_visual_pose(const RetainedVisualSample& sample, std::uint64_t* preparation_count) {
    auto& prepared=*sample.prepared;
    if(prepared.derived_pose_valid && owned_visual_pose_key_matches(prepared.derived_pose_key,sample)) {
#ifdef EID3D11_TESTING
        ++prepared.derived_pose_reuses;
#endif
        return true;
    }
    NativeWorldGeometry::Pose* pose=nullptr;
#ifdef EID3D11_TESTING
    if(preparation_count) ++*preparation_count;
#endif
    if(!retained_visual_gpu_pose(sample,pose)||!pose)return false;
    prepared.derived_pose_key=owned_visual_pose_key(sample);prepared.derived_pose_valid=true;
    if(++prepared.derived_pose_generation==0)++prepared.derived_pose_generation;
#ifdef EID3D11_TESTING
    ++prepared.derived_pose_builds;
#endif
    return true;
}


// Ground contacts of one visible static object at its current pose: each
// visible, non-crown part's posed triangles, with every vertex's height
// above the base ground (ground_cover::contact_footprints). Vertices above
// the highest decoded ground of every sector the part spans need no terrain
// sample; their height above that is a sufficient lower bound.
bool build_ground_contacts(const RetainedVisualSample& sample,const TerrainCpuAsset& terrain,
    const std::vector<float>& sector_top,std::vector<std::array<float,4>>& output, std::uint64_t* preparation_count) {
    output.clear();
    if(!prepare_world_visual_pose(sample, preparation_count))return false;
    const auto& assembly=*sample.assembly;
    const auto& root=sample.prepared->gpu_pose.root_to_world;
    // The node pose the GPU derives (nativePoseCS) at this phase.
    std::vector<VisualNodeSample> nodes;
    if(!sample_visual_skeleton(*assembly.skeleton,sample.selection,retained_visual_shape(sample),
        assembly.center_shift,sample.pose.phase,nodes,true))return false;
    const bool tree=ambient::tree_kind(assembly.skeleton->visual_reference.c_str())!=ambient::tree_none;
    const auto& shape=retained_visual_shape(sample);
    std::vector<std::array<float,3>> vertices;std::vector<std::uint32_t> triangles;
    std::vector<std::uint64_t> cells;std::vector<std::array<float,4>> rectangles;
    for(const auto& part:assembly.parts) {
        if(!part.geometry||part.skeleton_node>=nodes.size()||!retained_visual_part_visible(sample,part,false)||
           !(retained_visual_effective_alpha(sample,part)>0))continue;
        // The census separates trunk/root geometry from crown cards,
        // including the bare branch crowns on the Dead City's trees.
        if(tree&&part.name.find("crown")!=std::string::npos)continue;
        const auto& figure=*part.geometry;
        if((figure.shape_count!=1&&figure.shape_count!=8)||
           figure.positions.size()!=std::size_t(figure.position_groups)*figure.shape_count)continue;
        const auto& node=nodes[part.skeleton_node];
        const double w=node.rotation[0],qx=node.rotation[1],qy=node.rotation[2],qz=node.rotation[3];
        vertices.resize(figure.components.size());
        float low[2]{INFINITY,INFINITY},high[2]{-INFINITY,-INFINITY},lowest=INFINITY;
        bool valid=true;
        for(std::size_t c=0;c<figure.components.size();++c) {
            const std::uint32_t index=figure.components[c].position;
            if(index>=figure.position_groups*4u){valid=false;break;}
            double local[3]{};
            for(std::uint32_t endpoint=0;endpoint<figure.shape_count;++endpoint) {
                double weight=1;
                if(figure.shape_count==8)for(unsigned axis=0;axis<3;++axis)
                    weight*=(endpoint&(1u<<axis))?shape[axis]:1.0-shape[axis];
                const auto& group=figure.positions[std::size_t(endpoint)*figure.position_groups+index/4];
                local[0]+=weight*group.x[index%4];local[1]+=weight*group.y[index%4];local[2]+=weight*group.z[index%4];
            }
            if(index<node.morphs.size())for(unsigned axis=0;axis<3;++axis)local[axis]+=node.morphs[index][axis];
            // Node rotation (w, x, y, z) and translation, then the object matrix.
            const double tx=2*(qy*local[2]-qz*local[1]),ty=2*(qz*local[0]-qx*local[2]),tz=2*(qx*local[1]-qy*local[0]);
            const double model[3]{local[0]+w*tx+(qy*tz-qz*ty)+node.translation[0],
                                  local[1]+w*ty+(qz*tx-qx*tz)+node.translation[1],
                                  local[2]+w*tz+(qx*ty-qy*tx)+node.translation[2]};
            auto& v=vertices[c];
            for(unsigned row=0;row<3;++row)
                v[row]=float(root[row*4]*model[0]+root[row*4+1]*model[1]+root[row*4+2]*model[2]+root[row*4+3]);
            if(!std::isfinite(v[0])||!std::isfinite(v[1])||!std::isfinite(v[2])){valid=false;break;}
            for(unsigned axis=0;axis<2;++axis){low[axis]=std::min(low[axis],v[axis]);high[axis]=std::max(high[axis],v[axis]);}
            lowest=std::min(lowest,v[2]);
        }
        if(!valid||vertices.empty())continue;
        // The highest decoded ground any vertex of this part can lie over
        // (32 m sectors).
        float top=-INFINITY;
        const int sx0=std::max(0,int(std::floor(low[0]/32))),sy0=std::max(0,int(std::floor(low[1]/32)));
        const int sx1=std::min(int(terrain.sector_count_x)-1,int(std::floor(high[0]/32)));
        const int sy1=std::min(int(terrain.sector_count_y)-1,int(std::floor(high[1]/32)));
        for(int sy=sy0;sy<=sy1;++sy)for(int sx=sx0;sx<=sx1;++sx)
            top=std::max(top,sector_top[std::size_t(sy)*terrain.sector_count_x+std::size_t(sx)]);
        if(!(lowest<=top+ground_cover::kContactBand))continue; // suspended, or off the map
        for(auto& v:vertices) {
            float ground=0;
            if(v[2]>top+ground_cover::kContactBand)v[2]-=top;
            else if(sample_terrain_height(terrain,v[0],v[1],TerrainHeightLayer::ground,ground))v[2]-=ground;
            else v[2]=INFINITY; // outside the decoded ground
        }
        triangles.assign(figure.indices.begin(),figure.indices.end());
        ground_cover::contact_footprints(vertices,triangles,cells,rectangles);
        output.insert(output.end(),rectangles.begin(),rectangles.end());
    }
    return true;
}

bool build_critter_obstacles(const RetainedVisualFrame& frame,float x,float y,float radius,
    std::vector<std::array<float,6>>& output) noexcept try {
    output.clear();
    for(const auto& sample:frame.entities) {
        if(sample.values.unit||sample.values.hidden||(sample.values.object_flags&0x40u)||
           sample.status!=RetainedVisualStatus::ready||!sample.height_valid||!sample.assembly||
           !sample.assembly->skeleton||!sample.prepared)continue;
        NativeWorldBounds b{};
        if(!prepare_world_visual_bounds(sample,b))continue;
        if(b.maximum[0]<x-radius||b.minimum[0]>x+radius||b.maximum[1]<y-radius||b.minimum[1]>y+radius)continue;
        output.push_back({b.minimum[0],b.minimum[1],b.minimum[2],b.maximum[0],b.maximum[1],b.maximum[2]});
    }
    return true;
} catch(...) {output.clear();return false;}

bool build_critter_perches(const RetainedVisualFrame& frame,const TextureCpuAsset* atlas,WorldPerchCache& cache,float x,float y,float radius,std::vector<CritterPerchTree>& output,std::uint64_t* preparation_count) noexcept try {
    output.clear();
    auto& models=cache.models;
    // Perches are placed once per object placement; a burst spreads over calls.
    const auto start=std::chrono::steady_clock::now();
    bool building=true;
    for(const auto& sample:frame.entities) {
        if(sample.values.unit||sample.values.hidden||(sample.values.object_flags&0x40u)||
           sample.status!=RetainedVisualStatus::ready||!sample.height_valid||!sample.assembly||
           !sample.assembly->skeleton||!sample.prepared)continue;
        const char* model=sample.assembly->skeleton->visual_reference.c_str();
        const bool cactus=critters::cactus_model(model);
        if(!cactus&&ambient::tree_kind(model)==ambient::tree_none)continue;
        NativeWorldBounds b{};
        if(!prepare_world_visual_bounds(sample,b))continue;
        if(b.maximum[0]<x-radius||b.minimum[0]>x+radius||b.maximum[1]<y-radius||b.minimum[1]>y+radius)continue;
        auto& prepared=*sample.prepared;
        auto key=owned_visual_pose_key(sample);key.phase=0;
        if(!prepared.perches_valid||!same_visual_pose_key(prepared.perches_key,key)) {
            if(!building)continue;
            prepared.perches_valid=false;
            std::string model_key=sample.assembly->skeleton->visual_reference;
            for(const auto& part:sample.assembly->parts)model_key+='|'+part.name+':'+part.texture_source_name;
            if(atlas)model_key+="|atlas";
            auto found=models.find(model_key);
            if(found==models.end())found=models.emplace(model_key,choose_model_perches(sample,cactus,atlas)).first;
            const bool placed=place_perches(sample,found->second,prepared.perches,preparation_count);
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

constexpr float kGroundContactCell = 16.f;
constexpr double kGroundContactBudgetMs = .3;
struct WorldContactStore {
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
namespace {
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
void ground_contact_unfile(WorldContactStore& store,std::int32_t id,const WorldContactStore::Entry& entry) {
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
void ground_contact_file(WorldContactStore& store,std::int32_t id,const WorldContactStore::Entry& entry) {
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
    WorldContactStore::Entry& entry) {
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
        if(prepare_world_visual_bounds(sample,envelope)&&
           std::isfinite(envelope.minimum[0])&&std::isfinite(envelope.minimum[1])&&
           std::isfinite(envelope.maximum[0])&&std::isfinite(envelope.maximum[1]))
            entry.box={envelope.minimum[0],envelope.minimum[1],envelope.maximum[0],envelope.maximum[1]};
        else entry.bounded=false;
    }
    return true;
}
bool same_ground_contact_entry(const WorldContactStore::Entry& left,const WorldContactStore::Entry& right) noexcept {
    return same_lifecycle_identity(left.lifecycle,right.lifecycle)&&same_visual_resource(left.resource,right.resource)&&
        left.prepared==right.prepared&&left.built==right.built&&left.bounded==right.bounded&&left.box==right.box;
}
} // namespace

bool build_ground_cover_objects(const RetainedVisualFrame& frame,const RetainedTerrainSourceSnapshot& source,
    WorldContactCache& cache,float x,float y,float radius,std::vector<ground_cover::FootprintSplat>& output,
    std::uint64_t* preparation_count,RetainedStaticScene* owner,bool held) noexcept try {
    output.clear();
    if(!source.terrain)return false;
    if(!cache.store)cache.store=std::make_shared<WorldContactStore>();
    const auto& ground=*source.terrain;
    const auto& entities=frame.entities;
    auto& store=*cache.store;
    auto& scene=owner?*owner:retained_visual_scene();
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
        WorldContactStore::Entry entry;
        for(const auto& sample:entities)
            if(ground_contact_entry(sample,source,entry)) {
                ground_contact_file(store,sample.resource.signed_id,entry);
                store.entries[sample.resource.signed_id]=std::move(entry);
            }
        store.valid=true;
    } else {
        WorldContactStore::Entry entry;
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
        changes.revision);

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
    const auto emit=[&](const WorldContactStore::Entry& entry){
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
        const bool built=build_ground_contacts(*sample,ground,store.sector_top,prepared.ground_contacts,preparation_count);
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
} catch(...) {output.clear();cache.store.reset();return false;}




} // namespace owned
