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
    key.unit=sample.values.unit;
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

bool prepare_world_visual_bounds(const RetainedVisualSample& sample,
    NativeWorldBounds& bounds) {
    auto& prepared=*sample.prepared;const auto key=owned_visual_world_bounds_key(sample);
    if(prepared.derived_world_bounds_valid &&
        same_visual_world_bounds_key(prepared.derived_world_bounds_key,key)) {
        bounds=prepared.derived_world_bounds;
#ifdef EID3D11_TESTING
        ++prepared.derived_world_bounds_reuses;
#endif
        return true;
    }
    NativeWorldBounds value{};
    if(!retained_visual_world_bounds(sample,value))return false;
    prepared.derived_world_bounds_key=key;prepared.derived_world_bounds=value;
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
    key.unit=sample.values.unit;key.selection=sample.selection;key.phase=sample.pose.phase;
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


bool prepare_world_visual_pose(const RetainedVisualSample& sample, std::uint64_t* preparation_count) {
    auto& prepared=*sample.prepared;const auto key=owned_visual_pose_key(sample);
    if(prepared.derived_pose_valid && same_visual_pose_key(prepared.derived_pose_key,key)) {
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
    prepared.derived_pose_key=key;prepared.derived_pose_valid=true;
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

bool build_ground_cover_objects(const RetainedVisualFrame& frame,const RetainedTerrainSourceSnapshot& source,WorldContactCache& cache,float x,float y,float radius,
    std::vector<ground_cover::FootprintSplat>& output,std::uint64_t* preparation_count) noexcept try {
    output.clear();
    if(!source.terrain)return false;
    const auto& ground=*source.terrain;
    auto& sector_top=cache.sector_top;
    if(cache.terrain!=source.terrain || cache.generation!=source.source_generation ||
       cache.revision!=source.source_revision) {
    // Immutable terrain bounds need rebuilding only for a different source.
    cache.terrain.reset();
    sector_top.assign(std::size_t(ground.sector_count_x)*ground.sector_count_y,-INFINITY);
    for(const auto& sector:ground.sectors)
        if(sector.map_x<ground.sector_count_x&&sector.map_y<ground.sector_count_y) {
            auto& value=sector_top[std::size_t(sector.map_y)*ground.sector_count_x+sector.map_x];
            value=std::max(value,sector.bounds.maximum[2]);
        }
    cache.terrain=source.terrain;cache.generation=source.source_generation;cache.revision=source.source_revision;
    }
    // Contacts are built once per object placement. A burst (map entry, a far
    // zoom-out) spreads over frames; an object not yet built has no footprint.
    const auto start=std::chrono::steady_clock::now();
    bool building=true;
    for(const auto& sample:frame.entities) {
        if(sample.values.unit||sample.values.hidden||(sample.values.object_flags&0x40u)||
           sample.status!=RetainedVisualStatus::ready||!sample.height_valid||!sample.assembly||
           !sample.assembly->skeleton||!sample.prepared)continue;
        // The published entity envelope contains every part envelope (the
        // contract the owned scene selects consumers by); reject the whole
        // object before per-part work. Without one, fall through to the parts.
        NativeWorldBounds envelope{};
        if(prepare_world_visual_bounds(sample,envelope)&&
           (envelope.maximum[0]<x-radius||envelope.minimum[0]>x+radius||
            envelope.maximum[1]<y-radius||envelope.minimum[1]>y+radius))continue;
        // Contacts follow the object's placement, appearance and clip, not the
        // animation phase: an idling prop keeps the contact it was built with.
        auto& prepared=*sample.prepared;
        auto key=owned_visual_pose_key(sample);key.phase=0;
        if(!prepared.ground_contacts_valid||!same_visual_pose_key(prepared.ground_contacts_key,key)||
           prepared.ground_contacts_terrain_generation!=source.source_generation||
           prepared.ground_contacts_terrain_revision!=source.source_revision) {
            if(!building)continue;
            prepared.ground_contacts_valid=false;
            const bool built=build_ground_contacts(sample,ground,sector_top,prepared.ground_contacts,preparation_count);
            building=std::chrono::steady_clock::now()-start<std::chrono::microseconds(1500);
            if(!built)continue;
            prepared.ground_contacts_key=key;prepared.ground_contacts_valid=true;
            prepared.ground_contacts_terrain_generation=source.source_generation;
            prepared.ground_contacts_terrain_revision=source.source_revision;
        }
        for(const auto& rectangle:prepared.ground_contacts) {
            if(rectangle[0]+rectangle[2]<x-radius||rectangle[0]-rectangle[2]>x+radius||
               rectangle[1]+rectangle[3]<y-radius||rectangle[1]-rectangle[3]>y+radius)continue;
            output.push_back(ground_cover::object_splat(rectangle));
        }
    }
    return true;
} catch(...) {output.clear();return false;}


} // namespace owned
