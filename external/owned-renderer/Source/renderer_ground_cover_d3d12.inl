// Native D3D12 ground cover. Included after the shared shader table, mesh
// builders and CPU placement bounds in renderer_ground_cover.cpp.
// No D3D11 resource or context participates in this path.
// Every map runs the same cover: the active map is map 0 and each neighbour
// map one more (GcNeighbour::cover12), with its own grids, origin, window,
// density, zone, fog band and pool region, through the same programs.

HRESULT Renderer::ensure_ground_cover_d3d12() {
    auto& g=gc12_;
    if(g.programs.empty())append_ground_cover_shader_programs(g.programs);
    if(g.programs.size()!=gp_count)return E_FAIL;
    HRESULT hr=S_OK;
    const auto upload=[&](D3D12BufferRef& buffer,const void* data,UINT bytes,UINT stride,UINT flags=0)->HRESULT {
        if(buffer)return S_OK;
        D3D12BufferRef created;
        HRESULT result=d3d12_->create_buffer(bytes,stride,flags,created);
        if(SUCCEEDED(result)&&data)result=d3d12_->write_buffer(*created,0,data,bytes);
        if(SUCCEEDED(result))buffer=std::move(created);
        return result;
    };
    if(!g.vertices||!g.indices){
        const auto meshes=build_meshes();
        std::vector<GcVertex> vertices;std::vector<UINT> indices;
        for(UINT m=0;m<gm_count;++m){
            const UINT base=UINT(vertices.size());gc_mesh_first_vertex_[m]=base;
            gc_mesh_first_index_[m]=UINT(indices.size());gc_mesh_index_count_[m]=UINT(meshes[m].i.size());
            vertices.insert(vertices.end(),meshes[m].v.begin(),meshes[m].v.end());
            for(UINT index:meshes[m].i)indices.push_back(base+index);
            gc_args_template_[m]={gc_mesh_index_count_[m],0,gc_mesh_first_index_[m],0,0};
            // gcCullCS's bound: the farthest vertex from the root and the largest bend weight.
            float radius=0,bend=0;
            for(const auto& v:meshes[m].v){
                radius=std::max(radius,std::sqrt(v.p[0]*v.p[0]+v.p[1]*v.p[1]+v.p[2]*v.p[2]));bend=std::max(bend,std::abs(v.ex[1]));
            }
            // A hosted kind has no vertices of its own: twice its models' farthest
            // vertex (a rebound copy follows its host's complection).
            if(hosted_mesh(m))radius=2*hosted_kind_radius(m-gm_first_hosted);
            g.cull_mesh[m*2]=radius;g.cull_mesh[m*2+1]=bend;
        }
        if(FAILED(hr=upload(g.vertices,vertices.data(),UINT(vertices.size()*sizeof(GcVertex)),sizeof(GcVertex)))||
           FAILED(hr=upload(g.indices,indices.data(),UINT(indices.size()*4),0)))return hr;
        // The cave lichen colonies' art, with the meshes.
        UINT w=0,h=0;std::vector<std::vector<std::uint8_t>> levels;
        if(cave_lichen_art(w,h,levels)){
            D3D12TextureRef art;
            if(SUCCEEDED(d3d12_->create_texture(w,h,DXGI_FORMAT_BC1_UNORM,art,UINT(levels.size())))){
                bool ok=true;
                for(UINT level=0;level<levels.size()&&ok;++level){
                    const UINT lw=(std::max)(4u,w>>level),lh=(std::max)(4u,h>>level);
                    ok=SUCCEEDED(d3d12_->write_texture(*art,RECT{0,0,LONG(lw),LONG(lh)},levels[level].data(),(lw/4)*8,level));
                }
                if(ok)g.cave_art=std::move(art);
            }
        }
    }
    const auto& rules=rule_table();
    if(FAILED(hr=upload(g.rules,rules.data(),UINT(sizeof(rules)),sizeof(GroundCoverRule))))return hr;
    if(!g.rule_class){
        g.classes={};for(auto& list:gc_class_rules_)list.clear();
        std::vector<UINT> rule_class(rules.size());
        std::array<unsigned,gm_count> materials{};
        for(UINT r=0;r<rules.size();++r){
            const unsigned needs=place_features(rules[r]);unsigned c=0;
            while(c<kPlaceClassCount&&(needs&~kPlaceClasses[c].features))++c;
            if(c==kPlaceClassCount)return E_FAIL; // a rule no placement program serves
            auto& values=g.classes[c];values[4+values[0]++]=r;values[1]=c;
            gc_class_rules_[c].push_back(r);rule_class[r]=c;
            const unsigned bit=1u<<(UINT(rules[r].material)&15u);
            materials[rules[r].mesh]|=bit;materials[gc_shadow_far_mesh(rules[r].mesh)]|=bit;
        }
        for(UINT m=0;m<gm_count;++m){
            unsigned kind=0;while(kind<kGcPixelKinds&&(materials[m]&~kPixelKindMaterials[kind]))++kind;
            if(kind==kGcPixelKinds)return E_FAIL;
            gc_mesh_kind_[m]=std::uint8_t(hosted_mesh(m)?5u:kind); // hosted plants: plant cards, no rules
        }
        if(FAILED(hr=upload(g.rule_class,rule_class.data(),UINT(rule_class.size()*4),4)))return hr;
    }
    constexpr UINT writable=kD3D12BufferUnordered,indirect=writable|kD3D12BufferIndirect;
    // The counts and overflow buffers hold one region per map (render_ground_cover_d3d12).
    if(FAILED(hr=upload(g.cell_counts,nullptr,kPlaceClassCount*4+8,0,writable))||
       FAILED(hr=upload(g.rounds,nullptr,kPlaceClassCount*kGcMaxRounds*16,0,indirect))||
       FAILED(hr=upload(g.stage_counts[0],nullptr,kPlaceClassCount*kGcMaxRounds*16,0,indirect))||
       FAILED(hr=upload(g.stage_counts[1],nullptr,kPlaceClassCount*kGcMaxRounds*16,0,indirect))||
       FAILED(hr=upload(g.work,nullptr,(4+gm_count)*4,0,indirect))||
       FAILED(hr=upload(g.args,gc_args_template_.data(),UINT(sizeof(gc_args_template_)),0,indirect))||
       FAILED(hr=upload(g.visible_args,gc_args_template_.data(),UINT(sizeof(gc_args_template_)),0,indirect)))return hr;
    return S_OK;
}

// One map's immutable cover grids, tile atlas cells, fields and (unpainted)
// colour map: the active map's from the shared ambient grids, a neighbour's
// from its stream inputs at adoption.
HRESULT Renderer::build_ground_cover_map_d3d12(GcMapD3D12& map,UINT tx,UINT ty,UINT color_texels,
    const std::vector<std::uint8_t>& area,const std::vector<float>& height,const std::vector<float>& ground,
    const std::vector<std::uint16_t>& types,const std::vector<std::uint8_t>& base_types,const std::vector<GcTileGpu>& tiles,
    const std::vector<std::uint8_t>& used,const std::vector<std::array<std::uint8_t,4>>& trees) {
    const std::size_t t=std::size_t(tx)*ty,v=std::size_t(tx*2+1)*(ty*2+1);
    if(!t||area.size()!=t||types.size()!=t||base_types.size()!=t||trees.size()!=t||height.size()!=v||ground.size()!=v||tiles.empty())
        return E_INVALIDARG;
    const UINT vx=tx*2+1,vy=ty*2+1;
    const auto texture=[&](D3D12TextureRef& out,UINT w,UINT h,DXGI_FORMAT format,const void* data,UINT pitch)->HRESULT {
        HRESULT result=d3d12_->create_texture(w,h,format,out);
        return FAILED(result)?result:d3d12_->write_texture(*out,RECT{0,0,LONG(w),LONG(h)},data,pitch);
    };
    const auto sea=ground_cover_sea_mask(area,tx,ty);
    map.width=tx;map.height_tiles=ty;map.color_texels=color_texels;map.paint=used;
    HRESULT hr=S_OK;
    if(FAILED(hr=texture(map.area,tx,ty,DXGI_FORMAT_R8_UINT,area.data(),tx))||
       FAILED(hr=texture(map.height,vx,vy,DXGI_FORMAT_R32_FLOAT,height.data(),vx*4))||
       FAILED(hr=texture(map.ground,vx,vy,DXGI_FORMAT_R32_FLOAT,ground.data(),vx*4))||
       FAILED(hr=texture(map.type,tx,ty,DXGI_FORMAT_R16_UINT,types.data(),tx*2))||
       FAILED(hr=texture(map.sea,tx,ty,DXGI_FORMAT_R8_UNORM,sea.data(),tx))||
       FAILED(hr=texture(map.trees,tx,ty,DXGI_FORMAT_R8G8B8A8_UNORM,trees.data(),tx*4)))return hr;
    if(FAILED(hr=d3d12_->create_buffer(tiles.size()*sizeof(GcTileGpu),sizeof(GcTileGpu),0,map.tiles))||
       FAILED(hr=d3d12_->write_buffer(*map.tiles,0,tiles.data(),tiles.size()*sizeof(GcTileGpu)))||
       FAILED(hr=d3d12_->create_storage_texture(tx*2,ty*2,0,DXGI_FORMAT_R8G8B8A8_UNORM,map.fields)))return hr;
    const UINT cw=tx*color_texels,ch=ty*color_texels;UINT mips=1;for(UINT n=std::max(cw,ch);n>1;n>>=1)++mips;
    if(FAILED(hr=d3d12_->create_storage_texture(cw,ch,0,DXGI_FORMAT_R8G8B8A8_UNORM,map.color,mips)))return hr;
    const std::vector<UINT> zero(std::size_t(cw)*ch,0);
    if(FAILED(hr=d3d12_->write_texture(*map.color,RECT{0,0,LONG(cw),LONG(ch)},zero.data(),cw*4)))return hr;
    map.tile_classes.resize(t);
    for(std::size_t k=0;k<t;++k){
        UINT tree=0;for(UINT c=0;c<4;++c)if(trees[k][c])tree|=1u<<c;
        map.tile_classes[k]=UINT(area[k])|(UINT(base_types[k])<<8)|(tree<<16);
    }
    return S_OK;
}

HRESULT Renderer::prepare_ground_cover_map_d3d12(GcMapD3D12& map) {
    auto& g=gc12_;HRESULT hr=S_OK;
    D3D11_SAMPLER_DESC clamp{};clamp.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    clamp.AddressU=clamp.AddressV=clamp.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;clamp.MaxLOD=D3D11_FLOAT32_MAX;
    if(!map.fields_ready){
        GroundCoverConstants c{};c.grid[0]=float(map.width);c.grid[1]=float(map.height_tiles);
        d3d12_->clear_slots();d3d12_->set_constants(11,&c,sizeof(c));
        d3d12_->set_resource(29,map.area.get());d3d12_->set_resource(30,map.height.get());
        d3d12_->set_resource(31,map.ground.get());d3d12_->set_resource(41,map.sea.get());d3d12_->set_resource(43,map.type.get());
        d3d12_->set_unordered(3,map.fields.get());
        hr=d3d12_->dispatch(g.programs[gp_fields],dispatch_groups(map.width*2,8),dispatch_groups(map.height_tiles*2,8),1);
        d3d12_->clear_slots();if(hr!=S_OK)return hr;
        map.fields_ready=true;
    }
    bool painted=false;
    for(UINT material=0;material<map.paint.size();++material){
        if(map.paint[material]!=1)continue;
        D3D12TextureRef atlas;
        if(!map.atlases.empty()){if(material<map.atlases.size())atlas=map.atlases[material];}
        else {
            std::shared_ptr<const OwnedVisualSurface> surface;
            if(bridge_terrain_atlas(material,surface)&&surface){
                // Owned terrain can upload through the surface resolver without
                // populating the legacy terrain-page cache. Resolve the captured
                // generation directly, as the D3D11 paint path does.
                NativeOwnedTextureView view{};
                view.identity=surface->id;view.generation=surface->generation;view.surface=surface.get();
                std::uint64_t backing=0;
                hr=get_native_owned_texture_d3d12(view,atlas,backing,nullptr,true);
                if(FAILED(hr))return hr;
            }
        }
        if(!atlas)continue; // The source's atlas has not been drawn/uploaded yet.
        const UINT values[4]{material,map.width,map.height_tiles,map.color_texels};
        d3d12_->clear_slots();d3d12_->set_constants(13,values,sizeof(values));
        d3d12_->set_resource(38,map.tiles.get());d3d12_->set_resource(39,atlas.get());d3d12_->set_sampler(8,clamp);
        d3d12_->set_unordered(3,map.color.get());
        hr=d3d12_->dispatch(g.programs[gp_paint],dispatch_groups(map.width*map.color_texels,8),dispatch_groups(map.height_tiles*map.color_texels,8),1);
        d3d12_->clear_slots();if(hr!=S_OK)return hr;
        map.paint[material]=2;painted=true;map.color_ready=false;
    }
    if(painted||(!map.color_ready&&std::find(map.paint.begin(),map.paint.end(),2)!=map.paint.end())){
        hr=d3d12_->generate_mips(*map.color,d3d12_mip_program());
        if(hr!=S_OK)return hr;
        map.color_ready=true;gc_painted_frame_=native_frame_index_;
    }
    // Every material painted: the paint and field inputs are done.
    if(std::find(map.paint.begin(),map.paint.end(),1)==map.paint.end()){map.atlases.clear();map.tiles.reset();map.sea.reset();}
    return map.color_ready?S_OK:S_FALSE;
}

const Renderer::GcMapD3D12* Renderer::contact_cover_map_d3d12(UINT contact_map) const noexcept {
    if(!gc12_.vertices||scene_composited_)return nullptr;
    // The active map: once its cover drew this frame (native_batch_constants'
    // active_contact rule, as on D3D11).
    if(contact_map==0)return gc12_.map.color_ready&&gc12_.map.generation==ambient_map_generation_&&
        gc_drawn_frame_==native_frame_index_?&gc12_.map:nullptr;
    for(const auto& n:gc_neighbours_)if(n.contact_map==contact_map)return n.cover12.color_ready?&n.cover12:nullptr;
    return nullptr;
}

