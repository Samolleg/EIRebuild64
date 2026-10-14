// Renderer-owned ambient particles: camera-local, area-tied atmosphere (pollen,
// leaves, Dead City motes, cave dust/drips/spores, lava embers/ash, desert
// dust, fireflies). Stateless GPU particles: every position is a pure function
// of the particle index, the game clock and a per-emitter drift integrated on
// the CPU from game time, wrapped into one world-periodic tile repeated over
// the visible ground (the wrapped rain volume of GD3D11's DrawRain, without
// its simulation buffers). Density comes from the immutable MPR/MOB area classes sampled per
// particle on the GPU and the game's hour/weather/wind; see ambient_particles.h.
#include "ambient_particles.h"
#include "ambient_particle_frame.h"
#include "ambient_terrain_grids.h"
#include "ambient_mushroom_sources.h"
#include "owned_asset_loader.h"
#include "owned_terrain_front_end.h"
#include "renderer.h"
#include "engine_bridge.h"
#include "world_fog.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace owned {
namespace {
using namespace ambient;


static_assert(em_count == 15, "ambient_displacement_ holds one drift per emitter");

std::string ambient_basename(std::string name) {
    const auto slash = name.find_last_of("\\/");
    if (slash != std::string::npos) name.erase(0, slash + 1);
    const auto dot = name.find_last_of('.');
    if (dot != std::string::npos) name.erase(dot);
    for (char& c : name) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    return name;
}

HRESULT ambient_texture(ID3D11Device* device, UINT width, UINT height, DXGI_FORMAT format, const void* data,
                        UINT pitch, ComPtr<ID3D11ShaderResourceView>& view, bool updatable = false) {
    D3D11_TEXTURE2D_DESC d{};d.Width=width;d.Height=height;d.MipLevels=d.ArraySize=1;d.Format=format;
    d.SampleDesc.Count=1;d.Usage=updatable?D3D11_USAGE_DEFAULT:D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial{data,pitch,0};ComPtr<ID3D11Texture2D> texture;
    HRESULT hr=device->CreateTexture2D(&d,&initial,&texture);
    return FAILED(hr)?hr:device->CreateShaderResourceView(texture.Get(),nullptr,&view);
}
} // namespace

// A layer shift step (game water level) rewrites only the liquid-surface
// heights, wet area classes and wet emissive tiles, in place, and uploads the
// two small textures they live in. Bounded by the liquid vertices/tiles.
bool Renderer::update_ambient_liquid_level(const std::vector<float>& shifts) noexcept try {
    const auto previous_emissive = emissive_tiles_.size();
    if (!apply_liquid_level(ambient_liquid_level_, &shifts, ambient_height_, ambient_area_, emissive_tiles_)) return false;
    const UINT tiles_x = ambient_tiles_x_, verts_x = tiles_x*2u+1u;
    const auto upload = [&](ID3D11ShaderResourceView* view, const void* data, UINT pitch) {
        ComPtr<ID3D11Resource> resource;view->GetResource(&resource);
        context_->UpdateSubresource(resource.Get(),0,nullptr,data,pitch,0);
    };
    upload(ambient_area_srv_.Get(),ambient_area_.data(),tiles_x);
    upload(ambient_height_srv_.Get(),ambient_height_.data(),verts_x*4u);
    // Lava lights follow only a changed lava tile set (none on most maps).
    if (previous_emissive || !emissive_tiles_.empty()) emissive_generation_ = UINT64_MAX;
    ++ambient_liquid_level_updates_;
    return true;
} catch (const std::bad_alloc&) {
    return false;
}

// The shared world atmosphere (b10/t11/s7) is prepended so particles use the
// exact fog functions of world surfaces.
const std::string& owned_ambient_particle_shader_source() {
    static const std::string source = ambient_particle_shader_source();
    return source;
}

