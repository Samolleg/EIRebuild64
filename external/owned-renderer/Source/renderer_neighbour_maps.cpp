#include "renderer.h"
#include "neighbour_maps.h"
#include "neighbour_piece.h"
#include "neighbour_client.h"
#include "owned_resource_runtime.h"
#include "retained_static_scene.h"
#include "native_instance_flags.h"
#include "owned_terrain_front_end.h"
#include "retained_static_submission.h"
#include "debug_overlay_config.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cfloat>
#include <climits>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

namespace owned {
// The piece's terrain commands at the active map's current placement, in
// stream order, with their prebuilt meshes and atlas pages; commands wholly in
// full neighbour fog are not uploaded. The stream already rejected an
// incomplete terrain (NeighbourPiece::terrain_failure).
bool Renderer::submit_neighbour_terrain(const NeighbourPiece& piece,const std::array<float,16>& world_to_clip,
    NativeGeometrySubmitFn submit,void* opaque,const char*& failure) {
    failure="terrain.input";
    if(!submit || neighbours_.weak_owner.expired())return false;
    // This neighbour's contact maps (its tile grid with the material blend
    // rows and its atlas copy) bind to its terrain draws, as to its
    // decorations: key first part + 1 (Renderer::add_ground_cover_neighbour).
    const std::uint32_t contact_map=neighbours_.next_part+1;
    std::array<float,16> placement;
    failure="terrain.world_transform";
    if(!bridge_neighbour_world_transform(placement))return false;
    const std::array<float,16> offset{1,0,0,float(piece.selection.x),0,1,0,float(piece.selection.y),0,0,1,0,0,0,0,1};
    placement=retained_multiply_matrix(placement,offset);
    std::vector<NativeOwnedTextureMip> texture_mips;
    for(const auto& draw:piece.terrain) {
        NativeGeometryView view;
        view.geometry_revision=draw.geometry_revision;view.geometry_revision_valid=true;
        view.position_groups=draw.position_groups;view.normal_groups=draw.normal_groups;
        view.texture_coordinate_count=draw.texture_coordinate_count;view.component_count=draw.component_count;
        view.index_count=draw.index_count;
        view.object_to_clip=world_to_clip;
        view.semantic_kind=SemanticKind::terrain;view.flags=8u;
        view.decoded_channels=native_decoded_terrain;
        view.terrain=draw.terrain;view.terrain_page_count=draw.terrain_page_count;
        view.world.enabled=true;view.world.identity=&piece.identities[draw.identity];view.world.owner=&neighbours_.weak_owner;
        view.world.record_revision=1;view.world.part=draw.material;
        view.world.object_to_world=view.lighting.object_to_world=placement;view.lighting.enabled=1;
        view.world.world_to_clip=world_to_clip;
        view.bounds_minimum.fill(FLT_MAX);view.bounds_maximum.fill(-FLT_MAX);
        for(unsigned corner=0;corner<8;++corner)for(unsigned axis=0;axis<3;++axis) {
            float value=placement[axis*4+3];
            for(unsigned n=0;n<3;++n)value+=placement[axis*4+n]*((corner&(1u<<n))?draw.bounds_maximum[n]:draw.bounds_minimum[n]);
            view.bounds_minimum[axis]=(std::min)(view.bounds_minimum[axis],value);
            view.bounds_maximum[axis]=(std::max)(view.bounds_maximum[axis],value);
        }
        // Wholly inside full neighbour fog: nothing would be visible.
        if(neighbour_fog_hides({view.bounds_minimum[0],view.bounds_minimum[1],
            view.bounds_maximum[0],view.bounds_maximum[1]},piece.fog_cull)) {++piece.fog_culled;continue;}
        NativePreparedMeshHandle mesh;NativePreparedMaterialHandle skin;
        view.prepared_mesh=&mesh;view.prepared_material=&skin;
        view.prebuilt_mesh=&piece.meshes[draw.mesh];
        view.prepared_identity={INT_MIN,piece.epoch,piece.epoch,1,neighbours_.next_part++,0,1,true,true};
        view.owned_texture=neighbour_piece_texture_view(piece.textures[draw.texture],texture_mips);
        // Frozen decoration layers use their authored initial topology. The
        // current world light and atmosphere remain frame-owned GPU values.
        view.flags|=native_neighbour_flag;view.contribution={true,true,0};
        view.contact_map=contact_map;
        // The piece's raw edges (fog_edges); the shader insets them and
        // lets the band give way where other rendered pieces adjoin.
        view.terrain.fog_min[0]=placement[3]+float(piece.fog_edges.x0-piece.selection.x);
        view.terrain.fog_min[1]=placement[7]+float(piece.fog_edges.y0-piece.selection.y);
        view.terrain.fog_max[0]=placement[3]+float(piece.fog_edges.x1-piece.selection.x);
        view.terrain.fog_max[1]=placement[7]+float(piece.fog_edges.y1-piece.selection.y);
        view.ordering=draw.reorderable?NativeOrdering::reorderable:NativeOrdering::preserve;
        NativeGeometryResult result;
        failure="terrain.submit";
        if(!submit(opaque,view,result))return false;
    }
    failure=nullptr;
    return true;
}

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
    // The impostor slices go below: the active map's statics name them.
    release_active_statics();
    impostor_probe_.clear();
    for(const auto& [key,draws]:neighbours_.opaque.objects) {
        (void)key;for(const auto& draw:draws)release_native_queued_draw(draw.record,false);
    }
    for(const auto& draw:neighbours_.ordered)release_native_queued_draw(draw.record,false);
    // Their sun and shelter casters go with them; the static sun layers
    // redraw without them (neighbour_caster_key).
    release_native_shadow_bake(native_neighbour_shadow_bake_);
    ++neighbour_caster_revision_;
    neighbours_={};
    neighbour_receiver_cells_.clear();neighbour_receivers_publish({});
    dedicated_geometry_stale_=true; // their prebuilt pages go once unreferenced
    neighbour_border_fog_publish({});
    gc_neighbours_.clear(); // their ground cover goes with them
    liquid_surf_neighbours_.clear();++liquid_surf_serial_; // and their surf
    // Their impostors: the atlas keeps its size and is baked over again from
    // slice 0 (the 'stale slice reader' this once kept the count for was the
    // odd-slice rounding fold, e73a182d).
    impostors_.slices.clear();impostors_.count=0;impostor_atlas_dump_forget();
}

