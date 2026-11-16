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
void build_liquid_ripples(const RetainedVisualFrame& frame,const TerrainCpuAsset& terrain,
    const OwnedTerrainGeometryValues& geometry,bool draw_undetected_units,NativeLiquidRipples& output) noexcept {
    output={};output.meta[1]=static_cast<float>(frame.time);
    if(!terrain.sector_count_x||!terrain.sector_count_y)return;
    for(const auto& unit:frame.entities) {
        if(output.meta[0]>=output.position_speed.size())break;
        // Only units the game shows the player: an undetected unit must not
        // reveal itself through the water either.
        if(!unit.values.unit||!unit.height_valid||!retained_visual_sample_shown(unit,draw_undetected_units)||
           unit.values.invisible||unit.values.dead)continue;
        const auto& p=unit.pose.root;
        if(!std::isfinite(p[0])||!std::isfinite(p[1])||!std::isfinite(p[2])||
           p[0]<0||p[1]<0||p[0]>=terrain.sector_count_x*32||p[1]>=terrain.sector_count_y*32)continue;
        const unsigned x=static_cast<unsigned>(p[0]),y=static_cast<unsigned>(p[1]);
        const auto& sector=terrain.sectors[(y/32)*terrain.sector_count_x+x/32];
        const auto owner=terrain_base_liquid_object(terrain,sector,(y%32)*33+x%32);
        if(owner==UINT16_MAX||terrain.objects[owner].self_illumination>0)continue;
        float ground=0,surface=0;
        if(!sample_terrain_height(terrain,p[0],p[1],TerrainHeightLayer::ground,ground)||
           !sample_terrain_height(terrain,p[0],p[1],TerrainHeightLayer::surface,surface,&geometry.layer_shifts)||
           surface<=ground||p[2]>surface+.05f||p[2]<ground-.05f)continue;
        // Flying or levitating units can report a root at ground height; only
        // a posed model that actually reaches the surface disturbs the water.
        float lowest=std::numeric_limits<float>::max();
        if(unit.assembly)for(std::size_t part=0;part<unit.assembly->parts.size();++part){
            NativeWorldBounds bounds{};
            if(retained_visual_part_bounds(unit,part,bounds)&&bounds.valid)lowest=std::min(lowest,bounds.minimum[2]);
        }
        if(lowest!=std::numeric_limits<float>::max()&&lowest>surface+.1f)continue;
        const auto n=static_cast<unsigned>(output.meta[0]++);
        output.position_speed[n]={p[0],p[1],surface,unit.liquid_speed};
        std::uint32_t hash=static_cast<std::uint32_t>(unit.lifecycle.id)^static_cast<std::uint32_t>(unit.lifecycle.generation);
        output.id[n]=static_cast<std::uint32_t>(unit.lifecycle.id)*0x9e3779b1u^static_cast<std::uint32_t>(unit.lifecycle.generation);
        hash^=hash>>16;hash*=0x7feb352du;hash^=hash>>15;
        output.phase[n][0]=(hash&0xffffu)*(6.2831853f/65536.f);
        // Body size scales the wake: the unit's own box half extent (CUnit
        // +0x144, the native local bounding-box maxima), falling back to the
        // model envelope. A rat makes tiny ripples, a troll a big wake.
        const float half_extent=std::max(unit.values.target_half_extent[0],unit.values.target_half_extent[1]);
        output.phase[n][1]=half_extent>0?std::clamp(half_extent,.05f,2.5f):
            unit.envelope_radius>0?std::clamp(unit.envelope_radius*.35f,.05f,2.5f):.35f;
        output.phase[n][2]=unit.liquid_direction[0];output.phase[n][3]=unit.liquid_direction[1];
        OwnedLiquidFlow flow;
        if(owned_terrain_liquid_flow(terrain,(y/32)*terrain.sector_count_x+x/32,(y%32)*33+x%32,&geometry,flow))
            output.flow[n]={flow.current[0],flow.current[1],0.f,0.f};
    }
}
static GroundUnitTrail snow_trail_row(const RetainedVisualSample& unit) noexcept {
    return {snow_trail_key(unit.resource),snow_trail_extent(unit.values.target_half_extent)};
}
void build_ground_cover_units(const RetainedVisualFrame& frame,const TerrainCpuAsset& terrain,
    float center_x,float center_y,float radius,bool draw_undetected_units,std::vector<std::array<float,4>>& output,
    std::vector<GroundUnitDetail>* details,std::vector<GroundUnitTrail>* trails) noexcept {
    output.clear();
    if(details)details->clear();
    if(trails)trails->clear();
    if(!terrain.sector_count_x||!terrain.sector_count_y)return;
    const float reach=radius*radius;
    try {
        for(const auto& unit:frame.entities) {
            // Corpses only for the critters' carrion birds: still in the
            // scene and not fading out (the unit material alpha channel).
            // Only units the game shows the player (the drawing rule): an
            // undetected unit must not reveal itself through the cover.
            const bool corpse=details&&unit.values.dead&&unit.values.fade_alpha>.5f;
            if(!unit.values.unit||!unit.height_valid||!retained_visual_sample_shown(unit,draw_undetected_units)||
               unit.values.invisible||(unit.values.dead&&!corpse))continue;
            const auto& p=unit.pose.root;
            if(!std::isfinite(p[0])||!std::isfinite(p[1])||!std::isfinite(p[2]))continue;
            const float dx=p[0]-center_x,dy=p[1]-center_y;
            if(dx*dx+dy*dy>reach)continue;
            float ground=0;
            if(!sample_terrain_height(terrain,p[0],p[1],TerrainHeightLayer::ground,ground))continue;
            GroundUnitDetail detail{};
            if(details){
                const auto& l=unit.lifecycle;
                detail.id=critters::splitmix(critters::splitmix(std::uint64_t(std::uint32_t(l.id))^(l.generation<<32))^
                    l.positive_world_epoch);
            }
            if(details){
                const auto& s=unit.selection;
                if(!s.neutral&&s.database&&s.ordinal<s.database->entries.size())
                    detail.stance=critters::stance_from_clip(s.database->entries[s.ordinal].name.c_str());
                detail.dead=corpse;
                detail.blood=unit.values.blood;
                detail.all_model_parts=unit.assembly?unit.assembly->all_model_parts:0;
                detail.visibility=unit.values.visibility;
                detail.loudness=unit.values.loudness;
                detail.vision=unit.values.vision;
                detail.controlled=unit.values.controlled;
                detail.pose=unit.values.pose;
                detail.moving=unit.values.moving;
                detail.undead=unit.values.undead_prototype || (unit.assembly&&unit.assembly->skeleton&&
                    critters::undead_model(unit.assembly->skeleton->visual_reference.c_str()));
                detail.crawler=unit.assembly&&unit.assembly->skeleton&&
                    critters::crawler_model(unit.assembly->skeleton->visual_reference.c_str());
            }
            if(corpse){output.push_back({p[0],p[1],ground,unit.envelope_radius});details->push_back(detail);if(trails)trails->push_back(snow_trail_row(unit));continue;}
            // Cover rows (no details) leave levitating races out: they never
            // part the grass. Critter threat rows keep them.
            if(!details&&!ground_cover::unit_presses_cover(unit.values.locomotion))continue;
            // Cover contact is the unit position, independent of animation.
            // Posed part envelopes change between walking, running and crawling
            // and are unsuitable for either contact or the pressing radius.
            if(!details){
                if(p[2]>ground+.5f||!(snow_trail_extent(unit.values.target_half_extent)>0))continue;
                output.push_back({p[0],p[1],std::max(p[2],ground),unit.envelope_radius});
                if(trails)trails->push_back(snow_trail_row(unit));
                continue;
            }
            // Critter threat rows retain their posed-height rule.
            float lowest=std::numeric_limits<float>::max();
            if(unit.assembly)for(std::size_t part=0;part<unit.assembly->parts.size();++part){
                NativeWorldBounds bounds{};
                if(retained_visual_part_bounds(unit,part,bounds)&&bounds.valid)lowest=std::min(lowest,bounds.minimum[2]);
            }
            if(lowest!=std::numeric_limits<float>::max()&&lowest>ground+.5f)continue;
            output.push_back({p[0],p[1],std::max(p[2],ground),unit.envelope_radius});
            if(details)details->push_back(detail);
            if(trails)trails->push_back(snow_trail_row(unit));
        }
    } catch(...) { output.clear(); if(details)details->clear(); if(trails)trails->clear(); }
}

