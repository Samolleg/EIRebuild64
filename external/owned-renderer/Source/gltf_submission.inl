// Included in namespace owned. Uses the same retained pose binding and GPU pool.
namespace {
bool prepare_gltf_pose(const RetainedVisualSample &sample,
                       const std::shared_ptr<const skn::RuntimeAsset> &asset)
{
    auto &prepared = *sample.prepared;
    auto &current = prepared.gpu_pose;
    const auto base = current.binding->model->nodes.size();
    if (prepared.motion_asset != asset) prepared.gltf_playback.reset();
    if (prepared.motion_asset != asset || prepared.motion_source_binding != current.binding)
    {
        prepared.motion_source_binding = current.binding;
        auto model = std::make_shared<ModelCpuAsset>(*current.binding->model);
        for (const auto &joint : asset->asset.joints)
        {
            ModelNodeCpuAsset node;
            node.name = joint.name;
            node.transform_available = false;
            ModelLink link;
            link.child = joint.name;
            link.parent_index = joint.parent < 0 ? UINT_MAX
                                                 : static_cast<unsigned>(base) + joint.parent;
            if (joint.parent >= 0)
                link.parent = asset->asset.joints[joint.parent].name;
            model->nodes.push_back(std::move(node));
            model->links.push_back(std::move(link));
        }
        auto binding = std::make_shared<VisualSkeletonBinding>(*current.binding);
        binding->model = std::move(model);
        binding->tracks.resize(binding->model->nodes.size(), nullptr);
        binding->gltf_animation = asset->gltf_asset->gpu_animation;
        binding->gltf_node_base = static_cast<unsigned>(base);
        prepared.motion_binding = std::move(binding);
        prepared.motion_asset = asset;
        prepared.motion_pose_valid = false;
    }
    current.binding = prepared.motion_binding;
    const auto &animation = *asset->gltf_asset->animation;
    if (!prepared.gltf_playback) prepared.gltf_playback = std::make_shared<gltf::PlaybackState>();
    auto &state = *prepared.gltf_playback;
    gltf::Request request;
    unsigned clip = UINT32_MAX;
    const bool neutral = sample.selection.neutral;
    if (neutral) {
        clip = gltf::named_clip(animation, "idle");
        gltf::animation_require(clip != UINT32_MAX, "neutral pose requires idle");
        request.flags = 0x80180000u; request.frames = 1;
    } else {
        gltf::animation_require(sample.selection.database &&
            sample.selection.ordinal < sample.selection.database->entries.size(), "host animation selection");
        const auto &entry = sample.selection.database->entries[sample.selection.ordinal];
        request.legacy = entry.name; request.flags = entry.flags; request.frames = entry.length;
        request.event_flags = entry.special_flags; request.steps = entry.step_frames;
        request.hit = entry.hit_frame; request.sound = entry.sound_frame; request.hides = entry.hide_frames;
        clip = gltf::resolve_clip(animation, request);
        gltf::validate_event_phase(animation.clips[clip], request);
    }
    const double clock = sample.pose.time / 15.0;
    const double absolute = double(sample.pose.cycle) + sample.pose.phase / request.frames;
    const gltf::ActionToken token{sample.pose.intent_revision, sample.pose.selection,
        neutral ? UINT32_MAX : sample.selection.ordinal};
    const bool seek = state.active && (clock < state.last_clock ||
        (state.events.active && token == state.events.action && absolute < state.events.absolute));
    const auto playback = state.Update(clip, gltf::clip_time(animation.clips[clip], request, sample.pose.phase),
        token, clock, seek, (request.flags & 0x3c0000u) == 0x1c0000u);
    current.baked.reset();
    current.gltf_clip = playback.clip; current.gltf_previous = playback.previous;
    current.gltf_time = playback.time; current.gltf_previous_time = playback.previous_time; current.gltf_blend = playback.blend;
    // These notifications describe the visual timeline only. Existing host ADB
    // callbacks remain the sole source of damage, projectiles, sounds and hides.
    // Private previews never deliver gameplay/cosmetic events to the host.
    if (!sample.completed_pose && !neutral)
        state.events.Advance(animation.clips[clip], token, absolute, gltf::loop_request(request), seek,
            [&](std::uint64_t cycle, const gltf::Event &event) {
                const std::string detail = "id=" + std::to_string(sample.resource.signed_id) +
                    " action=" + std::to_string(token.intent) + " cycle=" + std::to_string(cycle) +
                    " clip=" + animation.clips[clip].name + " event=" + event.id + " name=" + event.name;
                bridge_append_probe_event("gltf_visual_event", detail.c_str());
            });
    current.overrides.reset();
    if (sample.assembly->main_hero_visual && sample.secondary_physics) {
        auto& secondary=prepared.breast_physics;
        if(!secondary || secondary->asset!=asset->gltf_asset) {
            secondary=std::make_shared<gltf::BreastState>();
            if(!secondary->bind(asset->gltf_asset))secondary.reset();
        }
        if(secondary) {
            const auto* ui=sample.preview_physics;
            secondary->update(sample.secondary_clock,playback,ui?ui->simulation:current.root_to_world,
                seek,unsigned(base),sample.secondary_frozen,ui);
            current.overrides=secondary->overrides;
        }
    }
    current.height_scale = 1;
    current.contact_world_height = asset->gltf_asset->radius * 2;
    return true;
}
} // namespace