void append_ambient_particle_shader_programs(std::vector<ShaderProgram>& out) {
    const std::string& source = owned_ambient_particle_shader_source();
    out.push_back({source, "ambient", nullptr, "ambientVS", "vs_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
    out.push_back({source, "ambient", nullptr, "ambientPS", "ps_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
    out.push_back({source, "ambient", nullptr, "fallShellVS", "vs_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
    out.push_back({source, "ambient", nullptr, "fallShellPS", "ps_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
}

// Base resources belong to the immutable terrain and are built once per map:
// heights and wet classes at the current layer shifts, spray from the flow
// of the authored rest surface. Later layer shifts update in place.
bool Renderer::rebuild_ambient_particle_map(const AmbientWorldSource& inputs, const WorldEnvironment& environment) noexcept try {
    if (!inputs.terrain || !inputs.geometry) return false;
    const auto& terrain = *inputs.terrain;
    const bool same = !ambient_terrain_.owner_before(inputs.terrain) && !inputs.terrain.owner_before(ambient_terrain_) &&
        !ambient_terrain_.expired();
    const std::string name = ambient_basename(inputs.map && !inputs.map->terrain_resource.empty()
        ? inputs.map->terrain_resource : terrain.project_resource);
    ambient_menu_ = name.find("mainmenu") != std::string::npos;
    ambient_zone_ = classify_zone(name.c_str(), environment.cave, environment.region);
    // The same map never rebuilds: animation/noise change nothing here and a
    // layer shift step is a bounded in-place update.
    const bool same_map = !ambient_source_map_.owner_before(inputs.map) && !inputs.map.owner_before(ambient_source_map_);
    if (same && same_map && ambient_geometry_) {
        const bool ready = ambient_area_srv_ && ambient_height_srv_ && ambient_ground_srv_ && ambient_type_srv_ && ambient_sources_srv_;
        if (!ready) return false;
        if (ambient_geometry_->layer_shifts != inputs.geometry->layer_shifts &&
            !update_ambient_liquid_level(inputs.geometry->layer_shifts)) return false;
        ambient_geometry_ = inputs.geometry;
        return true;
    }
    if (!same) {
        ambient_terrain_ = inputs.terrain;ambient_geometry_.reset();
        ambient_area_srv_.Reset();ambient_height_srv_.Reset();ambient_ground_srv_.Reset();
        ambient_type_srv_.Reset();ambient_sources_srv_.Reset();emissive_tiles_.clear();
        ++ambient_map_generation_;
        ambient_displacement_ = {};ambient_sand_front_ = 0;
        ambient_weather_hours_ = -1;  // a new map snaps the weather ramp
    }
    TerrainGrids grids;
    if (!build_terrain_grids(terrain, inputs.map.get(), grids, inputs.geometry.get())) return false;
    const std::uint32_t tiles_x = grids.tiles_x, tiles_y = grids.tiles_y, verts_x = tiles_x*2u+1u, verts_y = tiles_y*2u+1u;
    const auto& types = grids.types; const auto& ground = grids.ground;
    build_native_contact_cells(ground, verts_x, verts_y); // the contact blend's batch test (renderer.cpp)
    ambient_sources_.assign(std::size_t(tiles_x)*tiles_y,{});
    mushroom_sources(inputs.map.get(), tiles_x, tiles_y, ambient_sources_);
    for (std::uint32_t y=0;y<tiles_y;++y) for (std::uint32_t x=0;x<tiles_x;++x)
        ambient_sources_[std::size_t(y)*tiles_x+x][1] =
            static_cast<std::uint8_t>(std::lround(cave_drip_at(grids,x,y)*255.f));
    ambient_liquid_level_ = std::move(grids.level);
    ambient_area_ = std::move(grids.area);emissive_tiles_ = std::move(grids.emissive);
    ambient_height_ = std::move(grids.height);ambient_tiles_x_ = tiles_x;ambient_tiles_y_ = tiles_y;
    start_terrain_bake_cells(); // the terrain material cache's cell tables, on a worker
    // Waterfall sources (impact, lip) of every tile a level can flood, from
    // the flow of the surface shown at map build (ambient_particle_frame.h).
    ambient_fall_sources(terrain,inputs.geometry.get(),ambient_liquid_level_.liquid_area,tiles_x,ambient_sources_);
    {
        auto fall_tiles = ambient_fall_tiles(ambient_sources_,tiles_x,ambient_fall_impact_count_);
        ambient_fall_tile_count_ = std::uint32_t(fall_tiles.size());
        if (fall_tiles.empty()) fall_tiles.push_back(0); // a valid one-element buffer, never read
        D3D11_BUFFER_DESC d{};d.ByteWidth=UINT(fall_tiles.size()*4u);d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        const D3D11_SUBRESOURCE_DATA initial{fall_tiles.data(),0,0};
        D3D11_SHADER_RESOURCE_VIEW_DESC v{};v.Format=DXGI_FORMAT_R32_UINT;v.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;v.Buffer.NumElements=UINT(fall_tiles.size());
        ComPtr<ID3D11Buffer> buffer;ambient_fall_tiles_srv_.Reset();
        if (FAILED(device_->CreateBuffer(&d,&initial,&buffer)) || FAILED(device_->CreateShaderResourceView(buffer.Get(),&v,&ambient_fall_tiles_srv_))) {
            ambient_failed_ = true;
            OutputDebugStringA("EID3D11 ambient particles: waterfall tile list unavailable; ambient particles omitted.\n");
            return false;
        }
        const auto& falls = procedural_fall_map(terrain)->falls;
        const auto list = ambient_fall_list(falls);
        ambient_fall_count_ = std::uint32_t(falls.size());
        D3D11_BUFFER_DESC fd{};fd.ByteWidth=UINT(list.size()*16u);fd.Usage=D3D11_USAGE_IMMUTABLE;fd.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        const D3D11_SUBRESOURCE_DATA fall_data{list.data(),0,0};
        D3D11_SHADER_RESOURCE_VIEW_DESC fv{};fv.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;fv.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;fv.Buffer.NumElements=UINT(list.size());
        ComPtr<ID3D11Buffer> fall_buffer;ambient_falls_srv_.Reset();
        if (FAILED(device_->CreateBuffer(&fd,&fall_data,&fall_buffer)) || FAILED(device_->CreateShaderResourceView(fall_buffer.Get(),&fv,&ambient_falls_srv_))) {
            ambient_failed_ = true;
            OutputDebugStringA("EID3D11 ambient particles: procedural fall list unavailable; ambient particles omitted.\n");
            return false;
        }
        // Waterfall shells: one immutable buffer of every fall's curtain.
        const auto& shells = procedural_fall_map(terrain)->shells;
        ambient_fall_shell_srv_.Reset();ambient_fall_shell_vertices_ = 0;
        if (!shells.empty()) {
            D3D11_BUFFER_DESC sd{};sd.ByteWidth=UINT(shells.size()*16u);sd.Usage=D3D11_USAGE_IMMUTABLE;sd.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            const D3D11_SUBRESOURCE_DATA shell_data{shells.data(),0,0};
            D3D11_SHADER_RESOURCE_VIEW_DESC sv{};sv.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;sv.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;sv.Buffer.NumElements=UINT(shells.size());
            ComPtr<ID3D11Buffer> shell_buffer;
            if (FAILED(device_->CreateBuffer(&sd,&shell_data,&shell_buffer)) || FAILED(device_->CreateShaderResourceView(shell_buffer.Get(),&sv,&ambient_fall_shell_srv_))) {
                ambient_fall_shell_srv_.Reset();
                OutputDebugStringA("EID3D11 waterfall shells: buffer unavailable; waterfall foam omitted.\n");
            } else ambient_fall_shell_vertices_ = UINT(shells.size()/3u);
        }
    }
    ambient_area_srv_.Reset();ambient_height_srv_.Reset();ambient_sources_srv_.Reset();
    if (FAILED(ambient_texture(device_.Get(),tiles_x,tiles_y,DXGI_FORMAT_R8_UINT,ambient_area_.data(),tiles_x,ambient_area_srv_,true)) ||
        FAILED(ambient_texture(device_.Get(),verts_x,verts_y,DXGI_FORMAT_R32_FLOAT,ambient_height_.data(),verts_x*4u,ambient_height_srv_,true)) ||
        (!same && FAILED(ambient_texture(device_.Get(),verts_x,verts_y,DXGI_FORMAT_R32_FLOAT,ground.data(),verts_x*4u,ambient_ground_srv_))) ||
        (!same && FAILED(ambient_texture(device_.Get(),tiles_x,tiles_y,DXGI_FORMAT_R8_UINT,types.data(),tiles_x,ambient_type_srv_))) ||
        FAILED(ambient_texture(device_.Get(),tiles_x,tiles_y,DXGI_FORMAT_R8G8B8A8_UNORM,ambient_sources_.data(),tiles_x*4u,ambient_sources_srv_))) {
        ambient_failed_ = true;
        OutputDebugStringA("EID3D11 ambient particles: area/height textures unavailable; ambient particles omitted.\n");
        return false;
    }
    ambient_geometry_ = inputs.geometry;
    ambient_source_map_ = inputs.map;
    emissive_generation_ = UINT64_MAX; // a new map's emissive tiles
    return true;
} catch (const std::bad_alloc&) {
    ambient_failed_ = true;
    OutputDebugStringA("EID3D11 ambient particles: CPU allocation failed; ambient map omitted.\n");
    bridge_append_probe_event("ambient_map_omitted", "CPU allocation failed");
    return false;
}

void Renderer::render_ambient_particles() {
    const int level = configured_integer(debug_overlay_.config(),"ambient_particles",1);
    // F10 line: the outcome of this pass, or the first reason it drew nothing.
    ambient_status_frame_ = native_frame_index_;
    const auto status = [&](bool problem, const char* format, auto... values) {
        std::snprintf(ambient_status_,sizeof(ambient_status_),format,values...);ambient_status_problem_ = problem;
    };
    if (level <= 0) return status(false,"Particles: off");
    if (ambient_failed_) return status(true,"Particles: disabled after a resource/shader failure (see debug output)");
    // Only a frame whose world terrain was drawn has a world camera and depth.
    if (ambient_terrain_frame_ != native_frame_index_) return status(true,"Particles: no terrain frame this frame");
    if (!native_world_clip_valid_) return status(true,"Particles: no world camera");
    if (!depth_srv_ || !rtv_) return status(true,"Particles: no scene depth/target");
    AmbientWorldSource inputs;
    if (!bridge_ambient_world_source(inputs) || !inputs.terrain) return status(true,"Particles: no terrain source");
    // The one environment reader shared with the fog (clock, cave, region,
    // weather). The fog snapshot belongs to the bridge frame of the scene
    // capture; composition can run after that frame advanced, so read the
    // same values directly then instead of dropping the particles.
    WorldEnvironment environment = bridge_world_fog_snapshot().environment;
    const char* environment_source = "fog frame";
    if (!environment.valid) { bridge_read_world_environment(environment); environment_source = "direct"; }
    if (!environment.valid) return status(true,"Particles: game clock/weather unreadable");
    if (!rebuild_ambient_particle_map(inputs,environment)) return status(true,"Particles: area/height grid unavailable");
    if (ambient_menu_) return status(false,"Particles: main menu map (none)");
    const auto fail = [&](const char* reason) {
        ambient_failed_ = true; OutputDebugStringA(reason);
        status(true,"Particles: disabled after a resource/shader failure (see debug output)");
    };
    if (!ambient_vs_) {
        ComPtr<ID3DBlob> vs, ps, errors;
        std::vector<ShaderProgram> programs;
        append_ambient_particle_shader_programs(programs);
        if (FAILED(compile_shader(programs[0], &vs, &errors)) || FAILED(compile_shader(programs[1], &ps, &errors))) {
            if (errors) OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
            return fail("EID3D11 ambient particles: shaders unavailable; ambient particles omitted.\n");
        }
        D3D11_BUFFER_DESC b{};b.ByteWidth=sizeof(AmbientConstants);b.Usage=D3D11_USAGE_DYNAMIC;
        b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;b.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        D3D11_BLEND_DESC blend{};auto& t=blend.RenderTarget[0];t.BlendEnable=TRUE;
        t.SrcBlend=t.SrcBlendAlpha=D3D11_BLEND_ONE;t.DestBlend=t.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;
        t.BlendOp=t.BlendOpAlpha=D3D11_BLEND_OP_ADD;t.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;
        if (FAILED(device_->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&ambient_vs_)) ||
            FAILED(device_->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&ambient_ps_)) ||
            FAILED(device_->CreateBuffer(&b,nullptr,&ambient_constants_)) ||
            FAILED(device_->CreateBlendState(&blend,&ambient_blend_)) ||
            FAILED(device_->CreateRasterizerState(&raster,&ambient_raster_))) {
            ambient_vs_.Reset();
            return fail("EID3D11 ambient particles: pipeline objects unavailable; ambient particles omitted.\n");
        }
    }
    const Quality quality = quality_level(level);
    if (level != ambient_level_) { ambient_level_ = level; ambient_displacement_ = {}; }

    // Game time: frozen while paused, faster at fast speed; loads and jumps do not drift.
    const double seconds = environment.world_time*kAmbientSecondsPerSegment;
    double dt = ambient_time_ < 0 ? 0 : seconds - ambient_time_;
    if (!(dt >= 0 && dt < 2)) dt = 0;
    ambient_time_ = seconds;

    // Camera, focus and particle window (ambient_particle_frame.h).
    AmbientView view;
    if (const char* failure = ambient_view(native_world_clip_,target_width(),ambient_height_,ambient_tiles_x_,ambient_tiles_y_,quality,
            seconds,configured_floating_point(debug_overlay_.config(),"ambient_particle_size",1.f),view))
        return status(true,"%s",failure);
    const auto& focus = view.focus;const double reach = view.reach;
    const auto& window_min = view.window_min;const auto& window_max = view.window_max;
    const unsigned columns = view.columns, rows = view.rows;
    const float density = configured_floating_point(debug_overlay_.config(),"ambient_particle_density",1.f);
    AmbientConstants& c = view.constants;

    // Environment: region, cave, hour and weather from the game; wind is the
    // renderer's shared wind of this world frame (the map's wind when wind
    // variation is off), with its global gust pulse.
    const auto zone = static_cast<ZoneClass>(ambient_zone_);
    const float night = fog_night(environment.hour);
    // Weather thins the particles out and back in with the rain fog's ramp.
    const double weather_elapsed = ambient_weather_hours_ < 0 ? -1.0 : environment.game_hours - ambient_weather_hours_;
    const Weather weather = ramp_weather({ambient_rain_,ambient_snow_}, weather_amount(environment), weather_elapsed);
    ambient_rain_ = weather.rain;ambient_snow_ = weather.snow;ambient_weather_hours_ = environment.game_hours;
    const bool sheltered = zone == zone_cave || zone == zone_gipat_cave;
    const WeatherWindGpu& shared_wind = weather_wind();
    const double wind[2]{shared_wind.direction[0],shared_wind.direction[1]};
    const double wind_speed = (.35+2.4*std::clamp(double(inputs.wind_force_normalized),0.0,1.0))*(sheltered?.1:1.0)*
        double(shared_wind.speed)*double(shared_wind.pulse);

    if (native_world_lighting_valid_) {
        for (unsigned k = 0; k < 3; ++k) {
            c.ambient[k] = native_world_lighting_.ambient[k];c.sun_direction[k] = native_world_lighting_.sunlight_direction[k];
            c.sun_color[k] = native_world_lighting_.sunlight_diffuse[k];
        }
    } else { c.ambient[0] = c.ambient[1] = c.ambient[2] = .2f; c.sun_direction[2] = -1; c.sun_color[0] = c.sun_color[1] = c.sun_color[2] = .6f; }
    const auto& fog = ambient_terrain_state_;
    c.fog_range[0] = fog.fog_min[0];c.fog_range[1] = fog.fog_min[1];c.fog_range[2] = fog.fog_max[0];c.fog_range[3] = fog.fog_max[1];
    c.fog_control[0] = fog.fog_distance;c.fog_control[1] = fog.fog_enabled ? 1.f : 0.f;
    neighbour_fog_constants(neighbour_fog_, c.fog_seam, c.fog_curve, c.fog_span_lo, c.fog_span_hi);
    c.fog_color[0] = ((ambient_fog_color_>>16)&255)/255.f;c.fog_color[1] = ((ambient_fog_color_>>8)&255)/255.f;
    c.fog_color[2] = (ambient_fog_color_&255)/255.f;

    // Rebind the shared world atmosphere before drawing. A binding failure
    // omits particles instead of drawing an unfogged overlay.
    if (FAILED(bind_world_fog())) return status(true,"Particles: world atmosphere unavailable (omitted)");

    // Waterfall shells (procedural_falls.h build_fall_shell): each fall's
    // foam as its own curtain over the map's liquid overlay, depth tested
    // against the world (read only), drawn before the spray and mist.
    if (ambient_fall_shell_srv_ && ambient_fall_shell_vertices_ && world_dsv_ && SUCCEEDED(ensure_liquid_normal_map())) {
        if (!fall_shell_vs_) {
            ComPtr<ID3DBlob> vs, ps, errors;
            std::vector<ShaderProgram> programs;
            append_ambient_particle_shader_programs(programs);
            D3D11_DEPTH_STENCIL_DESC depth{};depth.DepthEnable=TRUE;depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;
            depth.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;
            if (FAILED(compile_shader(programs[2], &vs, &errors)) || FAILED(compile_shader(programs[3], &ps, &errors)) ||
                FAILED(device_->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&fall_shell_vs_)) ||
                FAILED(device_->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&fall_shell_ps_)) ||
                FAILED(device_->CreateDepthStencilState(&depth,&fall_shell_depth_))) {
                if (errors) OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
                OutputDebugStringA("EID3D11 waterfall shells: shaders unavailable; waterfall foam omitted.\n");
                fall_shell_vs_.Reset();ambient_fall_shell_srv_.Reset();ambient_fall_shell_vertices_ = 0;
            }
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (fall_shell_vs_ && SUCCEEDED(context_->Map(ambient_constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped))) {
            std::memcpy(mapped.pData,&c,sizeof(c));context_->Unmap(ambient_constants_.Get(),0);
            transition_profile_pass("waterfall_shells");
            ID3D11ShaderResourceView* saved_shell=nullptr;context_->VSGetShaderResources(6,1,&saved_shell);
            ID3D11ShaderResourceView* saved_detail=nullptr;context_->PSGetShaderResources(7,1,&saved_detail);
            ID3D11SamplerState* saved_sampler=nullptr;context_->PSGetSamplers(0,1,&saved_sampler);
            ID3D11ShaderResourceView* no_depth=nullptr;context_->PSSetShaderResources(2,1,&no_depth); // depth is bound for testing
            context_->OMSetRenderTargets(1,rtv_.GetAddressOf(),world_dsv_.Get());
            D3D11_VIEWPORT viewport{0, 0, float(target_width()), float(target_height()), 0, 1};context_->RSSetViewports(1,&viewport);
            context_->RSSetState(ambient_raster_.Get());context_->OMSetDepthStencilState(fall_shell_depth_.Get(),0);
            const float factor[4]{1,1,1,1};context_->OMSetBlendState(ambient_blend_.Get(),factor,~0u);
            context_->IASetInputLayout(nullptr);context_->IASetVertexBuffers(0,0,nullptr,nullptr,nullptr);
            context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context_->VSSetShader(fall_shell_vs_.Get(),nullptr,0);context_->PSSetShader(fall_shell_ps_.Get(),nullptr,0);
            context_->GSSetShader(nullptr,nullptr,0);
            context_->VSSetConstantBuffers(0,1,ambient_constants_.GetAddressOf());context_->PSSetConstantBuffers(0,1,ambient_constants_.GetAddressOf());
            context_->VSSetShaderResources(6,1,ambient_fall_shell_srv_.GetAddressOf());
            context_->PSSetShaderResources(7,1,liquid_detail_srv_.GetAddressOf());
            context_->PSSetSamplers(0,1,liquid_normal_sampler_.GetAddressOf());
            bind_owned_sky_pixel_inputs();  // borderFogColor reads the sky probe
            context_->Draw(ambient_fall_shell_vertices_,0);
            context_->OMSetDepthStencilState(nullptr,0);
            context_->VSSetShaderResources(6,1,&saved_shell);context_->PSSetShaderResources(7,1,&saved_detail);
            context_->PSSetSamplers(0,1,&saved_sampler);
            if (saved_shell) saved_shell->Release();
            if (saved_detail) saved_detail->Release();
            if (saved_sampler) saved_sampler->Release();
            profiler_.add_draw(ambient_fall_shell_vertices_/3u,1);profiler_.add_batch(1);
        }
    }

    // Runs inside composite_world's state guard; the slots it does not save are restored here.
    ID3D11ShaderResourceView* saved_vs[6]{};context_->VSGetShaderResources(0,6,saved_vs);
    ID3D11ShaderResourceView* saved_ps2=nullptr;context_->PSGetShaderResources(2,1,&saved_ps2);
    transition_profile_pass("ambient_particles");
    context_->OMSetRenderTargets(1,rtv_.GetAddressOf(),nullptr); // depth is read as a texture
    D3D11_VIEWPORT viewport{0, 0, float(target_width()), float(target_height()), 0, 1};context_->RSSetViewports(1,&viewport);
    context_->RSSetState(ambient_raster_.Get());context_->OMSetDepthStencilState(nullptr,0);
    const float factor[4]{1,1,1,1};context_->OMSetBlendState(ambient_blend_.Get(),factor,~0u);
    context_->IASetInputLayout(nullptr);context_->IASetVertexBuffers(0,0,nullptr,nullptr,nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    context_->VSSetShader(ambient_vs_.Get(),nullptr,0);context_->PSSetShader(ambient_ps_.Get(),nullptr,0);
    context_->GSSetShader(nullptr,nullptr,0);
    context_->VSSetConstantBuffers(0,1,ambient_constants_.GetAddressOf());context_->PSSetConstantBuffers(0,1,ambient_constants_.GetAddressOf());
    ID3D11ShaderResourceView* grids[6]{ambient_area_srv_.Get(),ambient_height_srv_.Get(),ambient_ground_srv_.Get(),ambient_sources_srv_.Get(),
                                       ambient_fall_tiles_srv_.Get(),ambient_falls_srv_.Get()};
    context_->VSSetShaderResources(0,6,grids);context_->PSSetShaderResources(2,1,depth_srv_.GetAddressOf());
    bind_owned_sky_pixel_inputs();  // borderFogColor reads the sky probe
    // Sand gust fronts travel with the shared wind's effective force (the
    // wind floor keeps zero-force maps gusting), spell speed and pulse; game
    // time, so pause holds them; integrated so speed changes never jump them.
    const double sand_period = double(kSandFrontSpacing)*1024.0;
    const double sand_travel = (1.0+3.0*double(shared_wind.force))*double(shared_wind.speed)*double(shared_wind.pulse);
    ambient_sand_front_ = std::fmod(ambient_sand_front_+sand_travel*dt,sand_period);
    {
        const double length = std::sqrt(double(inputs.wind_direction[0])*inputs.wind_direction[0]+
                                        double(inputs.wind_direction[1])*inputs.wind_direction[1]);
        c.gust[0] = length > 1e-6 ? float(inputs.wind_direction[0]/length) : .7071068f;
        c.gust[1] = length > 1e-6 ? float(inputs.wind_direction[1]/length) : .7071068f;
        c.gust[2] = sheltered ? 0.f : sand_gust_strength(shared_wind.gust,shared_wind.force,shared_wind.pulse);
        c.gust[3] = float(ambient_sand_front_);
    }
    unsigned draws = 0, instances = 0;
    Quality scaled = quality;scaled.density *= std::max(density,0.f);
    char drawn[128] = "";
    static const char* const kNames[em_count] = {"pollen","leaves","motes","cavedust","drips","spores","embers","ash","sand","fireflies","snow glitter","splashes","mist","fall foam","lip spray"};
    for (unsigned index = 0; index < em_count; ++index) {
        const auto e = static_cast<Emitter>(index);
        const auto& style = emitter_style(e,zone);
        auto& displacement = ambient_displacement_[index];
        const double side = 2.0*quality.radius, band = style.height_max-style.height_min;
        const double drift = wind_speed*style.wind_response;
        displacement[0] = std::fmod(displacement[0]+wind[0]*drift*dt,side);
        displacement[1] = std::fmod(displacement[1]+wind[1]*drift*dt,side);
        displacement[2] = std::fmod(displacement[2]+style.vertical_speed*dt,band);
        const float scale = environment_scale(e,zone,environment.region,night,weather);
        const AffinityTable table = emitter_affinity_table(e,zone);
        const unsigned pool = emitter_pool(e,scaled,zone);
        if (scale <= .001f || !pool) continue;
        const bool mushroom = e == em_spores && sheltered;
        // Waterfall emitters fill the map's waterfall tiles (no window scan).
        std::uint32_t fall_first = 0, fall_count = 0;
        if (waterfall_emitter(e)) {
            ambient_fall_range(e,ambient_fall_impact_count_,ambient_fall_count_,fall_first,fall_count);
            if (!fall_count) continue;
        } else {
        // Skip emitters with no matching area anywhere in the window.
        float presence = 0;
        const int x0 = int(std::floor(window_min[0]*.5))-1, x1 = int(std::floor(window_max[0]*.5))+1;
        const int y0 = int(std::floor(window_min[1]*.5))-1, y1 = int(std::floor(window_max[1]*.5))+1;
        // Every tile for localized sources: a mushroom pocket may be narrow.
        const int stride = mushroom ? 1 : 2;
        for (int y = y0; y <= y1 && presence <= 0; y += stride)
            for (int x = x0; x <= x1 && presence <= 0; x += stride) {
                if (x < 0 || y < 0 || x >= int(ambient_tiles_x_) || y >= int(ambient_tiles_y_)) continue;
                const auto v = ambient_area_[std::size_t(y)*ambient_tiles_x_+x];
                const auto& source = ambient_sources_[std::size_t(y)*ambient_tiles_x_+x];
                presence = mushroom ? float(source[2]) : tile_affinity(table,static_cast<AreaClass>(v&15u),v>>4);
                if (e == em_drips) presence *= source[1];
            }
        if (presence <= 0) continue;
        }
        const double velocity[2]{wind[0]*drift,wind[1]*drift};
        ambient_emitter_constants(c,e,style,table,mushroom,pool,scale,displacement.data(),velocity);
        const float window_x = c.window[0];
        if (waterfall_emitter(e)) c.window[0] = float(fall_first);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context_->Map(ambient_constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped))) break;
        std::memcpy(mapped.pData,&c,sizeof(c));context_->Unmap(ambient_constants_.Get(),0);
        const unsigned count = waterfall_emitter(e) ? pool*fall_count : pool*columns*rows;
        c.window[0] = window_x;
        context_->DrawInstanced(4,count,0,0);++draws;instances += count;
        const std::size_t used = std::strlen(drawn);
        std::snprintf(drawn+used,sizeof(drawn)-used,"%s%s %u",used?", ":"",kNames[index],count);
    }
    static const char* const kZones[zone_count] = {"outdoor","dead city","cave","gipat cave","snow","desert"};
    static const char* const kAreas[area_count] = {"bare","grass","dry","sand","snow","swamp","water","lava"};
    const int fx = int(focus[0]*.5), fy = int(focus[1]*.5);
    const std::uint8_t at = fx >= 0 && fy >= 0 && fx < int(ambient_tiles_x_) && fy < int(ambient_tiles_y_)
        ? ambient_area_[std::size_t(fy)*ambient_tiles_x_+fx] : 0;
    status(draws == 0,"Particles L%d: %s | %s, focus %s trees %u | h%.1f night %.2f rain %.2f snow %.2f | %ux%u tiles to %.0f m | env %s | %s",
        level,draws ? "drawn" : "nothing matches here",kZones[zone < zone_count ? zone : 0],kAreas[(at&15u) < area_count ? (at&15u) : 0],
        unsigned(at>>4),environment.hour,night,weather.rain,weather.snow,columns,rows,reach,environment_source,draws ? drawn : "-");
    context_->VSSetShaderResources(0,6,saved_vs);context_->PSSetShaderResources(2,1,&saved_ps2);
    for (auto* view : saved_vs) if (view) view->Release();
    if (saved_ps2) saved_ps2->Release();
    profiler_.add_draw(draws,draws);profiler_.add_batch(draws);
}

} // namespace owned