HRESULT Renderer::render_neighbour_maps(const FixedState& frame,
    const std::array<float,16>& world_to_clip,bool ordered) noexcept {
    const auto* option=debug_overlay_.config().find("neighbour_maps");
    const bool enabled=option && option->boolean_value;
    neighbour_maps_enable(enabled);
    if(!enabled) {
        if(neighbours_.owner){neighbour_client_stop();release_neighbour_maps();}
        return S_OK;
    }
    const auto source=retained_static_terrain_source_snapshot();
    if(!source.map || !source.terrain) {
        if(neighbours_.owner)release_neighbour_maps();
        return S_OK;
    }
    const auto epoch=source.map->generation;
    impostor_label_clip_=world_to_clip; // impostor debug labels (renderer_diagnostics.cpp)

    // Fully fogged neighbour content is never uploaded, so a new range
    // restreams the neighbours. The draw distance caps the range.
    const double fog_range=neighbour_fog_reach(configured_floating_point(debug_overlay_.config(),"neighbour_fog_range",1.f),
        configured_integer(debug_overlay_.config(),"draw_distance",300));
    try {
        // An impostor bake that could not be recorded (bake_neighbour_impostors_d3d12)
        // left its crowns on their triangles: the neighbours stream again so
        // the next bake covers them, instead of keeping that fallback.
        const bool rebake=impostors_.rebake&&!impostors_.failed;
        impostors_.rebake=false;
        if(rebake)bridge_append_probe_event("impostor_rebake","an impostor bake was omitted; the neighbour maps rebuild to bake it");
        if(neighbours_.epoch!=epoch || neighbours_.renderer_generation!=native_static_generation_ ||
            neighbours_.fog_range!=fog_range || rebake) {
            if(neighbours_.owner && (neighbours_.fog_range!=fog_range || rebake))neighbour_client_stop();
            release_neighbour_maps();
            neighbours_.epoch=epoch;neighbours_.renderer_generation=native_static_generation_;
            neighbours_.fog_range=fog_range;
            neighbours_.owner=std::make_shared<const int>(0);neighbours_.weak_owner=neighbours_.owner;
            ResourceStoreConfig mounts;
            if(owned_resource_runtime_mounts(mounts))neighbour_client_begin(source.map->terrain_resource,epoch,mounts);
        }
        if(!ordered) {
            // One neighbour per frame in play; while the loading screen is
            // held, every ready one. Their geometry arrives in prebuilt pages
            // (no render-thread upload burst), so adoption is cheap.
            for(unsigned taken=0;taken==0||load_reveal_holding_;++taken) {
            auto upload=neighbour_client_take(epoch);
            if(!upload)break;
            {
                const auto old_mesh=native_mesh_resident_bytes_;
                const auto old_texture=native_owned_texture_bytes(false)+native_owned_texture_bytes(true);
                native_texture_share_floor_=++native_owned_texture_serial_;
                native_texture_shared_=native_texture_shared_bytes_=0;
                const auto first_ordered=neighbours_.ordered.size();
                const auto first_part=neighbours_.next_part;
                neighbours_.peak_decoded_bytes=(std::max)(neighbours_.peak_decoded_bytes,upload->decoded_bytes);
                // Receiver cells of this piece, kept only if it is accepted.
                std::map<std::pair<int,int>,NativeWorldBounds> piece_cells;
                struct Submission {Renderer* self;const FixedState* frame;const char* failure;
                    std::map<std::pair<int,int>,NativeWorldBounds>* cells;};
                Submission submission{this,&frame,nullptr,&piece_cells};
                const auto submit=[](void* opaque,const NativeGeometryView& input,NativeGeometryResult& result)->bool {
                    auto& context=*static_cast<Submission*>(opaque);auto& self=*context.self;
                    {
                        bool usable=true;
                        for(unsigned axis=0;axis<3;++axis)
                            usable=usable&&std::isfinite(input.bounds_minimum[axis])&&std::isfinite(input.bounds_maximum[axis])&&
                                input.bounds_minimum[axis]<=input.bounds_maximum[axis];
                        if(usable) {
                            const std::pair<int,int> key{int(std::floor((input.bounds_minimum[0]+input.bounds_maximum[0])*.5f/32.f)),
                                int(std::floor((input.bounds_minimum[1]+input.bounds_maximum[1])*.5f/32.f))};
                            auto& cell=(*context.cells)[key];
                            if(!cell.valid){cell.minimum=input.bounds_minimum;cell.maximum=input.bounds_maximum;cell.valid=true;}
                            else for(unsigned axis=0;axis<3;++axis) {
                                cell.minimum[axis]=(std::min)(cell.minimum[axis],input.bounds_minimum[axis]);
                                cell.maximum[axis]=(std::max)(cell.maximum[axis],input.bounds_maximum[axis]);
                            }
                        }
                    }
                    FixedState state=neighbour_submission_state(*context.frame,input.semantic_kind,input.ordering);
                    if(native_astral_record(input.flags)) {
                        // Astral rift veils and clumps: the active map's states
                        // (retained_visual_part_submission): nearly every texel
                        // kept, and its winding, so the authored double-sided
                        // triangles do not both blend over one pixel.
                        state.render[D3DRENDERSTATE_ALPHAREF]=1;
                        state.render[D3DRENDERSTATE_CULLMODE]=D3DCULL_CW;
                    }
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
                if(!d3d12_)bake_neighbour_impostors(*upload);
                else bake_neighbour_impostors_d3d12(*upload);
                // Prebuilt meshes no draw registered (fog culled, a rejected piece)
                // are released before the upload dies, also on an exception.
                struct UnusedPrebuilt {Renderer& self;const NeighbourPiece* upload;
                    void release() noexcept {if(upload)self.release_unused_prebuilt(*upload);upload=nullptr;}
                    ~UnusedPrebuilt(){release();}} unused_prebuilt{*this,upload.get()};
                const char* step=!upload->welded?"terrain.not_welded":
                    upload->terrain_failure.empty()?nullptr:upload->terrain_failure.c_str();
                bool accepted=!step&&submit_neighbour_terrain(*upload,world_to_clip,submit,&submission,step);
                if(accepted && !placed){accepted=false;step="decoration.world_transform";}
                if(accepted) {
                    // Each MOB object's ground-contact span over all its nodes.
                    std::unordered_map<std::uint32_t,std::array<float,2>> contact;
                    for(const auto& node:upload->decorations) {
                        auto local=node.object_to_world;local[3]+=float(upload->selection.x);local[7]+=float(upload->selection.y);
                        const auto z=contact_world_vertical_bounds(node.bounds_center,node.bounds_minimum,node.bounds_maximum,
                            retained_multiply_matrix(map_placement,local));
                        auto& span=contact.try_emplace(node.candidate,std::array<float,2>{FLT_MAX,-FLT_MAX}).first->second;
                        span[0]=(std::min)(span[0],z[0]);span[1]=(std::max)(span[1],z[1]);
                    }
                    // Each node's world box and placement; nodes wholly in full
                    // fog are not registered.
                    const auto place=[&](const NeighbourPieceDecoration& node,std::array<float,16>& to_world,
                        std::array<float,3>& low,std::array<float,3>& high) {
                        auto matrix=node.object_to_world;matrix[3]+=float(upload->selection.x);matrix[7]+=float(upload->selection.y);
                        to_world=retained_multiply_matrix(map_placement,matrix);
                        low.fill(FLT_MAX);high.fill(-FLT_MAX);
                        for(unsigned corner=0;corner<8;++corner)for(unsigned axis=0;axis<3;++axis) {
                            float value=to_world[axis*4+3];
                            for(unsigned n=0;n<3;++n)value+=to_world[axis*4+n]*(node.bounds_center[n]+
                                ((corner&(1u<<n))?node.bounds_maximum[n]:node.bounds_minimum[n]));
                            low[axis]=(std::min)(low[axis],value);high[axis]=(std::max)(high[axis],value);
                        }
                        return neighbour_fog_hides({low[0],low[1],high[0],high[1]},upload->fog_cull);
                    };
                    // An impostor object whose lead part is fogged out has
                    // nothing drawing its quad: its other parts keep their
                    // triangles.
                    std::unordered_set<std::uint32_t> lead_fogged;
                    for(const auto& node:upload->decorations) {
                        if(!node.impostor||!node.impostor_lead)continue;
                        std::array<float,16> to_world;std::array<float,3> low,high;
                        if(place(node,to_world,low,high))lead_fogged.insert(node.candidate);
                    }
                    std::vector<NativeOwnedTextureMip> texture_mips;
                    for(const auto& node:upload->decorations) {
                    const auto& figure=upload->figures[node.figure];
                    const auto& texture=upload->textures[node.texture];
                    // The figure's mesh at this decoration's shape, shared by every
                    // node drawing it at that shape, in object space.
                    NativeLodIdentity observation;
                    observation.id=static_cast<std::int32_t>(node.candidate);observation.candidate_id=true;
                    observation.generation=epoch;observation.neighbour=true;
                    observation.zone=upload->selection.placement.zone;observation.map=upload->selection.placement.mpr;
                    observation.mob=upload->selection.placement.mob_path;
                    const auto slash=observation.mob.find_last_of("/\\");
                    if(slash!=std::string::npos)observation.mob.erase(0,slash+1);
                    observation.model=upload->identities[figure.identity].canonical_name;
                    const auto mod=observation.model.find(".mod/");
                    if(mod!=std::string::npos)observation.model.resize(mod);
                    const auto model_slash=observation.model.find_last_of("/\\");
                    if(model_slash!=std::string::npos)observation.model.erase(0,model_slash+1);
                    NativeGeometryView view;view.lod_observer=&observation;
                    view.position_groups=figure.position_groups;view.normal_groups=figure.normal_groups;
                    view.texture_coordinate_count=figure.texture_coordinate_count;
                    view.component_count=figure.component_count;view.index_count=figure.index_count;
                    view.world.enabled=true;view.world.identity=&upload->identities[figure.identity];
                    view.world.shape=node.shape;view.world.object_space=true;
                    view.prebuilt_mesh=&upload->meshes[node.mesh];
                    // Its object's merged impostor: the lead part draws the
                    // quad, the others switch with it (static_lod.h).
                    if(node.impostor&&!lead_fogged.count(node.candidate)) {
                        view.world.impostor=neighbour_impostor_slice(upload->impostors[node.impostor-1].key);
                        view.world.impostor_follower=!node.impostor_lead;
                    }
                    view.prepared_identity={INT_MIN,epoch,epoch,1,neighbours_.next_part++,0,1,true,true};
                    observation.part=view.prepared_identity.part_index;
                    NativePreparedMeshHandle mesh;NativePreparedMaterialHandle material;
                    view.prepared_mesh=&mesh;view.prepared_material=&material;
                    view.geometry_revision={1,0,0,0,0};view.geometry_revision_valid=true;
                    view.world.record_revision=1;view.world.owner=&neighbours_.weak_owner;
                    if(place(node,view.world.object_to_world,view.bounds_minimum,view.bounds_maximum)){++upload->fog_culled;continue;}
                    if(view.world.impostor)try { // impostor debug labels and table
                        ImpostorProbeEntry entry;entry.key=upload->impostors[node.impostor-1].key;
                        entry.slice=view.world.impostor-1;entry.lead=!view.world.impostor_follower;entry.object=node.object;
                        for(unsigned axis=0;axis<3;++axis){entry.centre[axis]=(view.bounds_minimum[axis]+view.bounds_maximum[axis])*.5f;
                            entry.extent=(std::max)(entry.extent,(view.bounds_maximum[axis]-view.bounds_minimum[axis])*.5f);}
                        const auto& m=world_to_clip;const float* p=entry.centre;
                        const float cx=m[0]*p[0]+m[1]*p[1]+m[2]*p[2]+m[3],cy=m[4]*p[0]+m[5]*p[1]+m[6]*p[2]+m[7];
                        const float cw=m[12]*p[0]+m[13]*p[1]+m[14]*p[2]+m[15];
                        entry.screen[0]=cw>1e-3f?(cx/cw*.5f+.5f)*3840.f:-1e9f;entry.screen[1]=cw>1e-3f?(.5f-cy/cw*.5f)*2160.f:-1e9f;entry.screen[2]=cw;
                        impostor_probe_.push_back(std::move(entry));
                    }catch(...){}
                    view.lighting.object_to_world=view.world.object_to_world;view.lighting.enabled=1;
                    view.world.world_to_clip=world_to_clip;view.object_to_clip=retained_multiply_matrix(world_to_clip,view.world.object_to_world);
                    view.terrain.tiles_per_texture_side=1;
                    view.terrain.fog_min[0]=map_placement[3]+float(upload->fog_edges.x0);
                    view.terrain.fog_min[1]=map_placement[7]+float(upload->fog_edges.y0);
                    view.terrain.fog_max[0]=map_placement[3]+float(upload->fog_edges.x1);
                    view.terrain.fog_max[1]=map_placement[7]+float(upload->fog_edges.y1);
                    view.owned_texture=neighbour_piece_texture_view(texture,texture_mips);
                    std::array<std::vector<NativeOwnedTextureMip>,3> environmentMips;
                    if(node.environment_material) {
                        view.skn_material.identity=node.environment_material;
                        std::copy_n(node.environment_factors.begin(),4,view.skn_material.factors);
                        std::copy_n(node.environment_base.begin(),4,view.skn_material.base);
                        std::copy_n(node.environment_emission.begin(),4,view.skn_material.emission);
                        for(unsigned k=0;k<3;++k)if(node.environment_maps[k]<upload->textures.size())
                            view.skn_material.maps[k]=neighbour_piece_texture_view(upload->textures[node.environment_maps[k]],environmentMips[k]);
                        retained_visual_part_fixed_state(RetainedVisualSample{},view.owned_fixed_state);
                        view.owned_fixed_state.z_write_enable=node.environment_alpha!=2;
                        view.owned_fixed_state.alpha_blend_enable=node.environment_alpha==2;
                        view.owned_fixed_state.alpha_test_enable=node.environment_alpha==1;
                        view.owned_fixed_state.alpha_func=D3DCMP_GREATER;
                        view.owned_fixed_state.alpha_ref=UINT(node.environment_cutoff*255);
                        view.owned_fixed_state.address_u=node.environment_address_u;view.owned_fixed_state.address_v=node.environment_address_v;
                        view.owned_fixed_state.cull_mode=node.environment_factors[3]>1?D3DCULL_NONE:D3DCULL_CW;
                        if(node.environment_alpha==0)view.owned_texture.opaque_mips=UINT_MAX;
                        else view.lighting.material_alpha*=node.environment_alpha_factor;
                    }
                    view.semantic_kind=SemanticKind::figure;view.flags=native_neighbour_flag|native_ground_contact_flag|8u;
                    // Sway and foliage by the main map's rule (the stream read
                    // them from the assembly): a tree's parts sway around its
                    // root node; a swaying plant's cut-out part is foliage.
                    view.flags|=node.flags;
                    if(node.sway) {
                        const std::array<float,4> root{node.root_world[0]+float(upload->selection.x),
                            node.root_world[1]+float(upload->selection.y),node.root_world[2],1.f};
                        for(unsigned axis=0;axis<3;++axis)
                            view.world.sway_root[axis]=map_placement[axis*4]*root[0]+map_placement[axis*4+1]*root[1]+
                                map_placement[axis*4+2]*root[2]+map_placement[axis*4+3];
                    }
                    const auto& span=contact[node.candidate];
                    view.contact_world_height=span[1]-span[0];view.contact_map=first_part+1;
                    view.ordering=NativeOrdering::reorderable;view.contribution={true,true,0};
                    if(node.environment_material&&node.environment_alpha==2)view.ordering=NativeOrdering::preserve;
                    if(native_astral_record(node.flags)) {
                        // Astral rift veils and clumps (astral_rift.h): blended in
                        // order without depth writes, no ground contact; the clumps
                        // unlit in the atlas colours, as on the active map.
                        view.ordering=NativeOrdering::preserve;
                        view.flags&=~native_ground_contact_flag;
                        if(((node.flags>>native_astral_shift)&3u)==2u){view.lighting.enabled=0;view.flags|=4u;view.default_diffuse=0xffffffffu;}
                    }
                    NativeGeometryResult result;if(!submit(&submission,view,result)){accepted=false;step="decoration.submit";break;}
                    }
                }
                if(accepted) {
                    ++neighbour_caster_revision_; // its decorations cast from the next sun redraw
                    for(const auto& [key,box]:piece_cells) {
                        auto& cell=neighbour_receiver_cells_[key];
                        if(!cell.valid){cell=box;continue;}
                        for(unsigned axis=0;axis<3;++axis) {
                            cell.minimum[axis]=(std::min)(cell.minimum[axis],box.minimum[axis]);
                            cell.maximum[axis]=(std::max)(cell.maximum[axis],box.maximum[axis]);
                        }
                    }
                    std::vector<NativeWorldBounds> cells;cells.reserve(neighbour_receiver_cells_.size());
                    for(const auto& [key,box]:neighbour_receiver_cells_){(void)key;cells.push_back(box);}
                    neighbour_receivers_publish(std::move(cells));
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
                    for(std::size_t i=first_ordered;i<neighbours_.ordered.size();++i) {
                        const auto record=neighbours_.ordered[i].record;
                        release_native_queued_draw(record,false);
                    }
                    neighbours_.ordered.resize(first_ordered);
                }
                // Ground cover inputs while the decoded map is still alive.
                if(accepted&&!d3d12_)add_ground_cover_neighbour(*upload,map_placement,first_part+1);
                else if(accepted)add_neighbour_blend_maps_d3d12(*upload,map_placement,first_part+1); // with its cover map
                if(accepted)add_liquid_surf_neighbour(*upload,map_placement);
                neighbours_.geometry_bytes+=native_mesh_resident_bytes_-old_mesh;
                neighbours_.texture_bytes+=native_owned_texture_bytes(false)+native_owned_texture_bytes(true)-old_texture;
                native_texture_share_floor_=0;
                // One line per neighbour; a rejected neighbour is omitted whole.
                char impostor_split[160]{};
                if(impostors_.failed)std::snprintf(impostor_split,sizeof(impostor_split)," impostors_failed");
                char text[1536];std::snprintf(text,sizeof(text),
                    "map=%s accepted=%d failed_step=%s%s%s gpu_geometry=%llu gpu_textures=%llu textures_shared=%llu/%llu decoded_payload_peak=%llu cpu_geometry_retained=0 omitted_decorations=%zu unbuilt_decorations=%u fog_culled=%u impostors=%u/%zu bake_ms=%.1f atlas_slices=%u%s lod=%s figures:%u/%u triangles:%llu>%llu/%llu/%llu lod_ms=%.1f prebuild_ms=%.1f lod_indices_mb=%.3f load_ms=%.1f %s",
                    upload->selection.placement.mpr.c_str(),accepted?1:0,accepted?"none":step?step:"unknown",
                    submission.failure?"/":"",submission.failure?submission.failure:"",
                    neighbours_.geometry_bytes,neighbours_.texture_bytes,native_texture_shared_,native_texture_shared_bytes_,neighbours_.peak_decoded_bytes,upload->omitted_decorations,upload->unbuilt_decorations,upload->fog_culled,
                    impostors_.baked-impostors_before,upload->impostors.size(),impostors_.bake_ms-impostor_ms_before,impostors_.count,
                    impostor_split,
                    // prebuilt=none: the stream built no pages; the piece was rejected (terrain.prebuild_failed).
                    upload->pages.empty()?"prebuilt=none":"ready",upload->lod_stats.reduced_figures,upload->lod_stats.figures,
                    (unsigned long long)upload->lod_stats.full_triangles,(unsigned long long)upload->lod_stats.level_triangles[0],
                    (unsigned long long)upload->lod_stats.level_triangles[1],(unsigned long long)upload->lod_stats.level_triangles[2],
                    upload->lod_stats.milliseconds,upload->lod_stats.prebuild_milliseconds,
                    12.0*double(upload->lod_stats.level_triangles[0]+upload->lod_stats.level_triangles[1]+
                        upload->lod_stats.level_triangles[2])/1048576.0,upload->lod_stats.load_milliseconds,
                    upload->decoration_summary.c_str());
                bridge_neighbour_diagnostic(text);
                unused_prebuilt.release();
                upload.reset();neighbour_client_uploaded(epoch);
            }
            }
            auto error=neighbour_client_error(epoch);
            if(error!=neighbours_.error) {neighbours_.error=std::move(error);if(!neighbours_.error.empty())bridge_neighbour_diagnostic(neighbours_.error.c_str());}
            // Each neighbour's tile families (beach sand, terrain material blend)
            // as soon as they load, with or without its cover pass.
            for(auto& n:gc_neighbours_)apply_neighbour_tile_families(n);
            // Contact needs painted neighbour colours even before its cover pass,
            // and also when cover is disabled. Reuse the existing paint pipeline.
            if(!d3d12_&&!gc_neighbours_.empty()&&configured_floating_point(debug_overlay_.config(),"contact_blend",.7f)>0&&
                ensure_ground_cover_pipeline()==S_OK){
                for(auto& n:gc_neighbours_)if(!n.prepared&&FAILED(prepare_ground_cover_neighbour(n))){
                    bridge_neighbour_diagnostic("neighbour contact colour unavailable; contact blend omitted");
                }
            }
            return flush_native_static_set(neighbours_.opaque,frame);
        }
        if(neighbours_.ordered.empty())return S_OK;
        // The neighbours' liquid sectors keep their order, so the build never
        // rejects one whole (drawVisible is skipped for preservesOrder): every
        // loaded neighbour's liquid went through the queue, the build and the
        // liquid passes each frame, and a flush with no water on screen still
        // took its reflection frame (scene copy, trace targets, march, filter).
        // Here the same box test first (drawVisible's frustum part, the cull
        // clip, the record's world bounds grown by 8 m for waves, surf and
        // falls): a sector outside the view is not queued, the rest keep
        // their order, and no sector in view means no flush. Option
        // coverage_cuts without 4: all queued, as before.
        const bool cull_boxes=d3d12_&&(configured_integer(debug_overlay_.config(),"coverage_cuts",63)&4)!=0;
        const auto& cull_clip=native_cull_frozen_?native_cull_clip_:native_world_clip_;
        const auto in_view=[&](const NativeSelectedRecord& selected) {
            const auto& record=native_instance_records_[selected.record];
            if(!(record.descriptor.instance_index&0x80000000u))return true; // not world-space bounds: kept
            const float* const lo=record.descriptor.bounds_min;const float* const hi=record.descriptor.bounds_max;
            unsigned outside=63u;
            for(unsigned corner=0;corner<8&&outside;++corner) {
                const float p[3]={(corner&1)?hi[0]+8.f:lo[0]-8.f,(corner&2)?hi[1]+8.f:lo[1]-8.f,(corner&4)?hi[2]+8.f:lo[2]-8.f};
                float clip[4];
                for(unsigned row=0;row<4;++row)clip[row]=cull_clip[row*4]*p[0]+cull_clip[row*4+1]*p[1]+cull_clip[row*4+2]*p[2]+cull_clip[row*4+3];
                if(!(std::isfinite(clip[0])&&std::isfinite(clip[1])&&std::isfinite(clip[2])&&std::isfinite(clip[3])))return true;
                unsigned mask=0;
                if(clip[0]<-clip[3])mask|=1;if(clip[0]>clip[3])mask|=2;if(clip[1]<-clip[3])mask|=4;if(clip[1]>clip[3])mask|=8;
                if(clip[2]<0)mask|=16;if(clip[2]>clip[3])mask|=32;
                outside&=mask;
            }
            return outside==0;
        };
        std::size_t queued=0;
        for(const auto& draw:neighbours_.ordered) {
            if(cull_boxes&&!in_view(draw))continue;
            enqueue_native_record(draw);++queued;
        }
        if(!queued)return S_OK;
        native_static_frame_=&frame;const auto hr=flush_native();native_static_frame_=nullptr;
        return hr;
    } catch(...) {
        neighbour_client_uploaded(epoch);native_static_frame_=nullptr;
        bridge_neighbour_diagnostic("Neighbour map allocation failed; background omitted");
        release_neighbour_maps();return S_OK;
    }
}

// ---- Item 5: the active map's object-space statics ----

// One catalog node's object-space record in its model's frame; an admitted
// object's registration replaces placement, bounds, texture and values.
NativeGeometryView Renderer::active_static_view(const NeighbourPiece& piece,ActiveStaticNode& node,
    const std::array<float,16>& world_to_clip,std::vector<NativeOwnedTextureMip>& mips) {
    const auto& draw=piece.decorations[node.decoration];
    const auto& figure=piece.figures[draw.figure];
    NativeGeometryView view;
    view.position_groups=figure.position_groups;view.normal_groups=figure.normal_groups;
    view.texture_coordinate_count=figure.texture_coordinate_count;
    view.component_count=figure.component_count;view.index_count=figure.index_count;
    view.world.enabled=true;view.world.identity=&piece.identities[figure.identity];
    view.world.shape=draw.shape;view.world.object_space=true;
    view.prebuilt_mesh=&piece.meshes[draw.mesh];
    // Its model's merged impostor (shared with neighbours by model key).
    if(draw.impostor) {
        view.world.impostor=neighbour_impostor_slice(piece.impostors[draw.impostor-1].key);
        view.world.impostor_follower=!draw.impostor_lead;
    }
    view.prepared_identity=node.identity;view.prepared_mesh=&node.mesh;view.prepared_material=&node.material;
    view.geometry_revision={1,0,0,0,0};view.geometry_revision_valid=true;
    view.world.record_revision=1;view.world.owner=&active_statics_.weak_owner;
    view.world.object_to_world=view.lighting.object_to_world=draw.object_to_world;view.lighting.enabled=1;
    view.world.world_to_clip=world_to_clip;view.object_to_clip=retained_multiply_matrix(world_to_clip,draw.object_to_world);
    view.bounds_minimum.fill(FLT_MAX);view.bounds_maximum.fill(-FLT_MAX);
    for(unsigned corner=0;corner<8;++corner)for(unsigned axis=0;axis<3;++axis) {
        float value=draw.object_to_world[axis*4+3];
        for(unsigned n=0;n<3;++n)value+=draw.object_to_world[axis*4+n]*(draw.bounds_center[n]+
            ((corner&(1u<<n))?draw.bounds_maximum[n]:draw.bounds_minimum[n]));
        view.bounds_minimum[axis]=(std::min)(view.bounds_minimum[axis],value);
        view.bounds_maximum[axis]=(std::max)(view.bounds_maximum[axis],value);
    }
    if(draw.texture<piece.textures.size())view.owned_texture=neighbour_piece_texture_view(piece.textures[draw.texture],mips);
    view.semantic_kind=SemanticKind::figure;view.flags=native_ground_contact_flag|8u|draw.flags;
    view.ordering=NativeOrdering::reorderable;view.contribution={true,true,0};
    return view;
}

// Per frame, before objects register: a new world source generation
// publishes the eligible MOB ids (trees 53 and stones 55, no quest object);
// the models the bridge requested since go to the worker as one batch, and a
// finished batch is adopted.
void Renderer::update_active_statics(const FixedState& frame,const std::array<float,16>& world_to_clip) noexcept try {
    auto& a=active_statics_;
    const bool enabled=configured_boolean(debug_overlay_.config(),"main_map_object_space",true);
    const std::uint64_t generation=enabled?retained_static_terrain_source_identity().source_generation:0;
    if(a.generation!=generation) {
        release_active_statics();
        if(!generation)return;
        const auto source=retained_static_terrain_source_snapshot();
        if(source.source_generation!=generation || !source.map)return; // again next frame
        static std::uint64_t catalogs=0;
        auto index=std::make_shared<ActiveStaticsIndex>();
        index->catalog=++catalogs;
        for(const auto& object:source.map->objects)
            if((object.family==53 || object.family==55 || (object.environment_profile&&object.family>=53&&object.family<=56)) && object.quest==0 &&
               object.rejection==StaticObjectRejection::none && object.nid && object.nid<=0x7fffffffu)
                index->eligible.insert(static_cast<std::int32_t>(object.nid));
        a.generation=generation;a.catalog=index->catalog;a.index=index;
        a.owner=std::make_shared<const int>(0);a.weak_owner=a.owner;
        active_statics_index_publish(std::make_shared<ActiveStaticsIndex>(*index));
        return;
    }
    if(!generation || !a.index)return;
    // Refused registrations: one line per failing step with its count and
    // first examples, at most once a second while they change.
    if(const auto now=std::chrono::steady_clock::now();now-a.refusals_time>std::chrono::seconds(1)) {
        a.refusals_time=now;
        const auto refusals=active_statics_refusals();
        if(refusals.revision!=a.refusals_logged) {
            a.refusals_logged=refusals.revision;
            for(std::size_t k=0;k<refusals.counts.size();++k) {
                if(!refusals.counts[k])continue;
                std::string text="main_map_statics refusal step="+std::string(object_space_refusal_name(ObjectSpaceRefusal(k)))+
                    " count="+std::to_string(refusals.counts[k]);
                for(const auto& example:refusals.examples[k])text+=" | "+example;
                bridge_neighbour_diagnostic(text.c_str());
            }
        }
    }
    if(a.building) {
        bool failed=false;std::string failure;std::vector<std::uint64_t> models,failed_models;
        auto piece=active_statics_take(generation,failed,failure,models,failed_models);
        if(!piece && !failed)return; // still building
        a.building=false;
        if(!piece) {
            // The whole batch stays posed; its models are not requested again.
            for(const auto key:models)a.index->failed.insert(key);
            for(const auto key:failed_models)a.index->failed.insert(key);
            active_statics_index_publish(std::make_shared<ActiveStaticsIndex>(*a.index));
            failure="main_map_statics batch failed ("+failure+"); its objects stay posed";
            bridge_neighbour_diagnostic(failure.c_str());
            return;
        }
        adopt_active_statics(std::shared_ptr<NeighbourPiece>(std::move(piece)),models,failed_models,frame,world_to_clip);
        return;
    }
    auto requests=active_statics_take_requests();
    if(requests.empty())return;
    a.building=true;a.started=std::chrono::steady_clock::now();
    active_statics_begin(generation,std::move(requests));
} catch(...) {release_active_statics();}

// A batch's pages and impostors, and every node registered once and parked
// (its mesh stays resident while no object draws it); then the index with
// its models ready.
void Renderer::adopt_active_statics(std::shared_ptr<NeighbourPiece> piece,const std::vector<std::uint64_t>& models,
    const std::vector<std::uint64_t>& failed_models,const FixedState& frame,const std::array<float,16>& world_to_clip) noexcept try {
    auto& a=active_statics_;
    const auto piece_index=static_cast<std::uint32_t>(a.pieces.size());
    a.pieces.push_back(std::move(piece));++a.batches;
    auto& p=*a.pieces.back();
    const auto old_mesh=native_mesh_resident_bytes_;
    adopt_prebuilt_pages(p);
    adopt_prebuilt_textures(p);
    // The resident cache owns adopted textures; drop temporary duplicates and uploads.
    p.prebuilt_textures.clear();
    const UINT baked_before=impostors_.baked;
    if(!d3d12_)bake_neighbour_impostors(p);else bake_neighbour_impostors_d3d12(p);
    const auto baked=impostors_.baked-baked_before;
    a.impostors_baked+=baked;a.impostor_objects+=p.impostors.size();
    std::unordered_set<std::uint64_t> batch;
    for(std::uint32_t i=0;i<p.decorations.size();++i) {
        const auto& d=p.decorations[i];
        if(d.object<0 || std::size_t(d.object)>=models.size())continue;
        auto& model=a.models[models[d.object]];
        if(batch.insert(models[d.object]).second){model.piece=piece_index;model.nodes.clear();}
        ActiveStaticNode node;node.decoration=i;node.node=d.node;
        // Renderer namespace, apart from neighbour parts (plan generation 2).
        node.identity={INT_MIN,a.generation,a.generation,2,i,piece_index,1,true,true};
        model.nodes.push_back(node);
    }
    const FixedState state=neighbour_submission_state(frame,SemanticKind::figure,NativeOrdering::reorderable);
    std::vector<NativeOwnedTextureMip> mips;
    std::uint64_t nodes_total=0,parked=0;
    unsigned environment_examples=0;
    for(const auto key:batch)for(auto& node:a.models[key].nodes) {
        ++nodes_total;
        const auto first=native_pending_draws_.size();
        auto view=active_static_view(p,node,world_to_clip,mips);
        if(p.decorations[node.decoration].environment_material && environment_examples++<16) {
            const auto& draw=p.decorations[node.decoration];
            const auto& figure=p.figures[draw.figure];
            bridge_neighbour_diagnostic(("main_map_glb model="+p.identities[figure.identity].canonical_name+
                " planned_impostor="+std::to_string(draw.impostor)+" atlas="+std::to_string(view.world.impostor)+
                " lead="+std::to_string(draw.impostor_lead)).c_str());
        }
        NativeGeometryResult result;
        const HRESULT hr=queue_native_terrain(view,state,nullptr,&result);
        if(FAILED(hr) || native_pending_draws_.size()!=first+1) {
            if(p.decorations[node.decoration].environment_material) {
                const auto& figure=p.figures[p.decorations[node.decoration].figure];
                const auto detail=p.identities[figure.identity].canonical_name+" hr="+std::to_string(hr)+
                    " step="+(result.failure_operation?result.failure_operation:"enqueue_count");
                active_statics_note_refusal(ObjectSpaceRefusal::adopt,detail.c_str());
            }
            for(std::size_t k=first;k<native_pending_draws_.size();++k)release_native_queued_draw(native_pending_draws_[k].record,true);
            native_pending_draws_.resize(first);continue;
        }
        const auto selected=native_pending_draws_[first];
        --native_instance_records_[selected.record].command.mesh->pending_references;
        native_pending_draws_.resize(first);
        node.record=selected.record;++parked;
    }
    a.parked+=parked;
    release_unused_prebuilt(p);
    std::uint64_t ready=0;
    for(const auto key:batch) {
        const auto& model=a.models[key];
        bool all=!model.nodes.empty(),impostor=false;
        for(const auto& node:model.nodes){all=all&&node.record!=UINT_MAX;impostor|=p.decorations[node.decoration].impostor!=0;}
        // LOD only: no part of the model has an impostor plan.
        if(!impostor)++a.lod_only_models;
        if(all){a.index->models.insert(key);++ready;}
        else a.index->failed.insert(key);
    }
    for(const auto key:failed_models)a.index->failed.insert(key);
    active_statics_index_publish(std::make_shared<ActiveStaticsIndex>(*a.index));
    a.build_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-a.started).count();
    char text[1024];std::snprintf(text,sizeof(text),
        "main_map_statics batch=%llu models=%llu/%zu nodes=%llu parked=%llu impostors=%u/%zu lod figures:%u/%u triangles:%llu>%llu/%llu/%llu gpu_geometry=%llu eligible=%zu ready_total=%zu failed_total=%zu lod_only_total=%llu build_ms=%.0f %s",
        (unsigned long long)a.batches,(unsigned long long)ready,models.size()+failed_models.size(),(unsigned long long)nodes_total,
        (unsigned long long)parked,baked,p.impostors.size(),p.lod_stats.reduced_figures,p.lod_stats.figures,
        (unsigned long long)p.lod_stats.full_triangles,(unsigned long long)p.lod_stats.level_triangles[0],
        (unsigned long long)p.lod_stats.level_triangles[1],(unsigned long long)p.lod_stats.level_triangles[2],
        (unsigned long long)(native_mesh_resident_bytes_-old_mesh),a.index->eligible.size(),a.index->models.size(),
        a.index->failed.size(),(unsigned long long)a.lod_only_models,a.build_ms,p.decoration_summary.c_str());
    bridge_neighbour_diagnostic(text);
} catch(...) {release_active_statics();}

