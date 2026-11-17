#include "model_preview_producer.h"
#include "../Game/OwnedRender/ModelPreviewLighting.h"
#include "model_draw.h"
#include <iostream>
#include <stdexcept>

using namespace owned;
namespace {
void require(bool value, const char* reason) {
    if (!value) throw std::runtime_error(reason);
}
std::vector<NativeGeometryView> sources;
std::vector<NativePreparedPartSubmission> records;
NativePreparedMeshHandle mesh;
NativePreparedMaterialHandle materialHandle;
unsigned groupCalls = 0, partCalls = 0;
bool groupAvailable = true;
const std::array<float,16> placement{2,0,0,3, 0,2,0,4, 0,0,2,5, 0,0,0,1};
const std::array<float,16> clip{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
void fixtures(unsigned count) {
    sources.clear(); sources.resize(count); records.clear(); records.resize(count);
    groupCalls = partCalls = 0; groupAvailable = true;
    for (unsigned i = 0; i < count; ++i) {
        auto& source = sources[i];
        source.world.enabled = true; source.world.record_revision = 1;
        source.prepared_identity = {7,1,1,1,i,0,1,true};
        source.prepared_mesh = &mesh; source.prepared_material = &materialHandle;
        source.owned_texture.identity = 10 + i;
        source.flags = native_unit_flag | native_ground_contact_flag | 12u;
        records[i].source = &source;
    }
}
}

namespace owned {
int g_fur_mode = 0;
// Fixture boundary only: emulate retained records, which keep pass admission
// in NativePreparedPartSubmission, separate from their immutable source view.
// The complete production preview producer and FIG material helpers run below.
bool retained_visual_gpu_pose(const RetainedVisualSample& sample, NativeWorldGeometry::Pose*& pose) {
    pose = &sample.prepared->gpu_pose; pose->owner = sample.prepared; return true;
}
bool retained_visual_merged_groups(const RetainedVisualSample&) { return groupAvailable; }
bool retained_visual_group_submission(const RetainedVisualSample&, std::size_t index,
    const NativeWorldBounds& bounds, NativeContribution contribution, double, std::uint8_t,
    NativePreparedPartSubmission*& output) {
    ++groupCalls; output = &records.at(index);
    output->contribution = contribution; output->bounds = bounds;
    output->values.flags = sources[index].flags;
    return true;
}
bool retained_visual_part_bounds(const RetainedVisualSample&, std::size_t, NativeWorldBounds& bounds) {
    bounds = {{-1,-1,-1},{1,1,1},true}; return true;
}
bool retained_visual_part_submission(const RetainedVisualSample&, std::size_t index,
    const NativeWorldBounds& bounds, NativeContribution contribution, double, std::uint8_t,
    NativePreparedPartSubmission*& output) {
    ++partCalls; output = &records.at(index);
    output->contribution = contribution; output->bounds = bounds; return true;
}
}

int main() try {
    RetainedVisualSample sample;
    sample.prepared = std::make_shared<VisualPreparedResources>();
    auto asset = std::make_shared<skn::RuntimeAsset>();
    auto gltf = std::make_shared<gltf::Asset>(); gltf->radius = gltf->animation_radius = 1;
    asset->gltf_asset = gltf; asset->asset.materials.resize(3);
    // Store BLEND first: the producer must still submit OPAQUE/MASK before it.
    asset->asset.materials[0].alpha = 2; asset->asset.materials[0].base[3] = .5f;
    asset->asset.materials[1].alpha = 0; asset->asset.materials[1].base[3] = .2f;
    asset->asset.materials[2].alpha = 1; asset->asset.materials[2].base[3] = .75f;
    sample.prepared->motion_asset = asset; sample.prepared->skn_selected = true;
    sample.prepared->merged_groups.resize(3);
    for (unsigned i = 0; i < 3; ++i) {
        auto& group = sample.prepared->merged_groups[i]; group.parts = {i}; group.skn_material = i; group.skn_asset = asset;
    }
    fixtures(3);
    unsigned legacyCalls = 0;
    const auto read = [&](std::size_t, ModelPartFacts&) { ++legacyCalls; return false; };
    const auto bind = [&](unsigned) { ++legacyCalls; return false; };
    const auto prepare = [&](std::size_t, ModelPreviewPart&) { ++legacyCalls; return false; };
    const auto wound = [&](std::size_t, const ModelPreviewPart&) { ++legacyCalls; return false; };
    std::vector<std::size_t> submitted;
    const ModelPreviewInput input{sample,placement,clip,0,255,20,false};
    const auto submit = [&](const NativeGeometryView& view, const ModelPreviewPart& part) {
        require(view.contribution.known && view.contribution.color && !view.contribution.light_mask,
                "glTF preview lacks color-pass admission (world.prepared_identity rejection)");
        require(view.world.pose == &sample.prepared->gpu_pose && !view.world.owner->expired(), "private pose lost");
        require(view.world.object_to_world == placement && view.world.world_to_clip == clip, "preview placement lost");
        require(view.prepared_identity.presentation_serial == 20 && view.interface_preview, "preview identity/domain lost");
        require(view.owned_texture.identity == sources[part.ordinal].owned_texture.identity, "material texture replaced");
        require((view.flags & (12u | native_ground_contact_flag)) == 0 && (view.flags & native_unit_flag), "preview flags");
        const auto& group=sample.prepared->merged_groups[part.ordinal];
        const auto& material = group.skn_asset->asset.materials[group.skn_material];
        require(view.lighting.material_alpha == (material.alpha == 0 ? 1.f : material.base[3]), "alpha mode lost");
        submitted.push_back(part.ordinal); return true;
    };
    ModelPartResult result;
    require(produce_model_preview(input,0,read,bind,prepare,submit,wound,result), "glTF preview failed");
    require(submitted == std::vector<std::size_t>{1,2,0} && result.submitted == 3 && result.visible == 1,
            "OPAQUE/MASK/BLEND order or visible result");
    require(!legacyCalls && groupCalls == 3 && !partCalls, "glTF used FIG callbacks");
    for (const auto& source : sources) require(!source.contribution.known, "immutable source mutated");
    std::cout << "PASS glTF color admission, private pose/placement, materials and alpha order\n";
    auto item = std::make_shared<skn::RuntimeAsset>(); item->asset.materials.resize(1);
    item->asset.materials[0].alpha=2;item->asset.materials[0].base[3]=.4f;
    auto& attachment=sample.prepared->merged_groups[2];
    attachment.skn_asset=item;attachment.skn_material=0;attachment.gltf_weapon=true;
    auto equipment=std::make_shared<gltf::EquipmentSelection>();equipment->radius=4;
    sample.prepared->gltf_equipment=equipment;
    submitted.clear();
    require(produce_model_preview(input,0,read,bind,prepare,submit,wound,result),"item preview failed");
    require(submitted==std::vector<std::size_t>{1,0,2},"item used body material/alpha order");
    require(records[2].bounds.maximum[0]>placement[3]+10,"item bounds clipped to body");
    sample.visibility.hide_weapons=true;submitted.clear();
    require(produce_model_preview(input,0,read,bind,prepare,submit,wound,result),"hidden weapon rejected preview");
    require(submitted==std::vector<std::size_t>{1,0},"hidden weapon submitted");
    sample.visibility.hide_weapons=false;sample.visibility.hide_arrow=true;attachment.gltf_arrow=true;
    submitted.clear();
    require(produce_model_preview(input,0,read,bind,prepare,submit,wound,result),"hidden arrow rejected preview");
    require(submitted==std::vector<std::size_t>{1,0},"hidden arrow submitted");
    sample.visibility.hide_arrow=false;
    std::cout << "PASS independent item materials, expanded bounds, weapon/arrow visibility in production preview\n";
    groupAvailable = false;
    require(!produce_model_preview(input,0,read,bind,prepare,submit,wound,result), "failed groups accepted");
    groupAvailable = true;
    require(!produce_model_preview(input,0,read,bind,prepare,
        [](const NativeGeometryView&, const ModelPreviewPart&) { return false; },wound,result), "queue failure ignored");
    std::cout << "PASS producer/queue failures propagate\n";

    fixtures(2); sample.prepared->motion_asset.reset();
    auto assembly = std::make_shared<VisualAssemblyAsset>(); assembly->parts.resize(2);
    sample.assembly = assembly; unsigned binds = 0, draws = 0;
    require(produce_model_preview(input,2,
        [](std::size_t, ModelPartFacts&) { return true; },
        [&](unsigned) { ++binds; return true; },
        [](std::size_t i, ModelPreviewPart& part) {
            part.ordinal = i; part.hidden = i == 1;
            part.material.default_colors = true; part.material.diffuse = 0xff123456; return true;
        },
        [&](const NativeGeometryView& view, const ModelPreviewPart&) {
            require(view.contribution.known && view.contribution.color && !view.contribution.light_mask, "FIG admission changed");
            require(view.default_diffuse == 0xff123456 && (view.flags & 4u), "FIG default material changed");
            ++draws; return true;
        },wound,result), "FIG preview failed");
    require(draws == 1 && binds == 1 && partCalls == 1 && !groupCalls, "FIG hidden part/set handling changed");
    std::cout << "PASS FIG preview color/material and hidden-part regression\n";
    OwnedRender::ModelPreviewLighting studio;
    studio.world.ambient={1,1,1,1};studio.points.resize(3);
    require(OwnedRender::produce_model_preview_lighting(clip,{0,0,5},studio),"embedded studio policy rejected");
    require(studio.points.empty() && studio.world.ambient[0]==.3f &&
        studio.world.sunlight_direction[0]<0 && studio.world.sunlight_direction[1]>0,
        "embedded preview retained full ambient/world point lights");
    draws=0;
    require(produce_model_preview(input,2,
        [](std::size_t,ModelPartFacts&){return true;},[](unsigned){return true;},
        [](std::size_t i,ModelPreviewPart& part){part.ordinal=i;part.hidden=i==1;
            part.material.default_colors=false;
            part.material.lighting=OwnedRender::model_preview_tinted_lighting(placement,0xff808080u);return true;},
        [&](const NativeGeometryView& view,const ModelPreviewPart&){
            require(view.lighting.enabled && !(view.flags&4u),"embedded FIG bypassed studio lighting");
            require(std::abs(view.lighting.material_diffuse[0]-128.f/255)<1e-6f &&
                view.lighting.object_to_world==placement,"preview tint/placement lost");++draws;return true;},wound,result),"lit FIG preview failed");
    require(draws==1,"lit FIG pass count changed");
    const auto priorDomain=model_draw_domain;
    { ModelDrawScope head(ModelDrawDomain::preview);
      require(model_draw_is_preview(),"standalone head studio scope missing");
    }
    require(model_draw_domain==priorDomain,"head scope leaked to item icons/world");
    std::cout<<"PASS embedded x64 studio replaces full ambient/world points, FIG retains tint with one color submission, portrait scope restores\n";
    // The UI caller, not CUnit/model name/id, selects decorative-menu semantics.
    for(const char* menu:{"ESC","options","save-load"}) {
        (void)menu;
        ModelDrawScope character(ModelDrawDomain::preview);
        require(model_draw_is_character_preview(),"character role lost");
        const auto characterLight=studio.world;
        {
            ModelDrawScope decoration(ModelDrawDomain::interface_decoration);
            require(model_draw_is_preview() && !model_draw_is_character_preview(),"decoration inherited character light");
            for(bool claimed:{false,true}) {
                require(!model_draw_uses_retained_world(true,claimed,model_draw_domain),"decoration entered world route");
                require(!model_draw_writes_pick_state(model_draw_domain),"decoration wrote world picking");
            }
            owned::ModelLightingFacts world;world.ambient=0xff102030u;world.sunlight=true;
            world.diffuse={.2f,.4f,.6f};world.direction={0,0,-2};
            std::vector<owned::ModelPointLightFacts> candidates(5);
            for(auto& p:candidates){p.value.radius=1;p.value.color={.1f,.2f,.3f};}
            candidates[0].value.center={10.5f,0,0}; // radius + ten-unit admission margin
            candidates[1].value.center={12,0,0};
            candidates[2].inactive=true;candidates[3].sunlight=true;candidates[4].value.radius=-1;
            unsigned cursor=0;OwnedRender::ModelPreviewLighting menuLight;
            require(OwnedRender::produce_interface_decoration_lighting(world,{0,0,0},
                [&](owned::ModelPointLightFacts& p){if(cursor==candidates.size())return false;p=candidates[cursor++];return true;},menuLight),"menu light rejected");
            auto oldFacts=world;oldFacts.ambient=0xffffffffu;
            NativeWorldLighting expected;
            require(produce_model_lighting(oldFacts,expected),"pre-X64GLTF12 reference");
            require(menuLight.world.ambient==expected.ambient && menuLight.world.sunlight_diffuse==expected.sunlight_diffuse &&
                menuLight.world.sunlight_direction==expected.sunlight_direction,"decorative lighting differs from pre-X64GLTF12 facts");
            require(menuLight.points.size()==1 && menuLight.points[0].source_id==1 &&
                menuLight.points[0].center==candidates[0].value.center,"decorative point admission changed");
            // Default FIG colours must remain prelit/default; no conversion to studio tint.
            draws=0;
            require(produce_model_preview(input,2,
                [](std::size_t,ModelPartFacts&){return true;},[](unsigned){return true;},
                [](std::size_t i,ModelPreviewPart& part){part.ordinal=i;part.hidden=i==1;
                    part.material.default_colors=!model_draw_is_character_preview();part.material.diffuse=0xff123456;return true;},
                [&](const NativeGeometryView& view,const ModelPreviewPart&){
                    require(!view.lighting.enabled && (view.flags&4u) && view.default_diffuse==0xff123456,
                        "menu default colour converted to lit tint");++draws;return true;},wound,result),"decorative FIG preview");
            require(draws==1,"menu added colour/shadow submissions");
            { ModelDrawScope head(ModelDrawDomain::preview);
              require(model_draw_is_character_preview(),"nested head inherited decoration role");
            }
            require(model_draw_domain==ModelDrawDomain::interface_decoration,"head failed to restore menu role");
        }
        require(model_draw_is_character_preview(),"menu failed to restore character role");
        require(OwnedRender::produce_model_preview_lighting(clip,{0,0,5},studio),"character light after menu");
        require(studio.world.ambient==characterLight.ambient && studio.world.sunlight_direction==characterLight.sunlight_direction &&
            studio.world.sunlight_diffuse==characterLight.sunlight_diffuse,"hero studio parameters changed after menu");
    }
    require(model_draw_domain==priorDomain,"menu scope leaked into normal world draw");
    std::cout<<"PASS ESC/options/save-load decoration role, pre-X64GLTF12 light/FIG colours, nested head/hero restoration and world/picking isolation\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n'; return 1;
}