void build_ground_cover_corpses(const RetainedVisualFrame& frame,const TerrainCpuAsset& terrain,
    float center_x,float center_y,float radius,bool draw_undetected_units,
    std::vector<ground_cover::FootprintSplat>& output) noexcept {
    if(!terrain.sector_count_x||!terrain.sector_count_y)return;
    const float reach=radius*radius;
    const std::size_t first=output.size();
    try {
        for(const auto& unit:frame.entities) {
            // The critters' corpse rule: dead, still in the scene and not fading
            // out (unit material alpha), and shown: never a hidden or invisible body.
            if(!unit.values.unit||!unit.values.dead||!(unit.values.fade_alpha>.5f)||!unit.height_valid||
               !retained_visual_sample_shown(unit,draw_undetected_units)||unit.values.invisible)continue;
            const auto& p=unit.pose.root;
            if(!std::isfinite(p[0])||!std::isfinite(p[1])||!std::isfinite(p[2]))continue;
            const float dx=p[0]-center_x,dy=p[1]-center_y;
            if(dx*dx+dy*dy>reach)continue;
            float ground=0;
            if(!sample_terrain_height(terrain,p[0],p[1],TerrainHeightLayer::ground,ground))continue;
            // The posed body: its centre is where it lies; a body held above
            // the ground (airborne) presses nothing.
            NativeWorldBounds body{};
            bool posed=false;
            if(unit.assembly)for(std::size_t part=0;part<unit.assembly->parts.size();++part){
                NativeWorldBounds bounds{};
                if(!retained_visual_part_bounds(unit,part,bounds)||!bounds.valid)continue;
                if(!posed){body=bounds;posed=true;continue;}
                for(unsigned k=0;k<3;++k){
                    body.minimum[k]=std::min(body.minimum[k],bounds.minimum[k]);
                    body.maximum[k]=std::max(body.maximum[k],bounds.maximum[k]);
                }
            }
            if(posed&&body.minimum[2]>ground+.5f)continue;
            const float x=posed?.5f*(body.minimum[0]+body.maximum[0]):p[0];
            const float y=posed?.5f*(body.minimum[1]+body.maximum[1]):p[1];
            if(!std::isfinite(x)||!std::isfinite(y))continue;
            // World forward is model -Y through the same root rotation as drawing.
            const auto forward=visual_root_vector(unit.pose,{0.f,-1.f,0.f});
            output.push_back(ground_cover::corpse_splat(x,y,forward[0],forward[1],unit.envelope_radius));
        }
    } catch(...) { output.resize(first); }
}

} // namespace owned