void Renderer::release_active_caster(std::int32_t object) noexcept {
    auto& a=active_statics_;
    const auto found=a.casters.find(object);
    if(found==a.casters.end())return;
    for(const auto record:found->second.records)
        if(record<native_instance_records_.size())release_native_queued_draw(record,false);
    a.casters.erase(found);++a.caster_revision;
}

void Renderer::release_active_statics() noexcept {
    auto& a=active_statics_;
    const bool any=a.generation||!a.pieces.empty()||a.building||!a.keys.empty()||!a.casters.empty();
    std::vector<std::uint64_t> keys;
    try {for(const auto& [key,object]:a.keys){(void)object;keys.push_back(key);}} catch(...) {}
    for(const auto key:keys)remove_native_static_object(key);
    for(auto& [object,caster]:a.casters){(void)object;
        for(const auto record:caster.records)if(record<native_instance_records_.size())release_native_queued_draw(record,false);}
    for(auto& [key,model]:a.models){(void)key;for(auto& node:model.nodes)
        if(node.record<native_instance_records_.size())release_native_queued_draw(node.record,false);}
    for(const auto& piece:a.pieces)if(piece)release_unused_prebuilt(*piece);
    release_native_shadow_bake(native_active_shadow_bake_);
    native_active_casters_=native_active_doubled_casters_=0;
    const auto revision=a.caster_revision+1;
    a=ActiveStatics{};a.caster_revision=revision;
    if(any) {
        active_statics_stop();active_statics_index_publish(nullptr);
        dedicated_geometry_stale_=true; // the catalog's prebuilt pages go once unreferenced
    }
}

