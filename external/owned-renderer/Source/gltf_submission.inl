// Included in namespace owned. Uses the same retained pose binding and GPU pool.
namespace {
bool prepare_gltf_pose(const RetainedVisualSample &sample,
                       const std::shared_ptr<const skn::RuntimeAsset> &asset)
{
    auto &prepared = *sample.prepared;
    auto &current = prepared.gpu_pose;
    const auto base = current.binding->model->nodes.size();
    if (prepared.motion_asset != asset) prepared.gltf_playback.reset();
    prepare_physique(sample,*asset->gltf_asset);
    const bool shapeChanged=prepared.motion_asset!=asset || !prepared.motion_binding ||
        prepared.motion_binding->physique!=prepared.motion_physique;
    if (shapeChanged || prepared.motion_source_binding != current.binding)
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
        binding->physique=prepared.motion_physique;
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
    if (sample.riding) {
        const char* name="ride_seated";
        switch(sample.rideTransition) {
            case 1: name="ride_mount_left"; break;
            case -1: name="ride_mount_right"; break;
            case 2: name="ride_dismount_left"; break;
            case -2: name="ride_dismount_right"; break;
        }
        clip = gltf::named_clip(animation,name);
        gltf::animation_require(clip!=UINT32_MAX,"riding pose");
        request.flags=0x80180000u; request.frames=1;
    } else if (neutral) {
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
    const bool calmRider=sample.riding && !sample.rideTransition && sample.riding_calm && !sample.values.dead;
    const auto idle=state.riding_idle.Update(calmRider,clock,true);
    float idleTime=-1;
    if(idle.variant) {
        static constexpr const char* riderClips[]{"ride_idle_look_left","ride_idle_look_right","ride_idle_settle"};
        const auto name=riderClips[idle.variant-1];
        const auto selected=gltf::named_clip(animation,name);
        if(selected!=UINT32_MAX) {clip=selected;idleTime=(std::min)(idle.time,animation.clips[clip].duration);}
    }
    const double absolute = double(sample.pose.cycle) + sample.pose.phase / request.frames;
    const gltf::ActionToken token{sample.pose.intent_revision, sample.pose.selection,
        neutral ? UINT32_MAX : sample.selection.ordinal};
    const bool seek = state.active && (clock < state.last_clock ||
        (state.events.active && token == state.events.action && absolute < state.events.absolute));
    if(!state.active || seek || !(token==state.action) || clip!=state.value.clip) {
        prepared.previous_attachment_action=std::move(prepared.attachment_action);
        prepared.attachment_action=request.legacy;
    }
    const float localTime=sample.rideTransition ? animation.clips[clip].duration*sample.rideProgress :
        idleTime>=0 ? idleTime : gltf::clip_time(animation.clips[clip],request,sample.pose.phase);
    bool landingBlend=false;
    if (!sample.riding && state.active && state.value.clip<animation.clips.size()) {
        const auto& name=animation.clips[state.value.clip].name;
        if (name=="ride_dismount_left" || name=="ride_dismount_right") {
            const auto grounded=gltf::named_clip(animation,
                name=="ride_dismount_left"?"ride_grounded_left":"ride_grounded_right");
            gltf::animation_require(grounded!=UINT32_MAX,"missing grounded riding pose");
            // The actor now owns translation and yaw. Keep the same final
            // joint pose in actor space for the ordinary locomotion crossfade.
            state.value.clip=grounded; state.value.time=0; state.value.blend=1;
            landingBlend=true;
        }
    }
    const auto previousName=state.active && state.value.clip<animation.clips.size() ?
        animation.clips[state.value.clip].name : std::string{};
    const bool leavingTransfer=(previousName.rfind("ride_mount_",0)==0 ||
        previousName.rfind("ride_dismount_",0)==0) && !landingBlend;
    const bool riderIdleTransition=sample.riding && !sample.rideTransition &&
        (previousName.rfind("ride_idle_",0)==0 || animation.clips[clip].name.rfind("ride_idle_",0)==0);
    const auto playback = state.Update(clip, localTime,
        token, clock, seek && !landingBlend, sample.rideTransition || leavingTransfer || (request.flags & 0x3c0000u) == 0x1c0000u,
        riderIdleTransition?.30f:.12f);
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
    if(prepared.motion_physique){
        const std::array<float,5> key{float(playback.clip),float(playback.previous),playback.time,playback.previous_time,playback.blend};
        if(!prepared.sole_valid || prepared.sole_playback!=key){
            prepared.sole_offset={};
            const auto& body=*asset->gltf_asset;
            for(unsigned i=0;i<2;++i){
                const auto& chain=body.attachment_chain(body.geometry.joints[body.sole_nodes[i]].name);
                gltf::Affine neutral{1,0,0,0,0,0,-1,0,0,1,0,0,0,0,0,1},shaped=neutral;
                for(const auto& joint:chain){
                    const auto local=gltf::socket_local(animation,joint,playback);
                    neutral=gltf::multiply(neutral,local);
                    shaped=gltf::multiply(shaped,prepared.motion_physique->apply(joint.node,local));
                }
                const auto& point=body.sole_points[i];
                for(unsigned r=0;r<3;++r){
                    float delta=neutral[r*4+3]-shaped[r*4+3];
                    for(unsigned c=0;c<3;++c)delta+=(neutral[r*4+c]-shaped[r*4+c])*point[c];
                    prepared.sole_offset[r]+=.5f*delta;
                }
            }
            prepared.sole_playback=key;prepared.sole_valid=true;
        }
        current.physique_sole=prepared.sole_offset;
        current.root_to_world=current.placed_with_sole(current.root_to_world);
    }
    current.overrides.reset();
    current.height_scale = 1;
    const auto& outfit=prepare_gltf_equipment(sample,*asset->gltf_asset);
    current.attachment_nodes=outfit.mechanism_nodes;
    if(outfit.mechanism_nodes || outfit.support_arm) {
        const auto& body=*asset->gltf_asset;
        using Overrides=std::vector<NativeWorldGeometry::Pose::Override>;
        auto rows=std::make_shared<Overrides>();
        rows->reserve(outfit.mechanism_nodes+(outfit.support_arm?outfit.support_arm->nodes.size():0));
        const auto start=unsigned(base+body.geometry.joints.size())+body.gpu_animation->morphs;
        const gltf::Affine basis{1,0,0,0,0,0,-1,0,0,1,0,0,0,0,0,1};
        const auto root=gltf::multiply(current.root_to_world,basis);
        const gltf::WeaponBowConstraint* bow=nullptr;
        for(const auto& item:outfit.items)if(item.asset->bow_constraint) {
            // Ambiguous pairings leave both items on their ordinary tracks.
            if(bow){bow=nullptr;break;}
            bow=item.asset->bow_constraint.get();
        }
        float draw=0;
        if(bow && !neutral && (request.flags&0x20u) && (request.event_flags&1u))
            draw=gltf::weapon_draw_weight(sample.pose.phase,request.frames,request.hides[0],request.hides[1])*playback.blend;
        gltf::Point3 drawing{},grip{};
        if(draw>0) {
            const auto hand=[&](const std::array<std::string,4>& markers) {
                gltf::Point3 value{};
                for(const auto& marker:markers) {
                    const auto pose=gltf::socket_pose(animation,body.attachment_chain(marker),playback,root,
                                                       prepared.motion_physique.get());
                    for(unsigned r=0;r<3;++r)value[r]+=pose[r*4+3]*.25f;
                }
                return value;
            };
            drawing=hand(bow->drawing_bones);grip=hand(bow->grip_bones);
        }
        for(const auto& item:outfit.items) {
            if(item.asset->mechanism.empty() && !item.asset->projectile_aim)continue;
            auto socket=gltf::socket_pose(animation,body.attachment_chain(item.socket),playback,root,
                                          prepared.motion_physique.get());
            for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)socket[r*4+3]+=socket[r*4+c]*item.offset[c];
            if(item.asset->projectile_aim) {
                auto value=socket;
                if(bow && draw>0)gltf::weapon_projectile(value,socket,*item.asset->projectile_aim,drawing,grip,draw);
                NativeWorldGeometry::Pose::Override out{};
                out.node=start+item.mechanism_offset;
                for(unsigned r=0;r<3;++r)std::copy_n(value.data()+r*4,4,out.value.rows[r].begin());
                rows->push_back(out);
                continue;
            }
            const auto& mechanism=*item.asset->mechanism_animation;
            const auto local=[&](unsigned c,float t,const std::string& action) {
                const auto m=c==UINT32_MAX?UINT32_MAX:gltf::named_clip(mechanism,action);
                const float d=c==UINT32_MAX?0:animation.clips.at(c).duration;
                return std::make_pair(m,m!=UINT32_MAX && d>0?t/d*mechanism.clips.at(m).duration:0.f);
            };
            const auto now=local(playback.clip,playback.time,prepared.attachment_action);
            const auto before=local(playback.previous,playback.previous_time,prepared.previous_attachment_action);
            const gltf::Playback motion{now.first,before.first,now.second,before.second,playback.blend};
            std::array<gltf::Affine,16> values{};
            for(const auto& joint:item.asset->mechanism) {
                values.at(joint.node)=gltf::multiply(socket,gltf::socket_local(mechanism,joint,motion));
            }
            if(item.asset->bow_constraint && item.asset->bow_constraint.get()==bow && draw>0) {
                gltf::weapon_bow(values,*bow,drawing,draw);
            }
            for(const auto& joint:item.asset->mechanism) {
                const auto& value=values.at(joint.node);
                NativeWorldGeometry::Pose::Override out{};
                out.node=start+item.mechanism_offset+joint.node;
                for(unsigned r=0;r<3;++r)std::copy_n(value.data()+r*4,4,out.value.rows[r].begin());
                rows->push_back(out);
            }
        }
        if(outfit.support_arm && !sample.visibility.hide_weapons) {
            const gltf::EquipmentAsset* held=nullptr;
            for(const auto& item:outfit.items)if(item.asset->two_hand_support && item.slot=="hand_r") {
                if(held){held=nullptr;break;}
                held=&item;
            }
            if(held) {
                const auto& support=*held->asset->two_hand_support;
                const auto weight=[&](const std::string& action,unsigned clip,float time) {
                    return clip==UINT32_MAX ? 0.f : gltf::weapon_support_weight(
                        support,action,time,animation.clips.at(clip).duration);
                };
                const float now=weight(prepared.attachment_action,playback.clip,playback.time);
                const float before=weight(prepared.previous_attachment_action,playback.previous,playback.previous_time);
                const float strength=before+(now-before)*playback.blend;
                if(strength>0) {
                    const auto& plan=*outfit.support_arm;
                    std::array<gltf::Affine,256> arm{};
                    for(unsigned i=0;i<plan.nodes.size();++i) {
                        const auto& node=plan.nodes[i];
                        arm[i]=node.parent<0 ? gltf::socket_pose(animation,
                                                                 body.attachment_chain(body.geometry.joints[node.joint.node].name),playback,
                                                                 root,prepared.motion_physique.get()) :
                            gltf::multiply(arm[unsigned(node.parent)],gltf::socket_local(animation,node.joint,playback,
                                                                                        prepared.motion_physique.get()));
                    }
                    const auto origin=[](const gltf::Affine& a){return gltf::Point3{a[3],a[7],a[11]};};
                    const auto shoulder=origin(arm[plan.upper]),elbow=origin(arm[plan.forearm]),wrist=origin(arm[plan.hand]);
                    gltf::Point3 palm{};
                    for(unsigned marker:plan.markers)for(unsigned c=0;c<3;++c)palm[c]+=.25f*arm[marker][c*4+3];
                    auto weapon=gltf::socket_pose(animation,body.attachment_chain(held->socket),playback,root,
                                                  prepared.motion_physique.get());
                    for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)weapon[r*4+3]+=weapon[r*4+c]*held->offset[c];
                    if(support.mirror) {
                        bool primary_changed=false;
                        if(gltf::weapon_mirrored_grip(arm,primary_changed,plan,support,weapon,strength)) {
                            for(unsigned i=0;i<plan.nodes.size();++i)
                              if(plan.nodes[i].output && (!plan.nodes[i].primary || primary_changed)) {
                                NativeWorldGeometry::Pose::Override out{};
                                out.node=unsigned(base)+plan.nodes[i].joint.node;
                                for(unsigned r=0;r<3;++r)std::copy_n(arm[i].data()+r*4,4,out.value.rows[r].begin());
                                rows->push_back(out);
                            }
                        }
                    } else {
                    gltf::WeaponArmSolution solution;
                    if(gltf::weapon_support_ik(solution,weapon,support,shoulder,elbow,wrist,palm,strength)) {
                        for(unsigned i=0;i<plan.nodes.size();++i)if(plan.nodes[i].output) {
                            const bool forearm=plan.nodes[i].forearm;
                            const auto value=gltf::weapon_rotate_around(arm[i],
                                forearm?solution.forearm_rotation:solution.upper_rotation,
                                forearm?solution.elbow:solution.shoulder,
                                forearm?solution.new_elbow:solution.shoulder);
                            NativeWorldGeometry::Pose::Override out{};
                            out.node=unsigned(base)+plan.nodes[i].joint.node;
                            for(unsigned r=0;r<3;++r)std::copy_n(value.data()+r*4,4,out.value.rows[r].begin());
                            rows->push_back(out);
                        }
                    }
                    }
                }
            }
        }
        if(!rows->empty()) {
            std::stable_sort(rows->begin(),rows->end(),[](const auto& a,const auto& b){return a.node<b.node;});
            current.overrides=std::move(rows);
        }
    }
    current.contact_world_height = asset->gltf_asset->radius * 2;
    return true;
}
} // namespace
