#include "renderer.h"
#include "neighbour_maps.h"
#include "owned_resource_runtime.h"
#include "retained_static_scene.h"
#include "native_instance_flags.h"
#include "owned_terrain_front_end.h"
#include <cmath>
#include <cstdio>
#include <cfloat>
#include <climits>

namespace owned {
FixedState neighbour_submission_state(const FixedState& frame,SemanticKind kind,
    NativeOrdering ordering) noexcept {
    FixedState state=frame;
    const bool ordered=ordering==NativeOrdering::preserve,figure=kind==SemanticKind::figure;
    state.render[D3DRENDERSTATE_ZENABLE]=D3DZB_TRUE;
    state.render[D3DRENDERSTATE_ZWRITEENABLE]=ordered?FALSE:TRUE;
    state.render[D3DRENDERSTATE_ZFUNC]=D3DCMP_LESSEQUAL;
    state.render[D3DRENDERSTATE_ALPHABLENDENABLE]=ordered;
    state.render[D3DRENDERSTATE_SRCBLEND]=D3DBLEND_SRCALPHA;
    state.render[D3DRENDERSTATE_DESTBLEND]=D3DBLEND_INVSRCALPHA;
    state.render[D3DRENDERSTATE_ALPHATESTENABLE]=figure;
    state.render[D3DRENDERSTATE_ALPHAFUNC]=D3DCMP_GREATER;
    // The active map's retained statics cut out at 200; matching it keeps
    // neighbour foliage cutouts as thin as on its own map.
    state.render[D3DRENDERSTATE_ALPHAREF]=figure?200:0;
    // Terrain keeps the game's world winding: the engine draws the world,
    // terrain included, under D3DCULL_CW (the active terrain replays that
    // captured state). D3DCULL_CCW rejected the front faces and left only
    // the far-side back faces, so neighbour hills rendered inside out.
    state.render[D3DRENDERSTATE_CULLMODE]=figure?D3DCULL_NONE:D3DCULL_CW;
    state.render[D3DRENDERSTATE_SPECULARENABLE]=FALSE;
    state.stage[0][D3DTSS_COLOROP]=D3DTOP_MODULATE;
    state.stage[0][D3DTSS_COLORARG1]=D3DTA_TEXTURE;state.stage[0][D3DTSS_COLORARG2]=D3DTA_DIFFUSE;
    state.stage[0][D3DTSS_ALPHAOP]=D3DTOP_MODULATE;
    state.stage[0][D3DTSS_ALPHAARG1]=D3DTA_TEXTURE;state.stage[0][D3DTSS_ALPHAARG2]=D3DTA_DIFFUSE;
    // Terrain atlas pages clamp; decorations wrap, as the active map's
    // retained statics do: stone and trunk UVs run to about 1.8 (nast*,
    // nafltr*), and clamping smeared the edge texels into stripes.
    const DWORD address=figure?D3DTADDRESS_WRAP:D3DTADDRESS_CLAMP;
    state.stage[0][D3DTSS_ADDRESSU]=address;state.stage[0][D3DTSS_ADDRESSV]=address;
    state.stage[0][D3DTSS_MINFILTER]=D3DTFN_LINEAR;state.stage[0][D3DTSS_MAGFILTER]=D3DTFG_LINEAR;
    state.stage[0][D3DTSS_MIPFILTER]=D3DTFP_LINEAR;
    return state;
}

void Renderer::release_neighbour_maps() noexcept {
    for(const auto& [key,draws]:neighbours_.opaque.objects) {
        (void)key;for(const auto& draw:draws)release_native_queued_draw(draw.record,false);
    }
    for(const auto& draw:neighbours_.ordered)release_native_queued_draw(draw.record,false);
    neighbours_={};
    dedicated_geometry_stale_=true; // their prebuilt pages go once unreferenced
    neighbour_border_fog_publish({});
    gc_neighbours_.clear(); // their ground cover goes with them
    // Their impostors: the atlas keeps its size and is baked over again.
    impostors_.slices.clear();impostors_.count=0;
}

HRESULT Renderer::render_neighbour_maps(const FixedState& frame,
    const std::array<float,16>& world_to_clip,bool ordered) noexcept {
    const auto* option=debug_overlay_.config().find("neighbour_maps");
    const bool enabled=option && option->boolean_value;
    neighbour_maps_enable(enabled);
    if(!enabled) {
        if(neighbours_.owner){neighbour_maps_stop();release_neighbour_maps();}
        return S_OK;
    }
    const auto source=retained_static_terrain_source_snapshot();
    if(!source.map || !source.terrain) {
        if(neighbours_.owner)release_neighbour_maps();
        return S_OK;
    }
    const auto epoch=source.map->generation;
    // Fully fogged neighbour content is never uploaded, so a new range
    // restreams the neighbours. The draw distance caps the range.
    const double fog_range=neighbour_fog_reach(configured_floating_point(debug_overlay_.config(),"neighbour_fog_range",1.f),
        configured_integer(debug_overlay_.config(),"draw_distance",300));
    try {
        if(neighbours_.epoch!=epoch || neighbours_.renderer_generation!=native_static_generation_ ||
            neighbours_.fog_range!=fog_range) {
            if(neighbours_.owner && neighbours_.fog_range!=fog_range)neighbour_maps_stop();
            release_neighbour_maps();
            neighbours_.epoch=epoch;neighbours_.renderer_generation=native_static_generation_;
            neighbours_.fog_range=fog_range;
            neighbours_.owner=std::make_shared<const int>(0);neighbours_.weak_owner=neighbours_.owner;
            ResourceStoreConfig mounts;
            if(owned_resource_runtime_mounts(mounts))neighbour_maps_begin(source.map->terrain_resource,epoch,mounts);
        }
        if(!ordered) {
            // One neighbour per frame in play; while the loading screen is
            // held, every ready one. Their geometry arrives in prebuilt pages
            // (no render-thread upload burst), so adoption is cheap.
            for(unsigned taken=0;taken==0||load_reveal_holding_;++taken) {
            auto upload=neighbour_maps_take(epoch);
            if(!upload)break;
            {
                const auto old_mesh=native_mesh_resident_bytes_;
                const auto old_texture=native_owned_texture_bytes(false)+native_owned_texture_bytes(true);
                native_texture_share_floor_=++native_owned_texture_serial_;
                native_texture_shared_=native_texture_shared_bytes_=0;
                const auto first_ordered=neighbours_.ordered.size();
                const auto first_part=neighbours_.next_part;
                neighbours_.peak_decoded_bytes=(std::max)(neighbours_.peak_decoded_bytes,upload->decoded_bytes);
                struct Submission {Renderer* self;const FixedState* frame;const char* failure;};
                Submission submission{this,&frame,nullptr};
                const auto submit=[](void* opaque,const NativeGeometryView& input,NativeGeometryResult& result)->bool {
                    auto& context=*static_cast<Submission*>(opaque);auto& self=*context.self;
                    const FixedState state=neighbour_submission_state(*context.frame,input.semantic_kind,input.ordering);
                    const auto first=self.native_pending_draws_.size();
                    const HRESULT hr=self.queue_native_terrain(input,state,nullptr,&result);
                    if(FAILED(hr)) {
                        context.failure=result.failure_operation?result.failure_operation:"queue_native_terrain";
                        return false;
                    }
                    if(input.ordering!=NativeOrdering::preserve) {
                        bool registered=false;
                        if(FAILED(self.adopt_native_static_draws(self.neighbours_.opaque,input.prepared_identity.part_index,first,registered)) ||
                            !registered) {context.failure="adopt_native_static_draws";return false;}
                    } else {
                        self.neighbours_.ordered.reserve(self.neighbours_.ordered.size()+self.native_pending_draws_.size()-first);
                        for(std::size_t i=first;i<self.native_pending_draws_.size();++i) {
                            const auto draw=self.native_pending_draws_[i];
                            --self.native_instance_records_[draw.record].command.mesh->pending_references;
                            self.neighbours_.ordered.push_back(draw);
                        }
                        self.native_pending_draws_.resize(first);
                    }
                    return true;
                };
                // Each piece carries its raw edges: its band gives way in the
                // shaders wherever another rendered piece adjoins it
                // (neighbour_piece_fog_density), wholly or partly.
                const NeighbourBounds active{0,0,source.terrain->sector_count_x*32.0,source.terrain->sector_count_y*32.0};
                const NeighbourBounds own{upload->selection.x,upload->selection.y,
                    upload->selection.x+upload->selection.placement.width,upload->selection.y+upload->selection.placement.height};
                upload->fog_edges=own;
                // Cull against the coverage every selected piece gives, in world space.
                std::array<float,16> map_placement{};
                const bool placed=bridge_neighbour_world_transform(map_placement);
                if(ambient_terrain_state_.fog_enabled)upload->fog_cull=neighbour_fog_translated(neighbour_fog_field(active,
                    neighbour_fog_contacts(active,upload->selected),fog_range),map_placement[3],map_placement[7]);
                // The stream welded its shared edges and set its outside liquid
                // (neighbour_maps.cpp), in layout order, before handing it over.
                // Pages the stream built: registering a draw finds its mesh
                // there instead of decoding and uploading on this thread.
                adopt_prebuilt_pages(*upload);
                adopt_prebuilt_textures(*upload);
                // Crown impostors of its cut-out figures, before they register.
                const UINT impostors_before=impostors_.baked;const double impostor_ms_before=impostors_.bake_ms;
                bake_neighbour_impostors(*upload);
                // Prebuilt meshes no draw registered (fog culled, a rejected piece)
                // are released before the upload dies, also on an exception.
                struct UnusedPrebuilt {Renderer& self;const NeighbourMapUpload* upload;
                    void release() noexcept {if(upload)self.release_unused_prebuilt(*upload);upload=nullptr;}
                    ~UnusedPrebuilt(){release();}} unused_prebuilt{*this,upload.get()};
                const char* step=upload->welded?nullptr:"terrain.not_welded";
                bool accepted=upload->welded&&bridge_upload_neighbour_terrain(*upload,neighbours_.weak_owner,world_to_clip,
                    submit,&submission,neighbours_.next_part,step);
                if(accepted && !placed){accepted=false;step="decoration.world_transform";}
                if(accepted)for(std::size_t candidate_index=0;candidate_index<upload->decorations.size();++candidate_index){
                    const auto& candidate=upload->decorations[candidate_index];
                    float contact_lo=FLT_MAX,contact_hi=-FLT_MAX;
                    for(const auto& node:candidate->nodes){
                        auto local=node.object_to_world;local[3]+=float(upload->selection.x);local[7]+=float(upload->selection.y);
                        const auto z=contact_world_vertical_bounds(node.bounds.center,node.bounds.minimum,node.bounds.maximum,
                            retained_multiply_matrix(map_placement,local));
                        contact_lo=(std::min)(contact_lo,z[0]);contact_hi=(std::max)(contact_hi,z[1]);
                    }
                    for(std::size_t node_index=0;node_index<candidate->nodes.size();++node_index) {
                    const auto& node=candidate->nodes[node_index];
                    const auto& figure=*node.figure;
                    // The figure's mesh at this decoration's shape, shared by every
                    // node drawing it at that shape, in object space.
                    NativeGeometryView view=neighbour_figure_view(figure,node.positions.data(),candidate->source->shape);
                    if(const auto found=upload->prebuilt_figures.find(neighbour_figure_shape(node.figure,candidate->source->shape));
                       found!=upload->prebuilt_figures.end())view.prebuilt_mesh=&found->second;
                    view.world.impostor=neighbour_impostor_slice(node.figure,node.texture_identity);
                    view.prepared_identity={INT_MIN,epoch,epoch,1,neighbours_.next_part++,0,1,true,true};
                    NativePreparedMeshHandle mesh;NativePreparedMaterialHandle material;
                    view.prepared_mesh=&mesh;view.prepared_material=&material;
                    view.geometry_revision={1,0,0,0,0};view.geometry_revision_valid=true;
                    view.world.record_revision=1;view.world.owner=&neighbours_.weak_owner;
                    auto matrix=node.object_to_world;matrix[3]+=float(upload->selection.x);matrix[7]+=float(upload->selection.y);
                    view.world.object_to_world=retained_multiply_matrix(map_placement,matrix);
                    view.lighting.object_to_world=view.world.object_to_world;view.lighting.enabled=1;
                    view.world.world_to_clip=world_to_clip;view.object_to_clip=retained_multiply_matrix(world_to_clip,view.world.object_to_world);
                    view.bounds_minimum.fill(FLT_MAX);view.bounds_maximum.fill(-FLT_MAX);
                    for(unsigned corner=0;corner<8;++corner)for(unsigned axis=0;axis<3;++axis) {
                        float value=view.world.object_to_world[axis*4+3];
                        for(unsigned n=0;n<3;++n)value+=view.world.object_to_world[axis*4+n]*(node.bounds.center[n]+
                            ((corner&(1u<<n))?node.bounds.maximum[n]:node.bounds.minimum[n]));
                        view.bounds_minimum[axis]=(std::min)(view.bounds_minimum[axis],value);
                        view.bounds_maximum[axis]=(std::max)(view.bounds_maximum[axis],value);
                    }
                    if(neighbour_fog_hides({view.bounds_minimum[0],view.bounds_minimum[1],
                        view.bounds_maximum[0],view.bounds_maximum[1]},upload->fog_cull)){++upload->fog_culled;continue;}
                    view.terrain.tiles_per_texture_side=1;
                    view.terrain.fog_min[0]=map_placement[3]+float(upload->fog_edges.x0);
                    view.terrain.fog_min[1]=map_placement[7]+float(upload->fog_edges.y0);
                    view.terrain.fog_max[0]=map_placement[3]+float(upload->fog_edges.x1);
                    view.terrain.fog_max[1]=map_placement[7]+float(upload->fog_edges.y1);
                    view.owned_texture.identity=node.texture_identity;view.owned_texture.generation=1;
                    view.owned_texture.resource=&node.texture->identity;view.owned_texture.value_source=1;
                    view.owned_texture.format=node.texture_format;view.owned_texture.mips=node.texture_mips.data();
                    view.owned_texture.mip_count=static_cast<UINT>(node.texture_mips.size());
                    view.semantic_kind=SemanticKind::figure;view.flags=native_neighbour_flag|native_ground_contact_flag|8u;
                    view.contact_world_height=contact_hi-contact_lo;view.contact_map=first_part+1;
                    view.ordering=NativeOrdering::reorderable;view.contribution={true,true,0};
                    NativeGeometryResult result;if(!submit(&submission,view,result)){accepted=false;step="decoration.submit";break;}
                }
                    if(!accepted)break;
                }
                if(accepted) {
                    neighbours_.accepted.push_back(own);
                    const auto edges=neighbour_fog_edges(active,neighbours_.accepted);
                    neighbour_border_fog_publish({active,edges,neighbour_fog_contacts(active,neighbours_.accepted),fog_range});
                }
                if(!accepted) {
                    dedicated_geometry_stale_=true; // its registered prebuilt meshes go once unreferenced
                    for(auto i=neighbours_.opaque.objects.begin();i!=neighbours_.opaque.objects.end();) {
                        if(i->first<first_part){++i;continue;}
                        for(const auto& draw:i->second)release_native_queued_draw(draw.record,false);
                        i=neighbours_.opaque.objects.erase(i);neighbours_.opaque.dirty=true;
                    }
                    for(std::size_t i=first_ordered;i<neighbours_.ordered.size();++i)release_native_queued_draw(neighbours_.ordered[i].record,false);
                    neighbours_.ordered.resize(first_ordered);
                }
                // Ground cover inputs while the decoded map is still alive.
                if(accepted)add_ground_cover_neighbour(*upload,map_placement,first_part+1);
                neighbours_.geometry_bytes+=native_mesh_resident_bytes_-old_mesh;
                neighbours_.texture_bytes+=native_owned_texture_bytes(false)+native_owned_texture_bytes(true)-old_texture;
                native_texture_share_floor_=0;
                // One line per neighbour; a rejected neighbour is omitted whole.
                char text[1536];std::snprintf(text,sizeof(text),
                    "map=%s accepted=%d failed_step=%s%s%s gpu_geometry=%llu gpu_textures=%llu textures_shared=%llu/%llu decoded_payload_peak=%llu cpu_geometry_retained=0 omitted_decorations=%zu fog_culled=%u impostors=%u/%zu bake_ms=%.1f atlas_slices=%u%s lod=%s figures:%u/%u triangles:%llu>%llu/%llu/%llu lod_ms=%.1f prebuild_ms=%.1f %s",
                    upload->selection.placement.mpr.c_str(),accepted?1:0,accepted?"none":step?step:"unknown",
                    submission.failure?"/":"",submission.failure?submission.failure:"",
                    neighbours_.geometry_bytes,neighbours_.texture_bytes,native_texture_shared_,native_texture_shared_bytes_,neighbours_.peak_decoded_bytes,upload->omitted_decorations,upload->fog_culled,
                    impostors_.baked-impostors_before,upload->impostor_sources.size(),impostors_.bake_ms-impostor_ms_before,impostors_.count,
                    impostors_.failed?" impostors_failed":"",
                    // prebuilt=none: the prebuild failed and decorations decoded here, without LOD levels.
                    upload->prebuilt_pages.empty()?"prebuilt=none":"ready",upload->lod_stats.reduced_figures,upload->lod_stats.figures,
                    (unsigned long long)upload->lod_stats.full_triangles,(unsigned long long)upload->lod_stats.level_triangles[0],
                    (unsigned long long)upload->lod_stats.level_triangles[1],(unsigned long long)upload->lod_stats.level_triangles[2],
                    upload->lod_stats.milliseconds,upload->lod_stats.prebuild_milliseconds,
                    upload->decoration_summary.c_str());
                bridge_neighbour_diagnostic(text);
                unused_prebuilt.release();
                upload.reset();neighbour_maps_uploaded(epoch);
            }
            }
            auto error=neighbour_maps_error(epoch);
            if(error!=neighbours_.error) {neighbours_.error=std::move(error);if(!neighbours_.error.empty())bridge_neighbour_diagnostic(neighbours_.error.c_str());}
            // Each neighbour's tile families (beach sand, terrain material blend)
            // as soon as they load, with or without its cover pass.
            for(auto& n:gc_neighbours_)apply_neighbour_tile_families(n);
            // Contact needs painted neighbour colours even before its cover pass,
            // and also when cover is disabled. Reuse the existing paint pipeline.
            if(!gc_neighbours_.empty()&&configured_floating_point(debug_overlay_.config(),"contact_blend",.7f)>0&&
                ensure_ground_cover_pipeline()==S_OK){
                for(auto& n:gc_neighbours_)if(!n.prepared&&FAILED(prepare_ground_cover_neighbour(n))){
                    bridge_neighbour_diagnostic("neighbour contact colour unavailable; contact blend omitted");
                }
            }
            return flush_native_static_set(neighbours_.opaque,frame);
        }
        if(neighbours_.ordered.empty())return S_OK;
        for(const auto& draw:neighbours_.ordered)enqueue_native_record(draw);
        native_static_frame_=&frame;const auto hr=flush_native();native_static_frame_=nullptr;
        return hr;
    } catch(...) {
        neighbour_maps_uploaded(epoch);native_static_frame_=nullptr;
        bridge_neighbour_diagnostic("Neighbour map allocation failed; background omitted");
        release_neighbour_maps();return S_OK;
    }
}
}