// An admitted object's parts as object-space records: each part matched to
// its model's catalog node by model node and figure, with the live node
// placement and the posed record's bounds, texture and values (highlight,
// fade, fog ride on them). False queued: a part did not match, posed stays.
HRESULT Renderer::queue_active_static_parts(const NativeStaticSubmission& request,const FixedState& frame,
    std::vector<ActiveStaticHandles>& handles,bool caster,NativeGeometryResult* result,bool& queued) {
    queued=false;
    auto& a=active_statics_;
    const auto found=a.models.find(request.model);
    if(found==a.models.end() || found->second.piece>=a.pieces.size()) {
        char text[96];std::snprintf(text,sizeof(text),"object=%d model=%016llx",request.object,(unsigned long long)request.model);
        active_statics_note_refusal(ObjectSpaceRefusal::model_missing,text);return S_OK;
    }
    auto& model=found->second;
    const auto& piece=*a.pieces[model.piece];
    const auto& submission=*request.records;
    std::vector<ActiveStaticNode*> matched(submission.count,nullptr);
    std::vector<std::uint8_t> leads(piece.impostors.size()+1,0);
    for(UINT i=0;i<submission.count;++i) {
        const auto* part=submission.parts[i];
        if(!part || !part->source || !part->source->world.identity)
            {active_statics_note_refusal(ObjectSpaceRefusal::part_source);return S_OK;}
        const auto& source=*part->source;
        bool node_seen=false;
        for(auto& node:model.nodes) {
            if(node.node!=source.world.pose_node || node.record==UINT_MAX)continue;
            node_seen=true;
            const auto& figure=piece.figures[piece.decorations[node.decoration].figure];
            if(figure.component_count==source.component_count && figure.index_count==source.index_count &&
               piece.identities[figure.identity].key()==source.world.identity->key()){matched[i]=&node;break;}
        }
        if(!matched[i]) {
            // Which catalog nodes the model has against the part asked for.
            std::string text="object="+std::to_string(request.object)+(caster?" caster":" colour")+" part="+std::to_string(i)+"/"+
                std::to_string(submission.count)+" pose_node="+std::to_string(source.world.pose_node)+" components="+
                std::to_string(source.component_count)+" indices="+std::to_string(source.index_count)+" catalog:";
            for(const auto& node:model.nodes) {
                const auto& figure=piece.figures[piece.decorations[node.decoration].figure];
                text+=" "+std::to_string(node.node)+(node.record==UINT_MAX?"!":"")+"/"+std::to_string(figure.component_count)+"/"+
                    std::to_string(figure.index_count)+(piece.identities[figure.identity].key()==source.world.identity->key()?"=":"~");
            }
            active_statics_note_refusal(node_seen?ObjectSpaceRefusal::figure_mismatch:ObjectSpaceRefusal::node_unmatched,text.c_str());
            return S_OK;
        }
        const auto& draw=piece.decorations[matched[i]->decoration];
        if(draw.impostor && draw.impostor_lead)leads[draw.impostor]=1;
    }
    handles.assign(submission.count,{});
    const auto first=native_pending_draws_.size();
    std::vector<NativeOwnedTextureMip> mips;
    for(UINT i=0;i<submission.count;++i) {
        const auto& part=*submission.parts[i];const auto& values=part.values;
        auto view=active_static_view(piece,*matched[i],*submission.world_to_clip,mips);
        // Without its lead part nothing would draw the object's quad.
        if(view.world.impostor && !leads[piece.decorations[matched[i]->decoration].impostor])
            {view.world.impostor=0;view.world.impostor_follower=false;}
        // Its own identity and handles (colour 3, caster 4): objects of one
        // model share meshes, never prepared slots.
        view.prepared_identity={request.object,a.generation,a.generation,caster?4u:3u,i,0,1,true,true};
        view.prepared_mesh=&handles[i].mesh;view.prepared_material=&handles[i].material;
        const auto& m=request.node_to_world[i];
        view.world.object_to_world=view.lighting.object_to_world=m;
        view.object_to_clip=retained_multiply_matrix(*submission.world_to_clip,m);
        view.bounds_minimum=part.bounds.minimum;view.bounds_maximum=part.bounds.maximum;
        // The posed record's texture (archive member or game surface).
        view.owned_texture=part.source->owned_texture;
        view.skn_material=part.source->skn_material;
        // The posed sway bits name the root pose node; object space carries
        // the anchor instead (retained_object_space_sway_flags).
        view.flags=((values.flags&native_sway_flag)?(values.flags&(native_sway_flag-1u)):values.flags)|request.sway_flags[i];
        view.default_diffuse=values.default_diffuse;view.default_specular=values.default_specular;
        view.lighting.enabled=values.lighting_enabled;view.lighting.material_alpha=values.alpha;
        view.lighting.material_diffuse=values.diffuse;view.lighting.emissive=values.emissive;
        view.lighting.specular=values.specular;view.u_shift=values.u_shift;view.v_shift=values.v_shift;
        view.ordering=static_cast<NativeOrdering>(values.ordering);
        view.owned_fixed_state=part.source->owned_fixed_state;view.owned_fixed_state.specular_enable=values.specular_enable;
        view.world.sway_root=request.sway_root;view.contact_world_height=request.contact_height;
        auto state=native_owned_submission_state(frame,view.owned_fixed_state);
        // A cut-out part of a fully opaque object draws as the neighbour
        // maps draw the same models (neighbour_submission_state): alpha
        // tested, unblended, so it takes the depth prepass and shades only
        // its visible texels. A fading object keeps its blend.
        if(state.render[D3DRENDERSTATE_ALPHATESTENABLE]&&state.render[D3DRENDERSTATE_ALPHABLENDENABLE]&&
           state.render[D3DRENDERSTATE_ZWRITEENABLE]&&values.alpha>=254.5f/255.f)
            state.render[D3DRENDERSTATE_ALPHABLENDENABLE]=FALSE;
        if(FAILED(queue_native_terrain(view,state,nullptr,result)) || native_pending_draws_.size()!=first+i+1) {
            active_statics_note_refusal(ObjectSpaceRefusal::queue);
            for(std::size_t k=first;k<native_pending_draws_.size();++k)release_native_queued_draw(native_pending_draws_[k].record,true);
            native_pending_draws_.resize(first);return S_OK;
        }
    }
    queued=true;
    return S_OK;
}

