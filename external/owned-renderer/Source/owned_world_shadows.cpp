#include "embedded_perf06.h"
#ifdef EID3D11_EMBEDDED
#include "embedded_host.h"
#endif
// The bridge's scene half: directional and point shadow casters (visual,
// static and terrain), the shadow pass, rain occlusion, shadow input reuse and
// prepare_owned_world_shadows_from. Reads only the captured frame copies
// (owned_world_frame.h); host-buildable.
#include "owned_world_frame.h"
#include "neighbour_maps.h"
#include "point_shadow_policy.h"
#include "renderer.h"
#include "shadow_far_retention.h"
#include "weather_wind.h"

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



bool owned_visual_part_casts(const RetainedVisualSample& sample,const VisualPartAsset& part,
    bool draw_undetected) noexcept {
    if(!part.casts_shadow || !retained_visual_part_visible(sample,part,draw_undetected) ||
       (sample.values.unit ? !sample.visibility.casts_shadow : (sample.values.object_flags&8u)!=0))return false;
    if(!part.material)return false;
    const float alpha=retained_visual_effective_alpha(sample,part);
    // Both covered world spans use texture*diffuse and GREATER. An opacity
    // below the established reference cannot survive for any texture texel.
    return alpha>float(sample.values.unit?10:200)/256.f;
}
namespace {

bool append_owned_visual_shadow_values(std::vector<RetainedShadowCasterValue>& casters,
    const std::vector<std::int32_t>* selected_ids=nullptr,
    const std::vector<std::int32_t>* time_active_ids=nullptr) {
    auto& frame=g_owned_visual_world_frame;
    const auto append_candidate=[&](const OwnedVisualPartCandidate& candidate) {
            const auto index=candidate.entity;const auto part=candidate.part;
            auto& prepared=frame.prepared[index];const auto& sample=frame.values->entities[index];
            if(time_active_ids && !std::binary_search(time_active_ids->begin(),
                time_active_ids->end(),sample.lifecycle.id))return true;
            if(part>=kMergedPartBase) {
                // A merged group casts with the unit's envelope and one revision.
                auto& group=sample.prepared->merged_groups[part-kMergedPartBase];
                bool casts=false;for(const auto member:group.parts)casts|=member<prepared.parts.size()&&prepared.parts[member].casts;
                if(!casts || !prepared.contribution.light_mask)return true;
                RetainedShadowCasterValue value{};value.bounds=prepared.bounds;
                value.mask=retained_world_contribution(prepared.bounds,true,frame.coverage,&value.light_bounds).light_mask;
                if(!value.mask)return true;
                if(!retained_visual_group_depth_revision(sample,part-kMergedPartBase,frame.values->time,value.revision))return false;
                group.frame_revision=value.revision;group.frame_serial=frame.serial;
                casters.push_back(std::move(value));
                return true;
            }
            if(!prepared.parts[part].casts || !prepared.contribution.light_mask)return true;
            RetainedShadowCasterValue value{};
            const OwnedVisualPartPreparation* part_value=nullptr;
            if(!prepare_owned_visual_part(index,part,part_value))return false;
            value.bounds=part_value->bounds;
            value.light_bounds=part_value->light_bounds;
            value.mask=part_value->contribution.light_mask;
            if(!value.mask)return true;
            if(!retained_visual_depth_revision(sample,part,frame.values->time,value.revision))return false;
            prepared.parts[part].depth_revision=value.revision;prepared.parts[part].depth_serial=frame.serial;
            casters.push_back(std::move(value));
            return true;
    };
    if(selected_ids) {
        for(const auto id:*selected_ids) {
            const auto found=std::lower_bound(frame.values->entities.begin(),frame.values->entities.end(),id,
                [](const RetainedVisualSample& sample,std::int32_t value){return sample.lifecycle.id<value;});
            if(found==frame.values->entities.end() || found->lifecycle.id!=id)continue;
            const auto index=static_cast<std::size_t>(found-frame.values->entities.begin());
            for(auto candidate=frame.prepared[index].candidate_begin;
                candidate<frame.prepared[index].candidate_end;++candidate)
                if(!append_candidate(frame.candidates[candidate]))return false;
        }
    } else for(const auto& candidate:frame.candidates)
        if(!append_candidate(candidate))return false;
    return true;
}

bool append_owned_static_shadow_values(std::vector<RetainedShadowCasterValue>& casters,
    const std::vector<std::int32_t>& time_active_ids,
    const std::vector<std::int32_t>* selected_ids=nullptr) {
    auto& frame=g_owned_visual_world_frame;
    const auto append_entity=[&](std::size_t index) {
        auto& prepared=frame.prepared[index];
        if(!prepared.bounds.valid || !prepared.casting_parts)return true;
        const auto& sample=frame.values->entities[index];
        if(std::binary_search(time_active_ids.begin(),time_active_ids.end(),sample.lifecycle.id))return true;
        if(!prepare_owned_visual_pose(sample))return false;
        for(std::size_t part=0;part<prepared.parts.size();++part) {
            if(!prepared.parts[part].casts)continue;
            const OwnedVisualPartPreparation* value=nullptr;
            if(!prepare_owned_visual_part(index,part,value))return false;
            RetainedShadowCasterValue caster{};
            caster.bounds=value->bounds;
            caster.light_bounds=value->light_bounds;
            caster.mask=value->contribution.light_mask;
            caster.persistent_static=true;
            if(!retained_visual_depth_revision(sample,part,frame.values->time,
                caster.revision))return false;
            prepared.parts[part].depth_revision=caster.revision;prepared.parts[part].depth_serial=frame.serial;
            casters.push_back(std::move(caster));
        }
        return true;
    };
    // Entities are ordered by lifecycle id, so a journal-dirty update looks
    // up only its ids (same order as a walk of the whole list) instead of
    // visiting every entity of the world.
    if(selected_ids) {
        const auto& entities=frame.values->entities;
        auto from=entities.begin();
        for(const auto id:*selected_ids) {
            from=std::lower_bound(from,entities.end(),id,
                [](const RetainedVisualSample& sample,std::int32_t value){return sample.lifecycle.id<value;});
            if(from==entities.end())break;
            if(from->lifecycle.id!=id)continue;
            if(!append_entity(static_cast<std::size_t>(from-entities.begin())))return false;
            ++from;
        }
        return true;
    }
    for(std::size_t index=0;index<frame.prepared.size();++index)
        if(!append_entity(index))return false;
    return true;
}

// Per-frame sun depth selection culls against the projection each map is
// drawn with this frame (the renderer may retain an older far projection).
struct OwnedSunDepthCull {
    struct Slice { std::array<float,16> world_to_light{}; };
    std::array<Slice,5> slices{};
    std::uint8_t retained_mask=0;
    // Maps whose static layer scrolls (NativeShadowCullProjection).
    std::uint8_t scroll_mask=0;
    std::array<std::array<double,4>,5> scroll_copied{};
    unsigned side=0;
    const std::array<std::vector<std::uint64_t>,5>* scroll_entered=nullptr;
};
// Static-layer slices a terrain part or static caster is needed in: a
// scrolled map needs only casters reaching its exposed strip and terrain
// parts that entered its list; the renderer applies the same rule again.
template<class ShadowFrame>
std::uint8_t owned_static_scroll_mask(const ShadowFrame&,const NativeWorldBounds&,
    std::uint8_t mask,std::uint64_t) noexcept { return mask; }
inline std::uint8_t owned_static_scroll_mask(const OwnedSunDepthCull& cull,const NativeWorldBounds& light,
    std::uint8_t mask,std::uint64_t terrain_key) noexcept {
    const auto scrolled=static_cast<std::uint8_t>(mask&cull.scroll_mask);
    if(!scrolled)return mask;
    for(unsigned slice=0;slice<5;++slice){
        if(!(scrolled&(1u<<slice)))continue;
        if(terrain_key && cull.scroll_entered){
            const auto& entered=(*cull.scroll_entered)[slice];
            if(std::binary_search(entered.begin(),entered.end(),terrain_key))continue;
        }
        if(!shadow_scroll_strip_reached_light(light,cull.slices[slice].world_to_light,
               cull.scroll_copied[slice],cull.side))
            mask&=static_cast<std::uint8_t>(~(1u<<slice));
    }
    return mask;
}
template<class ShadowFrame>
std::uint8_t owned_depth_retained_slices(const ShadowFrame&) noexcept { return 0; }
inline std::uint8_t owned_depth_retained_slices(const OwnedSunDepthCull& cull) noexcept {
    return cull.retained_mask;
}

template<class ShadowFrame>
bool collect_owned_visual_depth(std::uint8_t dirty,const ShadowFrame& shadows,
    const RetainedWorldPointLight* point_light=nullptr,
    const std::vector<OwnedVisualPartCandidate>* members=nullptr) {
    auto& frame=g_owned_visual_world_frame;
    for(const auto& candidate:(members?*members:frame.candidates)) {
            const auto index=candidate.entity;const auto part_index=candidate.part;
            auto& prepared=frame.prepared[index];const auto& sample=frame.values->entities[index];
            // The cheap rejects first; the binary search after them (all pure tests).
            const std::uint8_t retained=point_light ? 0 : owned_depth_retained_slices(shadows);
            if(!prepared.bounds.valid || (point_light ?
                !owned_point_shadow_influence(prepared.bounds,*point_light) :
                !((prepared.contribution.light_mask|retained)&dirty)))continue;
            if(frame.persistent_static_shadows && !point_light &&
               !std::binary_search(frame.shadow_time_active_visuals.begin(),
                   frame.shadow_time_active_visuals.end(),sample.lifecycle.id))continue;
            if(part_index>=kMergedPartBase) {
                const auto group_index=part_index-kMergedPartBase;
                auto& group=sample.prepared->merged_groups[group_index];
                bool casts=false;for(const auto member:group.parts)casts|=member<prepared.parts.size()&&prepared.parts[member].casts;
                if(!casts)continue;
                const auto mask=shadow_depth_slice_mask(point_light ? dirty : prepared.contribution.light_mask,
                    retained,dirty,static_cast<unsigned>(shadows.slices.size()),[&](unsigned slice){
                        return retained_world_bounds_visible(prepared.bounds,shadows.slices[slice].world_to_light);});
                if(!mask)continue;
                if(group.frame_serial!=frame.serial || !group.frame_revision.revision) {
                    if(!retained_visual_group_depth_revision(sample,group_index,frame.values->time,group.frame_revision))return false;
                    group.frame_serial=frame.serial;
                }
                NativeDepthSelection selected;
                selected.mesh=&group.mesh;selected.material=&group.material;
                selected.identity={sample.lifecycle.id,sample.lifecycle.generation,sample.lifecycle.positive_world_epoch,
                    sample.resource.serial,static_cast<UINT>(part_index),0,frame.serial,true};
                selected.revision=group.frame_revision;selected.bounds=prepared.bounds;
                selected.source_revision=group.record.world.enabled ? group.record_revision : 0;
                selected.producer_index=static_cast<UINT>(index);selected.producer_part=static_cast<UINT>(part_index);
                selected.mask=mask;frame.depth_selected.push_back(selected);
                continue;
            }
            if(!prepared.parts[part_index].casts)continue;
            const OwnedVisualPartPreparation* value=nullptr;
            if(!prepare_owned_visual_part(index,part_index,value)) {
                const NativePreparedIdentity identity{sample.lifecycle.id,sample.lifecycle.generation,
                    sample.lifecycle.positive_world_epoch,sample.resource.serial,static_cast<UINT>(part_index),0,frame.serial,true};
                return reject_owned_shadow_part("envelope",identity);
            }
            const auto mask=shadow_depth_slice_mask(point_light ? dirty : value->contribution.light_mask,
                retained,dirty,static_cast<unsigned>(shadows.slices.size()),[&](unsigned slice){
                    return retained_world_bounds_visible(value->bounds,shadows.slices[slice].world_to_light);});
            if(!mask)continue;
            auto& depth_part=prepared.parts[part_index];
            if(depth_part.depth_serial!=frame.serial){depth_part.depth_revision={};depth_part.depth_serial=frame.serial;}
            if(!depth_part.depth_revision.revision &&
               !retained_visual_depth_revision(sample,part_index,frame.values->time,
                   depth_part.depth_revision))return false;
            auto& source=sample.prepared->parts[part_index];
            NativeDepthSelection selected;
            selected.mesh=&source.mesh;selected.material=&source.material;
            selected.identity={sample.lifecycle.id,sample.lifecycle.generation,sample.lifecycle.positive_world_epoch,
                sample.resource.serial,static_cast<UINT>(part_index),0,frame.serial,true};
            selected.revision=value->depth_revision;selected.bounds=value->bounds;
            // Shadow publication already compared immutable depth sources and current pose.
            // An assembly change unrelated to this part need not reconstruct it.
            selected.source_revision=source.record.world.geometry_radius==source.geometry_radius ? source.record_revision : 0;
            selected.producer_index=static_cast<UINT>(index);selected.producer_part=static_cast<UINT>(part_index);
            selected.mask=mask;frame.depth_selected.push_back(selected);
    }
    return true;
}

bool collect_owned_missing_static_depth(
    const std::vector<RetainedShadowCasterValue>& missing,std::uint8_t dirty) {
    auto& frame=g_owned_visual_world_frame;
    for(const auto& caster:missing) {
        const auto found=std::lower_bound(frame.values->entities.begin(),
            frame.values->entities.end(),caster.revision.signed_id,
            [](const RetainedVisualSample& sample,std::int32_t id) {
                return sample.lifecycle.id<id;
            });
        if(found==frame.values->entities.end() ||
           found->lifecycle.id!=caster.revision.signed_id)return false;
        const auto index=static_cast<std::size_t>(found-frame.values->entities.begin());
        const auto part=caster.revision.part;
        auto& prepared=frame.prepared[index];
        if(!prepared.bounds.valid || part>=prepared.parts.size() ||
           !prepared.parts[part].casts || !prepare_owned_visual_pose(*found))return false;
        const OwnedVisualPartPreparation* value=nullptr;
        if(!prepare_owned_visual_part(index,part,value))return false;
        NativeShadowCasterRevision revision{};
        if(!retained_visual_depth_revision(*found,part,frame.values->time,revision) ||
           revision!=caster.revision)return false;
        const auto mask=static_cast<std::uint8_t>(caster.mask&dirty);
        if(!mask)continue;
        auto& source=found->prepared->parts[part];
        NativeDepthSelection selected{};
        selected.mesh=&source.mesh;selected.material=&source.material;
        selected.identity={found->lifecycle.id,found->lifecycle.generation,
            found->lifecycle.positive_world_epoch,found->resource.serial,
            part,0,frame.serial,true};
        selected.revision=revision;selected.bounds=value->bounds;
        selected.source_revision=source.record.world.geometry_radius==
            source.geometry_radius ? source.record_revision : 0;
        selected.producer_index=static_cast<UINT>(index);
        selected.producer_part=part;
        selected.mask=mask;selected.persistent_static=true;
        frame.depth_selected.push_back(selected);
    }
    return true;
}

bool build_owned_point_shadow_faces(const RetainedWorldPointLight& light,
    unsigned first_face,NativeShadowFrame& result) noexcept {
    if(first_face!=0&&first_face!=4)return false;
    NativePointShadowFrame cube;
    if(!build_owned_point_shadow_cube(light,cube))return false;
    result={};
    for(unsigned face=0;face<4&&first_face+face<6;++face)result.slices[face]=cube.slices[first_face+face];
    return true;
}

struct OwnedTerrainShadowCommand {
    OwnedTerrainCommand command;
    NativeWorldBounds bounds;
    NativeWorldBounds light_bounds;
    std::uint8_t mask=0;
    bool alpha_uv=false;
    std::shared_ptr<const OwnedVisualSurface> texture;
    OwnedTerrainPreparedPart* prepared=nullptr; // Borrowed from pinned source owner for this call.
    NativeShadowCasterRevision revision;
};

} // namespace
bool capture_owned_terrain_shadow_material() noexcept {
    NativeFrameState pass{};
    auto& material=g_owned_terrain_frame.shadow_material;material={};
    if(g_native_sink_frame_state && g_native_sink_frame_state(g_native_sink_context,pass) &&
       pass.material.enabled) { material=owned_terrain_base_material(pass.material);return true; }
    if(g_visual_publication_active) report_object_plan_failure("owned_shadow_terrain_material_unavailable");
    return false;
}
namespace {

void report_owned_terrain_shadow_material(std::uint32_t material,const char* reason) noexcept {
    char text[128]{};
    _snprintf_s(text,sizeof(text),_TRUNCATE,"owned_shadow_terrain_material_%u_%s",material,reason);
    report_object_plan_failure(text);
}

bool terrain_shadow_alpha_inputs(const NativeOwnedFixedState& state,bool& texture,bool& fog) noexcept {
    texture=false;fog=false;
    if(state.alpha_func<D3DCMP_NEVER || state.alpha_func>D3DCMP_ALWAYS)return false;
    // An unconditional comparison has no texture/UV dependency, even if the
    // inherited stage expression normally reads the texture.
    if(state.alpha_func==D3DCMP_ALWAYS)return true;
    const auto argument=[&](std::uint32_t value) {
        if((value&~0x3fu) || (value&0xfu)>D3DTA_SPECULAR)return false;
        texture|=(value&0xfu)==D3DTA_TEXTURE;
        fog|=(value&0xfu)==D3DTA_SPECULAR;return true;
    };
    switch(state.alpha_op) {
    case D3DTOP_DISABLE:return true;
    case D3DTOP_SELECTARG1:return argument(state.alpha_arg1);
    case D3DTOP_SELECTARG2:return argument(state.alpha_arg2);
    case D3DTOP_MODULATE:case D3DTOP_MODULATE2X:case D3DTOP_ADD:
        return argument(state.alpha_arg1) && argument(state.alpha_arg2);
    default:return false;
    }
}

std::shared_ptr<const OwnedVisualSurface> owned_terrain_shadow_texture(
    std::uint32_t material,RetainedStaticScene& scene) {
#ifdef EID3D11_EMBEDDED
    auto& frame=g_owned_terrain_frame;
    std::shared_ptr<const OwnedVisualSurface> value;
    const char* failure=nullptr;
    if(!bridge_terrain_atlas(material,value,&failure) || !value) {
        embedded_report_render_failure("embedded_input_missing",failure?failure:"terrain_shadow_texture");
        return nullptr;
    }
    if(!scene.publish_terrain_texture(frame.source_values.source_generation,
        frame.source_values.source_revision,material,value)) {
        embedded_report_render_failure("embedded_input_rejected","terrain_shadow_texture");
        return nullptr;
    }
    frame.source_values.textures=scene.terrain_source_snapshot().textures;
    return value;
#else
    auto& frame=g_owned_terrain_frame;
    if(material>=frame.shadow_texture_observed.size())return nullptr;
    if(!frame.shadow_texture_observed[material]) {
        frame.shadow_texture_observed[material]=2; // unavailable for this packet until publication succeeds
        void* texture=nullptr;IDirectDrawSurface7* surface=nullptr;std::uint32_t flags=0;
        std::shared_ptr<const OwnedVisualSurface> value;
        // One bounded source read per required material in this completed world
        // packet, independent of color admission. The Surface owner avoids any
        // new byte copy for an unchanged generation and publishes later writes.
        if(g_scene_terrain_shadow_textures) {
            // Scene thread: the game thread read the map's atlas surfaces
            // when it queued this command.
            if(material<g_scene_terrain_shadow_textures->size())value=(*g_scene_terrain_shadow_textures)[material];
        } else if(!(texture=owned_terrain_texture_wrapper(material)) ||
           !visual_texture_surface(texture,surface,flags) || !surface || !g_visual_surface_reader ||
           !g_visual_surface_reader(surface,value))value.reset();
        if(!scene.publish_terrain_texture(frame.source_values.source_generation,
            frame.source_values.source_revision,material,value))return nullptr;
        frame.source_values.textures=scene.terrain_source_snapshot().textures;
        if(value)frame.shadow_texture_observed[material]=1;
    }
    if(frame.shadow_texture_observed[material]!=1)return nullptr;
    const auto& textures=frame.source_values.textures;
    if(!textures)return nullptr;
    const auto found=textures->find(material);
    return found!=textures->end() && found->second.available ? found->second.surface : nullptr;
#endif
}

bool same_shadow_terrain_inputs(const OwnedTerrainFrameInputs& a,const OwnedTerrainFrameInputs& b) noexcept;
bool same_shadow_static_animation(const NativeTerrainState& a,const NativeTerrainState& b,bool wave_active) noexcept;

// Directional terrain caster values are a pure function of the terrain frame
// inputs, the terrain source/animation, the shadow depth cull/fill state and the coverage
// fields the slice mask reads. When all of them match the previous full build,
// its casters and commands are reused verbatim (a still camera rebuilt ~3,000
// terrain casters every frame).
struct TerrainShadowCasterCache {
    bool valid=false;
    OwnedTerrainFrameInputs inputs{};
    std::uint64_t source_generation=0,source_revision=0,structural_revision=0;
    std::array<float,16> object_to_world{};std::array<std::uint32_t,2> depth_state{}; // the only material fields the builder reads
    std::array<std::array<float,3>,3> light_axes{};bool spatial_selection=false;std::uint32_t map_side=0;
    std::array<NativeWorldBounds,5> receivers{};std::array<float,5> radius{};
    std::array<std::array<std::uint64_t,256>,2> far_cells{};
    std::vector<RetainedShadowCasterValue> casters;std::vector<OwnedTerrainShadowCommand> commands;
};
TerrainShadowCasterCache g_terrain_shadow_caster_cache;
bool same_world_bounds(const NativeWorldBounds& a,const NativeWorldBounds& b) noexcept {
    return a.valid==b.valid&&(!a.valid||(a.minimum==b.minimum&&a.maximum==b.maximum));
}

bool append_owned_terrain_shadow_values_uncached(std::vector<RetainedShadowCasterValue>& casters,
    std::vector<OwnedTerrainShadowCommand>& commands,RetainedStaticScene& scene,
    const std::vector<std::uint32_t>* selected_sectors,const NativePointShadowFrame* point_faces,bool* unused,
    const std::vector<std::uint32_t>* point_members);

// Terrain sun casters per spatial part, kept across frames. A pan moves the
// sun maps every frame, which missed the whole-list cache below and rebuilt
// every part (bounds, light-space bounds, render and far commands: about
// 1 ms per pan frame in zone18). A part's light bounds depend only on its
// geometry and the sun axes, its commands only on its sector's LOD and
// stitching; only its slice mask follows the moving maps, so that is all a
// pan recomputes. A detail's prepared depth part is found once per command:
// the prepared table's nodes never move or go away (unordered_map, never
// erased), and the cache names the table it was found in by its control
// block (prepared_owner: a weak reference, so it neither keeps an old map's
// resources alive nor can match a later table); another table clears every
// part. The hash lookup per emitted caster on every pan frame was ~2% of the
// main thread.
// Every write of a terrain prepared part's depth values or revision bumps
// this serial. Within one TerrainShadowPartCache the depth values are fixed
// (its keys hold the placement and depth state), so a detail that confirmed
// its part's revision needs to read the part again only after a write. The
// read was a random hash-node access per emitted caster: 0.42-0.52 ms on
// zone3 frames emitting ~3,100 casters.
std::uint64_t g_terrain_depth_writes=0;
struct TerrainShadowPartCache {
    // depth_revision: the prepared part's depth revision this detail last
    // confirmed against the cache's depth values, valid while no terrain
    // depth record has been written since (g_terrain_depth_writes).
    struct Detail {
        bool present=false; OwnedTerrainCommand command{}; OwnedTerrainPreparedPart* prepared=nullptr;
        std::uint64_t depth_revision=0,depth_writes=0;
    };
    struct Part {
        bool valid=false;
        std::uint32_t sector=UINT32_MAX,material=0;std::uint8_t subparts=0;
        std::uint32_t signature=UINT32_MAX; // near command lod | stitch bits
        NativeWorldBounds bounds{},light_bounds{};
        std::array<Detail,2> details{};
    };
    bool valid=false;
    std::uint64_t source_generation=0,source_revision=0,structural_revision=0;
    std::array<float,16> object_to_world{};std::array<std::uint32_t,2> depth_state{};
    std::array<std::array<float,3>,3> light_axes{};
    std::weak_ptr<OwnedTerrainPreparedResources> prepared_owner;
    std::vector<Part> parts;
    // Spatial indices in caster order (sector, material, subpart): the spatial
    // list is material-major, so emitting in list order left a full sort of
    // every terrain caster on each pan frame (~2% of the main thread). Stale
    // after a reset or a changed part; a stale order only costs the sort.
    std::vector<std::uint32_t> order;
    bool order_stale=true;
    std::size_t emitted=0; // casters of the last build (reserve size)
};
TerrainShadowPartCache g_terrain_shadow_part_cache;

bool append_owned_terrain_shadow_values_incremental(std::vector<RetainedShadowCasterValue>& casters,
    std::vector<OwnedTerrainShadowCommand>& commands) {
    auto& frame=g_owned_terrain_frame;const auto& source=*frame.source_values.terrain;
    const auto& coverage=g_owned_visual_world_frame.coverage;
    const auto& geometry=*frame.source_values.geometry;
    if(!frame.shadow_material.enabled){report_owned_terrain_shadow_material(UINT_MAX,"base_depth_state_unavailable");return false;}
    if(!frame.source_values.prepared)return false;
    auto& cache=g_terrain_shadow_part_cache;
    const std::array<std::uint32_t,2> depth_state{frame.shadow_material.cull_mode,frame.shadow_material.fill_mode};
    if(!cache.valid || cache.source_generation!=frame.source_values.source_generation ||
       cache.source_revision!=frame.source_values.source_revision ||
       cache.structural_revision!=geometry.structural_revision || cache.object_to_world!=frame.object_to_world ||
       cache.depth_state!=depth_state ||
       cache.prepared_owner.owner_before(frame.source_values.prepared) ||
       frame.source_values.prepared.owner_before(cache.prepared_owner)) {
        cache.parts.clear();
        cache.source_generation=frame.source_values.source_generation;cache.source_revision=frame.source_values.source_revision;
        cache.structural_revision=geometry.structural_revision;cache.object_to_world=frame.object_to_world;
        cache.depth_state=depth_state;cache.light_axes=coverage.light_axes;
        cache.prepared_owner=frame.source_values.prepared;cache.valid=true;cache.order_stale=true;
    } else if(cache.light_axes!=coverage.light_axes) {
        // A sun move changes only the light-space bounds; bounds, commands,
        // far commands and prepared parts follow geometry, LOD and the
        // prepared table, which the keys above hold (zone3 rebuilt all
        // ~3,300 parts every ~42 presents for this: ~2.4 ms).
        for(auto& part:cache.parts)
            if(part.valid)retained_world_light_contribution(part.bounds,true,coverage,part.light_bounds);
        cache.light_axes=coverage.light_axes;
    }
    // The same depth values for every terrain caster of this call.
    OwnedTerrainPreparedPart::DepthValues depth{};
    depth.object_to_world=frame.object_to_world;
    depth.state[0]=frame.shadow_material.cull_mode;depth.state[1]=frame.shadow_material.fill_mode;
    depth.state[2]=static_cast<UINT>(NativeOpacityClass::opaque);depth.alpha=1;
    const auto& spatial_parts=frame.front_end.spatial_parts;
    cache.parts.resize(spatial_parts.size());
    if(cache.order_stale || cache.order.size()!=spatial_parts.size()) {
        cache.order.resize(spatial_parts.size());
        for(std::size_t index=0;index<spatial_parts.size();++index)cache.order[index]=static_cast<std::uint32_t>(index);
        std::stable_sort(cache.order.begin(),cache.order.end(),[&](std::uint32_t a,std::uint32_t b) {
            const auto& x=spatial_parts[a];const auto& y=spatial_parts[b];
            if(x.source_sector!=y.source_sector)return x.source_sector<y.source_sector;
            if(x.material!=y.material)return x.material<y.material;
            return x.subpart_mask<y.subpart_mask;
        });
        cache.order_stale=false;
    }
    // Sized by the previous emission (its demand), so the outputs do not
    // regrow while ~3,000 casters are appended.
    const std::size_t first_emitted=casters.size();
    const std::size_t expected=(std::max)(cache.emitted,spatial_parts.size()/4);
    casters.reserve(casters.size()+expected);commands.reserve(commands.size()+expected);
    auto& prepared_parts=frame.source_values.prepared->parts;
    for(const auto spatial_index:cache.order) {
        const auto& spatial=spatial_parts[spatial_index];
        const auto sector=spatial.source_sector;
        const auto material=static_cast<std::uint32_t>(spatial.material);
        if(!owned_terrain_base_shadow_material(source,material))continue;
        unsigned first=0;
        while(first<4 && !(spatial.subpart_mask&(1u<<first)))++first;
        if(first==4)return false;
        auto& part=cache.parts[spatial_index];
        if(!part.valid || part.sector!=sector || part.material!=material || part.subparts!=spatial.subpart_mask) {
            if(part.valid)cache.order_stale=true;
            part=TerrainShadowPartCache::Part{};
            OwnedTerrainCommand probe{};
            probe.source_sector=sector;probe.material=static_cast<std::uint16_t>(material);
            probe.subpart_mask=spatial.subpart_mask;probe.spatial_index=static_cast<std::uint32_t>(spatial_index);
            NativeWorldBounds local{};
            if(!owned_terrain_spatial_bounds(frame.front_end,probe,local,part.bounds))return false;
            retained_world_light_contribution(part.bounds,true,coverage,part.light_bounds);
            part.sector=sector;part.material=material;part.subparts=spatial.subpart_mask;part.valid=true;
        }
        ++frame.front_end.spatial_caster_reuses;
        if(g_bridge_sample.active)++g_bridge_sample.terrain_spatial_bounds_reused;
        ++g_owned_visual_world_frame.shadow_considered;
        const auto mask=part.light_bounds.valid?retained_light_mask(part.light_bounds,coverage):std::uint8_t(0);
        if(!mask)continue;
        ++g_owned_visual_world_frame.shadow_eligible;
        // Commands follow the sector's current LOD and stitching.
        OwnedTerrainCommand near_command{};
        if(!owned_terrain_render_command(frame.front_end,sector,material,spatial.subpart_mask,near_command))return false;
        const std::uint32_t signature=near_command.lod|(near_command.lu_higher?4u:0u)|(near_command.rd_higher?8u:0u);
        if(part.signature!=signature) {
            near_command.spatial_index=static_cast<std::uint32_t>(spatial_index);
            part.details[0]={true,near_command,nullptr};part.signature=signature;
        }
        if((mask&0x18u) && !part.details[1].present) {
            OwnedTerrainCommand far_command{};
            if(!owned_terrain_far_shadow_command(frame.front_end,sector,material,spatial.subpart_mask,far_command))return false;
            far_command.spatial_index=static_cast<std::uint32_t>(spatial_index);
            part.details[1]={true,far_command,nullptr};
        }
        // Near before far unless the far variant sorts first (same part key
        // order as the caster sort below).
        const auto variant_of=[](const OwnedTerrainCommand& command) {
            return UINT(command.lod)|(command.lu_higher?4u:0u)|(command.rd_higher?8u:0u);};
        const bool far_first=(mask&0x07u) && (mask&0x18u) && part.details[0].present && part.details[1].present &&
            (part.details[1].command.subpart_mask<part.details[0].command.subpart_mask ||
             (part.details[1].command.subpart_mask==part.details[0].command.subpart_mask &&
              variant_of(part.details[1].command)<variant_of(part.details[0].command)));
        for(unsigned step=0;step<2;++step) {
            const unsigned detail=far_first?1u-step:step;
            const auto detail_mask=static_cast<std::uint8_t>(mask&(detail==0?0x07u:0x18u));
            auto& cached=part.details[detail];
            if(!detail_mask || !cached.present)continue;
            OwnedTerrainShadowCommand item{};
            item.command=cached.command;item.bounds=part.bounds;item.light_bounds=part.light_bounds;
            item.mask=detail_mask;
            const UINT variant=item.command.lod|(item.command.lu_higher?4u:0u)|(item.command.rd_higher?8u:0u);
            if(!cached.prepared || !cached.depth_revision || cached.depth_writes!=g_terrain_depth_writes) {
                if(!cached.prepared) {
                    const std::array<UINT,4> key{sector,material,item.command.subpart_mask,variant};
                    if(prepared_parts.size()>=65536 && prepared_parts.find(key)==prepared_parts.end())return false;
                    cached.prepared=&prepared_parts[key];
                }
                auto& prepared=*cached.prepared;
                if(!prepared.depth_revision || !(prepared.depth_values==depth)){
                    prepared.depth_values=depth;prepared.depth_revision=next_retained_shadow_revision();
                    ++g_terrain_depth_writes;
                }
                cached.depth_revision=prepared.depth_revision;cached.depth_writes=g_terrain_depth_writes;
            }
            RetainedShadowCasterValue value{};value.bounds=item.bounds;value.mask=detail_mask;
            value.light_bounds=item.light_bounds;
            value.revision={static_cast<std::int32_t>(sector),(material<<8u)|(UINT(item.command.subpart_mask)<<4u)|variant,
                frame.source_values.source_generation,frame.source_values.source_generation,
                frame.source_values.source_revision,cached.depth_revision,true};
            item.prepared=cached.prepared;item.revision=value.revision;
            casters.push_back(std::move(value));commands.push_back(std::move(item));
        }
    }
    cache.emitted=casters.size()-first_emitted;
    return true;
}

bool append_owned_terrain_shadow_values(std::vector<RetainedShadowCasterValue>& casters,
    std::vector<OwnedTerrainShadowCommand>& commands,RetainedStaticScene& scene,
    const std::vector<std::uint32_t>* selected_sectors=nullptr,
    const NativePointShadowFrame* point_faces=nullptr,bool* unused=nullptr,
    const std::vector<std::uint32_t>* point_members=nullptr) {
    if(selected_sectors||point_faces||point_members)
        return append_owned_terrain_shadow_values_uncached(casters,commands,scene,selected_sectors,point_faces,unused,point_members);
    auto& frame=g_owned_terrain_frame;auto& cache=g_terrain_shadow_caster_cache;
    const auto& coverage=g_owned_visual_world_frame.coverage;
    const auto& geometry=*frame.source_values.geometry;
    const auto matches=[&]() noexcept {
        const auto& ci=cache.inputs;const auto& fi=frame.front_end.inputs;
        if(!cache.valid)return false;
        if(!(ci.camera_position==fi.camera_position&&ci.terrain_origin==fi.terrain_origin&&ci.lod_distances==fi.lod_distances&&ci.maximum_lod==fi.maximum_lod&&ci.force_full_detail==fi.force_full_detail))return false;
        if(ci.game_visibility_fingerprint!=fi.game_visibility_fingerprint)return false;
        if(!(ci.game_visibility_rows==fi.game_visibility_rows&&ci.game_visibility_min_y==fi.game_visibility_min_y))return false;
        if(ci.object_to_clip!=fi.object_to_clip)return false;
        if(!same_shadow_terrain_inputs(ci,fi))return false;
        // The builder reads no animation state (waves displace in the shader and
        // are re-rendered through the dirty path) and casters are base terrain,
        // which no layer shift moves.
        if(cache.source_generation!=frame.source_values.source_generation||cache.source_revision!=frame.source_values.source_revision||
           cache.structural_revision!=geometry.structural_revision||cache.object_to_world!=frame.object_to_world)return false;
        if(cache.depth_state!=std::array<std::uint32_t,2>{frame.shadow_material.cull_mode,frame.shadow_material.fill_mode})return false;
        if(cache.light_axes!=coverage.light_axes||cache.spatial_selection!=coverage.spatial_selection||cache.map_side!=coverage.map_side)return false;
        for(unsigned i=0;i<5;++i)
            if(!same_world_bounds(cache.receivers[i],coverage.slices[i].receivers_in_light)||cache.radius[i]!=coverage.slices[i].light_radius)return false;
        if(!(cache.far_cells[0]==coverage.slices[3].far_receiver_cells&&cache.far_cells[1]==coverage.slices[4].far_receiver_cells))return false;
        return true;
    };
    if(coverage.valid&&matches()){
        casters.insert(casters.end(),cache.casters.begin(),cache.casters.end());
        commands.insert(commands.end(),cache.commands.begin(),cache.commands.end());
        return true;
    }
    const auto first_caster=casters.size(),first_command=commands.size();
    cache.valid=false;
    if(!append_owned_terrain_shadow_values_incremental(casters,commands))return false;
    if(!coverage.valid)return true;
    // Stored in caster order (all terrain: sector, then part), so a frame
    // that reuses it sorts only its dynamic casters. The incremental builder
    // emits in this order; any sorted sequence is a valid result of the sort.
    const auto caster_order=[](const auto& a,const auto& b) {
        if(a.revision.signed_id!=b.revision.signed_id)return a.revision.signed_id<b.revision.signed_id;
        return a.revision.part<b.revision.part;
    };
    if(!std::is_sorted(casters.begin()+first_caster,casters.end(),caster_order))
        std::sort(casters.begin()+first_caster,casters.end(),caster_order);
    try {
        cache.casters.assign(casters.begin()+first_caster,casters.end());
        cache.commands.assign(commands.begin()+first_command,commands.end());
        cache.inputs=frame.front_end.inputs;
        cache.source_generation=frame.source_values.source_generation;cache.source_revision=frame.source_values.source_revision;
        cache.structural_revision=geometry.structural_revision;cache.object_to_world=frame.object_to_world;
        cache.depth_state={frame.shadow_material.cull_mode,frame.shadow_material.fill_mode};
        cache.light_axes=coverage.light_axes;cache.spatial_selection=coverage.spatial_selection;cache.map_side=coverage.map_side;
        for(unsigned i=0;i<5;++i){cache.receivers[i]=coverage.slices[i].receivers_in_light;cache.radius[i]=coverage.slices[i].light_radius;}
        cache.far_cells[0]=coverage.slices[3].far_receiver_cells;cache.far_cells[1]=coverage.slices[4].far_receiver_cells;
        cache.valid=true;
    } catch(...) {cache.valid=false;}
    return true;
}

bool append_owned_terrain_shadow_values_uncached(std::vector<RetainedShadowCasterValue>& casters,
    std::vector<OwnedTerrainShadowCommand>& commands,RetainedStaticScene&,
    const std::vector<std::uint32_t>* selected_sectors,
    const NativePointShadowFrame* point_faces,bool*,
    const std::vector<std::uint32_t>* point_members) {
    auto& frame=g_owned_terrain_frame;const auto& source=*frame.source_values.terrain;
    // Base terrain is unconditional opaque depth; upper layers receive only.
    if(!frame.shadow_material.enabled){report_owned_terrain_shadow_material(UINT_MAX,"base_depth_state_unavailable");return false;}
    {   // Each spatial part emits up to two casters (near and far detail);
        // growing these vectors of large values dominated a rebuild.
        const auto members=point_members?point_members->size():frame.front_end.spatial_parts.size();
        casters.reserve(casters.size()+members*2);commands.reserve(commands.size()+members*2);
    }
        for(std::size_t member=0;member<(point_members?point_members->size():frame.front_end.spatial_parts.size());++member) {
                const auto spatial_index=point_members?(*point_members)[member]:member;
                const auto& spatial=frame.front_end.spatial_parts[spatial_index];
                const auto sector=spatial.source_sector;
                if(selected_sectors && !std::binary_search(selected_sectors->begin(),
                    selected_sectors->end(),sector))continue;
                const auto material=static_cast<std::uint32_t>(spatial.material);
                if(!owned_terrain_base_shadow_material(source,material))continue;
                unsigned part=0;
                while(part<4 && !(spatial.subpart_mask&(1u<<part)))++part;
                if(part==4)return false;
                OwnedTerrainShadowCommand item{};
                item.command.source_sector=sector;item.command.material=static_cast<std::uint16_t>(material);
                item.command.subpart_mask=spatial.subpart_mask;
                item.command.spatial_index=static_cast<std::uint32_t>(spatial_index);
                NativeWorldBounds local{};
                if(!owned_terrain_spatial_bounds(frame.front_end,item.command,local,item.bounds))return false;
                ++frame.front_end.spatial_caster_reuses;
                if(g_bridge_sample.active)++g_bridge_sample.terrain_spatial_bounds_reused;
                ++g_owned_visual_world_frame.shadow_considered;
                if(point_faces){
                    for(unsigned face=0;face<6;++face)
                        if(point_faces->slices[face].enabled && retained_world_bounds_visible(
                            item.bounds,point_faces->slices[face].world_to_light))
                            item.mask|=static_cast<std::uint8_t>(1u<<face);
                }else item.mask=retained_world_light_contribution(item.bounds,true,
                    g_owned_visual_world_frame.coverage,item.light_bounds);
                if(!item.mask)continue;
                ++g_owned_visual_world_frame.shadow_eligible;
                if(!owned_terrain_render_command(frame.front_end,sector,material,
                    spatial.subpart_mask,item.command))return false;
                item.command.spatial_index=static_cast<std::uint32_t>(spatial_index);
                const auto original=item;
                for(unsigned detail=0;detail<(point_faces?1u:2u);++detail) {
                item=original;
                item.mask=point_faces?original.mask:static_cast<std::uint8_t>(
                    original.mask&(detail==0?0x07u:0x18u));
                if(!item.mask)continue;
                if(detail==1) {
                    if(!owned_terrain_far_shadow_command(frame.front_end,sector,material,
                        spatial.subpart_mask,item.command))return false;
                    item.command.spatial_index=static_cast<std::uint32_t>(spatial_index);
                }
                RetainedShadowCasterValue value{};value.bounds=item.bounds;value.mask=item.mask;
                value.light_bounds=item.light_bounds;
                OwnedTerrainPreparedPart::DepthValues depth{};
                depth.object_to_world=frame.object_to_world;
                depth.state[0]=frame.shadow_material.cull_mode;depth.state[1]=frame.shadow_material.fill_mode;
                depth.state[2]=static_cast<UINT>(NativeOpacityClass::opaque);depth.alpha=1;
                // The caster revision below pins immutable ground topology by
                // source generation/revision. Liquid-depth colour metadata
                // changes the shared mesh, but cannot invalidate opaque depth.
                const UINT variant=item.command.lod|(item.command.lu_higher?4u:0u)|(item.command.rd_higher?8u:0u);
                const std::array<UINT,4> key{sector,material,item.command.subpart_mask,variant};
                if(!frame.source_values.prepared)return false;
                auto& parts=frame.source_values.prepared->parts;
                if(parts.size()>=65536 && parts.find(key)==parts.end())return false;
                auto& prepared=parts[key];
                if(!prepared.depth_revision || !(prepared.depth_values==depth)){
                    prepared.depth_values=depth;prepared.depth_revision=next_retained_shadow_revision();
                    ++g_terrain_depth_writes;
                }
                value.revision={static_cast<std::int32_t>(sector),(material<<8u)|(UINT(item.command.subpart_mask)<<4u)|variant,
                    frame.source_values.source_generation,frame.source_values.source_generation,
                    frame.source_values.source_revision,prepared.depth_revision,true};
                item.prepared=&prepared;item.revision=value.revision;
                casters.push_back(std::move(value));commands.push_back(std::move(item));
                }
            }
    return true;
}

template<class ShadowFrame>
bool submit_owned_terrain_shadow_parts(const OwnedTerrainShadowCommand* commands,std::size_t count,
    std::uint8_t dirty,const ShadowFrame& shadows,bool collect,NativeGeometryResult* failure=nullptr,
    const std::vector<std::uint8_t>* only=nullptr) {
    auto& frame=g_owned_terrain_frame;
    for(std::size_t index=0;index<count;++index) {
        if(only && (index>=only->size() || !(*only)[index]))continue;
        const auto& item=commands[index];
        std::uint8_t mask=item.mask&dirty;
        if(collect)mask=owned_static_scroll_mask(shadows,item.light_bounds,mask,item.revision.terrain?
            (std::uint64_t(std::uint32_t(item.revision.signed_id))<<32)|item.revision.part:0);
        // Registration (!collect) receives the mask this collection already
        // culled against the same frame's slices: no second visibility walk.
        if(collect)for(unsigned slice=0;slice<shadows.slices.size();++slice)
            if((mask&(1u<<slice)) && !retained_world_bounds_visible(item.bounds,shadows.slices[slice].world_to_light))
                mask&=static_cast<std::uint8_t>(~(1u<<slice));
        if(!mask)continue;
        if(collect) {
            if(!item.prepared)return false;
            const auto& source=frame.source_values;
            NativeDepthSelection selected;
            selected.mesh=&item.prepared->mesh;selected.material=&item.prepared->material;
            selected.identity={static_cast<std::int32_t>(item.command.source_sector),source.source_generation,
                source.source_generation,source.source_revision,
                (std::uint32_t(item.command.material)<<4u)|item.command.subpart_mask,0,
                frame.front_end.inputs.world_frame_serial,true,true};
            selected.revision=item.revision;selected.bounds=item.bounds;
            selected.source_revision=item.prepared->source_revision;
            selected.producer_index=static_cast<UINT>(index);selected.mask=mask;
            g_owned_visual_world_frame.depth_selected.push_back(selected);continue;
        }
        const auto reject=[&](const char* stage,const NativeGeometryResult* result=nullptr,
            const NativeOwnedTextureView* texture=nullptr) {
            const auto& source=frame.source_values;
            const NativePreparedIdentity identity{static_cast<std::int32_t>(item.command.source_sector),
                source.source_generation,source.source_generation,source.source_revision,
                (std::uint32_t(item.command.material)<<4u)|item.command.subpart_mask,0,
                frame.front_end.inputs.world_frame_serial,true,true};
            return reject_owned_shadow_part(stage,identity,result,texture);
        };
        OwnedTerrainSourceDecodeContext context{};NativeGeometryView* source_view=nullptr;
        const auto* phases=item.alpha_uv && item.command.source_sector<frame.dynamic.size() &&
            frame.dynamic[item.command.source_sector].captured ? &frame.dynamic[item.command.source_sector] : nullptr;
        if(!source_terrain_world_record(frame.source_values,frame.front_end,item.command,phases,
            {true,false,mask},context,source_view,item.alpha_uv)||!source_view)return reject("part_view");
        auto& view=*source_view;
        if(!apply_owned_terrain_world_transform(view))return reject("world_transform");
        view.owned_fixed_state=frame.shadow_material;
        auto& state=view.owned_fixed_state;
        state.z_enable=D3DZB_TRUE;state.z_write_enable=TRUE;state.z_func=D3DCMP_LESSEQUAL;
        state.enabled=true;state.alpha_test_enable=FALSE;
        state.alpha_blend_enable=FALSE;state.source_blend=D3DBLEND_ONE;state.destination_blend=D3DBLEND_ZERO;
        if(item.texture) {
            view.owned_texture.identity=item.texture->id;view.owned_texture.generation=item.texture->generation;
            view.owned_texture.surface=item.texture.get();
        }
        NativeGeometryResult result{};
        if(!g_native_sink_submit(g_native_sink_context,view,result)) {
            if(failure)*failure=result;
            return reject("sink",&result,&view.owned_texture);
        }
    }
    return true;
}

template<class ShadowFrame>
struct OwnedDepthRegistration {
    const std::vector<OwnedTerrainShadowCommand>& terrain;
    const ShadowFrame& shadows;
};

template<class ShadowFrame>
bool register_owned_depth_part(void* context,const NativeDepthSelection& selected,NativeGeometryResult& result) {
    const auto& registration=*static_cast<const OwnedDepthRegistration<ShadowFrame>*>(context);
    if(selected.identity.terrain_source) {
        if(selected.producer_index>=registration.terrain.size())return false;
        return submit_owned_terrain_shadow_parts(&registration.terrain[selected.producer_index],1,
            selected.mask,registration.shadows,false,&result);
    }
    auto& frame=g_owned_visual_world_frame;
    if(selected.producer_index>=frame.values->entities.size())return false;
    const auto& sample=frame.values->entities[selected.producer_index];
    NativePreparedPartSubmission* part=nullptr;
    const bool group=selected.producer_part>=kMergedPartBase;
    if(!(group ? retained_visual_group_submission(sample,selected.producer_part-kMergedPartBase,selected.bounds,
            {true,false,selected.mask},frame.values->time,255,part) :
        retained_visual_part_submission(sample,selected.producer_part,selected.bounds,
            {true,false,selected.mask},frame.values->time,255,part)))
        return reject_owned_shadow_part("part_record",selected.identity);
    const NativePreparedPartSubmission* parts[]={part};
    if(!submit_owned_visual_records(sample,parts,1,frame.coverage.world_to_clip,frame.serial,0,result))
        return reject_owned_shadow_part("sink",selected.identity,&result);
    return true;
}

} // namespace
void retire_owned_shadow_maps() noexcept {
    if(!g_native_sink_shadow_begin || !g_native_sink_shadow_end)return;
    try {
        NativeShadowFrame empty;std::uint8_t dirty=0;
        if(g_native_sink_shadow_begin(g_native_sink_context,empty,dirty))
            g_native_sink_shadow_end(g_native_sink_context,true);
    } catch(...) {}
}
namespace {

struct OwnedShadowSourceDelta {
    const RetainedShadowInputDomain* previous = nullptr;
    const std::vector<std::int32_t>* visuals = nullptr;
    const std::vector<std::uint32_t>* terrain = nullptr;
};

// Persistent static shadow casters: updated in place for journal-dirty
// objects, rebuilt on a reset. Every cascade bit is set; the GPU culls them
// per slice, and cascade fitting reads only the summary.
struct OwnedStaticCasterStore {
    std::vector<RetainedShadowCasterValue> casters;
    std::uint64_t revision = 0;
    std::uint64_t renderer_revision = 0;   // store revision the renderer last validated
    StaticShadowSummary summary;
    bool valid = false;
    // Per-map static revision (L-081): a map's static layer depends only on
    // the store casters inside its square, so a store change bumps only the
    // maps whose fitted or drawn square its old or new bounds reach. World
    // bounds of changed casters wait here until the next frame consumes them.
    std::vector<NativeWorldBounds> changed_bounds;
    bool changed_all = true;
    std::array<std::uint64_t,5> slice_revision{};
    // Projection each map was drawn with, reported after the last begin.
    std::array<std::array<float,16>,5> drawn{};
    std::uint8_t drawn_mask = 0;
};
OwnedStaticCasterStore g_owned_static_casters;

// Whether world bounds reach the [-1,1] light square of a sun map projection
// (orthographic rows 0 and 1).
bool owned_static_bounds_reach(const NativeWorldBounds& bounds,const std::array<float,16>& world_to_light) noexcept {
    if(!bounds.valid)return true;
    for(unsigned axis=0;axis<2;++axis){
        double value=world_to_light[axis*4+3],reach=0;
        for(unsigned component=0;component<3;++component){
            const double center=(double(bounds.minimum[component])+bounds.maximum[component])*.5;
            const double extent=(double(bounds.maximum[component])-bounds.minimum[component])*.5;
            value+=double(world_to_light[axis*4+component])*center;
            reach+=std::abs(double(world_to_light[axis*4+component]))*extent;
        }
        if(value-reach>1.0001 || value+reach< -1.0001)return false;
    }
    return true;
}

// Bumps the static revision of every map a pending store change reaches,
// in the square fitted this frame or the one its cached layer was drawn in,
// and publishes the per-map revisions through the summary.
void update_owned_static_slice_revisions(OwnedStaticCasterStore& store,
    const RetainedDirectionalCoverage& coverage) noexcept {
    for(unsigned index=0;index<5;++index){
        const auto& source=coverage.slices[index];
        if(!source.receivers_in_light.valid || !(source.light_radius>0))continue;
        bool touched=store.changed_all || !store.slice_revision[index];
        if(!touched){
            std::array<float,16> fitted{};
            for(unsigned axis=0;axis<2;++axis){
                for(unsigned component=0;component<3;++component)
                    fitted[axis*4+component]=coverage.light_axes[axis][component]/source.light_radius;
                fitted[axis*4+3]=-source.light_center[axis]/source.light_radius;
            }
            const bool drawn=(store.drawn_mask>>index)&1u;
            for(const auto& bounds:store.changed_bounds)
                if(owned_static_bounds_reach(bounds,fitted) ||
                   (drawn && owned_static_bounds_reach(bounds,store.drawn[index]))){touched=true;break;}
        }
        if(touched)store.slice_revision[index]=store.revision;
    }
    store.changed_bounds.clear();store.changed_all=false;
    store.summary.slice_revision=store.slice_revision;
}

bool append_owned_shadow_source_values(std::vector<RetainedShadowCasterValue>& casters,
    std::vector<OwnedTerrainShadowCommand>& terrain_commands,bool terrain_enabled,
    RetainedStaticScene& terrain_scene,const OwnedShadowSourceDelta* delta,
    const RetainedShadowSourceChanges* changes=nullptr,
    const RetainedShadowInputDomain* static_previous=nullptr) {
 PERF06_SCOPE("shadow_caster_values");
    auto& frame=g_owned_visual_world_frame;
    const auto order=[](const auto& a,const auto& b) {
        if(a.revision.terrain!=b.revision.terrain)return !a.revision.terrain;
        if(a.revision.signed_id!=b.revision.signed_id)
            return a.revision.signed_id<b.revision.signed_id;
        return a.revision.part<b.revision.part;
    };
    if(frame.persistent_static_shadows && changes) {
        auto& store=g_owned_static_casters;bool changed=false;
        if(static_previous && store.valid) {
            if(!changes->dirty_visuals.empty()) {
                const auto dirty=[&](const RetainedShadowCasterValue& value){
                    return std::binary_search(changes->dirty_visuals.begin(),changes->dirty_visuals.end(),
                        value.revision.signed_id);};
                // Removed values keep store (caster) order. The store holds
                // only visual casters sorted by (id, part), so each dirty id
                // is one range found by search, not a walk of every caster.
                std::vector<RetainedShadowCasterValue> removed;
                {
                    auto from=store.casters.begin();
                    for(const auto id:changes->dirty_visuals) {
                        from=std::lower_bound(from,store.casters.end(),id,
                            [](const RetainedShadowCasterValue& value,std::int32_t key){return value.revision.signed_id<key;});
                        auto to=from;
                        while(to!=store.casters.end() && to->revision.signed_id==id)++to;
                        removed.insert(removed.end(),from,to);from=to;
                    }
                }
                std::vector<RetainedShadowCasterValue> fresh;
                if(!append_owned_static_shadow_values(fresh,changes->time_active_visuals,&changes->dirty_visuals))return false;
                for(auto& value:fresh)value.mask=0x1f;
                std::sort(fresh.begin(),fresh.end(),order);
                // A journal-dirty object whose casters come back with the same
                // depth revision and bounds changes no shadow map; only the
                // differing casters' old and new bounds touch the maps.
                const auto same_bounds=[](const NativeWorldBounds& a,const NativeWorldBounds& b){
                    return a.valid==b.valid && a.minimum==b.minimum && a.maximum==b.maximum;};
                std::size_t i=0,j=0;bool differs=false;
                while(i<removed.size() || j<fresh.size()) {
                    if(j==fresh.size() || (i<removed.size() && order(removed[i],fresh[j]))) {
                        store.changed_bounds.push_back(removed[i].bounds);differs=true;++i;continue;
                    }
                    if(i==removed.size() || order(fresh[j],removed[i])) {
                        store.changed_bounds.push_back(fresh[j].bounds);differs=true;++j;continue;
                    }
                    if(removed[i].revision!=fresh[j].revision || !same_bounds(removed[i].bounds,fresh[j].bounds)) {
                        store.changed_bounds.push_back(removed[i].bounds);
                        store.changed_bounds.push_back(fresh[j].bounds);differs=true;
                    }
                    ++i;++j;
                }
                if(differs) {
                    store.casters.erase(std::remove_if(store.casters.begin(),store.casters.end(),dirty),
                        store.casters.end());
                    const auto middle=store.casters.size();
                    store.casters.insert(store.casters.end(),fresh.begin(),fresh.end());
                    std::inplace_merge(store.casters.begin(),store.casters.begin()+middle,store.casters.end(),order);
                    changed=true;
                }
                // Shadows off for a long stretch: stop listing, redraw all.
                if(store.changed_bounds.size()>4096){store.changed_bounds.clear();store.changed_all=true;}
            }
        } else {
            store.casters.clear();store.valid=false;
            store.changed_bounds.clear();store.changed_all=true;
            if(!append_owned_static_shadow_values(store.casters,changes->time_active_visuals))return false;
            for(auto& value:store.casters)value.mask=0x1f;
            std::sort(store.casters.begin(),store.casters.end(),order);
            store.valid=true;changed=true;
        }
        if(changed){if(++store.revision==0)++store.revision;store.summary.valid=false;}
        frame.shadow_source_diagnostic.static_casters_reused+=store.casters.size();
        if(g_bridge_sample.active)g_bridge_sample.shadow_static_casters_reused+=store.casters.size();
        // Per frame: only time-driven visuals and terrain. Terrain sorts after
        // every visual and arrives in order from its cache.
        if(!append_owned_visual_shadow_values(casters,nullptr,&changes->time_active_visuals))return false;
        const auto visual_end=static_cast<std::ptrdiff_t>(casters.size());
        std::sort(casters.begin(),casters.end(),order);
        if(terrain_enabled && !append_owned_terrain_shadow_values(casters,terrain_commands,terrain_scene))
            return false;
        if(!std::is_sorted(casters.begin()+visual_end,casters.end(),order))
            std::sort(casters.begin()+visual_end,casters.end(),order);
        return true;
    }
    if(!delta)return append_owned_visual_shadow_values(casters) &&
        (!terrain_enabled || append_owned_terrain_shadow_values(casters,terrain_commands,terrain_scene));
    casters=delta->previous->casters;
    casters.erase(std::remove_if(casters.begin(),casters.end(),[&](const auto& value) {
        return value.revision.terrain ?
            std::binary_search(delta->terrain->begin(),delta->terrain->end(),
                static_cast<std::uint32_t>(value.revision.signed_id)) :
            std::binary_search(delta->visuals->begin(),delta->visuals->end(),
                value.revision.signed_id);
    }),casters.end());
    frame.shadow_source_diagnostic.static_casters_reused+=casters.size();
    if(g_bridge_sample.active)g_bridge_sample.shadow_static_casters_reused+=casters.size();
    if(!append_owned_visual_shadow_values(casters,delta->visuals) ||
       (terrain_enabled && !append_owned_terrain_shadow_values(
           casters,terrain_commands,terrain_scene,delta->terrain)))return false;
    std::sort(casters.begin(),casters.end(),[](const auto& a,const auto& b) {
        if(a.revision.terrain!=b.revision.terrain)return !a.revision.terrain;
        if(a.revision.signed_id!=b.revision.signed_id)
            return a.revision.signed_id<b.revision.signed_id;
        return a.revision.part<b.revision.part;
    });
    return true;
}

// Far-cascade terrain shadow meshes ahead of time. The far cascades draw
// every caster sector at the fixed far command (LOD 2, no stitching), so
// that set is camera-independent and bounded by the map's spatial parts.
// Created on first use instead, a pan that moves the far cascades over new
// ground created up to 325 of them in one frame (frame accounting
// 2026-10-13: 163/163 and 325/325 first creations were terrain, depth-only,
// LOD 2; about 40 us each on the renderer thread). This walks the same parts
// and far commands as append_owned_terrain_shadow_values and asks the
// renderer for each mesh only (prepare_only), in time slices: long ones while
// the load is held, short ones after. A new terrain source restarts it.
struct TerrainFarShadowPrewarm {
    std::uint64_t source_generation = 0, source_revision = 0, structural_revision = 0;
    const void* prepared = nullptr;
    std::size_t cursor = 0, created = 0, failed = 0;
    std::int64_t ticks = 0;
    bool done = false;
    const char* first_failure = nullptr;   // the first failing stage, reported once
    HRESULT first_failure_hr = S_OK;
};
thread_local TerrainFarShadowPrewarm g_terrain_far_shadow_prewarm;
// Config terrain_far_shadow_prewarm (renderer begin_scene, every frame). Off
// stops the walk at once; meshes are then created on first use as before.
std::atomic<bool> g_terrain_far_shadow_prewarm_enabled{true};

void prewarm_owned_terrain_far_shadow_meshes() noexcept {
    if(!g_terrain_far_shadow_prewarm_enabled.load(std::memory_order_relaxed))return;
    try {
        auto& frame=g_owned_terrain_frame;
        if(!frame.source_values.terrain || !frame.source_values.geometry || !frame.source_values.prepared ||
           !frame.shadow_material.enabled || !g_native_sink_submit || !g_native_sink_context)return;
        auto& state=g_terrain_far_shadow_prewarm;
        const auto& geometry=*frame.source_values.geometry;
        if(state.source_generation!=frame.source_values.source_generation ||
           state.source_revision!=frame.source_values.source_revision ||
           state.structural_revision!=geometry.structural_revision ||
           state.prepared!=frame.source_values.prepared.get()) {
            state={};
            state.source_generation=frame.source_values.source_generation;
            state.source_revision=frame.source_values.source_revision;
            state.structural_revision=geometry.structural_revision;
            state.prepared=frame.source_values.prepared.get();
        }
        if(state.done)return;
        const std::uint64_t epoch=bridge_current_world_epoch();
        const bool held=g_load_reveal_enabled.load(std::memory_order_relaxed) && epoch &&
            epoch!=g_load_reveal_revealed_epoch.load(std::memory_order_relaxed);
        const std::int64_t frequency=static_cast<std::int64_t>(bridge_frequency());
        const std::int64_t budget=frequency*(held?25:2)/1000;
        const std::int64_t begin=bridge_now();
        const auto& source=*frame.source_values.terrain;
        const auto& spatial_parts=frame.front_end.spatial_parts;
        while(state.cursor<spatial_parts.size()) {
            const auto spatial_index=state.cursor++;
            const auto& spatial=spatial_parts[spatial_index];
            const auto material=static_cast<std::uint32_t>(spatial.material);
            if(!owned_terrain_base_shadow_material(source,material))continue;
            OwnedTerrainCommand far_command{};
            const auto failure=[&](const char* stage,HRESULT hr=S_OK) {
                ++state.failed;
                if(!state.first_failure){state.first_failure=stage;state.first_failure_hr=hr;}
            };
            if(!owned_terrain_far_shadow_command(frame.front_end,spatial.source_sector,material,spatial.subpart_mask,far_command)) {
                failure("far_command");continue;
            }
            far_command.spatial_index=static_cast<std::uint32_t>(spatial_index);
            OwnedTerrainSourceDecodeContext context{};NativeGeometryView* source_view=nullptr;
            if(!source_terrain_world_record(frame.source_values,frame.front_end,far_command,nullptr,
                   {true,false,0x18},context,source_view,false) || !source_view) { failure("world_record");continue; }
            if(!apply_owned_terrain_world_transform(*source_view)) { failure("world_transform");continue; }
            NativeGeometryView view=*source_view;
            view.prepare_only=true;
            NativeGeometryResult result{};
            if(g_native_sink_submit(g_native_sink_context,view,result))++state.created;
            else failure(result.failure_operation?result.failure_operation:"sink",result.failure_hresult);
            if(bridge_now()-begin>=budget)break;
        }
        state.ticks+=bridge_now()-begin;
        if(state.cursor>=spatial_parts.size()) {
            state.done=true;
            char detail[256];
            std::snprintf(detail,sizeof(detail),"parts=%zu prepared=%zu failed=%zu ms=%.1f first_failure=%s hr=%08lx",
                spatial_parts.size(),state.created,state.failed,1000.0*double(state.ticks)/double(frequency),
                state.first_failure?state.first_failure:"none",static_cast<unsigned long>(state.first_failure_hr));
            append_bridge_event("terrain_far_shadow_prewarm",detail,true);
        }
    } catch(...) {
        g_terrain_far_shadow_prewarm.done=true;
        append_bridge_event("terrain_far_shadow_prewarm","stopped: allocation failure",false);
    }
}

// Near terrain meshes ahead of the camera. The near sun maps (C0-C2) draw
// each base spatial part at its render command (LOD 0 with terrain LOD off),
// and transparent terrain draws each visible (sector, material) piece; both
// were created on first use, in bursts as a pan brought new ground into the
// near maps or the view (frame accounting 2026-10-14, Suslanger cascade
// route: every pan-frame terrain creation was LOD 0, up to 77 in one frame).
// Unlike the far set this depends on the camera, so it is a region around
// it: parts whose light bounds reach a near map's receiver square widened by
// kNearPrewarmLightMargin of its half extent per side, and transparent pieces
// (base pieces too with terrain LOD on) inside a camera frustum widened by
// kNearPrewarmViewMargin. Missing meshes are queued nearest first and created
// prepare-only after the sun pass in a slice sized by the queue (cleared in
// about eight frames, 0.3-2 ms; 25 ms while the load is held). A mesh the
// queue has not reached yet is still created on first use, as before.
struct TerrainNearPrewarm {
    std::uint64_t source_generation = 0, source_revision = 0, structural_revision = 0;
    const void* prepared = nullptr;
    // Per spatial part: the command variant + 1 last prepared (0 none).
    std::vector<std::uint32_t> depth_warmed, color_warmed;
    std::vector<std::pair<float,std::uint32_t>> queue;   // distance squared, spatial index | colour bit
    std::size_t head = 0;
    std::array<float,16> scanned_clip{};
    std::uint32_t frames_since_scan = 0;
    bool failure_reported = false;
};
thread_local TerrainNearPrewarm g_terrain_near_prewarm;
constexpr std::uint32_t kNearPrewarmColor = 0x80000000u;
constexpr float kNearPrewarmLightMargin = 0.5f;
constexpr float kNearPrewarmViewMargin = 0.5f;

void prewarm_owned_terrain_near_meshes() noexcept {
    if(!g_terrain_far_shadow_prewarm_enabled.load(std::memory_order_relaxed))return;
    try {
        auto& frame=g_owned_terrain_frame;
        if(!frame.source_values.terrain || !frame.source_values.geometry || !frame.source_values.prepared ||
           !frame.front_end.spatial_ready || !g_native_sink_submit || !g_native_sink_context)return;
        auto& state=g_terrain_near_prewarm;
        const auto& geometry=*frame.source_values.geometry;
        const auto& spatial_parts=frame.front_end.spatial_parts;
        if(state.source_generation!=frame.source_values.source_generation ||
           state.source_revision!=frame.source_values.source_revision ||
           state.structural_revision!=geometry.structural_revision ||
           state.prepared!=frame.source_values.prepared.get() ||
           state.depth_warmed.size()!=spatial_parts.size()) {
            state={};
            state.source_generation=frame.source_values.source_generation;
            state.source_revision=frame.source_values.source_revision;
            state.structural_revision=geometry.structural_revision;
            state.prepared=frame.source_values.prepared.get();
            state.depth_warmed.assign(spatial_parts.size(),0);
            state.color_warmed.assign(spatial_parts.size(),0);
            state.frames_since_scan=UINT32_MAX-1;
        }
        const auto& source=*frame.source_values.terrain;
        const auto& inputs=frame.front_end.inputs;
        const auto threshold=source.texture_count+2u;
        const auto variant_of=[](const OwnedTerrainCommand& command) {
            return std::uint32_t(command.lod)|(command.lu_higher?4u:0u)|(command.rd_higher?8u:0u);};
        // The (sector, material) piece starting at a spatial index: all its
        // parts, as the front end and the terrain set draw it.
        const auto piece_command=[&](std::uint32_t first,OwnedTerrainCommand& command) {
            const auto sector=spatial_parts[first].source_sector;const auto material=spatial_parts[first].material;
            std::uint8_t mask=0;
            for(std::size_t index=first;index<spatial_parts.size() && spatial_parts[index].source_sector==sector &&
                spatial_parts[index].material==material;++index)mask=static_cast<std::uint8_t>(mask|spatial_parts[index].subpart_mask);
            if(!owned_terrain_render_command(frame.front_end,sector,material,mask,command))return false;
            command.spatial_index=first;return true;
        };
        const auto part_command=[&](std::uint32_t index,OwnedTerrainCommand& command) {
            const auto& spatial=spatial_parts[index];
            if(!owned_terrain_render_command(frame.front_end,spatial.source_sector,spatial.material,spatial.subpart_mask,command))
                return false;
            command.spatial_index=index;return true;
        };
        if(state.frames_since_scan<UINT32_MAX)++state.frames_since_scan;
        // Rescan after the camera moved; at most every fourth frame, since the
        // margins hold several frames of pan.
        if(inputs.object_to_clip!=state.scanned_clip && state.frames_since_scan>=4) {
            state.queue.clear();state.head=0;state.frames_since_scan=0;state.scanned_clip=inputs.object_to_clip;
            const auto& coverage=g_owned_visual_world_frame.coverage;
            std::array<NativeWorldBounds,3> near_squares{};
            if(coverage.valid && coverage.directional)
                for(unsigned slice=0;slice<3;++slice) {
                    auto value=coverage.slices[slice].receivers_in_light;
                    if(!value.valid)continue;
                    for(unsigned axis=0;axis<2;++axis) {
                        const float margin=(value.maximum[axis]-value.minimum[axis])*.5f*kNearPrewarmLightMargin;
                        value.minimum[axis]-=margin;value.maximum[axis]+=margin;
                    }
                    near_squares[slice]=value;
                }
            const auto& part_cache=g_terrain_shadow_part_cache;
            const bool light_ready=coverage.valid && part_cache.valid &&
                part_cache.source_generation==frame.source_values.source_generation &&
                part_cache.source_revision==frame.source_values.source_revision &&
                part_cache.structural_revision==geometry.structural_revision &&
                part_cache.light_axes==coverage.light_axes && part_cache.parts.size()==spatial_parts.size();
            // Terrain LOD off: every command is LOD 0 unstitched, so a part prepared
            // once needs no command to be checked again.
            const bool fixed_variant=inputs.force_full_detail;
            auto clip=inputs.object_to_clip;
            for(unsigned index=0;index<8;++index)clip[index]/=1.f+kNearPrewarmViewMargin;
            const auto distance=[&](const NativeWorldBounds& bounds) {
                float sum=0;
                for(unsigned axis=0;axis<3;++axis) {
                    const float value=inputs.terrain_origin[axis]+(bounds.minimum[axis]+bounds.maximum[axis])*.5f-
                        inputs.camera_position[axis];
                    sum+=value*value;
                }
                return sum;
            };
            for(std::size_t begin=0;begin<spatial_parts.size();) {
                const auto sector=spatial_parts[begin].source_sector;const auto material=spatial_parts[begin].material;
                std::size_t end=begin;
                while(end<spatial_parts.size() && spatial_parts[end].source_sector==sector &&
                      spatial_parts[end].material==material)++end;
                const auto first=static_cast<std::uint32_t>(begin);
                // With terrain LOD off the base pieces are the persistent
                // terrain set, created when it is registered.
                if((material>=threshold || !inputs.force_full_detail) && !(fixed_variant && state.color_warmed[first])) {
                    float nearest=INFINITY;
                    for(std::size_t index=begin;index<end;++index)
                        if(retained_world_bounds_visible(spatial_parts[index].local_bounds,clip))
                            nearest=(std::min)(nearest,distance(spatial_parts[index].local_bounds));
                    OwnedTerrainCommand command{};
                    if(nearest<INFINITY && piece_command(first,command) && state.color_warmed[first]!=variant_of(command)+1u)
                        state.queue.push_back({nearest,first|kNearPrewarmColor});
                }
                if(light_ready && material<threshold && owned_terrain_base_shadow_material(source,material))
                    for(std::size_t index=begin;index<end;++index) {
                        const auto& light=part_cache.parts[index];
                        if(!light.valid || !light.light_bounds.valid || (fixed_variant && state.depth_warmed[index]))continue;
                        const auto& caster=light.light_bounds;
                        bool reaches=false;
                        for(const auto& receiver:near_squares)
                            if(receiver.valid && caster.minimum[2]<=receiver.maximum[2] &&
                               caster.maximum[0]>=receiver.minimum[0] && caster.minimum[0]<=receiver.maximum[0] &&
                               caster.maximum[1]>=receiver.minimum[1] && caster.minimum[1]<=receiver.maximum[1]){reaches=true;break;}
                        OwnedTerrainCommand command{};
                        if(reaches && part_command(static_cast<std::uint32_t>(index),command) &&
                           state.depth_warmed[index]!=variant_of(command)+1u)
                            state.queue.push_back({distance(spatial_parts[index].local_bounds),static_cast<std::uint32_t>(index)});
                    }
                begin=end;
            }
            std::sort(state.queue.begin(),state.queue.end());
        }
        if(state.head>=state.queue.size())return;
        const std::uint64_t epoch=bridge_current_world_epoch();
        const bool held=g_load_reveal_enabled.load(std::memory_order_relaxed) && epoch &&
            epoch!=g_load_reveal_revealed_epoch.load(std::memory_order_relaxed);
        const std::int64_t frequency=static_cast<std::int64_t>(bridge_frequency());
        const std::int64_t remaining=static_cast<std::int64_t>(state.queue.size()-state.head);
        const std::int64_t budget_us=held?25000:std::clamp<std::int64_t>(remaining*40/8,300,2000);
        const std::int64_t budget=frequency*budget_us/1000000;
        const std::int64_t begin=bridge_now();
        while(state.head<state.queue.size()) {
            const auto entry=state.queue[state.head++].second;
            const bool color=(entry&kNearPrewarmColor)!=0;
            const auto index=entry&~kNearPrewarmColor;
            if(index>=spatial_parts.size())continue;
            OwnedTerrainCommand command{};
            if(!(color?piece_command(index,command):part_command(index,command)))continue;
            auto& warmed=(color?state.color_warmed:state.depth_warmed)[index];
            const auto signature=variant_of(command)+1u;
            if(warmed==signature)continue;
            warmed=signature;   // failures too: never retried every frame
            const char* stage=nullptr;HRESULT hr=S_OK;
            OwnedTerrainSourceDecodeContext context{};NativeGeometryView* source_view=nullptr;
            // Mesh content does not depend on the contribution; a light bit
            // lets the record build without the colour pass's atlas phases.
            if(!source_terrain_world_record(frame.source_values,frame.front_end,command,nullptr,
                   {true,false,color?std::uint8_t(0x01):std::uint8_t(0x07)},context,source_view,false) || !source_view)
                stage="world_record";
            else if(!apply_owned_terrain_world_transform(*source_view))stage="world_transform";
            else {
                NativeGeometryView view=*source_view;
                view.prepare_only=true;
                NativeGeometryResult result{};
                if(!g_native_sink_submit(g_native_sink_context,view,result)) {
                    stage=result.failure_operation?result.failure_operation:"sink";hr=result.failure_hresult;
                }
            }
            if(stage && !state.failure_reported) {
                state.failure_reported=true;
                char detail[160];
                std::snprintf(detail,sizeof(detail),"first_failure=%s hr=%08lx sector=%u material=%u color=%d",
                    stage,static_cast<unsigned long>(hr),command.source_sector,unsigned(command.material),color?1:0);
                append_bridge_event("terrain_near_prewarm_failed",detail,false);
            }
            if(bridge_now()-begin>=budget)break;
        }
    } catch(...) {
        g_terrain_near_prewarm.queue.clear();g_terrain_near_prewarm.head=0;
    }
}

// Hands the renderer the static caster store: the whole store when its
// revision changed since the renderer last took it (or the renderer lost
// it), otherwise only the revision. During a static-layer redraw the
// renderer draws its baked store and returns casters it must register.
bool send_owned_static_store(bool force_store,std::vector<RetainedShadowCasterValue>& missing) {
    auto& store=g_owned_static_casters;
    if(!g_native_sink_shadow_static_depth)return false;
    // nullptr means "reuse the renderer's store", even when count is zero.
    // A full, empty store must still publish its revision (and clear an old
    // nonempty store). vector::data() may be null before the first caster.
    static const RetainedShadowCasterValue empty_store{};
    const auto send=[&](bool whole){
        const auto* values=store.casters.empty()?&empty_store:store.casters.data();
        return g_native_sink_shadow_static_depth(g_native_sink_context,
            whole?values:nullptr,whole?static_cast<std::uint32_t>(store.casters.size()):0u,
            store.revision,missing);
    };
    const bool whole=force_store || store.renderer_revision!=store.revision;
    if(send(whole)){store.renderer_revision=store.revision;return true;}
    // The renderer released its copy (world or device reset): send it whole.
    if(!whole && send(true)){store.renderer_revision=store.revision;return true;}
    store.renderer_revision=0;
    return false;
}

// A rejected sun shadow pass omits the frame's owned world; every such
// frame is counted and the reason logged (the first 16, then every 600th),
// so a repeating failure is never silent after the first report.


thread_local std::vector<OwnedTerrainShadowCommand> g_terrain_command_storage;
// execute_owned_shadow_pass scratch (plain values), cleared at each use.
thread_local std::vector<RetainedShadowCasterValue> g_shadow_missing_static_storage;
thread_local std::vector<std::uint8_t> g_shadow_terrain_only_storage;
thread_local std::vector<NativeTerrainShadowEntry> g_shadow_terrain_entry_storage;
thread_local std::vector<std::uint32_t> g_shadow_terrain_absent_storage;
bool execute_owned_shadow_pass(bool terrain_enabled,
    RetainedStaticScene& terrain_scene=retained_visual_scene(),
    const OwnedShadowSourceDelta* delta=nullptr,
    const RetainedShadowSourceChanges* changes=nullptr,
    const RetainedShadowInputDomain* static_previous=nullptr) noexcept {
 PERF06_SCOPE("sun_shadow_total");
    auto& frame=g_owned_visual_world_frame;
    frame.shadow_attempted=true;
    if(!frame.shadows_enabled){frame.shadow_ready=true;return true;}
    // Caves have no sun (cave_lighting.h): retire the sun maps once on entry
    // (an empty frame) and schedule no cascade while the zone stays a cave.
    static thread_local bool cave_sun_retired=false;
    if(g_native_world_lighting_frame.cave) {
        if(!cave_sun_retired){retire_owned_shadow_maps();cave_sun_retired=true;}
        frame.shadow_ready=true;return true;
    }
    cave_sun_retired=false;
    const bool traced=startup_begin(StartupStage::shadow);
    bool collecting=false,batch=false;
    try {
        auto& casters=frame.shadow_casters;casters.clear();
        // Kept capacity: ~3,000 commands (~0.5 MB) were allocated and regrown
        // on every frame. Emptied when the pass ends (texture references).
        auto& terrain_commands=g_terrain_command_storage;terrain_commands.clear();
        struct EmptyAtExit { std::vector<OwnedTerrainShadowCommand>& v; ~EmptyAtExit(){v.clear();} } empty_at_exit{terrain_commands};
        {
            CityRetainedOwnerGuard caster_timing(CityRetainedOwner::caster_values);
            if(!append_owned_shadow_source_values(casters,terrain_commands,terrain_enabled,
                terrain_scene,delta,changes,static_previous))
                throw "owned_shadow_caster_values";
        }
        auto& shadows=frame.shadows;
        {
            CityRetainedOwnerGuard fitting_timing(CityRetainedOwner::receiver_fitting);
            ++frame.shadow_source_diagnostic.caster_refits;
            if(g_bridge_sample.active)++g_bridge_sample.shadow_caster_refits;
            const StaticShadowSummary* statics=nullptr;
            if(frame.persistent_static_shadows && changes && g_owned_static_casters.valid) {
                auto& store=g_owned_static_casters;
                if(!store.summary.valid || store.summary.axes!=frame.coverage.light_axes)
                    build_static_shadow_summary(store.casters,frame.coverage.light_axes,store.revision,store.summary);
                update_owned_static_slice_revisions(store,frame.coverage);
                statics=&store.summary;
            }
            if(!build_retained_shadow_frame(frame.coverage,casters,shadows,statics))throw "owned_shadow_projection";
            shadows.world_epoch=bridge_current_world_epoch();
        }
        CityRetainedOwnerGuard registration_timing(CityRetainedOwner::depth_registration);
        std::uint8_t dirty=0;
        if(!g_native_sink_shadow_begin(g_native_sink_context,shadows,dirty))throw "owned_shadow_begin";
        OwnedSunDepthCull cull{};
        {
            NativeShadowCullProjection projection{};
            for(unsigned slice=0;slice<5;++slice)projection.world_to_light[slice]=shadows.slices[slice].world_to_light;
            if(g_native_sink_shadow_cull_projection)g_native_sink_shadow_cull_projection(g_native_sink_context,projection);
            for(unsigned slice=0;slice<5;++slice)cull.slices[slice].world_to_light=projection.world_to_light[slice];
            cull.retained_mask=projection.retained_mask;cull.scroll_mask=projection.scroll_mask;
            cull.scroll_copied=projection.scroll_copied;cull.side=projection.side;
            cull.scroll_entered=projection.scroll_entered;
            auto& store=g_owned_static_casters;store.drawn_mask=0;
            for(unsigned slice=0;slice<5;++slice)if(shadows.slices[slice].enabled){
                store.drawn[slice]=projection.world_to_light[slice];
                store.drawn_mask|=static_cast<std::uint8_t>(1u<<slice);
            }
        }
        frame.observed_retained_mask=cull.retained_mask;frame.observed_scroll_mask=cull.scroll_mask;
        if(frame_acct::enabled()) {
            for(unsigned slice=0;slice<5;++slice)if(dirty&(1u<<slice))frame_acct::add(frame_acct::sun_dirty_slices,1);
            if(dirty&1u)frame_acct::add(frame_acct::sun_slice0_dirty,1);
            if(dirty&8u)frame_acct::add(frame_acct::sun_slice3_dirty,1);
            if(dirty&16u)frame_acct::add(frame_acct::sun_slice4_dirty,1);
        }
        frame.shadow_source_diagnostic.dirty_mask|=dirty;
        if(g_bridge_sample.active)g_bridge_sample.shadow_dirty_mask|=dirty;
        collecting=true;
        if(dirty) {
            // Layered maps: the static layer (persistent static set and terrain)
            // is redrawn into its cache only for slices whose static content
            // changed; every dirty slice then starts from that cache and gets
            // the per-frame casters on top.
            const bool layered=frame.persistent_static_shadows;
            const auto phase=[&](NativeStaticSubmission::Operation operation) {
                NativeStaticSubmission request;request.operation=operation;
                NativeGeometryView view;view.static_submission=&request;NativeGeometryResult ignored{};
                if(!g_native_sink_submit(g_native_sink_context,view,ignored))throw "owned_shadow_phase";
                return static_cast<std::uint8_t>(request.generation);
            };
            const auto submit_selected=[&]() {
                if(frame.depth_selected.size()>UINT_MAX)throw "owned_shadow_selection_count";
                if(!frame.depth_selected.empty()) {
                    frame_acct::add(frame_acct::sun_depth_parts,static_cast<std::int64_t>(frame.depth_selected.size()));
                    OwnedDepthRegistration<OwnedSunDepthCull> registration{terrain_commands,cull};
                    NativeDepthSubmission submission{frame.depth_selected.data(),static_cast<UINT>(frame.depth_selected.size()),
                        &frame.coverage.world_to_clip,&registration,&register_owned_depth_part<OwnedSunDepthCull>};
                    NativeGeometryView dispatch;dispatch.depth_submission=&submission;
                    NativeGeometryResult result{};
                    if(!g_native_sink_submit(g_native_sink_context,dispatch,result))throw "owned_shadow_bulk_selection";
                    frame.shadow_submitted+=frame.depth_selected.size();
                    if(frame.observed_hover.signed_id)for(const auto& selected:frame.depth_selected)
                        if(!selected.identity.terrain_source && selected.identity.signed_id==frame.observed_hover.signed_id &&
                           selected.identity.plan_generation==frame.observed_hover.serial) {
                            ++frame.observed_sun_selections;frame.observed_sun_mask|=selected.mask;
                        }
                }
                frame.depth_selected.clear();
            };
            if(delta && terrain_enabled) {
                std::vector<RetainedShadowCasterValue> current_terrain;
                terrain_commands.clear();
                if(!append_owned_terrain_shadow_values(current_terrain,terrain_commands,terrain_scene))
                    throw "owned_shadow_delta_terrain_commands";
            }
            frame.depth_selected.clear();
            const std::uint8_t static_dirty=layered?phase(NativeStaticSubmission::Operation::shadow_static):dirty;
            if(static_dirty) {
                if(!g_native_sink_begin_batch(g_native_sink_context,SemanticKind::figure))throw "owned_shadow_batch";
                batch=true;
                auto& missing_static=g_shadow_missing_static_storage;missing_static.clear();
                if(frame.persistent_static_shadows) {
                    // The renderer draws its baked copy of the store (GPU
                    // culled per map and scroll strip); the store itself is
                    // sent only when its revision changed. Returned casters
                    // have no current record yet and are registered below.
                    if(!send_owned_static_store(false,missing_static))throw "owned_shadow_static_records";
                }
                // Terrain casters: the renderer draws the ones it holds baked
                // records for with these final masks (dirty static maps,
                // scroll strip and entered parts, as the depth path applies
                // them); the rest go through the depth path below.
                auto& terrain_only=g_shadow_terrain_only_storage;terrain_only.clear();
                const std::vector<std::uint8_t>* terrain_subset=nullptr;
                if(layered && g_native_sink_shadow_terrain_bake && !terrain_commands.empty()) {
                    auto& entries=g_shadow_terrain_entry_storage;entries.clear();entries.reserve(terrain_commands.size());
                    for(const auto& item:terrain_commands) {
                        std::uint8_t mask=item.mask&static_dirty;
                        mask=owned_static_scroll_mask(cull,item.light_bounds,mask,
                            (std::uint64_t(std::uint32_t(item.revision.signed_id))<<32)|item.revision.part);
                        entries.push_back({item.revision,mask});
                    }
                    auto& absent=g_shadow_terrain_absent_storage;absent.clear();
                    if(g_native_sink_shadow_terrain_bake(g_native_sink_context,entries.data(),
                           static_cast<std::uint32_t>(entries.size()),absent)) {
                        terrain_only.assign(entries.size(),0);
                        for(const auto index:absent)if(index<terrain_only.size())terrain_only[index]=1;
                        terrain_subset=&terrain_only;
                    }
                }
                if(!collect_owned_missing_static_depth(missing_static,static_dirty) ||
                   !submit_owned_terrain_shadow_parts(terrain_commands.data(),terrain_commands.size(),static_dirty,cull,true,
                       nullptr,terrain_subset) ||
                   (!layered && !collect_owned_visual_depth(dirty,cull)))
                    throw "owned_shadow_part";
                submit_selected();
                g_native_sink_end_batch(g_native_sink_context,true);batch=false;
            }
            if(layered) {
                phase(NativeStaticSubmission::Operation::shadow_dynamic);
                if(!g_native_sink_begin_batch(g_native_sink_context,SemanticKind::figure))throw "owned_shadow_batch";
                batch=true;
                if(!collect_owned_visual_depth(dirty,cull))throw "owned_shadow_part";
                submit_selected();
                g_native_sink_end_batch(g_native_sink_context,true);batch=false;
            }
        } else if(frame.persistent_static_shadows &&
                  g_owned_static_casters.renderer_revision!=g_owned_static_casters.revision) {
            // Without a redraw the renderer only keeps the new store.
            auto& missing_static=g_shadow_missing_static_storage;missing_static.clear();
            if(!send_owned_static_store(true,missing_static))throw "owned_shadow_static_records";
        }
        const bool success=g_native_sink_shadow_end(g_native_sink_context,true);collecting=false;
        if(!success)throw "owned_shadow_end";
        if(terrain_enabled){prewarm_owned_terrain_far_shadow_meshes();prewarm_owned_terrain_near_meshes();}
        frame.shadow_ready=true;
        startup_end(StartupStage::shadow,traced,"completed");
        return true;
    } catch(const char* reason) {
        startup_failure(StartupStage::shadow,reason);
        reject_owned_visual("shadow",reason);
        report_object_plan_failure(reason);
        report_owned_shadow_pass_failure(reason);
    } catch(...) {
        startup_failure(StartupStage::shadow,"owned_shadow_allocation");
        reject_owned_visual("shadow","owned_shadow_allocation");
        report_object_plan_failure("owned_shadow_allocation");
        report_owned_shadow_pass_failure("owned_shadow_allocation");
    }
    CityRetainedOwnerGuard cleanup_timing(CityRetainedOwner::depth_registration);
    if(batch)g_native_sink_end_batch(g_native_sink_context,false);
    if(collecting)g_native_sink_shadow_end(g_native_sink_context,false);
    retire_owned_shadow_maps();
    frame.shadow_ready=false;
    startup_end(StartupStage::shadow,traced,"rejected");
    return false;
}

struct OwnedPointMember {
    LifecycleIdentity identity{};
    NativeWorldBounds bounds{};
    bool dynamic=false;
    double moved=0;
};
struct OwnedPointLampMembers {
    std::map<std::int32_t,OwnedPointMember> entities;
    std::vector<std::uint32_t> terrain_static;
    std::set<std::int32_t> dynamic_entities;
};
struct OwnedPointTerrainValue {
    NativeWorldBounds bounds{};
    bool casts=false;
};
struct OwnedPointScene {
    PointShadowScheduler scheduler;
    std::vector<RetainedWorldPointLight> lights;
    std::vector<OwnedPointLampMembers> members;
    std::vector<OwnedPointTerrainValue> terrain;
    std::uint64_t epoch=0,gpu_generation=0,terrain_generation=0,terrain_revision=0,frame=0;
    std::uint32_t flags=0;
    bool undetected=false,terrain_enabled=false;
    std::array<float,16> terrain_transform{};
    double last_time=0;
};
thread_local OwnedPointScene g_point_scene;

// What the bridge last baked (or attempted) for the renderer's shelter map request.
struct RainOcclusionBaked { std::uint64_t serial = 0, store_revision = 0, epoch = 0, frame = 0; bool attempted = false; } g_rain_baked;

// Shelter map for owned precipitation (renderer_precipitation.cpp): the
// persistent static caster store drawn straight down through the shadow depth
// path into the renderer's one shelter slice. Baked when the renderer's
// request (a snapped square around the view focus), the store or the world
// epoch changes; units and animated objects are not in the store. A failed
// bake is named once and not retried until one of them changes.
void execute_owned_rain_occlusion_pass() noexcept {
    auto& frame=g_owned_visual_world_frame;
    const RainOcclusionRequest request=g_rain_request;
    if(!frame.values||!request.active||!request.side||!(request.half>0)||!(request.span>0))return;
    if(!g_native_sink_context||!g_native_sink_rain_begin||!g_native_sink_rain_end||!g_native_sink_begin_batch||
       !g_native_sink_end_batch||!g_native_sink_shadow_static_depth||!g_native_sink_submit)return;
    const auto& store=g_owned_static_casters;
    if(!store.valid||!frame.shadows_enabled)return;
    // The epoch captured with this frame (scene thread: the queued command's).
    const auto epoch=bridge_current_world_epoch();
    // A new square or world bakes at once. Static-object changes (the store
    // revision moves whenever any journal-dirty static changes, which some
    // maps do nearly every frame) re-bake at most every 60 world frames: a
    // per-frame re-bake was hidden storm cost (run 6).
    const bool request_changed=!g_rain_baked.attempted||g_rain_baked.serial!=request.serial||g_rain_baked.epoch!=epoch;
    const bool store_changed=g_rain_baked.store_revision!=store.revision;
    if(!request_changed&&!(store_changed&&frame.serial-g_rain_baked.frame>=60))return;
    g_rain_baked={request.serial,store.revision,epoch,frame.serial,true};
    NativePointShadowFrame faces{};auto& slice=faces.slices[0];
    const float h=request.half;
    slice.world_to_light={1/h,0,0,-request.center[0]/h, 0,1/h,0,-request.center[1]/h,
                          0,0,-1/request.span,request.top/request.span, 0,0,0,1};
    slice.camera_slice=slice.world_to_light;
    slice.bias={2*h/float(request.side),1/request.span,0,0};
    slice.enabled=true;
    bool collecting=false,batch=false;
    const auto failed=[&](const char* reason) {
        if(batch)g_native_sink_end_batch(g_native_sink_context,false);
        if(collecting)g_native_sink_rain_end(g_native_sink_context,false);
        static const char* last=nullptr;
        if(last!=reason){append_bridge_event("precip_shelter_failed",reason,true);last=reason;}
    };
    try {
        if(!g_native_sink_rain_begin(g_native_sink_context,slice,request.side))throw "shelter_begin";
        collecting=true;
        if(!g_native_sink_begin_batch(g_native_sink_context,SemanticKind::figure))throw "shelter_batch";
        batch=true;
        // The renderer draws its baked static store (issue #162) into the
        // shelter slice and returns the store casters without a current
        // record that the slice reaches; those register through the depth path.
        std::vector<RetainedShadowCasterValue> missing;
        if(!send_owned_static_store(false,missing))throw "shelter_static_store";
        frame.depth_selected.clear();
        if(!collect_owned_missing_static_depth(missing,1))throw "shelter_missing_casters";
        if(!frame.depth_selected.empty()) {
            if(frame.depth_selected.size()>UINT_MAX)throw "shelter_selection_count";
            const std::vector<OwnedTerrainShadowCommand> no_terrain;
            OwnedDepthRegistration<NativePointShadowFrame> registration{no_terrain,faces};
            NativeDepthSubmission submission{frame.depth_selected.data(),static_cast<UINT>(frame.depth_selected.size()),
                &frame.coverage.world_to_clip,&registration,&register_owned_depth_part<NativePointShadowFrame>};
            NativeGeometryView dispatch;dispatch.depth_submission=&submission;NativeGeometryResult result{};
            if(!g_native_sink_submit(g_native_sink_context,dispatch,result))throw "shelter_submission";
        }
        frame.depth_selected.clear();
        g_native_sink_end_batch(g_native_sink_context,true);batch=false;
        const bool committed=g_native_sink_rain_end(g_native_sink_context,true);collecting=false;
        if(!committed)throw "shelter_commit";
    } catch(const char* reason) {
        failed(reason);
    } catch(...) {
        failed("shelter_allocation");
    }
}

void execute_owned_point_shadow_pass(bool terrain_enabled,RetainedStaticScene& terrain_scene,
    bool /*terrain_static_stable*/,std::uint32_t class_flags,bool draw_undetected) noexcept {
 PERF06_SCOPE("point_shadow_total");
    CityRetainedOwnerGuard timing(CityRetainedOwner::point_shadow);
    auto& frame=g_owned_visual_world_frame;
    if(!frame.values)return;
    auto& state=g_point_scene;
    NativePointShadowStats stats{};stats.pass_ran=true;
    std::vector<NativePointShadowPublication> publication;
    try {
        const auto changes=terrain_scene.shadow_source_changes(RetainedShadowConsumer::point);
        // Shadow-casting lights within the merge distance share one cube.
        std::vector<RetainedWorldPointLight> lights;std::vector<int> shadow_leader;
        point_shadow_merge(frame.values->world_point_lights,g_point_shadow_merge_distance,lights,shadow_leader);
        publication.resize(lights.size());
        bool pressure=false;
        const bool enabled=frame.shadows_enabled&&g_owned_shadow_settings.point_shadows;
        const bool held=std::any_of(state.scheduler.lamps.begin(),state.scheduler.lamps.end(),
            [](const PointShadowLamp& lamp){return lamp.static_slot>=0;});
        if(held||(enabled&&std::any_of(lights.begin(),lights.end(),[](const RetainedWorldPointLight& light){return light.casts_shadow;}))){
            if(!g_native_sink_point_shadow_batch||!g_native_sink_point_shadow_allocate||!g_native_sink_point_shadow_release||!g_native_sink_point_shadow_begin||!g_native_sink_point_shadow_end||!g_native_sink_point_shadow_publish)
                throw "point_shadow_pool_sink_missing";
            std::uint64_t generation=0;
            if(g_native_sink_point_shadow_batch(g_native_sink_context,generation,pressure)){
                if(state.gpu_generation!=generation){
                    for(auto& lamp:state.scheduler.lamps){if(lamp.strength>0)++stats.strength_zeroed;lamp={};}
                    state.gpu_generation=generation;++stats.reset;++stats.reset_pool;
                }
            }else if(enabled)throw "point_shadow_pool_unavailable";
        }
        const auto release=[&](int slot){if(g_native_sink_point_shadow_release)
            g_native_sink_point_shadow_release(g_native_sink_context,state.gpu_generation,slot);};
        const auto release_all=[&](){for(const auto& lamp:state.scheduler.lamps){
            if(lamp.static_slot>=0)release(lamp.static_slot);if(lamp.overlay_slot>=0)release(lamp.overlay_slot);
            if(lamp.replacement_slot>=0)release(lamp.replacement_slot);}};
        if(!enabled){
            release_all();state={};
            if(g_native_sink_point_shadow_publish)g_native_sink_point_shadow_publish(g_native_sink_context,publication,stats);
            terrain_scene.acknowledge_shadow_source_changes(RetainedShadowConsumer::point,owned_scene_ack_revision(changes.revision));
            return;
        }
        const auto epoch=bridge_current_world_epoch(); // scene thread: the queued command's epoch
        const auto matches=point_shadow_matches(state.lights,lights);
        bool list_changed=state.lights.size()!=lights.size();std::size_t matched=0;
        for(std::size_t i=0;i<matches.size();++i){matched+=matches[i]<state.lights.size();list_changed|=matches[i]!=i;}
        if(list_changed){++stats.list;
            stats.reorder_only=matched==lights.size()&&matched==state.lights.size();
            stats.light=lights.size()+state.lights.size()-2*matched;
            remap_point_shadow_values(state.scheduler.lamps,matches,[&](const PointShadowLamp& lamp){
                if(lamp.strength>0)++stats.strength_zeroed;
                if(lamp.static_slot>=0)release(lamp.static_slot);if(lamp.overlay_slot>=0)release(lamp.overlay_slot);
                if(lamp.replacement_slot>=0)release(lamp.replacement_slot);});
            remap_point_shadow_values(state.members,matches,[](const OwnedPointLampMembers&){});
        }
        stats.reset_epoch=state.epoch!=epoch;stats.reset_journal_full=changes.full;
        stats.reset_class_flags=state.flags!=class_flags;stats.reset_undetected=state.undetected!=draw_undetected;
        const bool reset=stats.reset_epoch||stats.reset_journal_full||stats.reset_class_flags||stats.reset_undetected;
        if(reset){++stats.reset;for(unsigned i=0;i<lights.size();++i)state.scheduler.invalidate(i);}        state.epoch=epoch;state.flags=class_flags;state.undetected=draw_undetected;
        double time=0;
        // Scene thread: the game thread's copied frame time, never a game read.
        const bool time_valid=bridge_current_world_time(time);
        const double now=time_valid?time*kWeatherSecondsPerSegment:state.last_time;
        const float dt=state.last_time?float(std::min(.25,std::max(0.,now-state.last_time))):0;
        state.last_time=now;++state.frame;
        auto& terrain=g_owned_terrain_frame;
        const bool terrain_source_changed=terrain_enabled &&
            (state.terrain_generation!=terrain.source_values.source_generation||
             state.terrain_revision!=terrain.source_values.source_revision);
        const bool terrain_transform_changed=terrain_enabled&&state.terrain_transform!=terrain.object_to_world;
        const bool terrain_refresh=reset||terrain_source_changed||terrain_transform_changed||
            state.terrain_enabled!=terrain_enabled;
        if(terrain_refresh){
            state.terrain.clear();
            if(terrain_enabled)for(const auto& spatial:terrain.front_end.spatial_parts){
                OwnedPointTerrainValue value;value.bounds=spatial.world_bounds;
                value.casts=owned_terrain_base_shadow_material(*terrain.source_values.terrain,spatial.material);
                state.terrain.push_back(value);
            }
        }
        // Shadow shape: bake range, and an origin lifted just above the owned
        // terrain when a light sits at or below the ground (campfires), so the
        // ground itself does not bury it. Lamps above ground are unchanged.
        const auto shadow_shape=[&](RetainedWorldPointLight light,float bake)->RetainedWorldPointLight{
            light.radius=bake;float ground=0;
            if(terrain_enabled&&terrain.source_values.terrain&&
               sample_terrain_height(*terrain.source_values.terrain,light.center[0],light.center[1],ground)){
                const float floor_z=ground+2*point_shadow_source_radius(bake);
                if(light.center[2]<floor_z)light.center[2]=floor_z;
            }
            return light;
        };
        for(unsigned index=0;index<lights.size();++index){
            auto& members=state.members[index];auto& lamp=state.scheduler.lamps[index];
            const bool matched=matches[index]<state.lights.size();
            // A light that moved keeps its lamp state but is unshadowed until it
            // settles; small range animation stays inside the grow-only bake radius.
            if(matched&&point_shadow_moved(state.lights[matches[index]],lights[index]))lamp.last_move=now;
            const float bake_radius=point_shadow_bake_radius(lamp.bake_radius,lights[index].radius);
            const bool range_rebake=lamp.bake_radius>0&&bake_radius!=lamp.bake_radius;
            lamp.bake_radius=bake_radius;
            const bool moving=lamp.last_move>=0&&now-lamp.last_move<PointShadowPolicy::move_settle_seconds;
            const bool settled=lamp.moving&&!moving;
            lamp.moving=moving;
            if(range_rebake||settled){state.scheduler.invalidate(index);++stats.light;}
            RetainedWorldPointLight shaped=shadow_shape(lights[index],bake_radius);
            const auto& light=shaped;
            const bool gather=reset||!matched||range_rebake||settled;
            const auto update=[&](std::int32_t id,bool initial){
                const auto old=members.entities.find(id);
                const bool had=old!=members.entities.end();
                OwnedPointMember next{};
                const auto found=std::lower_bound(frame.values->entities.begin(),frame.values->entities.end(),id,
                    [](const RetainedVisualSample& sample,std::int32_t key){return sample.lifecycle.id<key;});
                bool has=false;
                if(found!=frame.values->entities.end()&&found->lifecycle.id==id){
                    const auto ordinal=std::size_t(found-frame.values->entities.begin());
                    const auto& prepared=frame.prepared[ordinal];
                    if(prepared.casting_parts&&point_shadow_intersects(prepared.point_membership_bounds,light)){
                        has=true;next.identity=found->lifecycle;next.bounds=prepared.point_membership_bounds;
                        const bool moving=had&&point_shadow_caster_moved(old->second.bounds,next.bounds);
                        if(moving&&!old->second.moved)++stats.static_to_dynamic;
                        next.moved=moving?now:(had?old->second.moved:0);
                        // Static only until it first moves: an entity that has moved stays
                        // in the dynamic overlay for this world, so repeated stop/start
                        // motion cannot keep invalidating the static bake.
                        next.dynamic=next.moved>0||
                            std::binary_search(changes.time_active_visuals.begin(),changes.time_active_visuals.end(),id);
                    }
                }
                // A mover already in the overlay cannot dirty the static bake.
                // Transitioning to/from static, removal and old bounds all count.
                if(!initial&&point_shadow_local_change(had?old->second.bounds:NativeWorldBounds{},
                    has?next.bounds:NativeWorldBounds{},light)&&
                    ((had&&!old->second.dynamic)||(has&&!next.dynamic))){
                    state.scheduler.invalidate(index);++stats.radius_dirty;}
                if(has)members.entities[id]=next;else if(had)members.entities.erase(old);
                if(has&&next.dynamic)members.dynamic_entities.insert(id);else members.dynamic_entities.erase(id);
            };
            if(gather){
                members.entities.clear();members.dynamic_entities.clear();
                if(light.casts_shadow)for(const auto& sample:frame.values->entities)update(sample.lifecycle.id,true);
            }else{
                for(auto id:changes.dirty_visuals)update(id,false);
                for(auto id:changes.time_active_visuals)
                    if(!std::binary_search(changes.dirty_visuals.begin(),changes.dirty_visuals.end(),id))update(id,false);
            }
            if(gather||terrain_refresh){
                const bool had_terrain=!members.terrain_static.empty();
                members.terrain_static.clear();
                for(unsigned part=0;part<state.terrain.size();++part){const auto& value=state.terrain[part];
                    if(value.casts&&point_shadow_intersects(value.bounds,light))
                        members.terrain_static.push_back(part);}
                if(!gather&&terrain_refresh&&(had_terrain||!members.terrain_static.empty())){state.scheduler.invalidate(index);++stats.terrain;}
            }
            const bool dynamic=!members.dynamic_entities.empty();
            if(dynamic&&!lamp.dynamic)lamp.next_overlay=0;
            lamp.dynamic=dynamic;
            stats.eligibility_lost+=state.scheduler.observe_casters(index,light.casts_shadow&&
                (!members.entities.empty()||!members.terrain_static.empty()),now);
        }
        state.lights=lights;state.terrain_enabled=terrain_enabled;
        state.terrain_transform=terrain.object_to_world;
        state.terrain_generation=terrain_enabled?terrain.source_values.source_generation:0;
        state.terrain_revision=terrain_enabled?terrain.source_values.source_revision:0;
        const auto anchor=point_shadow_focus(frame.coverage.world_to_clip,frame.shadow_receivers);
        state.scheduler.select(lights,anchor,now,dt,state.frame,
            [&](bool overlay,int& slot){return g_native_sink_point_shadow_allocate&&
                g_native_sink_point_shadow_allocate(g_native_sink_context,overlay,slot);},release,pressure);
        stats.allocation_failures=state.scheduler.allocation_failures;
        stats.pressure_drops=state.scheduler.pressure_drops;
        // Overlay cadence: the most important lamps (with hysteresis) redraw
        // moving casters every frame; the others every third frame.
        state.scheduler.rank_fast_overlays(unsigned(g_point_shadow_fast_overlays));
        for(std::size_t item=0;item<state.scheduler.work.size();++item){
            const unsigned index=state.scheduler.work[item];
            auto& lamp=state.scheduler.lamps[index];auto& members=state.members[index];
            const bool bake=state.scheduler.work_bakes[item];
            const int target=bake?state.scheduler.bake_slot(index,[&](bool overlay,int& slot){
                return g_native_sink_point_shadow_allocate(g_native_sink_context,overlay,slot);}):lamp.static_slot;
            if(bake&&target<0){state.scheduler.drop_least_important(now);continue;}
            bool baked=false,overlay=false;
            NativePointShadowFrame faces;
            RetainedWorldPointLight shaped=shadow_shape(lights[index],lamp.bake_radius);
            if(!build_owned_point_shadow_cube(shaped,faces))throw "point_shadow_projection";
            // A lamp whose shadow sphere is wholly off-screen cannot shade a
            // visible pixel: its moving-caster overlay waits (kept as drawn)
            // until any of the sphere enters the view, then refreshes.
            NativeWorldBounds sphere{};sphere.valid=true;
            for(unsigned axis=0;axis<3;++axis){
                sphere.minimum[axis]=shaped.center[axis]-shaped.radius;
                sphere.maximum[axis]=shaped.center[axis]+shaped.radius;
            }
            const bool on_screen=retained_world_bounds_visible(sphere,frame.coverage.world_to_clip);
            for(unsigned tier=0;tier<2;++tier){
                if(tier==0&&!bake)continue;
                if(tier==1&&(!lamp.dynamic||lamp.overlay_slot<0||state.frame<lamp.next_overlay||!on_screen||
                   (!lamp.fast&&(state.frame+index)%3!=0)))continue;
                bool collecting=false,batch=false;
                try{
                    std::vector<OwnedVisualPartCandidate> candidates;
                    const auto append_entity=[&](std::int32_t id,const OwnedPointMember& member){
                        const auto found=std::lower_bound(frame.values->entities.begin(),frame.values->entities.end(),id,
                            [](const RetainedVisualSample& sample,std::int32_t key){return sample.lifecycle.id<key;});
                        if(found==frame.values->entities.end()||found->lifecycle.id!=id||
                            found->lifecycle.generation!=member.identity.generation||
                            found->lifecycle.positive_world_epoch!=member.identity.positive_world_epoch)return;
                        const auto ordinal=std::size_t(found-frame.values->entities.begin());
                        const auto& prepared=frame.prepared[ordinal];
                        if(!prepared.bounds.valid)throw "point_shadow_caster_unavailable";
                        // Pose-derived part bounds from the pure, null-checked helper, so the
                        // fixture decision never depends on what the camera needed this frame.
                        // (The frame-cache helper assumes color-pipeline preconditions and
                        // crashed here on an entity without prepared resources.)
                        // A merged unit casts through its groups (#65); only
                        // unmerged parts keep the per-part fixture test.
                        const auto& entity=frame.values->entities[ordinal];
                        const bool merged=owned_unit_merged(entity);
                        if(merged)for(std::size_t group=0;group<entity.prepared->merged_groups.size();++group){
                            bool casts=false;
                            for(const auto member:entity.prepared->merged_groups[group].parts)
                                casts|=member<prepared.parts.size()&&prepared.parts[member].casts;
                            if(casts)candidates.push_back({ordinal,kMergedPartBase+group});
                        }
                        for(std::size_t part=0;part<prepared.parts.size();++part){
                            if(!prepared.parts[part].casts)continue;
                            if(merged&&owned_merged_member(entity,part))continue;
                            NativeWorldBounds part_bounds{};
                            if(retained_visual_part_bounds(frame.values->entities[ordinal],part,part_bounds)&&
                               point_shadow_fixture(part_bounds,shaped))continue;
                            candidates.push_back({ordinal,part});
                        }
                    };
                    if(tier)for(auto id:members.dynamic_entities)append_entity(id,members.entities.at(id));
                    else for(const auto& item:members.entities)if(!item.second.dynamic)append_entity(item.first,item.second);
                    std::vector<RetainedShadowCasterValue> values;
                    std::vector<OwnedTerrainShadowCommand> commands;
                    const auto& terrain_members=members.terrain_static;
                    if(terrain_enabled&&!tier&&!append_owned_terrain_shadow_values(values,commands,terrain_scene,nullptr,&faces,nullptr,&terrain_members))
                        throw "point_shadow_terrain_values";
                    frame.depth_selected.clear();
                    if(!collect_owned_visual_depth(63,faces,&lights[index],&candidates)||
                       !submit_owned_terrain_shadow_parts(commands.data(),commands.size(),63,faces,true))throw "point_shadow_selection";
                    if(tier&&frame.depth_selected.empty()){
                        lamp.overlay_valid=false;lamp.next_overlay=state.frame+PointShadowPolicy::empty_backoff;continue;}
                    const auto slot=tier?lamp.overlay_slot:target;
                    if(!g_native_sink_point_shadow_begin(g_native_sink_context,slot,index,faces))throw "point_shadow_begin";
                    collecting=true;
                    if(!g_native_sink_begin_batch(g_native_sink_context,SemanticKind::figure))throw "point_shadow_batch";
                    batch=true;
                    if(!frame.depth_selected.empty()){
                        if(frame.depth_selected.size()>UINT_MAX)throw "point_shadow_selection_count";
                        OwnedDepthRegistration<NativePointShadowFrame> registration{commands,faces};
                        NativeDepthSubmission submission{frame.depth_selected.data(),static_cast<UINT>(frame.depth_selected.size()),
                            &frame.coverage.world_to_clip,&registration,&register_owned_depth_part<NativePointShadowFrame>};
                        NativeGeometryView dispatch;dispatch.depth_submission=&submission;NativeGeometryResult result{};
                        if(!g_native_sink_submit(g_native_sink_context,dispatch,result))throw "point_shadow_submission";
                    }
                    g_native_sink_end_batch(g_native_sink_context,true);batch=false;
                    const bool committed=g_native_sink_point_shadow_end(g_native_sink_context,true);collecting=false;
                    if(!committed)throw "point_shadow_commit";
                    if(tier){overlay=true;++stats.overlays;}else{baked=true;++stats.rebaked;}
                }catch(...){
                    if(batch)g_native_sink_end_batch(g_native_sink_context,false);
                    if(collecting)g_native_sink_point_shadow_end(g_native_sink_context,false);
                    if(tier)lamp.overlay_valid=false;else state.scheduler.invalidate(index);
                    report_world_point_light_omission("point_shadow_tier_failed");
                    break;
                }
            }
            state.scheduler.commit(index,baked,overlay,release);
        }
        for(unsigned i=0;i<lights.size();++i){const auto& lamp=state.scheduler.lamps[i];
            if(lamp.valid){++stats.baked;publication[i]={lamp.static_slot,lamp.overlay_valid?lamp.overlay_slot:-1,lamp.strength,lamp.overlay_strength,lamp.bake_radius};
                publication[i].merged=true;publication[i].shadow_center=shadow_shape(lights[i],lamp.bake_radius).center;}
            if(lamp.eligible&&!lamp.retiring&&!lamp.suppressed&&!lamp.moving&&lamp.static_slot>=0&&lamp.stale)++stats.budget;
        }
        for(const auto& light:lights)stats.casting_lights+=light.casts_shadow?1u:0u;
        {   // Cluster heads publish their merged centre; members sample the head's cube.
            std::vector<bool> head(lights.size(),false);
            for(int l:shadow_leader)if(l>=0)head[std::size_t(l)]=true;
            for(unsigned i=0;i<lights.size();++i)if(head[i]&&publication[i].static_slot>=0){
                publication[i].merged=true;publication[i].shadow_center=lights[i].center;}
            for(unsigned i=0;i<lights.size();++i)if(shadow_leader[i]>=0){
                publication[i]=publication[std::size_t(shadow_leader[i])];++stats.merged_lights;}
        }
        for(unsigned i=0;i<lights.size();++i){
            const float fade=lights[shadow_leader[i]>=0?std::size_t(shadow_leader[i]):i].shadow_fade;
            publication[i].strength*=fade;publication[i].overlay_strength*=fade;
        }
        stats.allocation_failures=state.scheduler.allocation_failures;stats.pressure_drops=state.scheduler.pressure_drops;
        stats.strength_zeroed+=state.scheduler.strength_zeroed;
        if(g_native_sink_point_shadow_publish)g_native_sink_point_shadow_publish(g_native_sink_context,publication,stats);
        // Empty and entirely reused frames consume history too. Invalidation has
        // already retired stale bakes even when their rebuild waits for budget.
        if(terrain_scene.shadow_source_revision()==changes.revision)
            terrain_scene.acknowledge_shadow_source_changes(RetainedShadowConsumer::point,owned_scene_ack_revision(changes.revision));
    }catch(...){
        // Failure leaves committed cubes intact. Remap even when the pass failed
        // before it could adopt this frame's light order.
        try{
            const auto matches=point_shadow_matches(state.lights,frame.values->world_point_lights);
            publication.assign(matches.size(),{});
            for(std::size_t i=0;i<matches.size();++i)if(matches[i]<state.scheduler.lamps.size()){
                const auto& lamp=state.scheduler.lamps[matches[i]];
                if(lamp.valid)publication[i]={lamp.static_slot,lamp.overlay_valid?lamp.overlay_slot:-1,lamp.strength,lamp.overlay_strength,lamp.bake_radius};}
            if(g_native_sink_point_shadow_publish)g_native_sink_point_shadow_publish(g_native_sink_context,publication,stats);
        }catch(...){} // Renderer retains the last publication if even metadata allocation failed.
        report_world_point_light_omission("point_shadow_frame_failed");
    }
}
bool append_owned_terrain_receiver_bounds(OwnedTerrainRuntimeFrame& terrain,
    std::vector<NativeWorldBounds>& receivers) {
    // One receiver per visible sector: coverage clips every receiver against
    // the camera and each map, so per-command boxes multiplied that work while
    // their union changes the fitted envelopes only within the sector.
    // Scratch kept across frames (a fresh per-sector vector was a heap round
    // trip of ~100 KB on every frame the camera moved).
    thread_local std::vector<NativeWorldBounds> sectors;
    sectors.assign(terrain.source_values.terrain->sectors.size(),NativeWorldBounds{});
    for(const auto* commands:{&terrain.front_end.base_commands,&terrain.front_end.transparent_commands})
        for(const auto& command:*commands) {
            const auto& material=terrain.source_values.terrain->sectors[command.source_sector].materials[command.material];
            const auto* color=command.source_sector<terrain.dynamic.size() && terrain.dynamic[command.source_sector].captured
                ? &terrain.dynamic[command.source_sector] : nullptr;
            bool available=true;
            for(unsigned part=0;part<4;++part)if(command.subpart_mask&(1u<<part)) {
                const auto phases=material.subparts[part].required_animation_phases;
                if(phases && (!color || phases>color->animation_phases.size()))available=false;
            }
            if(!available)continue;
            NativeWorldBounds local{},world{};
            if(!owned_terrain_spatial_bounds(terrain.front_end,command,local,world))return false;
            ++terrain.front_end.spatial_receiver_reuses;
            if(g_bridge_sample.active)++g_bridge_sample.terrain_spatial_bounds_reused;
            if(command.source_sector>=sectors.size())return false;
            auto& merged=sectors[command.source_sector];
            if(!merged.valid)merged=world;
            else for(unsigned axis=0;axis<3;++axis){
                merged.minimum[axis]=std::min(merged.minimum[axis],world.minimum[axis]);
                merged.maximum[axis]=std::max(merged.maximum[axis],world.maximum[axis]);
            }
        }
    for(const auto& merged:sectors)if(merged.valid)receivers.push_back(merged);
    return true;
}

bool same_shadow_terrain_inputs(const OwnedTerrainFrameInputs& a,
    const OwnedTerrainFrameInputs& b) noexcept {
    return a.source_generation==b.source_generation && a.source_revision==b.source_revision &&
        a.terrain_origin==b.terrain_origin && a.camera_position==b.camera_position &&
        a.lod_distances==b.lod_distances && a.maximum_lod==b.maximum_lod &&
        a.force_full_detail==b.force_full_detail &&
        a.game_visibility_fingerprint==b.game_visibility_fingerprint &&
        a.game_visibility_domain_present==b.game_visibility_domain_present &&
        a.game_visibility_min_y==b.game_visibility_min_y &&
        a.game_visibility_rows==b.game_visibility_rows &&
        // This fingerprint includes world time even when no retained depth
        // source consumes animated UVs or waves. The active-source subset is
        // checked separately; it is not a receiver-admission input.
        a.object_to_clip==b.object_to_clip;
}

bool same_shadow_static_animation(const NativeTerrainState& a,
    const NativeTerrainState& b,bool wave_active) noexcept {
    return a.waves_enabled==b.waves_enabled &&
        (!wave_active || (a.wind_force==b.wind_force &&
            a.wind_direction[0]==b.wind_direction[0] &&
            a.wind_direction[1]==b.wind_direction[1] && a.wind_phase==b.wind_phase &&
            a.precise_time==b.precise_time)) &&
        a.fog_enabled==b.fog_enabled && a.fog_min[0]==b.fog_min[0] &&
        a.fog_min[1]==b.fog_min[1] && a.fog_max[0]==b.fog_max[0] &&
        a.fog_max[1]==b.fog_max[1] && a.fog_distance==b.fog_distance;
}

bool same_shadow_bounds(const NativeWorldBounds& a,const NativeWorldBounds& b) noexcept {
    return a.valid==b.valid && (!a.valid ||
        (a.minimum==b.minimum && a.maximum==b.maximum));
}

bool capture_active_visual_shadow_values(const RetainedVisualFrame& frame,
    const std::vector<std::int32_t>& ids,bool draw_undetected,std::uint32_t class_flags,
    const RetainedDirectionalCoverage& coverage,
    std::vector<RetainedShadowActiveVisualValue>& output) {
    output.clear();output.reserve(ids.size());
    for(const auto id:ids) {
        RetainedShadowActiveVisualValue value;value.signed_id=id;
        const auto found=std::lower_bound(frame.entities.begin(),frame.entities.end(),id,
            [](const RetainedVisualSample& sample,std::int32_t value){
                return sample.lifecycle.id<value;
            });
        if(found==frame.entities.end() || found->lifecycle.id!=id ||
            !found->mesh_declared || found->status!=RetainedVisualStatus::ready ||
            !found->assembly || !found->prepared ||
            !(class_flags&(found->values.unit?0x80u:0x40u))) {
            output.push_back(std::move(value));continue;
        }
        const auto& sample=*found;
        if(!prepare_owned_visual_admission(sample,draw_undetected,true))return false;
        const auto& admitted=sample.prepared->derived_admission_parts;
        bool visible=false,receives=false,casts=false;
        for(const auto& part:admitted){visible|=part.visible;receives|=part.receives;casts|=part.casts;}
        if(!visible){output.push_back(std::move(value));continue;}
        NativeWorldBounds envelope;
        if((receives||casts) && !prepare_owned_visual_world_bounds(sample,envelope))return false;
        if(receives && retained_world_bounds_visible(envelope,coverage.world_to_clip))
            value.receiver=envelope;
        if(casts && retained_world_contribution(envelope,true,coverage).light_mask) {
            if(!prepare_owned_visual_pose(sample))return false;
            for(std::size_t index=0;index<admitted.size();++index) {
                if(!admitted[index].casts)continue;
                RetainedShadowActiveVisualPart part;part.index=static_cast<std::uint32_t>(index);
                if(!retained_visual_part_bounds(sample,index,part.bounds))return false;
                ++g_owned_visual_world_frame.shadow_source_diagnostic.part_bounds_rebuilt;
                if(g_bridge_sample.active)++g_bridge_sample.shadow_bounds_rebuilt;
                if(!retained_world_contribution(part.bounds,true,coverage).light_mask)continue;
                if(!retained_visual_depth_revision(sample,index,frame.time,part.revision))return false;
                value.casters.push_back(std::move(part));
            }
        }
        output.push_back(std::move(value));
    }
    return true;
}

bool same_active_visual_shadow_value(const RetainedShadowActiveVisualValue& a,
    const RetainedShadowActiveVisualValue& b) noexcept {
    if(a.signed_id!=b.signed_id || !same_shadow_bounds(a.receiver,b.receiver) ||
       a.casters.size()!=b.casters.size())return false;
    for(std::size_t j=0;j<a.casters.size();++j)
        if(a.casters[j].index!=b.casters[j].index ||
            !same_shadow_bounds(a.casters[j].bounds,b.casters[j].bounds) ||
            a.casters[j].revision!=b.casters[j].revision)return false;
    return true;
}

void changed_active_visual_shadow_ids(
    const std::vector<RetainedShadowActiveVisualValue>& current,
    const std::vector<RetainedShadowActiveVisualValue>& prior,
    std::vector<std::int32_t>& changed) {
    std::size_t before=0;
    for(const auto& value:current) {
        while(before<prior.size() && prior[before].signed_id<value.signed_id)++before;
        if(before==prior.size() || prior[before].signed_id!=value.signed_id ||
           !same_active_visual_shadow_value(value,prior[before]))
            changed.push_back(value.signed_id);
    }
    for(const auto& value:prior) {
        const auto found=std::lower_bound(current.begin(),current.end(),value.signed_id,
            [](const auto& item,std::int32_t id){return item.signed_id<id;});
        if(found==current.end() || found->signed_id!=value.signed_id)
            changed.push_back(value.signed_id);
    }
    std::sort(changed.begin(),changed.end());
    changed.erase(std::unique(changed.begin(),changed.end()),changed.end());
}

void capture_active_terrain_shadow_values(const std::vector<std::uint32_t>& ids,
    std::vector<RetainedShadowActiveTerrainValue>& output) {
    output.clear();output.reserve(ids.size());
    const auto& dynamic=g_owned_terrain_frame.dynamic;
    for(const auto sector:ids){
        RetainedShadowActiveTerrainValue value;value.sector=sector;
        if(sector<dynamic.size() && dynamic[sector].captured){
            value.captured=true;
            value.phase_revision=dynamic[sector].phase_values.revision;
            value.phase_count=static_cast<std::uint32_t>(dynamic[sector].animation_phases.size());
            value.animated_tiles_enabled=dynamic[sector].animation.animated_tiles_enabled!=0;
        }
        output.push_back(value);
    }
}

void changed_active_terrain_shadow_sectors(
    const std::vector<RetainedShadowActiveTerrainValue>& current,
    const std::vector<RetainedShadowActiveTerrainValue>& prior,
    std::vector<std::uint32_t>& changed) {
    std::size_t before=0;
    for(const auto& value:current) {
        while(before<prior.size() && prior[before].sector<value.sector)++before;
        if(before==prior.size() || prior[before].sector!=value.sector ||
           value.captured!=prior[before].captured ||
           value.phase_revision!=prior[before].phase_revision ||
           value.phase_count!=prior[before].phase_count ||
           value.animated_tiles_enabled!=prior[before].animated_tiles_enabled)
            changed.push_back(value.sector);
    }
    for(const auto& value:prior) {
        const auto found=std::lower_bound(current.begin(),current.end(),value.sector,
            [](const auto& item,std::uint32_t sector){return item.sector<sector;});
        if(found==current.end() || found->sector!=value.sector)changed.push_back(value.sector);
    }
    std::sort(changed.begin(),changed.end());
    changed.erase(std::unique(changed.begin(),changed.end()),changed.end());
}

bool same_shadow_coverage(const RetainedDirectionalCoverage& a,
    const RetainedDirectionalCoverage& b) noexcept {
    if(a.valid!=b.valid || a.directional!=b.directional ||
        a.spatial_selection!=b.spatial_selection ||
        a.world_to_clip!=b.world_to_clip || a.light_axes!=b.light_axes)return false;
    for(unsigned i=0;i<5;++i)
        if(!same_shadow_bounds(a.slices[i].receivers_in_light,b.slices[i].receivers_in_light) ||
            !same_shadow_bounds(a.slices[i].far_receivers_in_light,b.slices[i].far_receivers_in_light) ||
            a.slices[i].far_receiver_cells!=b.slices[i].far_receiver_cells ||
            a.slices[i].world_to_clip!=b.slices[i].world_to_clip ||
            a.slices[i].light_center!=b.slices[i].light_center ||
            a.slices[i].light_radius!=b.slices[i].light_radius ||
            a.slices[i].blend_start_view_distance!=b.slices[i].blend_start_view_distance ||
            a.slices[i].blend_end_view_distance!=b.slices[i].blend_end_view_distance)return false;
    return true;
}

bool same_shadow_depth_lighting(const NativeWorldLighting& a,
    const NativeWorldLighting& b) noexcept {
    const auto directional=[](const NativeWorldLighting& value) {
        return value.sunlight_diffuse[0]>0 || value.sunlight_diffuse[1]>0 ||
            value.sunlight_diffuse[2]>0;
    };
    return a.sunlight_direction==b.sunlight_direction && directional(a)==directional(b);
}

bool reusable_shadow_terrain(const RetainedShadowInputDomain& cached,
    bool terrain_enabled,bool) noexcept {
    if(cached.terrain_enabled!=terrain_enabled)return false;
    if(!terrain_enabled)return true;
    const auto& terrain=g_owned_terrain_frame;
    if(!terrain.world_transform_valid || !terrain.source_values.geometry ||
        terrain.front_end.status!=OwnedTerrainPacketStatus::ready)return false;
    return cached.terrain_source_generation==terrain.source_values.source_generation &&
        cached.terrain_source_revision==terrain.source_values.source_revision &&
        cached.terrain_object_to_world==terrain.object_to_world &&
        same_shadow_terrain_inputs(cached.terrain_inputs,terrain.front_end.inputs);
}

enum class ShadowInputRoute : std::uint8_t { full, reuse, selective };
struct ShadowInputDecision {
    ShadowInputRoute route=ShadowInputRoute::full;
    std::vector<std::int32_t> visuals;
    std::vector<std::uint32_t> terrain;
};

ShadowInputDiagnostics g_shadow_reuse_inputs; // render thread only
void observe_shadow_reuse_inputs(const RetainedShadowInputDomain* cached,
    const RetainedShadowSourceChanges& changes,const std::array<float,16>& clip,
    const NativeWorldLighting& lighting,std::uint32_t flags,bool undetected,
    bool terrain_enabled,std::uint64_t revision){
    auto& d=g_shadow_reuse_inputs;const auto epoch=bridge_current_world_epoch();
    if(d.epoch!=epoch){d={};d.epoch=epoch;}++d.attempts;
    using Cause=ShadowInputDifference;
    std::array<bool,static_cast<unsigned>(Cause::count)> diff{};
    const auto note=[&](Cause cause,bool changed){diff[static_cast<unsigned>(cause)]=changed;};
    note(Cause::no_cached,!cached);note(Cause::journal_full,changes.full);
    note(Cause::terrain_dirty,changes.terrain_dirty&&!changes.terrain_time_only);
    note(Cause::sink_unavailable,!g_native_sink_shadow_begin||!g_native_sink_shadow_end);
    if(cached){
        note(Cause::world_to_clip,cached->world_to_clip!=clip);
        note(Cause::sun_direction,cached->lighting.sunlight_direction!=lighting.sunlight_direction);
        const auto lit=[](const NativeWorldLighting& v){return v.sunlight_diffuse[0]>0||v.sunlight_diffuse[1]>0||v.sunlight_diffuse[2]>0;};
        note(Cause::sun_enabled,lit(cached->lighting)!=lit(lighting));
        note(Cause::class_flags,cached->class_flags!=flags);note(Cause::undetected,cached->draw_undetected!=undetected);
        note(Cause::source_revision,cached->source_revision!=revision);
        note(Cause::terrain_enabled,cached->terrain_enabled!=terrain_enabled);
        if(terrain_enabled){const auto& t=g_owned_terrain_frame;
            note(Cause::terrain_unavailable,!t.world_transform_valid||!t.source_values.geometry||t.front_end.status!=OwnedTerrainPacketStatus::ready);
            note(Cause::terrain_identity,cached->terrain_source_generation!=t.source_values.source_generation||cached->terrain_source_revision!=t.source_values.source_revision||
                cached->terrain_inputs.source_generation!=t.front_end.inputs.source_generation||cached->terrain_inputs.source_revision!=t.front_end.inputs.source_revision);
            note(Cause::terrain_transform,cached->terrain_object_to_world!=t.object_to_world);
            const auto& a=cached->terrain_inputs;const auto& b=t.front_end.inputs;
            note(Cause::terrain_origin,a.terrain_origin!=b.terrain_origin);
            note(Cause::terrain_camera_position,a.camera_position!=b.camera_position);
            note(Cause::terrain_lod_distances,a.lod_distances!=b.lod_distances);
            note(Cause::terrain_maximum_lod,a.maximum_lod!=b.maximum_lod);
            note(Cause::terrain_full_detail,a.force_full_detail!=b.force_full_detail);
            note(Cause::terrain_visibility_fingerprint,a.game_visibility_fingerprint!=b.game_visibility_fingerprint);
            note(Cause::terrain_visibility_domain,a.game_visibility_domain_present!=b.game_visibility_domain_present);
            note(Cause::terrain_visibility_min_y,a.game_visibility_min_y!=b.game_visibility_min_y);
            note(Cause::terrain_visibility_rows,a.game_visibility_rows!=b.game_visibility_rows);
            note(Cause::terrain_object_to_clip,a.object_to_clip!=b.object_to_clip);
            note(Cause::terrain_animation,t.source_values.geometry&&!same_shadow_static_animation(cached->terrain_animation,t.source_values.geometry->animation,true));
        }
        // Entry-tuple-only witness: active caster values are not evaluated when
        // the existing world_to_clip gate returns early. No reuse policy changes.
        if(diff[static_cast<unsigned>(Cause::world_to_clip)]&&std::count(diff.begin(),diff.end(),true)==1){
            ++d.only_world_to_clip_frames;
            for(unsigned i=0;i<16;++i)d.only_world_to_clip_max_abs_delta=std::max(d.only_world_to_clip_max_abs_delta,std::abs(double(clip[i])-cached->world_to_clip[i]));
        }
    }
    for(unsigned i=0;i<diff.size();++i)d.counts[i]+=diff[i];
}
// The retained scene's shadow revision the preparation reasons about: on the
// scene thread the one its values were copied at (later game-thread changes
// belong to the next command), else the live one.
std::uint64_t owned_shadow_current_revision(const RetainedStaticScene& scene) noexcept {
    return g_scene_capture_revision ? g_scene_capture_revision : scene.shadow_source_revision();
}
ShadowInputDecision decide_owned_shadow_input(RetainedStaticScene& scene,
    const RetainedShadowInputDomain* cached,const RetainedShadowSourceChanges& changes,
    const RetainedVisualFrame& values,const std::array<float,16>& world_to_clip,
    const NativeWorldLighting& lighting,std::uint32_t flags,bool draw_undetected,
    bool terrain_enabled,std::uint64_t source_revision) {
    observe_shadow_reuse_inputs(cached,changes,world_to_clip,lighting,flags,draw_undetected,terrain_enabled,source_revision);
    const auto note=[](ShadowInputDifference cause){++g_shadow_reuse_inputs.counts[static_cast<unsigned>(cause)];};
    ShadowInputDecision result;
    if(!cached || cached->world_to_clip!=world_to_clip ||
       !same_shadow_depth_lighting(cached->lighting,lighting) ||
       cached->class_flags!=flags || cached->draw_undetected!=draw_undetected ||
       !reusable_shadow_terrain(*cached,terrain_enabled,false) || changes.full ||
       (changes.terrain_dirty && !changes.terrain_time_only))return result;
    std::vector<RetainedShadowActiveVisualValue> active_visuals;
    std::vector<RetainedShadowActiveTerrainValue> active_terrain;
    if(!capture_active_visual_shadow_values(values,changes.time_active_visuals,
        draw_undetected,flags,cached->coverage,active_visuals)){note(ShadowInputDifference::active_values_unavailable);return result;}
    auto& diagnostic=g_owned_visual_world_frame.shadow_source_diagnostic;
    diagnostic.active_visuals+=active_visuals.size();
    diagnostic.active_terrain+=changes.time_active_terrain_sectors.size();
    if(g_bridge_sample.active) {
        g_bridge_sample.shadow_active_visuals+=active_visuals.size();
        g_bridge_sample.shadow_active_terrain+=changes.time_active_terrain_sectors.size();
    }
    capture_active_terrain_shadow_values(changes.time_active_terrain_sectors,active_terrain);
    result.visuals=changes.dirty_visuals;
    changed_active_visual_shadow_ids(active_visuals,cached->active_visuals,result.visuals);
    changed_active_terrain_shadow_sectors(active_terrain,cached->active_terrain,result.terrain);
    if(terrain_enabled && !same_shadow_static_animation(cached->terrain_animation,
        g_owned_terrain_frame.source_values.geometry->animation,true)) {
        result.terrain.insert(result.terrain.end(),changes.wave_active_terrain_sectors.begin(),
            changes.wave_active_terrain_sectors.end());
        std::sort(result.terrain.begin(),result.terrain.end());
        result.terrain.erase(std::unique(result.terrain.begin(),result.terrain.end()),
            result.terrain.end());
    }
    if(!result.visuals.empty())note(ShadowInputDifference::changed_visuals);
    if(!result.terrain.empty())note(ShadowInputDifference::changed_terrain);
    diagnostic.changed_visuals+=result.visuals.size();
    diagnostic.changed_terrain+=result.terrain.size();
    if(g_bridge_sample.active) {
        g_bridge_sample.shadow_changed_visuals+=result.visuals.size();
        g_bridge_sample.shadow_changed_terrain+=result.terrain.size();
    }
    result.route=ShadowInputRoute::selective;
    if(result.visuals.empty() && result.terrain.empty() &&
       cached->source_revision==source_revision && g_native_sink_shadow_begin &&
       g_native_sink_shadow_end) {
        CityRetainedOwnerGuard registration_timing(CityRetainedOwner::depth_registration);
        std::uint8_t dirty=0;
        if(g_native_sink_shadow_begin(g_native_sink_context,cached->frame,dirty)) {
            diagnostic.dirty_mask|=dirty;
            if(g_bridge_sample.active)g_bridge_sample.shadow_dirty_mask|=dirty;
            const bool finished=!dirty&&g_native_sink_shadow_end(g_native_sink_context,true);
            if(!dirty&&!finished)note(ShadowInputDifference::renderer_end_failed);
            if(dirty)note(ShadowInputDifference::renderer_dirty);
            if(!dirty && finished &&
               owned_shadow_current_revision(scene)==cached->source_revision)
                result.route=ShadowInputRoute::reuse;
            else if(dirty)g_native_sink_shadow_end(g_native_sink_context,false);
            if(owned_shadow_current_revision(scene)!=cached->source_revision)note(ShadowInputDifference::source_changed_during_reuse);
        }else note(ShadowInputDifference::renderer_begin_failed);
    }
    return result;
}

} // namespace
void report_owned_shadow_pass_failure(const char* reason) noexcept {
#ifdef EID3D11_EMBEDDED
    embedded_report_render_failure("owned_shadow_pass_rejected", reason);
    return;
#endif
    static std::uint64_t failures=0;
    ++failures;
    if(failures>16 && failures%600)return;
    char detail[160];
    std::snprintf(detail,sizeof(detail),"reason=%s failures=%llu",reason?reason:"unknown",
        static_cast<unsigned long long>(failures));
    append_bridge_event("owned_shadow_pass_rejected",detail,false);
}
bool prepare_owned_world_shadows_from(OwnedShadowInputs in,
    const std::array<float,16>& world_to_clip) {
    auto& frame=g_owned_visual_world_frame;
    {
        const auto& pass=in.pass;
        const auto flags=in.flags;
        const bool draw_undetected=in.draw_undetected;
        const auto shadow_source_revision=in.shadow_source_revision;
        const auto shadow_lighting=shadow_projection_lighting(
            g_native_world_lighting_frame.lighting);
        auto& shadow_scene=retained_visual_scene();
        const bool terrain_enabled=(flags&0x100u)!=0 && g_owned_terrain_frame.source_values.terrain;
        std::vector<NativeWorldBounds> receivers;
        CityRetainedOwnerGuard decision_timing(CityRetainedOwner::shadow_input_decision);
        // Caves have no sun (cave_lighting.h): no sun maps, receivers or
        // retained sun domain there; point shadows keep pass.shadows_enabled.
        const bool sun=pass.shadows_enabled && !g_native_world_lighting_frame.cave;
        const auto cached=sun ? shadow_scene.shadow_input_domain() : nullptr;
        const auto changes=shadow_scene.shadow_source_changes(cached?cached->source_revision:0);
        std::shared_ptr<const RetainedVisualFrame> values=std::move(in.values);
        CityRetainedOwnerGuard point_lighting_timing(CityRetainedOwner::lighting_publish);
        if (g_native_sink_point_lights) {
            if (!g_native_sink_point_lights(g_native_sink_context,*values))
                report_world_point_light_omission("world_point_light.sink_unavailable");
        } else if (!values->world_point_lights.empty()) {
            report_world_point_light_omission("world_point_light.sink_missing");
        }
        point_lighting_timing.finish();
        auto decision=decide_owned_shadow_input(shadow_scene,cached.get(),changes,*values,
            world_to_clip,shadow_lighting,flags,draw_undetected,
            terrain_enabled,shadow_source_revision);
        decision_timing.finish();
        if(decision.route==ShadowInputRoute::reuse) {
            if(owned_shadow_current_revision(shadow_scene)==cached->source_revision) {
                ++frame.shadow_source_diagnostic.map_reuses;
                if(g_bridge_sample.active)++g_bridge_sample.shadow_map_reuses;
                if(!prepare_owned_visual_values(std::move(values),world_to_clip,
                    shadow_lighting,draw_undetected,flags,cached->receivers,pass.shadows_enabled,&cached->coverage,
                    changes.full?nullptr:&changes.dirty_visuals,cached->source_revision,shadow_source_revision))
                    throw "owned_shadow_scene_values";
                execute_owned_point_shadow_pass(terrain_enabled,shadow_scene,true,flags,draw_undetected);
                execute_owned_rain_occlusion_pass();
                // Reused maps skip execute_owned_shadow_pass, which pumps the
                // far-terrain prewarm on redraw frames. Pump it here too so a
                // stationary camera keeps finishing it under its own budget.
                if(terrain_enabled){prewarm_owned_terrain_far_shadow_meshes();prewarm_owned_terrain_near_meshes();}
                frame.shadow_attempted=true;frame.shadow_ready=true;
                if(g_native_sink_shadow_diagnostics)
                    g_native_sink_shadow_diagnostics(g_native_sink_context,frame.serial,
                        frame.shadow_source_diagnostic);
                return true;
            }
        }
        g_world_lamp_memory.camera=g_owned_terrain_frame.front_end.inputs.camera_position;
        g_world_lamp_memory.camera_valid=true;
        if(sun && terrain_enabled) {
            CityRetainedOwnerGuard receiver_timing(CityRetainedOwner::receiver_fitting);
            // Missing terrain material retires only its caster contributions.
            // It must not prevent independent entity samples or color output.
            // The base range captured this material before intervening game
            // work. Re-reading draw state here would change terrain semantics.
            auto& terrain=g_owned_terrain_frame;
            if(!terrain.world_transform_valid)throw "owned_shadow_terrain_world_transform_invalid";
            if(!terrain.source_values.geometry)throw "owned_shadow_terrain_geometry_unavailable";
            if(terrain.front_end.status!=OwnedTerrainPacketStatus::ready)throw "owned_shadow_terrain_packet_not_ready";
            // Receiver coverage comes from this invocation's color decisions.
            // Caster membership below still traverses the complete source.
            if(!append_owned_terrain_receiver_bounds(terrain,receivers))
                throw "owned_shadow_terrain_transform";
            // Rendered neighbour ground and decorations receive sun shadows
            // too: fit the maps to them as to the active map's sectors.
            neighbour_receivers_append_visible(world_to_clip,receivers);
        }
        if(!prepare_owned_visual_values(std::move(values),
            world_to_clip,shadow_lighting,draw_undetected,
            flags,receivers,pass.shadows_enabled,nullptr,changes.full?nullptr:&changes.dirty_visuals,
            cached?cached->source_revision:0,shadow_source_revision))
            throw "owned_shadow_scene_values";
        frame.persistent_static_shadows=sun;
        frame.shadow_time_active_visuals=changes.time_active_visuals;
        if(pass.shadows_enabled && g_bridge_sample.active)
            ++g_bridge_sample.shadow_receiver_refits;
        if(pass.shadows_enabled)++frame.shadow_source_diagnostic.receiver_refits;
        execute_owned_point_shadow_pass(terrain_enabled,shadow_scene,true,flags,draw_undetected);
        const bool reuse_static=cached && !changes.full &&
            cached->class_flags==flags && cached->draw_undetected==draw_undetected &&
            cached->frame.world_epoch==bridge_current_world_epoch();
        if(!execute_owned_shadow_pass(terrain_enabled,shadow_scene,
            nullptr,&changes,reuse_static?cached.get():nullptr))return false;
        execute_owned_rain_occlusion_pass();
        if(sun) {
            CityRetainedOwnerGuard snapshot_timing(CityRetainedOwner::domain_snapshot);
            // Publication is best effort: GPU success is not contingent on an
            // allocation for next frame's retained value snapshot.
            try {
                auto next=std::make_shared<RetainedShadowInputDomain>();
                next->source_revision=shadow_source_revision;
                next->world_to_clip=world_to_clip;
                next->lighting=shadow_lighting;
                next->class_flags=flags;
                next->draw_undetected=draw_undetected;
                next->terrain_enabled=terrain_enabled;
                if(terrain_enabled) {
                    const auto& terrain=g_owned_terrain_frame;
                    next->terrain_source_generation=terrain.source_values.source_generation;
                    next->terrain_source_revision=terrain.source_values.source_revision;
                    next->terrain_structural_revision=terrain.source_values.geometry->structural_revision;
                    next->terrain_animation=terrain.source_values.geometry->animation;
                    next->terrain_inputs=terrain.front_end.inputs;
                    next->terrain_object_to_world=terrain.object_to_world;
                    next->terrain_material=terrain.shadow_material;
                }
                next->coverage=frame.coverage;
                next->receivers=std::move(frame.shadow_receivers);
                next->casters=std::move(frame.shadow_casters);
                next->frame=std::move(frame.shadows);
                const auto active=shadow_scene.shadow_source_changes(shadow_source_revision);
                if(!capture_active_visual_shadow_values(*frame.values,active.time_active_visuals,
                       draw_undetected,flags,frame.coverage,next->active_visuals))
                    throw "owned_shadow_active_visual_values";
                capture_active_terrain_shadow_values(active.time_active_terrain_sectors,
                    next->active_terrain);
                shadow_scene.publish_shadow_input_domain(std::move(next));
            } catch (...) {}
        }
        if(g_native_sink_shadow_diagnostics)
            g_native_sink_shadow_diagnostics(g_native_sink_context,frame.serial,
                frame.shadow_source_diagnostic);
        return true;
    }
}
namespace {



} // namespace