HRESULT Renderer::render_ground_cover_d3d12(const FixedState& frame,const std::array<float,16>& world_to_clip) noexcept try {
    const int level=configured_integer(debug_overlay_.config(),"ground_cover",2);
    if(level<=0){
        if(gc12_.vertices){
            // Neighbour contact/blend art shares gc_neighbours_; the cover
            // switch owns only these cover resources, never that terrain art.
            gc12_={};for(auto& list:gc_class_rules_)list.clear();
            gc_capacity_.clear();gc_base_.clear();gc_zone_uploaded_.clear();gc_level_=-1;
            gc_object_mask_current_=false;gc_object_drawn_.clear();gc_object_width_=gc_object_height_=0;
            gc_trail_width_=gc_trail_height_=gc_trail_index_=0;gc_trail_recovery_=0;gc_trail_seconds_=-1;
            gc_snow_valid_=false;gc_snow_seconds_=-1;gc_snow_index_=0;gc_snow_tiles_={};gc_snow_full_writes_=0;snow_terrain12_.reset();
            gc_snow_tracks_.clear();gc_snow_history_.clear();gc_shadow_ready_=false;
        }
        return S_OK;
    }
    if(scene_composited_||ambient_terrain_frame_!=native_frame_index_||gc_drawn_frame_==native_frame_index_)return S_OK;
    const auto fail=[&](const char* reason,HRESULT hr)->HRESULT {
        d3d12_->clear_slots();d3d12_->set_index_buffer(nullptr);bind_world_targets_d3d12();
        char detail[384]{};std::snprintf(detail,sizeof(detail),"operation=ground_cover stage=%s hr=0x%08lx frame=%llu",reason,static_cast<unsigned long>(hr),static_cast<unsigned long long>(native_frame_index_));
        if(FAILED(hr))bridge_report_event("d3d12_failed",detail);
        d3d12_->omit(reason,true);return FAILED(hr)?hr:S_OK;
    };
    HRESULT hr=bind_world_d3d12();if(FAILED(hr))return hr;
    transition_profile_pass("ground_cover");
    if((hr=ensure_ground_cover_d3d12())!=S_OK)return fail("ground cover pipeline/resources",hr);
    AmbientWorldSource inputs;
    if(!bridge_ambient_world_source(inputs)||!inputs.terrain||!inputs.map)return fail("ground cover terrain source",S_FALSE);
    const auto environment=bridge_world_fog_snapshot().environment;
    if(!environment.valid||!rebuild_ambient_particle_map(inputs,environment))return fail("ground cover terrain/environment grids",S_FALSE);
    auto& g=gc12_;
    // The sun casters recorded by the previous placement were drawn by this
    // frame's shadow pass; this frame records its own (or none).
    for(auto& pass:g.casters){pass.frame.clear();pass.ground.reset();}
    gc_shadow_ready_=false;
    const auto texture=[&](D3D12TextureRef& out,UINT w,UINT h,DXGI_FORMAT format,const void* data,UINT pitch)->HRESULT {
        HRESULT result=d3d12_->create_texture(w,h,format,out);
        return FAILED(result)?result:d3d12_->write_texture(*out,RECT{0,0,LONG(w),LONG(h)},data,pitch);
    };
    // Same immutable terrain and authored liquid-height revision as the shared
    // ambient grids. A revision rebuilds the dependent shore fields and paint.
    const UINT tx=ambient_tiles_x_,ty=ambient_tiles_y_,vx=tx*2+1,vy=ty*2+1;
    const bool families=poll_terrain_tile_families();
    // Soft sand families take snow/sand trails (advance_snow_trails), as on D3D11.
    if(families&&gc_sand_generation_!=ambient_map_generation_){
        gc_sand_generation_=ambient_map_generation_;
        const auto soft=terrain_tile_soft_sand_corners(tile_families_);
        gc_soft_sand_=std::any_of(soft.begin(),soft.end(),[](std::uint8_t m){return m!=0;});
    }
    const UINT64 map_key=gc_hash(gc_hash(ambient_map_generation_,ambient_liquid_level_updates_),families?tile_families_generation_:UINT64_MAX);
    if(map_key!=g.map_key){
        std::vector<std::uint8_t> used;
        const auto tiles=ground_cover_tiles(*inputs.terrain,tx,ty,used);
        const auto trees=ground_cover_trees(*inputs.map,tx,ty);
        const auto slots=ambient::terrain_base_slots(*inputs.terrain);
        const auto base=ambient::terrain_base_types(slots,inputs.terrain->tile_types,{});
        const auto types=families?ambient::terrain_cover_types(slots,ground_cover_corner_lanes(tiles),inputs.terrain->tile_types,
            terrain_tile_sand_slots(tile_families_),terrain_tile_bare_corners(tile_families_),terrain_tile_soft_sand_corners(tile_families_)):
            std::vector<std::uint16_t>(base.begin(),base.end());
        if(types.size()!=std::size_t(tx)*ty||ambient_liquid_level_.ground.size()!=std::size_t(vx)*vy)return fail("ground cover grid dimensions",E_FAIL);
        GcMapD3D12 map;
        if(FAILED(hr=build_ground_cover_map_d3d12(map,tx,ty,4,ambient_area_,ambient_height_,ambient_liquid_level_.ground,types,base,tiles,used,trees)))
            return fail("ground cover grid upload",hr);
        map.generation=ambient_map_generation_;
        // Atlas identity and format conversion follow the existing owned
        // texture ingress, including the authored mip chain.
        const auto art=[&](const char* name,D3D12TextureRef& out)->HRESULT {
            for(const auto& [key,asset]:inputs.map->textures){
                if(!asset||!art_texture_matches(lower_name(key),name,asset->width,asset->height)||!asset->source||asset->mips.empty()||asset->mips.size()>16)continue;
                std::array<NativeOwnedTextureMip,16> levels{};
                for(UINT n=0;n<asset->mips.size();++n){const auto& m=asset->mips[n];
                    if(m.byte_offset>asset->source->size()||m.bytes>asset->source->size()-m.byte_offset)return E_INVALIDARG;
                    levels[n]={asset->source->data()+m.byte_offset,m.width,m.height,m.bytes};}
                NativeOwnedTextureView view{};view.value_source=UINT(asset->value_source)+1;
                view.identity=asset->value_source==TextureValueSource::archive?1:asset->value_serial;view.generation=1;
                view.resource=asset->value_source==TextureValueSource::archive?&asset->identity:nullptr;
                switch(asset->format){case MmpFormat::dxt1:view.format=NativeOwnedTextureFormat::dxt1;break;
                case MmpFormat::dxt3:view.format=NativeOwnedTextureFormat::dxt3;break;case MmpFormat::dxt5:view.format=NativeOwnedTextureFormat::dxt5;break;
                case MmpFormat::rgb565:view.format=NativeOwnedTextureFormat::rgb565;break;case MmpFormat::argb1555:view.format=NativeOwnedTextureFormat::argb1555;break;
                case MmpFormat::argb4444:view.format=NativeOwnedTextureFormat::argb4444;break;default:view.format=NativeOwnedTextureFormat::argb8888;break;}
                view.mips=levels.data();view.mip_count=UINT(asset->mips.size());view.opaque_mips=asset->opaque_mips;UINT64 backing=0;
                return get_native_owned_texture_d3d12(view,out,backing,nullptr,true);
            }
            return S_FALSE;
        };
        if(FAILED(hr=art("tree02",map.bark))||FAILED(hr=art("stone02",map.stone)))return fail("ground cover material atlas upload",hr);
        for(UINT k=0;k<4;++k)if(FAILED(hr=art(desert_plants::kPlantAtlas[k+1],map.plant[k])))return fail("ground cover plant atlas upload",hr);
        g.map=std::move(map);g.map_key=map_key;
        // This map's cover built from its tile families: the D3D11 point
        // (update_ground_cover_maps) that ends the load reveal's wait.
        if(families)gc_map_generation_=ambient_map_generation_;
        g.readbacks={};g.read_head=g.read_pending=0;g.demand_state=0;
    }
    if((hr=prepare_ground_cover_map_d3d12(g.map))!=S_OK)return fail("ground cover fields/terrain paint not ready",hr);
    update_ground_cover_terrain2_d3d12(); // the active map's terrain 2.0 classes and colour (while terrain2 is on)
    if(FAILED(hr=upload_native_point_lights_d3d12())||FAILED(hr=bind_world_fog()))return fail("ground cover light/fog inputs",hr);

    float inverse[16]{};
    if(!ambient_occlusion_invert(world_to_clip.data(),inverse)||std::abs(inverse[14])<1e-12f||inputs.terrain->sectors.empty())return fail("ground cover world camera",S_FALSE);
    const float eye[3]{inverse[2]/inverse[14],inverse[6]/inverse[14],inverse[10]/inverse[14]};
    float lowest=inputs.terrain->sectors.front().bounds.minimum[2],highest=inputs.terrain->sectors.front().bounds.maximum[2];
    for(const auto& sector:inputs.terrain->sectors){lowest=std::min(lowest,sector.bounds.minimum[2]);highest=std::max(highest,sector.bounds.maximum[2]);}
    const float cover_limit=float(configured_integer(debug_overlay_.config(),"ground_cover_distance",200));
    const Quality quality=quality_level(level);

    // The maps this frame: pool region p (0 the active map, neighbour i at
    // i + 1), its cover map, map-local ground bounds, world origin and the
    // cells within reach of the eye (and, on neighbours, not in full seam fog).
    struct MapPass {
        UINT region=0;GcMapD3D12* map=nullptr;const GcNeighbour* neighbour=nullptr;
        std::array<float,3> origin{};float lowest=0,highest=0,reach=0,density=1;
        CellWindow window;std::array<UINT,kPlaceClassCount> bounds{};
    };
    const auto within_reach=[&](MapPass& m)->bool {
        const float nx=float(m.map->width)*2,ny=float(m.map->height_tiles)*2;
        const float low=m.lowest+m.origin[2],high=m.highest+m.origin[2];
        float corner_reach=0;
        for(UINT k=0;k<4;++k){
            const float dx=m.origin[0]+(k&1?nx:0)-eye[0],dy=m.origin[1]+(k&2?ny:0)-eye[1];
            const float dz=std::max(std::abs(low-eye[2]),std::abs(high-eye[2]));
            corner_reach=std::max(corner_reach,std::sqrt(dx*dx+dy*dy+dz*dz));
        }
        m.reach=std::min({visible_ground_reach(inverse,eye,low,high,INFINITY,INFINITY),corner_reach,cover_limit});
        return neighbour_cover_window(eye,m.origin,m.reach,nx,ny,kCellSize,m.window);
    };
    std::vector<MapPass> maps;
    {
        MapPass active;active.map=&g.map;active.lowest=lowest;active.highest=highest;
        if(within_reach(active))maps.push_back(active);
    }
    for(std::size_t i=0;i<gc_neighbours_.size();++i){
        auto& n=gc_neighbours_[i];
        if(!n.cover12.width)continue;
        MapPass m;m.region=UINT(i+1);m.map=&n.cover12;m.neighbour=&n;m.origin=n.origin;m.lowest=n.lowest;m.highest=n.highest;
        m.density=kNeighbourCoverDensity;
        // Fields and paint once, in or out of reach (its contact colour reads
        // them). Unpainted, it places nothing (S_FALSE); a failed one is named.
        const HRESULT prepared=prepare_ground_cover_map_d3d12(n.cover12);
        if(prepared!=S_OK){if(FAILED(prepared))d3d12_->omit("neighbour ground cover fields/paint",false);continue;}
        if(!within_reach(m)||!neighbour_cover_fog_window(n.origin,kCellSize,neighbour_fog_world(),m.window))continue;
        maps.push_back(m);
    }

    // One pool region per map, sized by its own demand readback.
    const UINT passes=UINT(1+gc_neighbours_.size());
    if(gc_capacity_.size()<passes){gc_capacity_.resize(passes);gc_base_.resize(passes);}
    if(level!=gc_level_){gc_level_=level;for(auto& capacity:gc_capacity_)capacity.fill(0);g.instances.reset();g.pool_capacity=0;g.readbacks={};g.read_head=g.read_pending=0;g.demand_state=0;}
    // Hosted plants: this frame's host records; their meshes' pools hold every host.
    const bool hosted=gc_prepare_hosted();
    if(hosted&&gc_hosted_uploaded_!=gc_hosted_frame_.revision){
        g.hosted.reset();g.hosted_vertices.reset();g.hosted_indices.reset();gc_hosted_count_=0;
        // Tried once a revision: a failed upload stays omitted (named once) until the
        // records change, never rebuilt from the file every frame.
        gc_hosted_uploaded_=gc_hosted_frame_.revision;
        HostedUpload up;
        if(!build_hosted_upload(gc_hosted_frame_,up)||up.records.empty())return fail("hosted plant upload",E_FAIL);
        D3D12BufferRef records,vertices,indices;
        if(FAILED(hr=d3d12_->create_buffer(UINT64(up.records.size())*sizeof(HostedPlantGpu),sizeof(HostedPlantGpu),0,records))||
           FAILED(hr=d3d12_->write_buffer(*records,0,up.records.data(),up.records.size()*sizeof(HostedPlantGpu)))||
           FAILED(hr=d3d12_->create_buffer(UINT64(up.vertices.size())*sizeof(GcVertex),sizeof(GcVertex),0,vertices))||
           FAILED(hr=d3d12_->write_buffer(*vertices,0,up.vertices.data(),up.vertices.size()*sizeof(GcVertex)))||
           FAILED(hr=d3d12_->create_buffer(UINT64(up.indices.size())*4,4,0,indices))||
           FAILED(hr=d3d12_->write_buffer(*indices,0,up.indices.data(),up.indices.size()*4)))return fail("hosted plant buffers",hr);
        g.hosted_art.reset();
        if(!up.textures.empty()){
            std::vector<std::vector<std::uint8_t>> levels;UINT w=0,h=0,mips=0;
            if(!stack_hosted_textures(up.textures,levels,w,h,mips))return fail("hosted plant textures",E_FAIL);
            D3D12TextureRef art;
            if(FAILED(hr=d3d12_->create_texture(w,h,DXGI_FORMAT_BC1_UNORM,art,mips)))return fail("hosted plant textures",hr);
            for(UINT level=0;level<mips;++level){
                const UINT lw=(std::max)(4u,w>>level),lh=(std::max)(4u,h>>level);
                if(FAILED(hr=d3d12_->write_texture(*art,RECT{0,0,LONG(lw),LONG(lh)},levels[level].data(),(lw/4)*8,level)))return fail("hosted plant textures",hr);
            }
            g.hosted_art=std::move(art);
        }
        g.hosted=std::move(records);g.hosted_vertices=std::move(vertices);g.hosted_indices=std::move(indices);
        gc_hosted_count_=UINT(up.records.size());gc_hosted_uploaded_=gc_hosted_frame_.revision;log_hosted_upload(up);
    }
    if(passes!=g.passes){
        // Counts still in flight for the previous regions are dropped.
        D3D12BufferRef counts,overflow;
        if(FAILED(hr=d3d12_->create_buffer(UINT64(passes)*gm_count*4,0,kD3D12BufferUnordered,counts))||
           FAILED(hr=d3d12_->create_buffer(UINT64(passes)*gm_count*8,0,kD3D12BufferUnordered,overflow)))return fail("ground cover count buffers",hr);
        d3d12_->clear_buffer_uint(*overflow,0);
        g.counts=std::move(counts);g.overflow=std::move(overflow);g.passes=passes;
        g.readbacks={};g.read_head=g.read_pending=0;g.demand_state=0;gc_grow_=true;
    }
    while(g.read_pending){
        std::vector<std::uint8_t> bytes;hr=d3d12_->poll_readback(g.readbacks[g.read_head],bytes);
        if(hr==S_FALSE)break;
        // A copy the executor no longer holds (its readback list dropped the
        // oldest unpolled one) or a short one: the counts in flight are
        // dropped and the next placement requests new ones, as a region change does.
        if(FAILED(hr)||bytes.size()!=std::size_t(passes)*gm_count*4){
            char detail[96];std::snprintf(detail,sizeof(detail),"hr=%08lx bytes=%zu; %u pending dropped, requested again",
                static_cast<unsigned long>(FAILED(hr)?hr:E_FAIL),bytes.size(),g.read_pending);
            bridge_append_probe_event("ground_cover_readback_lost",detail);
            g.readbacks={};g.read_head=g.read_pending=0;break;
        }
        {std::array<std::uint32_t,gm_count> active{};std::memcpy(active.data(),bytes.data(),gm_count*4);log_hosted_demand(active.data());}
        std::uint64_t total_demand=0;
        for(UINT p=0;p<passes;++p)for(UINT m=0;m<gm_count;++m){
            UINT demand;std::memcpy(&demand,bytes.data()+(p*gm_count+m)*4,4);auto& capacity=gc_capacity_[p][m];
            total_demand+=demand;
            if(demand>capacity-capacity*3/10){capacity=demand+demand/2+64;gc_grow_=true;}
        }
        // Demand is GPU placement output, not a count of submitted indirect draws.
        // Log transitions and one reminder per 600 frames, bounded per renderer.
        if(g.demand_state!=(total_demand?2u:1u)||native_frame_index_-g.demand_frame>=600){
            char detail[384]{};std::snprintf(detail,sizeof(detail),"frame=%llu maps=%u visible_maps=%u demand=%llu pool=%u reason=%s",
                static_cast<unsigned long long>(native_frame_index_),passes,UINT(maps.size()),static_cast<unsigned long long>(total_demand),g.pool_capacity,
                total_demand?"placement_produced_instances":"placement_produced_zero_instances");
            bridge_report_event("d3d12_ground_cover_placement",detail);
            g.demand_state=total_demand?2u:1u;g.demand_frame=native_frame_index_;
        }
        g.read_head=(g.read_head+1)%UINT(g.readbacks.size());--g.read_pending;
    }
    if(!g.instances||gc_grow_){
        if(!g.instances)for(const auto& rule:rule_table()){
            const float band=.5f*(rule.lod_near+rule.lod_far)*quality.distance;
            gc_capacity_[0][rule.mesh]=std::max(gc_capacity_[0][rule.mesh],UINT(rule.density*quality.density*3.14159f*band*band*.3f)+64);
        }
        // A neighbour region without a size yet starts from the active map's
        // (its tufts go to the far meshes, so a far mesh takes its near mesh's
        // share too); its own demand corrects it within a few frames.
        for(UINT p=1;p<passes;++p)for(UINT m=0;m<gm_count;++m){
            if(gc_capacity_[p][m]||(kGcKeptByNeighbours>>m)&1u)continue;
            const UINT near_mesh=gc_shadow_near_mesh(m);
            gc_capacity_[p][m]=gc_capacity_[0][m]+(near_mesh!=m?gc_capacity_[0][near_mesh]:0)+64;
        }
        UINT total=0;
        for(UINT p=0;p<passes;++p){
            UINT pass_total=0;
            for(UINT m=0;m<gm_count;++m){gc_base_[p][m]=total+pass_total;pass_total+=gc_capacity_[p][m];}
            // One map's instances are one per-instance dispatch.
            if(pass_total>kGcStageLimit)return fail("ground cover pool exceeds one dispatch",E_INVALIDARG);
            total+=pass_total;
        }
        if(!g.instances||total>g.pool_capacity){
            const UINT capacity=total+total/4;
            D3D12BufferRef instances,shade,draw,casters,visible;
            // Sun caster lists (gcCasterListCS): 4 bytes per pool slot and close map.
            // Visible draw lists (gcCullCS): 4 bytes per pool slot.
            if(FAILED(hr=d3d12_->create_buffer(UINT64(capacity)*64,64,kD3D12BufferUnordered,instances))||
               FAILED(hr=d3d12_->create_buffer(UINT64(capacity)*16,16,kD3D12BufferUnordered,shade))||
               FAILED(hr=d3d12_->create_buffer(UINT64(capacity)*32,32,kD3D12BufferUnordered,draw))||
               FAILED(hr=d3d12_->create_buffer(UINT64(capacity)*12,4,kD3D12BufferUnordered,casters))||
               FAILED(hr=d3d12_->create_buffer(UINT64(capacity)*4,4,kD3D12BufferUnordered,visible)))return fail("ground cover pool allocation",hr);
            g.instances=std::move(instances);g.shade=std::move(shade);g.draw_inputs=std::move(draw);g.caster_slots=std::move(casters);
            g.visible=std::move(visible);g.pool_capacity=capacity;++g.pool_generation;
        }
        gc_grow_=false;
    }
    // The sun caster lists' draw arguments per pass, close map and mesh: map 0
    // draws every tuft with its near mesh, maps 1 and 2 with its far mesh; the
    // counts start at zero each frame.
    if(passes!=g.caster_passes){
        std::vector<std::array<UINT,5>> arguments(std::size_t(passes)*3*gm_count);
        for(std::size_t k=0;k<arguments.size();++k){
            const UINT mesh=UINT(k%gm_count),close=UINT(k/gm_count%3);
            arguments[k]=gc_args_template_[close==0?gc_shadow_near_mesh(mesh):gc_shadow_far_mesh(mesh)];
        }
        D3D12BufferRef buffer;
        if(FAILED(hr=d3d12_->create_buffer(UINT64(arguments.size())*20,0,kD3D12BufferUnordered|kD3D12BufferIndirect,buffer)))
            return fail("ground cover caster arguments",hr);
        g.caster_args=std::move(buffer);g.caster_template=std::move(arguments);g.caster_passes=passes;
    }
    if(g.casters.size()<passes)g.casters.resize(passes);

    GroundCoverConstants c{};
    std::memcpy(c.clip,world_to_clip.data(),sizeof(c.clip));jitter_clip(c.clip);
    const auto plane=[&](UINT index,float sign,UINT row){float v[4];
        for(UINT k=0;k<4;++k)v[k]=row==2&&sign==0?world_to_clip[8+k]:world_to_clip[12+k]+sign*world_to_clip[row*4+k];
        const float length=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);for(UINT k=0;k<4;++k)c.planes[index][k]=length>1e-12f?v[k]/length:v[k];};
    plane(0,1,0);plane(1,-1,0);plane(2,1,1);plane(3,-1,1);plane(4,0,2);plane(5,-1,2);
    std::copy(eye,eye+3,c.eye);c.eye[3]=float(std::fmod(environment.world_time*kGroundCoverSecondsPerSegment,16384.0));
    c.window[3]=kCellSize;
    // The active map's tile grid (as D3D11's frame constants): the snow trail
    // stamp reads the map's base types through it (gcSnowGround: authored
    // snow ground takes full depth). Each map's pass sets its own below.
    c.grid[0]=float(tx);c.grid[1]=float(ty);c.grid[2]=float(gm_count);c.grid[3]=float(height_);
    c.quality[0]=quality.distance;
    // Far LOD screen-size thinning (gcFarThin) is off: the far LOD keeps its
    // far_keep density, survivors grown at most grow_max (as before #230).
    c.far_thin[0] = 0.f; c.far_thin[1] = .25f;
    const auto& wind=weather_wind();const auto sway=sway_drive(wind.force,wind.speed,ambient_zone_==ambient::zone_cave||ambient_zone_==ambient::zone_gipat_cave);
    c.wind[0]=wind.direction[0];c.wind[1]=wind.direction[1];c.wind[2]=sway.amplitude;c.wind[3]=sway.travel;
    c.wind2[0]=wind.gust;c.wind2[1]=wind.pulse;c.wind2[2]=terrain_full_sun_?1.f:0.f;
    // Last world frame's sway clock (gcVS's object motion): this one's when the last frame was not the previous one.
    if(gc_motion_clock_frame_!=native_frame_index_){
        gc_motion_clock_[0]=gc_motion_clock_frame_+1u==native_frame_index_?gc_motion_clock_[1]:c.eye[3];gc_motion_clock_[1]=c.eye[3];
        gc_motion_clock_frame_=native_frame_index_;
    }
    c.wind2[3]=gc_motion_clock_[0];
    c.switches[0]=temporal_upscaler()?dlss_jitter_[0]+.5f:0.f; // gcCoverClip's dither frame offset (as nativeLodDiscard)
    c.switches[1]=frame.render[D3DRENDERSTATE_FOGENABLE]?1.f:0.f;c.switches[2]=desert_clump_map(ambient_map_name_)?1.f:0.f;c.switches[3]=float([&]{UINT art=(g.map.bark?1u:0u)|(g.map.stone?2u:0u);for(UINT k=0;k<4;++k)if(g.map.plant[k])art|=4u<<k;return art;}());
    c.view[1]=c.view[2]=kCellSize/float(kTrailTexelsPerCell);
    c.snow_step[3]=snow_cover_;
    const auto& fog=ambient_terrain_state_;
    c.fog_range[0]=fog.fog_min[0];c.fog_range[1]=fog.fog_min[1];c.fog_range[2]=fog.fog_max[0];c.fog_range[3]=fog.fog_max[1];
    c.fog_control[0]=fog.fog_distance;c.fog_control[1]=fog.fog_enabled?1.f:0.f;
    neighbour_fog_constants(neighbour_fog_,c.fog_seam,c.fog_curve,c.fog_span_lo,c.fog_span_hi);
    neighbour_fog_piece_constants(neighbour_fog_,c.fog_piece_info,c.fog_pieces);
    c.fog_color[0]=((ambient_fog_color_>>16)&255)/255.f;c.fog_color[1]=((ambient_fog_color_>>8)&255)/255.f;c.fog_color[2]=(ambient_fog_color_&255)/255.f;
    const float bark[4]{412/512.f,370/512.f,88/512.f,134/512.f},end[4]{435/512.f,8/512.f,65/512.f,67/512.f},stone[4]{0,64/256.f,200/256.f,192/256.f};
    std::memcpy(c.bark_rect,bark,sizeof(bark));std::memcpy(c.end_rect,end,sizeof(end));std::memcpy(c.stone_rect,stone,sizeof(stone));

    // The active map's interaction fields: current posed prop bounds (the
    // retained-scene publication, a world-space mask, never a replay of
    // object draws), shown units, visible game prints and corpses on the same
    // recovering field and authoritative game seconds as D3D11. Pause never
    // advances it. Neighbour maps have none (as on D3D11).
    bool trail_active=false;
    SnowTrailFrame snow; // no field read unless advanced this frame
    const bool active_drawn=!maps.empty()&&maps.front().region==0;
    constexpr float texel=kCellSize/float(kTrailTexelsPerCell);
    D3D12NativeState splat_state;splat_state.raster=fixed_raster_desc(D3DCULL_NONE,false,false);
    splat_state.depth=fixed_depth_desc(D3DZB_FALSE,FALSE,D3DCMP_ALWAYS);
    if(active_drawn){
        const int x0=maps.front().window.x0,y0=maps.front().window.y0,x1=maps.front().window.x1,y1=maps.front().window.y1;
        const float reach=maps.front().reach;
        const UINT64 mask_w=UINT64(x1-x0)*kTrailTexelsPerCell,mask_h=UINT64(y1-y0)*kTrailTexelsPerCell;
        if(mask_w>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION||mask_h>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)
            return fail("ground cover interaction window dimensions",E_INVALIDARG);
        if(!bridge_ground_cover_objects(eye[0],eye[1],reach+kCellSize,gc_object_input_))return fail("ground cover retained object bounds",E_FAIL);
        if(!g.object_mask||mask_w>gc_object_width_||mask_h>gc_object_height_){
            gc_object_width_=std::max(gc_object_width_,UINT(mask_w));gc_object_height_=std::max(gc_object_height_,UINT(mask_h));
            if(FAILED(hr=d3d12_->create_target(gc_object_width_,gc_object_height_,g.object_mask,DXGI_FORMAT_R8_UNORM,DXGI_FORMAT_UNKNOWN)))return fail("ground cover object mask",hr);
            gc_object_mask_current_=false;
        }
        c.object_window[0]=float(x0)*kCellSize;c.object_window[1]=float(y0)*kCellSize;
        c.object_window[2]=1.f/(gc_object_width_*texel);c.object_window[3]=1.f/(gc_object_height_*texel);
        const auto splats=[&](UINT vertex,UINT pixel,D3D12Texture& target,D3D12Buffer* data,UINT count,bool maximum)->HRESULT {
            D3D12NativeState state=splat_state;
            if(maximum){state.custom_blend.custom=1;state.custom_blend.source=state.custom_blend.destination=D3D11_BLEND_ONE;
                state.custom_blend.op=state.custom_blend.op_alpha=D3D11_BLEND_OP_MAX;
                state.custom_blend.write_mask=D3D11_COLOR_WRITE_ENABLE_RED|D3D11_COLOR_WRITE_ENABLE_GREEN;}
            d3d12_->set_target(&target);d3d12_->set_constants(11,&c,sizeof(c));
            if(data)d3d12_->set_resource(45,data);
            const UINT args[4]{count?6u:3u,count?count:1u,0,0};
            HRESULT result=native_d3d12_buffer(g.splat_args,sizeof(args),0,kD3D12BufferIndirect);
            if(SUCCEEDED(result))result=d3d12_->write_buffer(*g.splat_args,0,args,sizeof(args));
            if(SUCCEEDED(result))result=d3d12_->draw_indirect(g.programs[vertex],g.programs[pixel],state,nullptr,*g.splat_args,0);
            return result;
        };
        if(!object_mask_current(gc_object_mask_current_,gc_object_origin_x_,gc_object_origin_y_,gc_object_drawn_,x0,y0,gc_object_input_)){
            const float zero[4]{};d3d12_->clear_texture(*g.object_mask,zero);
            if(!gc_object_input_.empty()){
                const UINT64 bytes=gc_object_input_.size()*sizeof(FootprintSplat);
                if(bytes>UINT_MAX)return fail("ground cover object input dimensions",E_INVALIDARG);
                if(FAILED(hr=native_d3d12_buffer(g.objects,bytes,sizeof(FootprintSplat),0))||
                   FAILED(hr=d3d12_->write_buffer(*g.objects,0,gc_object_input_.data(),bytes)))return fail("ground cover object upload",hr);
                d3d12_->clear_slots();
                if((hr=splats(gp_object_vs,gp_object_ps,*g.object_mask,g.objects.get(),UINT(gc_object_input_.size()),true))!=S_OK)return fail("ground cover object-mask pipeline not ready",hr);
                d3d12_->clear_slots();profiler_.add_draw(1,1);profiler_.add_batch();
            }
            gc_object_drawn_=gc_object_input_;gc_object_origin_x_=x0;gc_object_origin_y_=y0;gc_object_mask_current_=true;
        }
        bridge_ground_cover_units(eye[0],eye[1],reach+kCellSize,gc_unit_input_,nullptr,&gc_unit_trails_);
        if(!bridge_ground_cover_footprints(gc_footprint_input_,&gc_snow_prints_))return fail("ground cover authoritative footprints",E_FAIL);
        bridge_ground_cover_corpses(eye[0],eye[1],reach+kCellSize,gc_footprint_input_);
        for(const auto& unit:gc_unit_input_)gc_footprint_input_.push_back(live_unit_splat(unit[0],unit[1],unit[3]));
        const double seconds=environment.world_time*kGroundCoverSecondsPerSegment;
        const float dt=gc_trail_seconds_>=0?float(std::max(seconds-gc_trail_seconds_,0.0)):0.f;
        const bool clear=gc_trail_recovery_<=0;trail_active=!gc_footprint_input_.empty()||!clear;
        D3D12TextureRef previous=clear?D3D12TextureRef{}:g.trail[gc_trail_index_];
        const int shift_x=clear?0:(x0-gc_trail_origin_x_)*int(kTrailTexelsPerCell),shift_y=clear?0:(y0-gc_trail_origin_y_)*int(kTrailTexelsPerCell);
        const bool grow=trail_active&&(!g.trail[0]||mask_w>gc_trail_width_||mask_h>gc_trail_height_);
        if(grow){
            gc_trail_width_=std::max(gc_trail_width_,UINT(mask_w));gc_trail_height_=std::max(gc_trail_height_,UINT(mask_h));
            D3D12TextureRef a,b,sources;
            if(FAILED(hr=d3d12_->create_target(gc_trail_width_,gc_trail_height_,a,DXGI_FORMAT_R16_UNORM,DXGI_FORMAT_UNKNOWN))||
               FAILED(hr=d3d12_->create_target(gc_trail_width_,gc_trail_height_,b,DXGI_FORMAT_R16_UNORM,DXGI_FORMAT_UNKNOWN))||
               FAILED(hr=d3d12_->create_target(gc_trail_width_,gc_trail_height_,sources,DXGI_FORMAT_R16G16_FLOAT,DXGI_FORMAT_UNKNOWN)))return fail("ground cover trail targets",hr);
            g.trail[0]=std::move(a);g.trail[1]=std::move(b);g.trail_sources=std::move(sources);gc_trail_index_=1;
        }
        c.trail_window[0]=float(x0)*kCellSize;c.trail_window[1]=float(y0)*kCellSize;
        c.trail_window[2]=gc_trail_width_?1.f/(gc_trail_width_*texel):0;c.trail_window[3]=gc_trail_height_?1.f/(gc_trail_height_*texel):0;
        c.trail_step[0]=float(shift_x);c.trail_step[1]=float(shift_y);c.trail_step[2]=dt/kTrailPressSeconds;c.trail_step[3]=dt/kTrailReleaseSeconds;
        c.view[3]=trail_active?1.f:0.f;
        if(trail_active&&(clear||grow||dt>0||shift_x||shift_y)){
            d3d12_->clear_slots();const float zero[4]{};d3d12_->clear_texture(*g.trail_sources,zero);
            if(!gc_footprint_input_.empty()){
                const UINT64 bytes=gc_footprint_input_.size()*sizeof(FootprintSplat);if(bytes>UINT_MAX)return fail("ground cover footprint dimensions",E_INVALIDARG);
                if(FAILED(hr=native_d3d12_buffer(g.footprints,bytes,sizeof(FootprintSplat),0))||
                   FAILED(hr=d3d12_->write_buffer(*g.footprints,0,gc_footprint_input_.data(),bytes)))return fail("ground cover footprint upload",hr);
                d3d12_->set_resource(46,previous.get());
                if((hr=splats(gp_trail_vs,gp_trail_ps,*g.trail_sources,g.footprints.get(),UINT(gc_footprint_input_.size()),true))!=S_OK)return fail("ground cover footprint pipeline not ready",hr);
            }
            const UINT target=gc_trail_index_^1u;d3d12_->clear_slots();d3d12_->set_resource(46,previous.get());d3d12_->set_resource(49,g.trail_sources.get());
            if((hr=splats(gp_trail_full_vs,gp_trail_decay_ps,*g.trail[target],nullptr,0,false))!=S_OK)return fail("ground cover trail recovery pipeline not ready",hr);
            d3d12_->clear_slots();gc_trail_index_=target;gc_trail_origin_x_=x0;gc_trail_origin_y_=y0;
            gc_trail_recovery_=gc_footprint_input_.empty()?std::max(0.f,gc_trail_recovery_-dt):kTrailReleaseSeconds;
            profiler_.add_draw(1+(gc_footprint_input_.empty()?0:1),1+(gc_footprint_input_.empty()?0:1));profiler_.add_batch(1+(gc_footprint_input_.empty()?0:1));
        }
        gc_trail_seconds_=seconds;
        // Snow/sand trails (snow_trails.h; the segments, tiles and window of
        // advance_snow_trails, as on D3D11): the previous field moved and
        // filled in, then this frame's steps MAX-composed over it, into the
        // other ping-pong target.
        snow=advance_snow_trails(seconds,eye,inverse,lowest,highest);
        std::memcpy(c.snow_window,snow.window,sizeof(c.snow_window));std::memcpy(c.snow_step,snow.step,sizeof(c.snow_step));
        std::memcpy(c.snow_tiles,snow.tiles.data(),sizeof(c.snow_tiles));std::memcpy(c.snow_write,snow.write.data(),sizeof(c.snow_write));
        if(snow.update){
            if(!g.snow[0]||!g.snow[1]){
                D3D12TextureRef a,b;
                if(FAILED(hr=d3d12_->create_target(snow_trails::kSide,snow_trails::kSide,a,DXGI_FORMAT_R16G16_FLOAT,DXGI_FORMAT_UNKNOWN))||
                   FAILED(hr=d3d12_->create_target(snow_trails::kSide,snow_trails::kSide,b,DXGI_FORMAT_R16G16_FLOAT,DXGI_FORMAT_UNKNOWN)))
                    return fail("ground cover snow trail field",hr);
                g.snow[0]=std::move(a);g.snow[1]=std::move(b);
            }
            const UINT snow_count=UINT(gc_snow_segment_input_.size());
            if(snow_count){
                const UINT64 bytes=UINT64(snow_count)*sizeof(snow_trails::Segment);if(bytes>UINT_MAX)return fail("ground cover snow trail segment dimensions",E_INVALIDARG);
                if(FAILED(hr=native_d3d12_buffer(g.snow_segments,bytes,sizeof(snow_trails::Segment),0))||
                   FAILED(hr=d3d12_->write_buffer(*g.snow_segments,0,gc_snow_segment_input_.data(),bytes)))return fail("ground cover snow trail segment upload",hr);
            }
            const UINT target=gc_snow_index_^1u;
            d3d12_->clear_slots();d3d12_->set_target(g.snow[target].get());d3d12_->set_constants(11,&c,sizeof(c));
            d3d12_->set_resource(53,gc_snow_valid_?g.snow[gc_snow_index_].get():nullptr);
            if((hr=d3d12_->draw_instanced(g.programs[gp_snow_tile_vs],g.programs[gp_snow_recover_ps],splat_state,nullptr,6,
                snow_trails::kTiles*snow_trails::kTiles,false))!=S_OK)return fail("ground cover snow trail recovery pipeline not ready",hr);
            UINT snow_draws=1;
            if(snow_count){
                D3D12NativeState stamp=splat_state;stamp.custom_blend.custom=1;stamp.custom_blend.source=stamp.custom_blend.destination=D3D11_BLEND_ONE;
                stamp.custom_blend.op=stamp.custom_blend.op_alpha=D3D11_BLEND_OP_MAX;
                stamp.custom_blend.write_mask=D3D11_COLOR_WRITE_ENABLE_RED|D3D11_COLOR_WRITE_ENABLE_GREEN;
                d3d12_->clear_slots();d3d12_->set_target(g.snow[target].get());d3d12_->set_constants(11,&c,sizeof(c));
                d3d12_->set_resource(52,g.snow_segments.get());d3d12_->set_resource(43,g.map.type.get());
                if((hr=d3d12_->draw_instanced(g.programs[gp_snow_stamp_vs],g.programs[gp_snow_stamp_ps],stamp,nullptr,6,snow_count,false))!=S_OK)
                    return fail("ground cover snow trail stamp pipeline not ready",hr);
                ++snow_draws;
            }
            d3d12_->clear_slots();
            gc_snow_index_=target;gc_snow_origin_=snow.origin;gc_snow_active_=snow.tiles;gc_snow_centre_=snow.centre;gc_snow_valid_=true;
            if(gc_snow_full_writes_)--gc_snow_full_writes_;
            profiler_.add_draw(snow_draws,snow_draws);profiler_.add_batch(snow_draws);
        }
        bind_world_targets_d3d12();
    }
    // The snow trail field this frame's cover reads (active map only).
    D3D12Texture* const snow_field=snow.update?g.snow[gc_snow_index_].get():nullptr;
    // The soft shelter map keeps grass under roofs and crowns green (t70):
    // the rain shelter's own maps (rain12_), chosen as the world draws bind them.
    const bool shelter=native_rain_valid_&&rain12_.shelter&&(snow_cover_>0.f||wet_level_>0.f); // rain too: the tufts' wetness (gcPrepCS)
    D3D12Texture* const shelter_map=!shelter?nullptr:native_shelter_soft_valid_?rain12_.soft[0].get():rain12_.shelter.get();
    if(shelter)gc_shelter_rows(c.shelter);
    // The native terrain frame (t127) every cover stage reads, as D3D11 binds
    // it to all stages: snowTrailRaw fades the trail by its smooth centre
    // (nativeSnowValue.yz); unbound it read zero and faded every trail out.
    const D3D12Buffer* const terrain_frame=native12_.terrain_frame.get();

    struct ZonePiece{float bounds[4];UINT zone,padding[3]{};};
    std::vector<ZonePiece> pieces{{{0,0,float(tx)*2,float(ty)*2},UINT(ambient_zone_)}};
    for(const auto& n:gc_neighbours_)pieces.push_back({{n.origin[0],n.origin[1],n.origin[0]+n.tiles_x*2.f,n.origin[1]+n.tiles_y*2.f},UINT(ambient::classify_zone(n.name.c_str(),false,environment.region))});
    std::vector<std::array<float,5>> zones;UINT64 zones_key=0;
    for(const auto& p:pieces){zones.push_back({p.bounds[0],p.bounds[1],p.bounds[2],p.bounds[3],float(p.zone)});
        for(float value:p.bounds){UINT bits;std::memcpy(&bits,&value,4);zones_key=gc_hash(zones_key,bits);}zones_key=gc_hash(zones_key,p.zone);}
    const auto* zone_data=reinterpret_cast<const std::uint8_t*>(pieces.data());const UINT zone_bytes=UINT(pieces.size()*sizeof(ZonePiece));
    if(gc_zone_uploaded_.size()!=zone_bytes||std::memcmp(gc_zone_uploaded_.data(),zone_data,zone_bytes)!=0){
        D3D12BufferRef buffer;if(FAILED(hr=d3d12_->create_buffer(zone_bytes,sizeof(ZonePiece),0,buffer))||
            FAILED(hr=d3d12_->write_buffer(*buffer,0,zone_data,zone_bytes)))return fail("ground cover zone input",hr);
        g.zones=std::move(buffer);gc_zone_uploaded_.assign(zone_data,zone_data+zone_bytes);
    }
    // Each map's per-cell rule masks bound its candidates per class; the
    // stage and cell buffers hold the largest map's round.
    UINT max_cell=gc_cell_slot_bound(quality.density,false);
    for(const auto& m:maps)if(desert_clump_map(m.neighbour?m.neighbour->name:ambient_map_name_))
        max_cell=std::max(max_cell,gc_cell_slot_bound(quality.density,true));
    UINT required=0,cells_needed=0;
    for(auto& m:maps){
        auto& map=*m.map;const UINT cells_x=(map.width+1)/2,cells_y=(map.height_tiles+1)/2;
        const bool desert_clumps=desert_clump_map(m.neighbour?m.neighbour->name:ambient_map_name_);
        // Terrain 2.0 adds the classes its field reaches per tile (bits 24-31),
        // the active map only (as on D3D11).
        const bool t2_tiles=!m.neighbour&&configured_terrain_blend(debug_overlay_.config())&&gc_t2_tile_classes_.size()==map.tile_classes.size();
        const std::uint64_t masks_key=t2_tiles?gc_hash(zones_key,gc_t2_rows_generation_+1):zones_key;
        if(map.masks_key!=masks_key){
            if(t2_tiles){
                std::vector<std::uint32_t> classes(map.tile_classes);
                for(std::size_t t=0;t<classes.size();++t)classes[t]|=std::uint32_t(gc_t2_tile_classes_[t])<<24;
                map.cell_masks=gc_cell_rule_masks(classes,map.width,map.height_tiles,zones,m.origin.data(),desert_clumps);
            }else map.cell_masks=gc_cell_rule_masks(map.tile_classes,map.width,map.height_tiles,zones,m.origin.data(),desert_clumps);
            if(FAILED(hr=texture(map.masks,cells_x,cells_y,kGcRuleMaskFormat,map.cell_masks.data(),UINT(cells_x*sizeof(GcRuleMask)))))return fail("ground cover cell masks",hr);
            map.masks_key=masks_key;
        }
        m.bounds=gc_window_bounds(map.cell_masks,cells_x,eye,m.window.x0,m.window.y0,m.window.x1,m.window.y1,m.lowest,m.highest,c.planes,
            m.origin.data(),quality.density*m.density,quality.distance,gc_class_rules_,desert_clumps);
        const UINT most=*std::max_element(m.bounds.begin(),m.bounds.end());
        if(!most)continue;
        required=std::max(required,std::max(kGcRoundSlots,most/8+max_cell+1));
        cells_needed=std::max(cells_needed,UINT((m.window.x1-m.window.x0)*(m.window.y1-m.window.y0)));
    }
    if(required>kGcStageLimit)return fail("ground cover stage dimensions",E_INVALIDARG);
    if(required>g.candidate_capacity){
        const UINT capacity=std::min(kGcStageLimit,std::max(required,g.candidate_capacity+g.candidate_capacity/2));
        D3D12BufferRef a,b;
        if(FAILED(hr=d3d12_->create_buffer(UINT64(capacity)*kGcCandidateBytes,kGcCandidateBytes,kD3D12BufferUnordered,a))||
           FAILED(hr=d3d12_->create_buffer(UINT64(capacity)*kGcSurvivorBytes,kGcSurvivorBytes,kD3D12BufferUnordered,b)))return fail("ground cover placement buffers",hr);
        g.candidates=std::move(a);g.survivors=std::move(b);g.candidate_capacity=capacity;
    }
    if(cells_needed>g.cell_capacity){
        const UINT capacity=std::max(cells_needed+cells_needed/4,1024u);D3D12BufferRef buffer;
        if(FAILED(hr=d3d12_->create_buffer(UINT64(capacity)*kPlaceClassCount*16,16,kD3D12BufferUnordered,buffer)))return fail("ground cover cell jobs",hr);
        g.cell_jobs=std::move(buffer);g.cell_capacity=capacity;
    }
    c.stage[0]=c.stage[1]=g.candidate_capacity;c.stage[2]=g.cell_capacity;c.round[0]=g.candidate_capacity-max_cell;
    // The caster mesh mask of the ground_cover_shadows level (gcCasterListCS);
    // each close map's list third starts at map * pool slots.
    const int cover_shadows=configured_integer(debug_overlay_.config(),"ground_cover_shadows",1);
    const std::uint64_t caster_mask=gc_shadow_caster_mask(cover_shadows);
    c.pass[1]=UINT(caster_mask);c.pass[2]=UINT(caster_mask>>32);c.pass[3]=g.pool_capacity;

    D3D11_SAMPLER_DESC clamp{};clamp.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;clamp.AddressU=clamp.AddressV=clamp.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;clamp.MaxLOD=D3D11_FLOAT32_MAX;
    const auto wrap=world_texture_sampler(D3DTFN_LINEAR,D3DTFG_LINEAR,D3DTFP_LINEAR,
        D3DTADDRESS_WRAP,D3DTADDRESS_WRAP,true,1u<<configured_integer(debug_overlay_.config(),"anisotropy",3)).description;
    D3D11_SAMPLER_DESC gather=clamp;gather.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
    D3D11_SAMPLER_DESC comparison=clamp;comparison.Filter=D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT;comparison.ComparisonFunc=D3D11_COMPARISON_LESS_EQUAL;
    comparison.AddressU=comparison.AddressV=comparison.AddressW=D3D11_TEXTURE_ADDRESS_BORDER;std::fill(comparison.BorderColor,comparison.BorderColor+4,1.f);
    D3D11_SAMPLER_DESC comparison_far=comparison;comparison_far.Filter=D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    auto receivers=shadow12_.constants;
    if(!native_shadow_failed_&&native_shadows_enabled_)for(UINT i=0;i<5;++i)if(native_shadow_frame_.slices[i].enabled&&!(receivers.control[0]&(1u<<i))){receivers.bias[4][2]=0;break;}
    const bool sun_on=receivers.control[0]!=0||receivers.bias[4][2]>.5f,sun_far=receivers.bias[4][2]>.5f,sun_soft=native_shadow_softness_[2]>.5f;
    bool sun_maps=false; // as D3D11: any enabled, valid sun map (the caster lists)
    for(UINT i=0;i<5;++i)sun_maps|=native_shadows_enabled_&&native_shadow_frame_.slices[i].enabled&&native_shadow_valid_[i];
    // As on D3D11, receiver variants follow this frame's published slots.
    const bool point_shadows=std::any_of(native_point_lights_.begin(),native_point_lights_.end(),
        [](const NativePointLightValue& light){return light.shadow_slots[0]>0;});
    const auto dispatch=[&](UINT program,UINT x,UINT y=1)->HRESULT {const HRESULT result=d3d12_->dispatch(g.programs[program],x,y,1);if(result==S_OK){++perf_native_compute_dispatches_;++native_frame_stats_.native_compute_dispatches;}return result;};
    static const std::vector<UINT> stage_reset=[] {std::vector<UINT> v(kPlaceClassCount*kGcMaxRounds*4,0);for(std::size_t i=0;i<v.size();i+=4)v[i+1]=v[i+2]=1;return v;}();
    // The cover's depth prepass (depth only: every colour target masked) and
    // its material pass, which shades only the prepass's surviving pixels.
    D3D12NativeState depth_state;depth_state.raster=fixed_raster_desc(D3DCULL_NONE,false,false);
    depth_state.depth=fixed_depth_desc(D3DZB_TRUE,TRUE,D3DCMP_LESSEQUAL);depth_state.custom_blend.custom=1;depth_state.custom_blend.write_mask=0;
    D3D12NativeState state;state.raster=depth_state.raster;state.depth=fixed_depth_desc(D3DZB_TRUE,FALSE,D3DCMP_EQUAL);
    // The active map's prepass with the world's mask pass (D3D11 masked):
    // the same draws also write the sun-shadow receiver record.
    D3D12NativeState receiver_state=depth_state;receiver_state.custom_blend={};
    // Neighbour cover (D3D11 gc_depth_): one pass, depth LESS_EQUAL with
    // write and the material's own alpha clip. Its thin far cover has little
    // overdraw, so a prepass only doubled its vertex work (c1101 Zone3obr 4K:
    // prepass 0.32 ms + material 0.35 ms against D3D11's single 0.38 ms).
    const D3D12NativeState single_state=receiver_state;
    // The receivers' stencil mark: the world mask pass shades only marked pixels.
    mark_receiver_stencil(receiver_state.depth);
    const bool mask_wanted=depth_prepass_d3d12()&&fog_camera_valid_&&!scene_composited_&&
        world_d3d12_bound_&&SUCCEEDED(ensure_prepass_targets_d3d12());
    const bool owner_cull=configured_integer(debug_overlay_.config(),"ground_cover_owner_cull",1)>0;
    const bool neighbour_reuse=configured_integer(debug_overlay_.config(),"ground_cover_neighbour_reuse",1)>0;
    // The demand words of every region are this frame's (cleared once).
    d3d12_->clear_buffer_uint(*g.counts,0);
    // Occlusion (gcCullCS, GcCull b13): this frame's terrain pyramid while
    // ground_cover_occlusion is on.
    struct GcCullConstants{float hiz[4];UINT control[4];float mesh[kGcCullRows*4];};
    static_assert(sizeof(GcCullConstants)%16==0);
    GcCullConstants cull{};
    if(configured_integer(debug_overlay_.config(),"ground_cover_occlusion",1)>0&&native_occlusion_active()&&native_hiz12_){
        // Screen coordinates map to full-resolution pixels / 2, including
        // the half texel from an odd extent; integer mip sizes still round down.
        cull.hiz[0]=(std::max)(1.f,float(width_)*.5f);cull.hiz[1]=(std::max)(1.f,float(height_)*.5f);cull.hiz[2]=float(native_hiz_mips_);cull.hiz[3]=1;cull.control[0]=1;
    }
    for(UINT m=0;m<gm_count;++m){cull.mesh[m*2]=gc_mesh_kind_[m]==3?-1.f:g.cull_mesh[m*2];cull.mesh[m*2+1]=g.cull_mesh[m*2+1];}
    if(FAILED(hr=d3d12_->write_buffer(*g.caster_args,0,g.caster_template.data(),g.caster_template.size()*20)))return fail("ground cover caster arguments",hr);
    UINT draws=0,neighbour_draws=0;bool neighbours_started=false;
    for(const auto& m:maps){
        const UINT p=m.region;auto& map=*m.map;
        if(!*std::max_element(m.bounds.begin(),m.bounds.end()))continue; // nothing can place in its visible cells
        if(p&&!neighbours_started){transition_profile_pass("ground_cover_neighbours");neighbours_started=true;}
        // This map's frame constants: its grids, origin (w 1: far meshes
        // only), window, density, zone, fog band and pool region.
        GroundCoverConstants mc=c;
        mc.switches[2]=desert_clump_map(m.neighbour?m.neighbour->name:ambient_map_name_)?1.f:0.f;
        std::copy(gc_capacity_[p].begin(),gc_capacity_[p].end(),mc.capacity);std::copy(gc_base_[p].begin(),gc_base_[p].end(),mc.base);
        const ContactBlendMaps& contact=m.neighbour?m.neighbour->contact:contact_maps_;
        mc.snow_materials[0]=UINT(contact.ready);mc.snow_materials[1]=UINT(contact.ready>>32);
        mc.snow_materials[2]=terrain2_table_entry(m.neighbour?m.neighbour->contact_map:0u); // piles read terrain 2.0 (D3D11 render_ground_cover)
        mc.window[0]=float(m.window.x0);mc.window[1]=float(m.window.y0);mc.window[2]=float(m.window.x1-m.window.x0);
        mc.grid[0]=float(map.width);mc.grid[1]=float(map.height_tiles);
        mc.quality[1]=quality.density*m.density;
        // Terrain 2.0 (the active map only, as on D3D11): its class shares (t62,
        // gcQuality.z) and colour map replace the per-tile classes and paint.
        const bool t2_cover=!m.neighbour&&configured_terrain_blend(debug_overlay_.config());
        mc.quality[2]=t2_cover?1.f:0.f;
        const D3D12Texture* const cover_color=t2_cover?gc_t2_color12_.get():map.color.get();
        const D3D12Texture* const cover_classes=t2_cover?gc_t2_classes12_.get():nullptr;
        // Outdoor neighbours of the same allod: its region's zone, never a cave.
        mc.quality[3]=m.neighbour?float(ambient::classify_zone(m.neighbour->name.c_str(),false,environment.region)):float(ambient_zone_);
        // A neighbour fogs by its piece's raw edges, as its terrain and decorations do.
        if(m.neighbour)std::memcpy(mc.fog_range,m.neighbour->fog_band.data(),sizeof(mc.fog_range));
        mc.view[0]=m.reach;
        const bool trails=!m.neighbour&&trail_active;
        if(!trails){mc.view[3]=0;std::memset(mc.trail_step,0,sizeof(mc.trail_step));}
        const bool snow_pressed=!m.neighbour&&snow_field; // no snow trails on neighbour maps
        if(!snow_pressed)mc.snow_window[3]=0;
        mc.origin[0]=m.origin[0];mc.origin[1]=m.origin[1];mc.origin[2]=m.origin[2];mc.origin[3]=m.neighbour?1.f:0.f;
        mc.pass[0]=p*gm_count*8;
        mc.stage[3]=UINT(m.window.y1-m.window.y0);
        std::array<UINT,kPlaceClassCount> rounds{};
        for(UINT i=0;i<kPlaceClassCount;++i){rounds[i]=m.bounds[i]?m.bounds[i]/mc.round[0]+1:0;if(rounds[i]>kGcMaxRounds)return fail("ground cover round count",E_INVALIDARG);}
        const auto placement=[&]{
            d3d12_->clear_slots();d3d12_->set_constants(11,&mc,sizeof(mc));d3d12_->set_constants(2,&receivers,sizeof(receivers));
            d3d12_->set_resource(29,map.area.get());d3d12_->set_resource(30,map.height.get());d3d12_->set_resource(31,map.ground.get());
            d3d12_->set_resource(32,map.fields.get());d3d12_->set_resource(33,cover_color);d3d12_->set_resource(62,cover_classes);d3d12_->set_resource(34,g.rules.get());
            d3d12_->set_resource(43,map.type.get());d3d12_->set_resource(47,map.trees.get());d3d12_->set_resource(50,g.zones.get());
            d3d12_->set_resource(44,trails?g.trail[gc_trail_index_].get():nullptr);d3d12_->set_resource(48,m.neighbour?nullptr:g.object_mask.get());
            d3d12_->set_resource(51,snow_pressed?snow_field:nullptr);d3d12_->set_resource(127,terrain_frame);d3d12_->set_resource(72,fire_burn_mask12());
            d3d12_->set_resource(59,g.rule_class.get());d3d12_->set_resource(61,map.masks.get());
            d3d12_->set_resource(9,shadow12_.maps.get());d3d12_->set_sampler(8,clamp);d3d12_->set_sampler(10,gather);
            d3d12_->set_unordered(0,g.instances.get());d3d12_->set_unordered(1,g.args.get());d3d12_->set_unordered(2,g.counts.get());d3d12_->set_unordered(4,g.overflow.get());
        };
        // Neighbour placement reuse (ground_cover_neighbour_reuse, default on):
        // a neighbour region's placement (cells, the three class stages and the
        // finalize) is a pure function of the inputs hashed below, and its
        // instances stay in their own pool region, so while those inputs are
        // unchanged (a still camera) the region keeps last frame's instances and
        // only its finalized counts and work prefix are restored. Sun gap, caster
        // lists, fog/wind and the cull run every frame as before. Excluded from the
        // key, read only by those later stages: the jittered clip, the sway clocks,
        // wind, the pixel dither offset. Neighbours take no units or trails.
        bool reused=false;
        if(m.neighbour&&neighbour_reuse){
            GroundCoverConstants key_constants=mc;
            std::memset(key_constants.clip,0,sizeof(key_constants.clip));key_constants.eye[3]=0;
            std::memset(key_constants.wind,0,sizeof(key_constants.wind));std::memset(key_constants.wind2,0,sizeof(key_constants.wind2));
            key_constants.switches[0]=0;
            // The snow trail field's scroll, recovery step and tiles: neighbours
            // never read the field (snow_window.w 0); the cover amount (w) stays.
            std::memset(key_constants.snow_step,0,3*sizeof(float));
            std::memset(key_constants.snow_tiles,0,sizeof(key_constants.snow_tiles));std::memset(key_constants.snow_write,0,sizeof(key_constants.snow_write));
            std::uint64_t key=gc_hash(gc_hash(gc_hash(gc_hash(map.generation,map.masks_key),gc_painted_frame_),UINT64(g.pool_capacity)|UINT64(passes)<<32),zones_key);
            const auto* words=reinterpret_cast<const std::uint32_t*>(&key_constants);
            for(std::size_t w=0;w<sizeof(key_constants)/4;++w)key=gc_hash(key,words[w]);
            for(UINT cl=0;cl<kPlaceClassCount;++cl)key=gc_hash(key,rounds[cl]);
            if(g.reuse.size()<passes)g.reuse.resize(passes);
            auto& reuse=g.reuse[p];
            if(reuse.valid&&reuse.key==key&&reuse.pool==g.pool_generation){
                if(FAILED(hr=d3d12_->copy_buffer(*g.args,*reuse.args,sizeof(gc_args_template_)))||
                   FAILED(hr=d3d12_->copy_buffer(*g.work,*reuse.work,d3d12_->buffer_bytes(*g.work))))return fail("ground cover neighbour reuse",hr);
                reused=true;
            }else{
                reuse.valid=false;reuse.key=key;
            }
        }
        if(!reused){
        if(FAILED(hr=d3d12_->write_buffer(*g.args,0,gc_args_template_.data(),sizeof(gc_args_template_))))return fail("ground cover draw arguments",hr);
        d3d12_->clear_buffer_uint(*g.cell_counts,0);
        placement();
        d3d12_->set_unordered(3,g.cell_counts.get());d3d12_->set_unordered(6,g.cell_jobs.get());
        if((hr=dispatch(gp_cells,dispatch_groups(UINT((m.window.x1-m.window.x0)*(m.window.y1-m.window.y0)),64)))!=S_OK)return fail("ground cover cell pipeline not ready",hr);
        placement();d3d12_->set_resource(56,g.cell_jobs.get());d3d12_->set_resource(58,g.cell_counts.get());d3d12_->set_unordered(7,g.rounds.get());
        if((hr=dispatch(gp_scan,kPlaceClassCount))!=S_OK)return fail("ground cover scan pipeline not ready",hr);
        for(auto& counts:g.stage_counts)if(FAILED(hr=d3d12_->write_buffer(*counts,0,stage_reset.data(),stage_reset.size()*4)))return fail("ground cover stage counters",hr);
        for(UINT cl=0;cl<kPlaceClassCount;++cl)for(UINT r=0;r<rounds[cl];++r){
            const UINT run[4]{r,0,0,0},offset=(cl*kGcMaxRounds+r)*16;
            for(UINT stage=0;stage<3;++stage){
                placement();d3d12_->set_constants(12,g.classes[cl].data(),sizeof(g.classes[cl]));d3d12_->set_constants(13,run,sizeof(run));
                D3D12Buffer* arguments=nullptr;
                if(stage==0){d3d12_->set_resource(56,g.cell_jobs.get());d3d12_->set_resource(57,g.rounds.get());d3d12_->set_unordered(3,g.stage_counts[0].get());d3d12_->set_unordered(7,g.candidates.get());arguments=g.rounds.get();}
                else if(stage==1){d3d12_->set_resource(54,g.stage_counts[0].get());d3d12_->set_unordered(3,g.stage_counts[1].get());d3d12_->set_unordered(6,g.candidates.get());d3d12_->set_unordered(7,g.survivors.get());arguments=g.stage_counts[0].get();}
                else{d3d12_->set_resource(54,g.stage_counts[1].get());d3d12_->set_unordered(5,g.shade.get());d3d12_->set_unordered(6,g.survivors.get());arguments=g.stage_counts[1].get();}
                const UINT program=kPlaceClasses[cl].program[stage==2?(trails||snow_pressed?2:3):stage];
                hr=d3d12_->dispatch_indirect(g.programs[program],*arguments,offset);
                if(hr!=S_OK)return fail("ground cover placement pipeline not ready",hr);
                ++perf_native_compute_dispatches_;++native_frame_stats_.native_compute_dispatches;
            }
        }
        // Hosted plants (the active map only): the host records into their mesh pools.
        if(!p&&hosted&&gc_hosted_count_&&g.hosted){
            placement();d3d12_->set_resource(65,g.hosted.get());d3d12_->set_unordered(5,g.shade.get());
            if((hr=dispatch(gp_hosted_cs,dispatch_groups(gc_hosted_count_,64)))!=S_OK)return fail("hosted plant pipeline not ready",hr);
        }
        placement();d3d12_->set_unordered(3,g.work.get());d3d12_->set_unordered(5,g.shade.get());
        if((hr=dispatch(gp_finalize,1))!=S_OK)return fail("ground cover finalize pipeline not ready",hr);
        if(m.neighbour&&neighbour_reuse){
            auto& reuse=g.reuse[p];
            const UINT64 work_bytes=d3d12_->buffer_bytes(*g.work);
            if(!reuse.args&&FAILED(hr=d3d12_->create_buffer(sizeof(gc_args_template_),0,0,reuse.args)))return fail("ground cover neighbour reuse",hr);
            if((!reuse.work||d3d12_->buffer_bytes(*reuse.work)!=work_bytes)&&FAILED(hr=d3d12_->create_buffer(work_bytes,0,0,reuse.work)))return fail("ground cover neighbour reuse",hr);
            if(FAILED(hr=d3d12_->copy_buffer(*reuse.args,*g.args,sizeof(gc_args_template_)))||
               FAILED(hr=d3d12_->copy_buffer(*reuse.work,*g.work,work_bytes)))return fail("ground cover neighbour reuse",hr);
            reuse.pool=g.pool_generation;reuse.valid=true;
        }
        } // !reused
        if(sun_on&&sun_soft){placement();d3d12_->set_resource(55,g.work.get());d3d12_->set_unordered(5,g.shade.get());
            if((hr=d3d12_->dispatch_indirect(g.programs[gp_shade+(sun_far?1:0)],*g.work,0))!=S_OK)return fail("ground cover sun shading pipeline not ready",hr);}
        // The next frame's sun casters of this map, culled per instance into
        // each close map's list (gcCasterListCS, as on D3D11).
        if(sun_maps&&caster_mask){placement();d3d12_->set_resource(55,g.work.get());d3d12_->set_unordered(6,g.caster_args.get());d3d12_->set_unordered(7,g.caster_slots.get());
            if((hr=d3d12_->dispatch_indirect(g.programs[gp_caster_list],*g.work,0))!=S_OK)return fail("ground cover sun caster list pipeline not ready",hr);
            ++perf_native_compute_dispatches_;++native_frame_stats_.native_compute_dispatches;}
        // Per-instance fog/cloud/wind, using this frame's authoritative fog and
        // sky inputs (a neighbour: its piece's fog band).
        d3d12_->clear_slots();d3d12_->set_constants(11,&mc,sizeof(mc));d3d12_->set_constants(10,&fog_frame_d3d12_,sizeof(fog_frame_d3d12_));
        d3d12_->set_constants(5,native12_.point_light_constants.data(),sizeof(native12_.point_light_constants));
        d3d12_->set_resource(kSkyCloudShadowSlotD3D12,sky_inputs_active_?sky12_.shadow.get():nullptr);
        d3d12_->set_resource(kSkyProbeSlotD3D12,sky_inputs_active_?sky12_.probe[sky_index_].get():nullptr);d3d12_->set_sampler(7,clamp);
        bind_world_fog_volume_d3d12(); // t11, as the D3D11 prep binds the integrated volume
        bind_cave_irradiance_d3d12(); // the cave fog light and exit glow
        d3d12_->set_resource(55,g.work.get());d3d12_->set_unordered(0,g.instances.get());d3d12_->set_unordered(1,g.args.get());d3d12_->set_unordered(6,g.draw_inputs.get());
        d3d12_->set_resource(127,terrain_frame);d3d12_->set_resource(72,fire_burn_mask12());d3d12_->set_resource(70,shelter_map);
        if((hr=d3d12_->dispatch_indirect(g.programs[gp_prep+(m.neighbour?4:0)+(mc.switches[1]>0?2:0)+(fog_cloud_visible_?1:0)],*g.work,0))!=S_OK)return fail("ground cover fog/wind pipeline not ready",hr);
        // Each mesh's visible list (gcCullCS): the placed instances the
        // terrain pyramid does not hide (all of them with the occlusion off).
        if(FAILED(hr=d3d12_->write_buffer(*g.visible_args,0,gc_args_template_.data(),sizeof(gc_args_template_))))return fail("ground cover visible arguments",hr);
        d3d12_->clear_slots();d3d12_->set_constants(11,&mc,sizeof(mc));d3d12_->set_constants(13,&cull,sizeof(cull));
        d3d12_->set_resource(55,g.work.get());d3d12_->set_resource(28,g.instances.get());d3d12_->set_resource(67,g.draw_inputs.get());
        d3d12_->set_resource(42,cull.hiz[3]>0?native_hiz12_.get():nullptr);
        d3d12_->set_unordered(1,g.visible_args.get());d3d12_->set_unordered(6,g.visible.get());
        if((hr=d3d12_->dispatch_indirect(g.programs[gp_cull],*g.work,0))!=S_OK)return fail("ground cover occlusion pipeline not ready",hr);
        ++perf_native_compute_dispatches_;++native_frame_stats_.native_compute_dispatches;
        if(!p)transition_profile_pass("ground_cover_draw");
        // Cover reads and writes the temporal sun-shadow history like world pixels (D3D11).
        const bool history=prepare_shadow_history();
        d3d12_->clear_slots();bind_world_targets_d3d12(history);bind_shadow_history_d3d12(history);
        bind_cave_irradiance_d3d12(); // per-vertex bounce, snow piles per pixel
        d3d12_->set_constants(11,&mc,sizeof(mc));d3d12_->set_constants(1,&native_world_lighting_,sizeof(native_world_lighting_));d3d12_->set_constants(2,&receivers,sizeof(receivers));
        d3d12_->set_constants(5,native12_.point_light_constants.data(),sizeof(native12_.point_light_constants));d3d12_->set_constants(10,&fog_frame_d3d12_,sizeof(fog_frame_d3d12_));
        d3d12_->set_resource(0,map.ground.get());d3d12_->set_resource(1,cover_color);d3d12_->set_resource(2,g.map.bark.get());d3d12_->set_resource(3,g.map.stone.get());
        d3d12_->set_resource(22,g.map.plant[0].get());d3d12_->set_resource(23,g.map.plant[1].get());d3d12_->set_resource(24,g.map.plant[2].get());
        d3d12_->set_resource(60,g.map.plant[3].get()); // the plant cards' atlases (kind 5)
        d3d12_->set_resource(121,g.hosted_art.get()); // hosted models' own art (art 5)
        d3d12_->set_resource(123,g.cave_art.get());   // the cave colonies' (art 6)
        d3d12_->set_resource(4,m.neighbour?nullptr:g.object_mask.get());
        d3d12_->set_resource(6,snow_pressed?snow_field:nullptr);d3d12_->set_resource(snow_trails::kFieldSlotD3D12,snow_pressed?snow_field:nullptr); // vertex t6, pixel nativeSnowTrail
        d3d12_->set_resource(70,shelter_map);d3d12_->set_resource(127,terrain_frame);d3d12_->set_resource(72,fire_burn_mask12());
        d3d12_->set_resource(69,g.hosted_vertices.get());d3d12_->set_resource(113,g.hosted_indices.get()); // hosted plants' vertices and indices
        d3d12_->set_resource(27,g.vertices.get());d3d12_->set_resource(28,g.instances.get());d3d12_->set_resource(31,map.ground.get());d3d12_->set_resource(33,cover_color);d3d12_->set_resource(67,g.draw_inputs.get());d3d12_->set_resource(68,g.visible.get());
        d3d12_->set_resource(17,native_point_lights_.empty()?nullptr:native12_.point_light_grid.get());d3d12_->set_resource(18,native_point_lights_.empty()?nullptr:native12_.point_lights.get());
        // The shared receiver reads the world's static and overlay atlases in
        // the cover vertex stage and the snow/sand pile pixel stage.
        for(UINT page=0;page<PointShadowTextureLayout::array_bindings;++page)
            d3d12_->set_resource(PointShadowTextureLayout::first_srv+page,
                page<native_point_shadow_pages_.size()?native_point_shadow_pages_[page].depth12.get():nullptr);
        d3d12_->set_resource(9,shadow12_.maps.get());d3d12_->set_resource(15,contact.tiles12.get());d3d12_->set_resource(16,contact.atlas12.get());
        d3d12_->set_sampler(8,clamp);d3d12_->set_sampler(9,wrap);d3d12_->set_sampler(1,comparison);d3d12_->set_sampler(2,comparison_far);d3d12_->set_sampler(4,comparison_far);d3d12_->set_sampler(10,gather);
        // The terrain noise lattice (t122, point wrap s12, as the world flush
        // binds it): snow and sand piles resolve terrain 2.0's warped material
        // field and edge noise through it (terrain2FlatColor). clear_slots
        // above dropped it, so D3D12 piles read the field without its warp
        // and noise and showed a material the ground there does not draw
        // (dirt on a snow pile, save1169; D3D11 keeps the frame's binding).
        d3d12_->set_resource(PointShadowTextureLayout::terrain_noise_srv,native12_.terrain_noise.get());
        {D3D11_SAMPLER_DESC noise{};noise.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;noise.AddressU=noise.AddressV=noise.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;noise.MaxLOD=0;d3d12_->set_sampler(12,noise);}
        // The owned sky probe (t111, s7) for the cover's sky reflection at
        // material_quality 3 (gcMaterialResponse; D3D11 keeps the world's t12).
        d3d12_->set_resource(kSkyProbeSlotD3D12,sky_inputs_active_?sky12_.probe[sky_index_].get():nullptr);d3d12_->set_sampler(7,clamp);
        // Piles on terrain 2.0 read its table, heights and material cache
        // (bind_terrain2_d3d12: t85-t109, t72-t77, s15) and terrain2Sampler
        // (s9, the contact sampler), set per mesh below; the other cover
        // kinds sample gcWrap there (D3D11 swaps s9 the same way).
        if(mc.snow_materials[2])bind_terrain2_d3d12();
        D3D11_SAMPLER_DESC contact_sampler=clamp;contact_sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        const auto pile_sampler=[&](UINT kind){d3d12_->set_sampler(9,kind==3&&mc.snow_materials[2]?contact_sampler:wrap);};
        d3d12_->set_index_buffer(g.indices.get());UINT map_draws=0;
        // Depth prepass (D3D11's cover prepass): the same placement and
        // alpha clip (gcDepthVS/PS) write the cover's depth with no colour,
        // then the material pass shades each covered pixel once, depth
        // EQUAL. On the active map the prepass also writes the sun-shadow
        // receiver record, the world's mask pass takes the sun and cloud
        // shadow of its pixels and the material pass reads it (the D3D11
        // masked variants). Neighbour cover takes no prepass (single_state)
        // and keeps its own sun filter.
        const bool receiver=mask_wanted&&!m.neighbour;
        if(receiver){transition_profile_pass("ground_cover_prepass");begin_world_prepass_d3d12();}
        // Owner culling (ground_cover_owner_cull, default on): the prepass also
        // writes each covered pixel's instance; the material pass then draws
        // only instances that own a pixel (gcOwnerMarkCS, gcOwnerListCS).
        UINT owner_width=0,owner_height=0;
        if(receiver&&owner_cull){
            UINT levels=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
            if(!d3d12_->texture_info(*prepass_rest12_,owner_width,owner_height,levels,format))return fail("ground cover owner target size",E_FAIL);
            if(!g.owner_ids||!d3d12_->texture_matches(*g.owner_ids,owner_width,owner_height,DXGI_FORMAT_R32_UINT)){
                g.owner_ids.reset();
                if(FAILED(hr=d3d12_->create_target(owner_width,owner_height,g.owner_ids,DXGI_FORMAT_R32_UINT,DXGI_FORMAT_UNKNOWN)))return fail("ground cover owner target",hr);
            }
            if(!g.owner_flags||d3d12_->buffer_bytes(*g.owner_flags)<UINT64(g.pool_capacity)*4){
                g.owner_flags.reset();
                if(FAILED(hr=d3d12_->create_buffer(UINT64(g.pool_capacity)*4,4,kD3D12BufferUnordered,g.owner_flags)))return fail("ground cover owner stamps",hr);
            }
            // A render target clear (the target has no UAV for clear_texture_uint): 0, no instance.
            const float none[4]{};d3d12_->clear_texture(*g.owner_ids,none);
            D3D12Texture* const targets[3]={prepass_rest12_.get(),prepass_normal12_.get(),g.owner_ids.get()};
            d3d12_->set_targets(targets,3,world_d3d12_.get());
        }
        const bool owners=owner_width!=0;
        if(!m.neighbour&&profiler_.pass_active())profiler_.sub_scope("cover_prepass");
        for(UINT mesh=0;mesh<gm_count&&!m.neighbour;++mesh){
            if(!gc_capacity_[p][mesh])continue;
            const UINT kind=gc_mesh_kind_[mesh],base[4]{gc_base_[p][mesh],0,gc_mesh_first_vertex_[mesh],hosted_mesh(mesh)?(mesh==gm_hosted_dead_vines?1u:2u):0u};
            const UINT vertex=hosted_mesh(mesh)?gp_hosted_depth_vs:kind==5?gp_plant_depth_vs:gp_depth_vs+(kind==3?1+(snow_pressed?1:0):kind==4?3:0),
                       pixel=art_mesh(mesh)?(owners?gp_hosted_depth_ps_owner:gp_hosted_depth_ps):kind==5?(owners?gp_plant_depth_ps_owner:gp_plant_depth_ps):
                             (owners?gp_depth_ps_owner:gp_depth_ps)+(kind==3?1:kind==4?2:0);
            d3d12_->set_constants(12,base,sizeof(base));hr=d3d12_->draw_indexed_indirect(g.programs[vertex],g.programs[pixel],receiver?receiver_state:depth_state,nullptr,*g.visible_args,mesh*20);
            if(hr!=S_OK)return fail("ground cover depth prepass pipeline not ready",hr);
            ++map_draws;
        }
        if(owners){
            // The material pass's lists: the instances owning a pixel. The
            // owner target leaves the targets first (it is read now); only the
            // slots set here change, the material bindings stay.
            D3D12Texture* const targets[2]={prepass_rest12_.get(),prepass_normal12_.get()};
            d3d12_->set_targets(targets,2,world_d3d12_.get());
            g.owner_stamp=g.owner_stamp+1?g.owner_stamp+1:1;
            const UINT control[4]{g.owner_stamp,owner_width,owner_height,0};
            d3d12_->set_constants(13,control,sizeof(control));
            d3d12_->set_resource(69,g.owner_ids.get());d3d12_->set_unordered(7,g.owner_flags.get());
            if((hr=dispatch(gp_owner_mark,dispatch_groups(owner_width,32),dispatch_groups(owner_height,8)))!=S_OK)return fail("ground cover owner pipeline not ready",hr);
            // t69 is also the hosted plants' vertices (the material pass's GC_HOSTED_VS): put them back.
            d3d12_->set_unordered(7,static_cast<const D3D12Buffer*>(nullptr));d3d12_->set_resource(69,g.hosted_vertices.get());
            if(FAILED(hr=d3d12_->write_buffer(*g.visible_args,0,gc_args_template_.data(),sizeof(gc_args_template_))))return fail("ground cover owner arguments",hr);
            d3d12_->set_resource(68,static_cast<const D3D12Buffer*>(nullptr));
            d3d12_->set_resource(55,g.work.get());d3d12_->set_resource(63,g.owner_flags.get());
            d3d12_->set_unordered(1,g.visible_args.get());d3d12_->set_unordered(6,g.visible.get());
            if((hr=d3d12_->dispatch_indirect(g.programs[gp_owner_list],*g.work,0))!=S_OK)return fail("ground cover owner list pipeline not ready",hr);
            ++perf_native_compute_dispatches_;++native_frame_stats_.native_compute_dispatches;
            d3d12_->set_unordered(1,static_cast<const D3D12Buffer*>(nullptr));d3d12_->set_unordered(6,static_cast<const D3D12Buffer*>(nullptr));
            d3d12_->set_resource(63,static_cast<const D3D12Buffer*>(nullptr));d3d12_->set_resource(55,static_cast<const D3D12Buffer*>(nullptr));
            d3d12_->set_resource(68,g.visible.get());
        }
        bool masked=false;
        if(receiver){
            const HRESULT mask=resolve_world_shadow_mask_d3d12();
            if(FAILED(mask)&&mask!=E_FAIL)return fail("ground cover shadow mask",mask);
            masked=mask==S_OK; // else the unmasked variants below
            // The traced sun mask (rt_sun_shadows) borrows t113 for its records and
            // leaves it empty: the hosted plants' indices go back for the material pass.
            d3d12_->set_resource(69,g.hosted_vertices.get());d3d12_->set_resource(113,g.hosted_indices.get());
            bind_world_targets_d3d12(history);
            if(!p)transition_profile_pass("ground_cover_draw");
        }
        if(profiler_.pass_active())profiler_.sub_scope("cover_material");
        // Upscaler object motion of the active map's swaying cover in the same
        // draws (GC_PIXEL_MOTION): the object motion targets bound at 3 and 4,
        // b13 the motion constants. Snow piles and neighbour cover bind none
        // (their writes there are dropped) and keep the camera motion.
        const bool cover_motion=!m.neighbour&&object_motion_ready_d3d12();
        if(cover_motion)d3d12_->set_constants(13,upscale12_.motion_constants.data(),20*sizeof(float)); // previous clip, jitter, render size
        bool motion_bound=false;
        // rt_ray_reconstruction: the cover draws its guide twins (GC_RR_GUIDES,
        // one a gcPS program in the list's order: gc_guide_twins) with the guide targets at 5-7.
        if(guide_frame_d3d12()&&g.guides.empty())append_ground_cover_guide_programs(g.guides);
        const GcGuideTwins& twins=gc_guide_twins();
        const bool cover_guides=guide_frame_d3d12()&&g.guides.size()==twins.count;
        if(cover_guides)bind_world_targets_d3d12(history,false,true);
        // rt_direct_lights, rt_indirect_light: on a traced frame the cover's guide twins draw as the world's
        // (renderer_native_d3d12.cpp twin_lamps): an empty lamp grid and no glow lights, the bounce volume's
        // strength 0, and gcDrawTraced set, so the traced passes light the cover as the ground around it.
        const bool cover_lamps_traced=cover_guides&&rt_direct12_.traced_frame==native_frame_index_&&!scene_composited_&&!hit_test_active_;
        const bool cover_bounce_traced=cover_guides&&rt_indirect12_.traced_frame==native_frame_index_&&!scene_composited_&&!hit_test_active_;
        const bool cover_traced=cover_lamps_traced||cover_bounce_traced;
        auto traced_lights=native12_.point_light_constants;
        if(cover_lamps_traced)traced_lights[1]=traced_lights[2]=traced_lights[3]=0;
        if(cover_bounce_traced)traced_lights[11]=0;
        if(cover_traced)d3d12_->set_constants(5,traced_lights.data(),sizeof(traced_lights));
        for(UINT mesh=0;mesh<gm_count;++mesh){
            if(!gc_capacity_[p][mesh])continue;
            const UINT kind=gc_mesh_kind_[mesh],sun=gc_sun_state(sun_on,sun_soft,sun_far),base[4]{gc_base_[p][mesh],cover_traced?1u:0u,gc_mesh_first_vertex_[mesh],hosted_mesh(mesh)?(mesh==gm_hosted_dead_vines?1u:2u):0u};
            if(cover_motion&&(kind!=3)!=motion_bound){motion_bound=kind!=3;bind_world_targets_d3d12(history,motion_bound,cover_guides);}
            const UINT vertex=hosted_mesh(mesh)?gp_hosted_vs+(point_shadows?1:0):gp_vs+(kind==3?2+(snow_pressed?1:0):point_shadows?1:0);
            // The masked programs (append_ground_cover_shader_programs): cover
            // [kind * 2], snow pile [point shadows * 2 + trails], strands after
            // the strands' own ten; this backend writes no AO share.
            const UINT pixel=kind==5?(art_mesh(mesh)?gp_hosted_ps:gp_plant_ps)+(masked?10:sun*2):
                masked?(kind==3?gp_ps_masked+6+(point_shadows?2u:0u)+(snow_pressed?1:0):kind==4?gp_ps_strand+10:gp_ps_masked+kind*2):
                kind==3?gp_ps_snow+(sun_on?4:0)+(point_shadows?2u:0u)+(snow_pressed?1:0):kind==4?gp_ps_strand+sun*2:gp_ps+(kind*5+sun)*2;
            pile_sampler(kind);
            // Every material program here is a gcPS with a twin (gc_guide_twins).
            const int twin=cover_guides?twins.of[pixel]:-1;
            d3d12_->set_constants(12,base,sizeof(base));hr=d3d12_->draw_indexed_indirect(g.programs[vertex],twin>=0?g.guides[twin]:g.programs[pixel],m.neighbour?single_state:state,nullptr,*g.visible_args,mesh*20);
            if(cover_guides&&hr==S_FALSE){
                // The twin's pipeline is not built yet: the mesh still draws this
                // frame with its own program, without its guides (counted).
                bind_world_targets_d3d12(history,motion_bound);
                if(cover_traced)d3d12_->set_constants(5,native12_.point_light_constants.data(),sizeof(native12_.point_light_constants)); // its own lamps and bounce light
                hr=d3d12_->draw_indexed_indirect(g.programs[vertex],g.programs[pixel],m.neighbour?single_state:state,nullptr,*g.visible_args,mesh*20);
                if(cover_traced)d3d12_->set_constants(5,traced_lights.data(),sizeof(traced_lights));
                bind_world_targets_d3d12(history,motion_bound,true);
                d3d12_->count("ground cover draws without Ray Reconstruction guides (twin pipeline not ready)");
            }
            if(hr!=S_OK)return fail("ground cover material pipeline not ready",hr);
            ++map_draws;
        }
        if(motion_bound||cover_guides)bind_world_targets_d3d12(history);
        if(cover_guides&&map_draws)d3d12_->count("ground cover maps with their Ray Reconstruction guides");
        // Counted in this map's own profiler pass (ground_cover_draw or ground_cover_neighbours).
        profiler_.add_draw(map_draws,map_draws);profiler_.add_batch(map_draws);(p?neighbour_draws:draws)+=map_draws;
        if(world_history_bound12_&&map_draws)shadow_history_written_=true;
        // Its sun casters for the next frame's shadow pass: these frame
        // constants and ground, its caster lists and instances in place.
        if(cover_shadows>0&&p<g.casters.size()){
            auto& pass=g.casters[p];const auto* bytes=reinterpret_cast<const std::uint8_t*>(&mc);
            pass.frame.assign(bytes,bytes+sizeof(mc));pass.ground=map.ground;if(!p)gc_shadow_ready_=true;
        }
        d3d12_->clear_slots();d3d12_->set_index_buffer(nullptr);
    }
    if(world_history_bound12_)bind_world_targets_d3d12(); // later passes never write the history
    d3d12_->count("ground cover colour passes");if(neighbour_draws)d3d12_->count("neighbour ground cover passes");
    if(g.read_pending<g.readbacks.size()){
        const UINT slot=(g.read_head+g.read_pending)%UINT(g.readbacks.size());
        if(FAILED(hr=d3d12_->request_readback(*g.counts,UINT64(passes)*gm_count*4,g.readbacks[slot],"ground cover demand")))return fail("ground cover demand copy",hr);++g.read_pending;
    }
    gc_drawn_frame_=native_frame_index_;scene_updated_=true;return S_OK;
} catch(const std::bad_alloc&){d3d12_->omit("ground cover CPU allocation",true);return E_OUTOFMEMORY;}