// Colour: the object's records under its static-set key.
HRESULT Renderer::set_native_static_object_space(const NativeStaticSubmission& request,
    const FixedState& frame,bool& registered,NativeGeometryResult* result) try {
    registered=false;
    if(native_shadow_collecting_ || !request.records || !request.node_to_world || !request.sway_flags ||
       !request.records->world_to_clip){active_statics_note_refusal(ObjectSpaceRefusal::invalid);return E_INVALIDARG;}
    auto& a=active_statics_;
    remove_native_static_object(request.key);
    auto& handles=a.handles[request.key];
    const auto first=native_pending_draws_.size();
    bool queued=false;
    const HRESULT queue_hr=queue_active_static_parts(request,frame,handles,false,result,queued);
    if(FAILED(queue_hr) || !queued){a.handles.erase(request.key);++a.refused;return queue_hr;}
    const HRESULT hr=adopt_native_static_draws(native_static_set_,request.key,first,registered);
    if(FAILED(hr))return hr;
    if(registered)a.keys[request.key]=request.object;
    else {a.handles.erase(request.key);++a.refused;active_statics_note_refusal(ObjectSpaceRefusal::adopt);}
    return S_OK;
} catch(...) {active_statics_note_refusal(ObjectSpaceRefusal::exception);return E_OUTOFMEMORY;}

