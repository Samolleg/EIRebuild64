#include "model_preview_producer.h"
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
    auto gltf = std::make_shared<gltf::Asset>(); gltf->radius = 1;
    asset->gltf_asset = gltf; asset->asset.materials.resize(3);
    // Store BLEND first: the producer must still submit OPAQUE/MASK before it.
    asset->asset.materials[0].alpha = 2; asset->asset.materials[0].base[3] = .5f;
    asset->asset.materials[1].alpha = 0; asset->asset.materials[1].base[3] = .2f;
    asset->asset.materials[2].alpha = 1; asset->asset.materials[2].base[3] = .75f;
    sample.prepared->motion_asset = asset; sample.prepared->skn_selected = true;
    sample.prepared->merged_groups.resize(3);
    for (unsigned i = 0; i < 3; ++i) {
        auto& group = sample.prepared->merged_groups[i]; group.parts = {i}; group.skn_material = i;
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
        const auto& material = asset->asset.materials[part.ordinal];
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
    return 0;
} catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n'; return 1;
}