// Sun shadow casters of the ground cover on D3D12 (draw_ground_cover_shadows'
// contract): the previous frame's recorded passes, each mesh's caster list of
// this close map from its own pool region, depth only. The caller binds the
// slice's shadow constants (b2) and depth target.
void Renderer::draw_ground_cover_shadows_d3d12(UINT slice,const RECT* scissor) noexcept {
    auto& g=gc12_;
    if(slice>=3||!d3d12_)return;
    const auto& config=debug_overlay_.config();
    const int level=configured_integer(config,"ground_cover_shadows",1);
    if(level<=0||configured_integer(config,"ground_cover",2)<=0||g.programs.size()!=gp_count||!g.vertices||!g.indices||!g.instances||
       !g.draw_inputs||!g.caster_args||!g.caster_slots)return;
    const std::uint64_t meshes=gc_shadow_caster_mask(level);
    if(!meshes)return;
    if(profiler_.pass_active())profiler_.sub_scope(level>=2?"ground_cover_all":"ground_cover_large");
    // Two-sided like the cover's own pass; depth only, the map's depth test.
    D3D12NativeState state;state.raster=fixed_raster_desc(D3DCULL_NONE,false,false);state.depth=fixed_depth_desc(D3DZB_TRUE,TRUE,D3DCMP_LESSEQUAL);
    static const ShaderProgram no_pixel{};
    d3d12_->set_resource(27,g.vertices.get());d3d12_->set_resource(28,g.instances.get());
    d3d12_->set_resource(66,g.caster_slots.get());d3d12_->set_resource(67,g.draw_inputs.get());
    d3d12_->set_resource(69,g.hosted_vertices.get());d3d12_->set_resource(113,g.hosted_indices.get()); // hosted plants' vertices and indices
    d3d12_->set_resource(127,native12_.terrain_frame.get());d3d12_->set_resource(72,fire_burn_mask12());
    // Plant cards cast through their alpha test (kind 5): their art and sampler.
    d3d12_->set_resource(2,g.map.bark.get());d3d12_->set_resource(22,g.map.plant[0].get());d3d12_->set_resource(23,g.map.plant[1].get());
    d3d12_->set_resource(24,g.map.plant[2].get());d3d12_->set_resource(60,g.map.plant[3].get());
    d3d12_->set_sampler(9,world_texture_sampler(D3DTFN_LINEAR,D3DTFG_LINEAR,D3DTFP_LINEAR,D3DTADDRESS_WRAP,D3DTADDRESS_WRAP,true,
        1u<<configured_integer(config,"anisotropy",3)).description);
    d3d12_->set_index_buffer(g.indices.get());
    UINT draws=0;bool stopped=false;
    const UINT passes=std::min({UINT(g.casters.size()),UINT(gc_capacity_.size()),g.caster_passes});
    for(UINT p=0;p<passes&&!stopped;++p){
        const auto& pass=g.casters[p];
        if(pass.frame.empty()||!pass.ground)continue;
        d3d12_->set_constants(11,pass.frame.data(),UINT(pass.frame.size()));d3d12_->set_resource(31,pass.ground.get());
        for(UINT mesh=0;mesh<gm_count;++mesh){
            if(!((meshes>>mesh)&1u)||!gc_capacity_[p][mesh])continue;
            // gcDrawBase: this close map's list third, then the mesh's pool base.
            const UINT base[4]{slice*g.pool_capacity+gc_base_[p][mesh],0,gc_mesh_first_vertex_[mesh],hosted_mesh(mesh)?(mesh==gm_hosted_dead_vines?1u:2u):0u};
            d3d12_->set_constants(12,base,sizeof(base));
            const bool plant=gc_mesh_kind_[mesh]==5;
            const HRESULT hr=d3d12_->draw_indexed_indirect(g.programs[hosted_mesh(mesh)?gp_hosted_shadow_vs:plant?gp_plant_shadow_vs:gp_shadow_vs],plant?g.programs[gp_plant_shadow_ps]:no_pixel,
                state,scissor,*g.caster_args,((p*3+slice)*gm_count+mesh)*20);
            if(hr!=S_OK){d3d12_->omit(FAILED(hr)?"ground cover sun casters (draw failed)":"ground cover sun casters (pipeline not ready)",false);stopped=true;break;}
            ++draws;
        }
    }
    if(profiler_.pass_active())profiler_.add_sub_draw(draws,0);
    d3d12_->set_resource(27,static_cast<const D3D12Buffer*>(nullptr));d3d12_->set_resource(28,static_cast<const D3D12Buffer*>(nullptr));
    d3d12_->set_resource(31,static_cast<const D3D12Texture*>(nullptr));
    for(const UINT slot:{2u,22u,23u,24u,60u})d3d12_->set_resource(slot,static_cast<const D3D12Texture*>(nullptr));
    d3d12_->set_resource(66,static_cast<const D3D12Buffer*>(nullptr));d3d12_->set_resource(67,static_cast<const D3D12Buffer*>(nullptr));
    d3d12_->set_resource(69,static_cast<const D3D12Buffer*>(nullptr));d3d12_->set_resource(113,static_cast<const D3D12Buffer*>(nullptr));
    d3d12_->set_index_buffer(nullptr);
}