// Casters: the bridge's caster store build registers every admitted object
// of the world (on screen or not) here, at its live placement, with its
// casting parts; the active bake draws them, the store none of them. An
// unchanged registration (model, node matrices, textures, flags) is kept.
HRESULT Renderer::set_native_active_caster(const NativeStaticSubmission& request,const FixedState& frame) noexcept try {
    request.registered=false;
    if(native_shadow_collecting_ || !request.records || !request.node_to_world || !request.sway_flags ||
       !request.records->world_to_clip){active_statics_note_refusal(ObjectSpaceRefusal::invalid);return E_INVALIDARG;}
    auto& a=active_statics_;
    const auto& submission=*request.records;
    std::uint64_t signature=1469598103934665603ull;
    const auto mix=[&](const void* data,std::size_t size){
        const auto* bytes=static_cast<const std::uint8_t*>(data);
        for(std::size_t k=0;k<size;++k){signature^=bytes[k];signature*=1099511628211ull;}};
    mix(&request.model,sizeof(request.model));mix(&a.catalog,sizeof(a.catalog));
    for(UINT i=0;i<submission.count;++i) {
        mix(request.node_to_world[i].data(),sizeof(float)*16);mix(&request.sway_flags[i],sizeof(std::uint32_t));
        const auto* part=submission.parts[i];
        if(part && part->source) {
            mix(&part->source->owned_texture.identity,sizeof(part->source->owned_texture.identity));
            mix(&part->source->owned_texture.generation,sizeof(part->source->owned_texture.generation));
            const auto* resource=part->source->owned_texture.resource;mix(&resource,sizeof(resource));
            mix(&part->values.flags,sizeof(part->values.flags));mix(&part->values.alpha,sizeof(part->values.alpha));
        }
    }
    mix(request.sway_root.data(),sizeof(float)*3);mix(&request.contact_height,sizeof(float));
    const auto existing=a.casters.find(request.object);
    if(existing!=a.casters.end() && existing->second.signature==signature){request.registered=true;return S_OK;}
    release_active_caster(request.object);
    ActiveStaticCaster caster;caster.signature=signature;
    const auto first=native_pending_draws_.size();
    bool queued=false;
    const HRESULT hr=queue_active_static_parts(request,frame,caster.handles,true,nullptr,queued);
    if(FAILED(hr) || !queued){++a.refused;return hr;}
    for(std::size_t k=first;k<native_pending_draws_.size();++k) {
        const auto record=native_pending_draws_[k].record;
        --native_instance_records_[record].command.mesh->pending_references;
        caster.records.push_back(record);
    }
    native_pending_draws_.resize(first);
    a.casters[request.object]=std::move(caster);++a.caster_revision;
    request.registered=true;
    return S_OK;
} catch(...) {active_statics_note_refusal(ObjectSpaceRefusal::exception);return E_OUTOFMEMORY;}
}