// The terrain caster caches, rebuilt on their next miss (bridge_release_world_caches).
void release_owned_terrain_shadow_caches() noexcept {
    g_owned_static_casters = {};
    g_point_scene = {};
    g_rain_baked = {};
    g_terrain_shadow_caster_cache.valid = false;
    decltype(g_terrain_shadow_caster_cache.casters)().swap(g_terrain_shadow_caster_cache.casters);
    decltype(g_terrain_shadow_caster_cache.commands)().swap(g_terrain_shadow_caster_cache.commands);
    g_terrain_shadow_part_cache.valid = false; g_terrain_shadow_part_cache.order_stale = true;
    g_terrain_shadow_part_cache.prepared_owner.reset();
    decltype(g_terrain_shadow_part_cache.parts)().swap(g_terrain_shadow_part_cache.parts);
    decltype(g_terrain_shadow_part_cache.order)().swap(g_terrain_shadow_part_cache.order);
}

void bridge_set_terrain_far_shadow_prewarm(bool enabled) noexcept {
    g_terrain_far_shadow_prewarm_enabled.store(enabled, std::memory_order_relaxed);
}

#if !defined(EID3D11_EMBEDDED)
ShadowInputDiagnostics bridge_shadow_reuse_diagnostics() noexcept {return g_shadow_reuse_inputs;}
#endif

} // namespace owned