// update_ground_cover_terrain2 on D3D12: the active map's terrain 2.0 class
// shares and colour map (terrain2CoverCS), once per change of the same key
// inputs, into storage textures (the class shares two RGBA8 slices, the
// colour map the per-tile paint's size, format and levels). No masks or
// styles buffer exist here (no program reads them), so their key bits stay
// 0; the styles' readiness is the active entry's style serial. While
// terrain2 is on and they are not built, land cover is absent.
void Renderer::update_ground_cover_terrain2_d3d12() noexcept try {
    const auto& config = debug_overlay_.config();
    auto& paint = gc12_.map;
    const UINT tx = ambient_tiles_x_, ty = ambient_tiles_y_;
    const bool ready = !gc_t2_failed_ && tx && ty && tile_blend_rows_ && contact_maps_.tiles12 && contact_maps_.atlas12 &&
                       gc_t2_cell_class12_ && gc_t2_tile_classes_.size() == std::size_t(tx) * ty && native12_.terrain_noise &&
                       paint.color && paint.width == tx && paint.height_tiles == ty &&
                       terrain2_table_count_ && terrain2_table_[0] == 0 && terrain2_map_.style_serial;
    if (!ready) { if (gc_t2_classes12_ || gc_t2_color12_) { gc_t2_classes12_.reset(); gc_t2_color12_.reset(); gc_t2_key_ = 0; gc_t2_valid_ = false; } return; }
    const bool low = !configured_terrain_material(config).height_blend;
    Terrain2CoverConstants k{};
    k.terrain2[0] = 1; k.terrain2[2] = float(terrain2_mask_bits_);
    k.maps[1][3] = 1;
    k.material[0] = configured_integer(config, "terrain_quality", 2) <= 1 ? float(0x100000) : 0.f; // T2_LEAN, as the ground draws
    k.material[2] = low ? 0.f : 1.f;
    k.cliff[3] = tile_blend_margin_;
    k.copied[0] = std::uint32_t(contact_maps_.ready); k.copied[1] = std::uint32_t(contact_maps_.ready >> 32);
    std::memcpy(&k.maps[2][2], k.copied, sizeof(k.copied));
    k.layout[0] = terrain_layout_origin_[0]; k.layout[1] = terrain_layout_origin_[1];
    k.grid[0] = tx * kTerrain2CoverTexels; k.grid[1] = ty * kTerrain2CoverTexels; k.grid[2] = kTerrain2CoverTexels;
    {
        // Record 3 (terrain2.hlsl terrain2Index): the bindless indices of the
        // map's grid, atlas and (once readable, terrain2_records_d3d12) heights.
        const bool heights = terrain2_map_.height12 && !terrain2_map_.mips12 && !terrain2_map_.baked.empty();
        std::uint32_t index[3]{d3d12_->bindless_index(*contact_maps_.tiles12), d3d12_->bindless_index(*contact_maps_.atlas12),
                               heights ? d3d12_->bindless_index(*terrain2_map_.height12) : 0u};
        if (index[0] == UINT32_MAX || index[1] == UINT32_MAX || index[2] == UINT32_MAX) return; // the bindless region grows next frame
        const float width = float(contact_maps_.width);
        std::memcpy(k.maps[3], index, sizeof(index)); std::memcpy(&k.maps[3][3], &width, sizeof(width));
    }
    std::uint64_t key = gc_hash(gc_hash(ambient_map_generation_, gc_t2_rows_generation_), terrain2_map_.baked.size());
    key = gc_hash(key, terrain2_map_.style_serial);
    {
        std::uint32_t words[sizeof(k) / 4]; std::memcpy(words, &k, sizeof(k));
        for (const std::uint32_t word : words) key = gc_hash(key, word);
    }
    if (key == gc_t2_key_ && gc_t2_valid_) return;
    const auto fail = [&](const char* reason, HRESULT hr) {
        gc_t2_classes12_.reset(); gc_t2_color12_.reset(); gc_t2_key_ = 0; gc_t2_valid_ = false; gc_t2_failed_ = true;
        char text[192]; std::snprintf(text, sizeof(text), "omitted reason=%s (D3D12 0x%08lx); no land cover on terrain 2.0", reason, static_cast<unsigned long>(hr));
        bridge_append_probe_event("ground_cover_terrain2", text);
    };
    if (gc_t2_programs12_.empty()) append_terrain2_cover_programs(gc_t2_programs12_);
    if (gc_t2_programs12_.empty()) return fail("program", E_FAIL);
    UINT cw = 0, ch = 0, mips = 0; DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    if (!d3d12_->texture_info(*paint.color, cw, ch, mips, format) || cw * 2 != k.grid[0] || ch * 2 != k.grid[1]) return fail("colour map layout", E_INVALIDARG);
    UINT have_w = 0, have_h = 0, have_mips = 0; DXGI_FORMAT have_format = DXGI_FORMAT_UNKNOWN;
    if (!gc_t2_classes12_ || !d3d12_->texture_info(*gc_t2_classes12_, have_w, have_h, have_mips, have_format) ||
        have_w != k.grid[0] || have_h != k.grid[1] || !gc_t2_color12_) {
        gc_t2_classes12_.reset(); gc_t2_color12_.reset();
        HRESULT hr = d3d12_->create_storage_texture(k.grid[0], k.grid[1], 0, DXGI_FORMAT_R8G8B8A8_UNORM, gc_t2_classes12_, 1, 2);
        if (SUCCEEDED(hr)) hr = d3d12_->create_storage_texture(cw, ch, 0, format, gc_t2_color12_, mips);
        if (FAILED(hr)) return fail("textures", hr);
    }
    D3D11_SAMPLER_DESC clamp{}; clamp.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    clamp.AddressU = clamp.AddressV = clamp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; clamp.MaxLOD = D3D11_FLOAT32_MAX;
    D3D11_SAMPLER_DESC wrap{}; wrap.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    wrap.AddressU = wrap.AddressV = wrap.AddressW = D3D11_TEXTURE_ADDRESS_WRAP; wrap.MaxLOD = 0;
    d3d12_->clear_slots();
    d3d12_->set_constants(0, &k, sizeof(k));
    d3d12_->set_resource(17, gc_t2_cell_class12_.get());
    d3d12_->set_resource(PointShadowTextureLayout::terrain_noise_srv, native12_.terrain_noise.get());
    d3d12_->set_sampler(8, clamp); d3d12_->set_sampler(9, clamp); d3d12_->set_sampler(12, wrap);
    d3d12_->set_unordered(0, gc_t2_classes12_.get()); d3d12_->set_unordered(1, gc_t2_color12_.get());
    HRESULT hr = d3d12_->dispatch(gc_t2_programs12_[0], dispatch_groups(k.grid[0], 8), dispatch_groups(k.grid[1], 8), 1);
    d3d12_->clear_slots();
    if (FAILED(hr)) return fail("dispatch", hr);
    if (hr != S_OK) { d3d12_->omit("ground cover on terrain 2.0 classes (pipeline not ready)", false); return; }
    hr = d3d12_->generate_mips(*gc_t2_color12_, d3d12_mip_program());
    if (FAILED(hr)) return fail("colour levels", hr);
    if (hr != S_OK) return;
    gc_t2_key_ = key; gc_t2_valid_ = true;
    char text[160];
    std::snprintf(text, sizeof(text), "ready texels=%ux%u cells_baked=%zu slices=0x%llx backend=D3D12", k.grid[0], k.grid[1],
                  terrain2_map_.baked.size(), static_cast<unsigned long long>(contact_maps_.ready));
    bridge_append_probe_event("ground_cover_terrain2", text);
} catch (...) { gc_t2_classes12_.reset(); gc_t2_color12_.reset(); gc_t2_valid_ = false; }
