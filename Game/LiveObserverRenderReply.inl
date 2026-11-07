bool AppendRender(Job& job,json& array,json value) {
    const auto bytes=value.dump(-1,' ',false,json::error_handler_t::replace).size()+1;
    if(job.offset+bytes>210000){job.response["truncated"]=true;return false;}
    job.offset+=bytes;array.push_back(std::move(value));return true;
}
json VisualAnimation(const owned::VisualObjectPose& pose) {
    const auto& selection=pose.selection;
    json result={{"ordinal",selection.ordinal},{"neutral",selection.neutral},{"phase",pose.phase},{"name",nullptr}};
    if(selection.database&&selection.ordinal<selection.database->entries.size())
        result["name"]=selection.database->entries[selection.ordinal].name;
    return result;
}
json GolemNode(const owned::EmbeddedGolemNode& node,size_t index) {
    return {{"index",index},{"valid",node.valid},{"position",node.position},{"rotation_wxyz",node.rotation}};
}
json CurseActor(const owned::EmbeddedCurseActor& actor) {
    return {{"incarnation",actor.incarnation},{"position",actor.base},{"present",actor.present},{"channel",actor.channel}};
}
bool RenderState(Job& job,Clock::time_point deadline,CWorldServer& world,const json& params) {
    if(!job.phase){
        if(params.contains("details")&&!params["details"].is_boolean())throw std::runtime_error("details must be a boolean");
        job.renderDetails=params.value("details",false);job.renderLimit=GetLimit(params,200,50);
        job.filterName=params.value("name",std::string());job.filterRadius=params.value("radius",2000.0);
        if(job.filterName.size()>256||!std::isfinite(job.filterRadius)||job.filterRadius<0||job.filterRadius>2000)
            throw std::runtime_error("invalid render_state name/radius filter");
    }
    if(job.phase<3){
        json query=params;
        query["radius"]=job.filterRadius;
        // Summary still counts every GPU row, without allocating object JSON.
        if(!Lod(job,deadline,world,query))return false;
        if(!job.response.value("ok",false))return true;
        job.phase=3;return false;
    }
    if(job.phase==3){
        const bool ready=job.response.value("status",std::string())=="ready";
        const auto frame=ready?job.lodSnapshot->camera_frame:0;
        for(const auto& candidate:job.renderFrames)
            if((!frame||candidate->frame==frame)&&candidate->epoch==owned::bridge_current_world_epoch())job.renderFrame=candidate;
        if(!job.renderFrame){job.response=Error("no matching host world frame retained for render_state");return true;}
        const auto& c=*job.renderFrame;const auto& env=c.world.environment;const auto& fog=c.world.fog;
        auto lodObjects=std::move(job.response["objects"]);
        json lod={{"status",job.response["status"]},{"request_id",job.lodRequest},
            {"snapshot_frame",job.response["snapshot_frame"]},{"camera_frame",ready?json(frame):json(nullptr)}};
        if(job.response.contains("reason"))lod["reason"]=job.response["reason"];
        if(job.response.contains("main_camera_max_delta"))lod["main_camera_max_delta"]=job.response["main_camera_max_delta"];
        const bool truncated=c.truncated||job.response.value("truncated",false);
        job.response={{"ok",true},{"status",ready?"ready":"partial"},{"world_epoch",c.epoch},{"world_frame",c.frame},
            {"camera",CameraJson(c.camera)},{"world_to_clip",c.world.camera.world_to_clip},
            {"lod",std::move(lod)},{"details",job.renderDetails},{"limit",job.renderLimit},
            {"name",job.filterName},{"radius",job.filterRadius},{"radius_origin","captured_camera_target_xy"},
            {"truncated",truncated},{"consistency",ready?"host inputs and GPU LOD share world_frame":"host frame; GPU LOD unavailable"},
            {"scope","published inputs and native colour-pass records; not pixel visibility"},
            {"summary",{{"host_world_figures",c.visuals?json(c.worldCount):json(nullptr)},
                {"world_figures",ready?json(job.renderWorldIds.size()):json(nullptr)},
                {"neighbour_figures",ready?json(job.renderNeighbourIds.size()):json(nullptr)},
                {"world_figure_parts",ready?json(job.renderWorldParts):json(nullptr)},
                {"neighbour_figure_parts",ready?json(job.renderNeighbourParts):json(nullptr)},
                {"particle_systems",c.particleCount},{"particle_sources",c.particleSources},{"particle_points",c.particlePoints}}},
            {"weather",{{"valid",env.valid},{"game_hours",env.game_hours},{"hour",env.hour},{"world_time",env.world_time},
                {"cave",env.cave},{"region",env.region},{"current",env.weather_current},{"next",env.weather_next},
                {"progress",env.weather_progress},{"rain_particles",env.rain_particles},{"snow_particles",env.snow_particles},
                {"start",env.weather_start},{"precipitation_strength",env.precipitation_strength},
                {"fog",{{"valid",fog.valid},{"enabled",fog.enabled},{"reason",fog.failure?fog.failure:""},
                    {"start",fog.fog.start},{"end",fog.fog.end},{"scale",fog.fog.scale},
                    {"projection",json::array({fog.fog.projection_33,fog.fog.projection_34,fog.fog.projection_43})}}}}},
            {"render_failures",{{"all_renderer_failures",nullptr},{"reason","no_existing_per_frame_total"},
                {"owned_frame_observed",c.visualResult},{"owned_frame_accepted",c.visualAccepted},
                {"owned_drawn",c.drawn},{"owned_omitted",c.omitted}}},
            {"world_objects",json::array()},{"lod_objects",job.renderDetails?std::move(lodObjects):json::array()},
            {"particles",json::array()},{"active_templates",json::array()},{"scene_actions",json::array()},
            {"plugin",{{"arena",{{"published",c.arenaPublished},{"attempted",c.arenaPresent}}},
                {"golem",{{"published",c.golemPublished},{"attempted",c.golemPresent}}},
                {"curse",{{"published",c.cursePublished},{"attempted",c.cursePresent}}}}},{"terrain2",{{"observed",false}}}};
        for(const auto& entry:c.templates)job.response["active_templates"].push_back({{"template_id",entry.first},{"systems",entry.second}});
        job.phase=4;job.bucket=0;job.item=0;job.offset=job.response.dump().size();return false;
    }
    if(!SameWorld(job,world)||job.renderFrame->epoch!=owned::bridge_current_world_epoch()){
        job.response=Error("world changed during render_state serialization");return true;
    }
    const auto& c=*job.renderFrame;
    while(Clock::now()<deadline){
        if(job.bucket==0){
            if(job.item<c.objects.size()){
                const auto& v=c.objects[job.item++];const auto animation=VisualAnimation(v.pose);
                AppendRender(job,job.response["world_objects"],{{"id",v.id},{"generation",v.generation},{"model",v.model},
                    {"status",v.status},{"position",v.pose.position},{"rotation_wxyz",v.pose.quaternion},{"shape",v.pose.shape},
                    {"object_flags",v.pose.object_flags},{"unit",v.values.unit},{"pose",v.values.pose},
                    {"dead",v.values.dead},{"invisible",v.values.invisible},{"fade_alpha",v.values.fade_alpha},{"animation",animation}});
                if(v.values.unit&&!v.pose.selection.neutral)
                    AppendRender(job,job.response["scene_actions"],{{"id",v.id},{"animation",animation},{"pose",v.values.pose},
                        {"scope","published scene animation; no command history"}});
                continue;
            }
        }else if(job.bucket==1){
            if(job.item<c.particles.size()){
                const auto& p=c.particles[job.item++];json sources=json::array();
                for(int i=0;i<p.sourceCount&&i<int(p.sources.size());++i){const auto& s=p.sources[i];
                    sources.push_back({{"offset",Point(s.respectCP)},{"speed",Point(s.vSpeed)},
                        {"reserved",Point(s.reserved)},{"reserved2",Point(s.reserved2)},{"phase",s.nPhase},{"lifetime",s.nLifeTime}});}
                AppendRender(job,job.response["particles"],{{"id",p.id},{"name",p.name},{"template_id",p.type},
                    {"carrier_id",p.carrier},{"position",p.position},{"submitted",p.submitted},{"points",p.points},
                    {"power",p.power},{"power2",p.power2},{"param1",p.param1},{"param2",p.param2},{"scale",p.scale},
                    {"direction",p.direction},{"source_count",p.sourceCount},{"sources_truncated",p.sourcesTruncated},
                    {"stream",{{"generating",p.generating},{"minimum_tries",p.minimumTries},
                        {"probability_percent",p.probability},{"maximum_particles",p.maximum},{"length_per_try",p.lengthPerTry}}},
                    {"sources",std::move(sources)}});continue;
            }
        }else if(job.bucket==2){
            if(c.arenaPresent){const auto& a=c.arena;auto& target=job.response["plugin"]["arena"];
                target={{"published",c.arenaPublished},{"attempted",true},{"hresult",int(c.arenaResult)},{"world_epoch",a.world_epoch},{"boss_id",a.boss_id},
                    {"tick",a.tick},{"base",a.base},{"camera",a.camera},{"dome",a.dome},{"flow",a.flow},
                    {"dome_time",a.dome_time},{"flow_time",a.flow_time},{"flow_fraction",a.flow_fraction},
                    {"phase",a.phase},{"wave",a.wave},{"wave_radius",a.wave_radius},{"towers",json::array()}};
                for(const auto& t:a.towers)target["towers"].push_back({{"crystal",t.crystal},{"present",t.present},{"broken",t.broken},{"break_time",t.break_time}});
                job.offset+=target.dump().size();
            }
        }else if(job.bucket==3){
            if(c.golemPresent){const auto& g=c.golem;auto& t=job.response["plugin"]["golem"];
                t={{"published",c.golemPublished},{"attempted",true},{"hresult",int(c.golemResult)},{"world_epoch",g.world_epoch},{"incarnation",g.incarnation},
                    {"boss_id",g.boss_id},{"tick",g.tick},{"stage",g.stage},{"phase",g.phase},{"active",g.active},
                    {"walls",g.walls},{"dead",g.dead},{"hard",g.hard},{"throw_count",g.throw_count},{"throw_launched",g.throw_launched},
                    {"timer",g.timer},{"target",json::array({g.target_x,g.target_y})},{"beam_end",json::array({g.beam_ex,g.beam_ey})},
                    {"aim",json::array({g.aim_x,g.aim_y})},{"ring_radius",g.ring_radius},{"base",g.base},{"camera",g.camera},
                    {"casts",g.casts},{"release",g.release},{"ready",g.ready},{"reflection_used",g.reflection_used},
                    {"slam_born",g.slam_born},{"slam",json::array({g.slam_x,g.slam_y,g.slam_angle})},{"grow_born",g.grow_born},
                    {"obelisk",json::array({g.obelisk_x,g.obelisk_y})},{"pulse_born",g.pulse_born},{"impact_born",g.impact_born},{"role",g.role},
                    {"detach_born",g.detach_born},{"detach_from",g.detach_from},{"detach_to",g.detach_to},
                    {"death_born",g.death_born},{"death_base",g.death_base},{"nodes",json::array()},
                    {"fields",json::array()},{"shots",json::array()},{"reflections",json::array()},
                    {"detached_pose",json::array()},{"death_pose",json::array()},{"ruby",json::array()}};
                for(size_t i=0;i<g.nodes.size();++i)t["nodes"].push_back(GolemNode(g.nodes[i],i));
                for(const auto& ruby:g.ruby)t["ruby"].push_back({{"present",ruby.present},{"dead",ruby.dead},{"base",ruby.base},{"nodes",json::array()}});
                job.offset+=t.dump().size();
            }
        }else if(job.bucket==4&&c.golemPresent&&job.renderDetails){
            const auto& g=c.golem;auto& t=job.response["plugin"]["golem"];
            if(job.item<g.fields.size()){
                const auto i=job.item++;const auto& f=g.fields[i];if(f.end>0)
                    AppendRender(job,t["fields"],{{"index",i},{"position",json::array({f.x,f.y,f.z})},{"radius",f.r},{"born",f.born},{"end",f.end},{"style",f.style}});
                continue;
            }
        }else if(job.bucket==5&&c.golemPresent&&job.renderDetails){
            const auto& g=c.golem;auto& t=job.response["plugin"]["golem"];
            if(job.item<g.shots.size()){
                const auto i=job.item++;const auto& s=g.shots[i];if(s.end>0)
                    AppendRender(job,t["shots"],{{"index",i},{"start",json::array({s.x,s.y,s.z})},{"end_position",json::array({s.ex,s.ey,s.ez})},
                        {"born",s.born},{"end",s.end},{"hand",s.hand},{"stage",s.stage},{"rotation_wxyz",s.q}});
                continue;
            }
        }else if(job.bucket==6&&c.golemPresent&&job.renderDetails){
            const auto& g=c.golem;auto& t=job.response["plugin"]["golem"];
            if(job.item<108){
                const auto i=job.item++;
                if(i<30){if(g.detached_pose[i].valid)AppendRender(job,t["detached_pose"],GolemNode(g.detached_pose[i],i));}
                else if(i<60){if(g.death_pose[i-30].valid)AppendRender(job,t["death_pose"],GolemNode(g.death_pose[i-30],i-30));}
                else {const auto actor=(i-60)/24,index=(i-60)%24;
                    if(g.ruby[actor].nodes[index].valid)AppendRender(job,t["ruby"][actor]["nodes"],GolemNode(g.ruby[actor].nodes[index],index));}
                continue;
            }
        }else if(job.bucket==7&&c.golemPresent&&job.renderDetails){
            const auto& g=c.golem;auto& t=job.response["plugin"]["golem"];
            if(job.item<size_t((std::min)(g.reflection_used,64))){
                const auto i=job.item++;const auto& r=g.reflections[i];if(r.end>0)
                    AppendRender(job,t["reflections"],{{"index",i},{"born",r.born},{"end",r.end},{"kind",r.kind},{"start",r.start},{"target",r.target}});
                continue;
            }
        }else if(job.bucket==8){
            if(c.cursePresent){const auto& q=c.curse;auto& t=job.response["plugin"]["curse"];
                t={{"published",c.cursePublished},{"attempted",true},{"hresult",int(c.curseResult)},{"world_epoch",q.world_epoch},{"incarnation",q.incarnation},
                    {"boss_id",q.boss_id},{"tick",q.tick},{"threat",q.threat},{"mage_mode",q.mage_mode},{"active",q.active},
                    {"threat_born",q.threat_born},{"position",json::array({q.x,q.y})},{"pad",json::array({q.pad_x,q.pad_y})},
                    {"lance",json::array({q.lance_ax,q.lance_ay,q.lance_bx,q.lance_by})},{"impact_at",q.impact_at},
                    {"impact",json::array({q.impact_x,q.impact_y})},{"ward_at",q.ward_at},{"camera",q.camera},
                    {"boss",CurseActor(q.boss)},{"mage",CurseActor(q.mage)},{"ward",CurseActor(q.ward)},{"enemies",json::array()}};
                for(const auto& actor:q.enemies)t["enemies"].push_back(CurseActor(actor));job.offset+=t.dump().size();
            }
        }else if(job.bucket==9){
            const owned::NativeTerrain2Observation empty;
            const auto& native=job.lodSnapshot?job.lodSnapshot->terrain2:empty;
            if(native.observed){auto& t=job.response["terrain2"];
                t={{"observed",true},{"enabled",native.enabled},{"table_ready",native.table_ready},{"table_maps",native.table_maps},
                    {"pools",json::array()},{"maps",json::array()}};
                for(size_t i=0;i<native.pools.size();++i){const auto& p=native.pools[i];t["pools"].push_back({{"index",i},
                    {"width",p.width},{"height",p.height},{"mips",p.mips},{"slices",p.slices},{"atlas_ready",p.atlas},{"heights_ready",p.heights}});}
                for(const auto& m:native.maps)t["maps"].push_back({{"contact_map",m.contact_map},{"bake_cells",m.pending_cells},
                    {"baked_cells",m.baked_cells},{"height_ready",m.ready},{"dirty",m.dirty},{"failed",m.failed}});
            }else job.response["terrain2"]["reason"]="no_admitted_native_capture";
        }else if(job.bucket>=10)return true;
        ++job.bucket;job.item=0;
    }
    return false;
}
