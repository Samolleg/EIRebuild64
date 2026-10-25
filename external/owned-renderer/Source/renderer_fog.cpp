// World fog atmosphere (world_fog.h): per-frame constants for every native
// world pixel and, for the full preset, the froxel in-scattering volume.
#include "renderer.h"
#include "ambient_occlusion.h"
#include "world_fog.h"

#include <d3dcompiler.h>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace owned {

#if !defined(EID3D11_SHADER_EMBED)
void Renderer::release_world_fog_volume() noexcept {
    for (auto& value : fog_scatter_) value.Reset();
    for (auto& value : fog_scatter_uav_) value.Reset();
    for (auto& value : fog_scatter_srv_) value.Reset();
    fog_integrated_.Reset();fog_integrated_uav_.Reset();fog_integrated_srv_.Reset();
    fog_volume_quality_=-1;fog_history_valid_=false;
}

#endif
void append_fog_volume_shader_programs(std::vector<ShaderProgram>& out) {
    static const std::string source = owned_native_shader_source() + kFogVolumeShader;
    out.push_back({source, nullptr, nullptr, "fogInjectCS", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
    out.push_back({source, nullptr, nullptr, "fogIntegrateCS", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
}

#if !defined(EID3D11_SHADER_EMBED)
HRESULT Renderer::ensure_world_fog_volume(int quality) noexcept {
    if (fog_integrated_srv_ && fog_volume_quality_ == quality) return S_OK;
    release_world_fog_volume();
    HRESULT hr = S_OK;
    if (!fog_inject_cs_) {
        std::vector<ShaderProgram> programs;
        append_fog_volume_shader_programs(programs);
        ComPtr<ID3DBlob> inject, integrate, error;
        if (FAILED(hr = compile_shader(programs[0], &inject, &error)) ||
            FAILED(hr = compile_shader(programs[1], &integrate, &error))) {
            if (error) OutputDebugStringA(static_cast<const char*>(error->GetBufferPointer()));
            fog_volume_reason_ = "volume shader compilation failed";
            return hr;
        }
        ComPtr<ID3D11ComputeShader> a, b;
        if (FAILED(hr = create_shader(device_.Get(),inject->GetBufferPointer(), inject->GetBufferSize(), nullptr, &a)) ||
            FAILED(hr = create_shader(device_.Get(),integrate->GetBufferPointer(), integrate->GetBufferSize(), nullptr, &b))) {
            fog_volume_reason_ = "volume shader creation failed";
            return hr;
        }
        fog_inject_cs_ = std::move(a);fog_integrate_cs_ = std::move(b);
    }
    const auto grid = fog_volume_grid(quality);
    D3D11_TEXTURE3D_DESC desc{};
    desc.Width = grid.width;desc.Height = grid.height;desc.Depth = grid.slices;desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    auto create = [&](ComPtr<ID3D11Texture3D>& texture, ComPtr<ID3D11UnorderedAccessView>& uav,
                      ComPtr<ID3D11ShaderResourceView>& srv) {
        HRESULT result = device_->CreateTexture3D(&desc, nullptr, &texture);
        if (SUCCEEDED(result)) result = device_->CreateUnorderedAccessView(texture.Get(), nullptr, &uav);
        if (SUCCEEDED(result)) result = device_->CreateShaderResourceView(texture.Get(), nullptr, &srv);
        return result;
    };
    if (FAILED(hr = create(fog_scatter_[0], fog_scatter_uav_[0], fog_scatter_srv_[0])) ||
        FAILED(hr = create(fog_scatter_[1], fog_scatter_uav_[1], fog_scatter_srv_[1])) ||
        FAILED(hr = create(fog_integrated_, fog_integrated_uav_, fog_integrated_srv_))) {
        release_world_fog_volume();
        fog_volume_reason_ = "volume textures could not be created";
        return hr;
    }
    fog_volume_quality_ = quality;
    return S_OK;
}

HRESULT Renderer::update_world_fog_mist() noexcept {
    const auto grid = bridge_world_mist_grid();
    if (!grid) return S_OK;  // keep the last map's texture until a new one is known
    if (grid->generation == fog_mist_generation_ && fog_mist_srv_) return S_OK;
    fog_mist_srv_.Reset();fog_mist_grid_ = {};fog_mist_generation_ = grid->generation;
    D3D11_TEXTURE2D_DESC desc{};desc.Width = grid->width;desc.Height = grid->height;desc.MipLevels = desc.ArraySize = 1;
    desc.SampleDesc.Count = 1;desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{grid->texels.data(), grid->width * 16u, 0};
    ComPtr<ID3D11Texture2D> texture;
    HRESULT hr = device_->CreateTexture2D(&desc, &data, &texture);
    if (SUCCEEDED(hr)) hr = device_->CreateShaderResourceView(texture.Get(), nullptr, &fog_mist_srv_);
    if (FAILED(hr)) return hr;
    fog_mist_grid_ = {grid->origin[0], grid->origin[1], 1.f / (grid->width * grid->cell), 1.f / (grid->height * grid->cell)};
    fog_terrain_low_z_ = grid->terrain_low_z;
    return S_OK;
}

// Once per world frame at the first native colour flush: constants, and the
// froxel volume for the full preset. The interface after composition keeps
// its border fog only.
HRESULT Renderer::bind_world_fog() noexcept {
    const std::uint64_t key = native_frame_index_ * 2u + (scene_composited_ ? 1u : 0u);
    if (fog_prepared_frame_ != key) {
        fog_prepared_frame_ = key;
        FogFrameGpu frame{};
        bool volume = false;
        const auto& config = debug_overlay_.config();
        const int preset = configured_integer(config, "fog_preset", 2);
        const bool test_state = configured_boolean(config, "fog_test_dense", false);
        const auto fog = bridge_world_fog_snapshot();
        const auto clip = bridge_scene_world_to_clip_snapshot();
        const char* problem = nullptr;  // first reason the chosen look is not fully engaged
        char values[320] = "";
        if (!scene_composited_) {sky_draw_pending_ = false;sky_inputs_active_ = false;bloom_fog_headroom_ = 0.f;}
        fog_camera_valid_ = false;  // fogClipToWorld inverts this frame's world camera (depth prepass reconstruction)
        if (scene_composited_) {
            // Interface figures after composition keep their border fog only.
        } else if (!clip.valid || !ambient_occlusion_invert(clip.world_to_clip.data(), frame.clip_to_world)) {
            problem = clip.valid ? "camera matrix not invertible" : "no verified world camera this frame";
        } else {
            fog_camera_valid_ = true;
            if (FAILED(update_world_fog_mist())) problem = "swamp mist texture creation failed";
            // Camera: the world point that clip space maps to w = 0.
            const float* inv = frame.clip_to_world;
            const float ew = inv[14];
            const float eye[3] = {ew != 0.f ? inv[2] / ew : 0.f, ew != 0.f ? inv[6] / ew : 0.f, ew != 0.f ? inv[10] / ew : 0.f};
            const auto& env = fog.environment;
            if (!env.valid && !problem) problem = "game clock/weather unreadable: neutral noon used";
            const bool outdoor = env.valid && !env.cave;
            const float hour = env.valid ? env.hour : 13.f;
            const float night = env.valid ? fog_night(hour) : 0.f;
            // Suslanger is desert: dust haze instead of morning ground fog and wisps.
            const bool desert = outdoor && env.region == fog_region_suslanger;
            float morning = outdoor ? fog_morning(env.game_hours, configured_floating_point(config, "fog_morning_chance", .35f)) * (desert ? .25f : 1.f) : 0.f;
            // Rain fog ramps in and out over game time; wetness eases after it.
            if (env.valid) {
                // A world change (map, menu, load) drops the old world's rain
                // haze and wetness; the new world's own weather sets them.
                if (fog_world_generation_ != ambient_map_generation_ || ambient_menu_) {
                    fog_world_generation_ = ambient_map_generation_;
                    fog_wetness_ = 0.f;fog_wetness_hours_ = -1.0;
                }
                const double elapsed = fog_wetness_hours_ < 0 ? -1.0 : env.game_hours - fog_wetness_hours_;
                fog_rain_level_ = fog_ramp_precipitation(fog_rain_level_, outdoor ? fog_precipitation(env) : 0.f, elapsed);
                fog_wetness_ = fog_update_wetness(fog_wetness_, fog_rain_level_, elapsed,
                    configured_floating_point(config, "fog_after_rain_hours", 4.f));
                fog_wetness_hours_ = env.game_hours;
            }
            const float precipitation = outdoor ? fog_rain_level_ : 0.f;
            float wetness = fog_wetness_;
            float visibility = configured_floating_point(config, "fog_visibility", 5000.f);
            if (test_state) {morning = 1.f;wetness = 1.f;visibility = 120.f;}  // dense morning fog now
            const float strength = configured_floating_point(config, "fog_strength", 1.f);
            const auto density = fog_density(.02f * configured_floating_point(config, "fog_density", .1f),
                                             morning, wetness, precipitation, night);
            const bool map_known = fog_mist_srv_ != nullptr;
            if (!map_known && !problem) problem = "map low ground unknown: height fog and mist off";
            float g[3] = {native_world_lighting_.sunlight_direction[0], native_world_lighting_.sunlight_direction[1],
                          native_world_lighting_.sunlight_direction[2]};
            const float gl = std::sqrt(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
            const auto& p = fog.fog;
            if (preset == 0 && !fog.valid && !problem) problem = "faithful preset needs the stock fog read";
            const int quality = configured_integer(config, "fog_volume_quality", 1);
            const auto grid = fog_volume_grid(quality);
            if (preset >= 2) {
                if (feature_level_ < D3D_FEATURE_LEVEL_11_0) fog_volume_reason_ = "feature level below 11_0";
                else if (!fog_volume_failed_ && FAILED(ensure_world_fog_volume(quality))) fog_volume_failed_ = true;
                volume = feature_level_ >= D3D_FEATURE_LEVEL_11_0 && !fog_volume_failed_;
                if (!volume && !problem) problem = fog_volume_reason_;
            }
            // The volume covers the near scene; the aerial term continues
            // analytically beyond it. The first metres stay haze-free.
            // Slices start at 2 m: the camera never gets closer, and finer
            // slices at gameplay distances keep the volume steady.
            const float nearDistance = 4.f, farDistance = 300.f, hazeFree = 10.f;
            // The test's 120 m is the final visibility, not a clear-noon
            // baseline to thicken again by morning and wetness (about 30 m).
            const float air = strength * (test_state ? fog_visibility_extinction(visibility) :
                fog_air_density(visibility, hour, night, morning, wetness, precipitation) * (desert ? 1.6f : 1.f));
            const float invLog = 1.f / std::log(farDistance / nearDistance);
            auto set4 = [](float* d, float a, float b, float c, float e) { d[0] = a; d[1] = b; d[2] = c; d[3] = e; };
            set4(frame.eye, eye[0], eye[1], eye[2], float(preset));
            // Height fog rests on the map's low ground; without the map it is omitted.
            const float height = map_known ? strength * density.height : 0.f, mist = map_known ? strength * density.mist : 0.f;
            set4(frame.height, height, fog_terrain_low_z_, 5.f, mist);
            set4(frame.volume, nearDistance, farDistance, invLog, volume ? 1.f : 0.f);
            set4(frame.screen, 1.f / width_, 1.f / height_, float(grid.slices), 2.5f);
            // Wisps drift with the shared renderer wind (weather_wind.h).
            set4(frame.volume_cell, .5f / grid.width, .5f / grid.height,
                 static_cast<float>(weather_wind_.wisp[0]), static_cast<float>(weather_wind_.wisp[1]));
            set4(frame.projection, p.projection_33, p.projection_34, p.projection_43, fog.valid ? 1.f : 0.f);
            set4(frame.stock, p.start, p.end, air, 0.f);
            set4(frame.sun, gl > 0 ? g[0] / gl : 0.f, gl > 0 ? g[1] / gl : 0.f, gl > 0 ? g[2] / gl : 1.f, .7f);
            // Cave air scatters no directional light (cave_lighting.h): no sun
            // in-scatter or glow; the haze takes the cave ambient only.
            const bool cave = env.valid && env.cave;
            const float scatter = cave ? 0.f : 1.f;
            set4(frame.sun_color, scatter * native_world_lighting_.sunlight_diffuse[0],
                 scatter * native_world_lighting_.sunlight_diffuse[1], scatter * native_world_lighting_.sunlight_diffuse[2], 1.f);
            if (fog_mist_srv_) std::memcpy(frame.mist_grid, fog_mist_grid_.data(), sizeof(frame.mist_grid));
            // frame.cloud and the probe flag are set by the owned sky below.
            std::memcpy(frame.previous_world_to_clip, fog_previous_clip_.data(), sizeof(frame.previous_world_to_clip));
            // No slice jitter for density (it shimmered on thin ground-fog
            // layers); the shaft taps take a per-frame jitter (w, cycle of
            // 16) and the history (.9) accumulates it, as UE volumetric fog.
            set4(frame.temporal, .5f, fog_history_valid_ && volume ? .9f : 0.f, 0.f,
                 float(native_frame_index_ % 16u));
            // Haze hue: the game's fog colour, desaturated (more at night),
            // pulled toward the world light and tinted by region. Keep the
            // light's brightness for in-scatter, not only its normalised hue.
            float tint[3];fog_region_tint(env.valid ? env.region : 0, env.valid && env.cave, tint);
            set4(frame.tint, tint[0], tint[1], tint[2], .55f - .3f * night);
            const auto& amb = native_world_lighting_.ambient;const auto& sd = native_world_lighting_.sunlight_diffuse;
            float light[3];fog_lit_white(amb.data(), cave ? amb.data() : sd.data(), light);
            set4(frame.light, light[0], light[1], light[2], .3f);
            // Drifting wisps and rolling low mist, stronger in morning fog,
            // after rain and at night; open water mists at dawn and night.
            const float wisps = desert ? .15f : std::clamp(.35f + morning + .6f * wetness + .3f * night, 0.f, 1.f);
            set4(frame.wisps, static_cast<float>(std::fmod(env.world_time, 100000.0)), wisps,
                 map_known ? strength * fog_open_water_density(env.region, morning, night, wetness) : 0.f, 0.f);
            std::memcpy(frame.previous_eye, fog_previous_eye_.data(), sizeof(float) * 4);
            // Sun shafts (sun_shafts.h): volumetric terms in the frame haze
            // colour (the sky's horizon formula on the flush's game fog
            // colour), or screen-space rays when chosen or the volume is off.
            const int shaft_mode = configured_integer(config, "sun_shafts", 2);
            const float shaft_strength = configured_floating_point(config, "sun_shaft_strength", kSunShaftDefaultStrength);
            const float sun_share = cave ? 0.f :
                sun_shaft_sun_share(amb.data(), sd.data(), frame.sun[2], night);
            const auto shafts = sun_shaft_volume(shaft_mode, volume, shaft_strength, sun_share, morning, wetness);
            const bool volume_shafts = shafts.shadow_share > 0.f || shafts.forward > 0.f;
            // Fixed shaft taps and the normal 0.8 history: rotating the taps
            // per frame reintroduced the jitter shimmer removed from the
            // slices (history is rejected under camera motion, so a moving
            // view showed raw per-frame noise carved into the haze).
            set4(frame.shaft, shafts.shadow_share, shafts.forward, shafts.headroom, 0.f);
            bloom_fog_headroom_ = volume_shafts ? shafts.headroom : 0.f;  // haze toward the sun may exceed lit white
            float shaft_haze[3];fog_lit_haze_color(sky_fog_color_, frame.tint, frame.light, shaft_haze);
            set4(frame.shaft_haze, shaft_haze[0], shaft_haze[1], shaft_haze[2], shafts.anisotropy);
            sun_rays_frame_ = UINT64_MAX;
            if (sun_shafts_screen_space(shaft_mode, volume) && shaft_strength > 0.f && sun_share > 0.f) {
                const auto rays = sun_ray_screen(clip.world_to_clip.data(), frame.sun);
                if (rays.active) {
                    // Rays need air: faint in clear noon haze, full in morning or rain fog.
                    const float haze_amount = 1.f - std::exp(-(air * 80.f + height * 20.f));
                    const float k = shaft_strength * rays.fade * sun_share * .6f * std::clamp(.2f + 2.5f * haze_amount, .2f, 1.5f);
                    for (int c = 0; c < 3; ++c) sun_rays_tint_[c] = std::clamp(sd[c], 0.f, 1.f) * k;
                    sun_rays_center_[0] = rays.center[0];sun_rays_center_[1] = rays.center[1];
                    sun_rays_haze_luma_ = std::clamp(sun_shaft_luma(shaft_haze), 0.f, .95f);
                    sun_rays_frame_ = native_frame_index_;
                }
            }
            // F10 component isolation: 1 air haze, 2 height fog, 3 mist and
            // wisps, 4 sun shafts, 5 lamp in-scatter (0 = everything).
            const int component = configured_integer(config, "fog_debug_component", 0);
            if (component) {
                if (component != 1 && component < 4) frame.stock[2] = 0.f;
                if (component != 2 && component < 4) frame.height[0] = 0.f;
                if (component != 3 && component < 4) {frame.height[3] = 0.f;frame.wisps[2] = 0.f;}
                if (component == 3) frame.height[0] = 0.f, frame.stock[2] = 0.f;
                if (component != 4) frame.sun_color[0] = frame.sun_color[1] = frame.sun_color[2] = 0.f;
                if (component != 4) frame.shaft[0] = frame.shaft[1] = 0.f;
                sun_rays_frame_ = UINT64_MAX;
                if (component != 5) frame.sun_color[3] = 0.f;
                if (component == 1 || component == 2) frame.wisps[1] = 0.f;
            }
            set4(frame.air, hazeFree, component && component != 4 ? 0.f : 2.5f, test_state ? 1.f : 0.f, float(component));
            // Draw distance fade, and where this frame's fog alone hides
            // everything: the camera-pass cull takes the nearer of the two.
            fog_cull_distance_ = fog_full_cover_distance(preset, fog.valid, p.end, frame.stock[2], hazeFree);
            frame.stock[3] = fog_draw_fade_distance(native_draw_distance_, fog_cull_distance_);
            fog_cull_frame_ = native_frame_index_;
            std::memcpy(fog_previous_clip_.data(), clip.world_to_clip.data(), sizeof(float) * 16);
            fog_previous_eye_ = {eye[0], eye[1], eye[2], 0.f};
            // The values feed the F10 line and the world_fog event (on a state change).
            char event_key[256];
            std::snprintf(event_key, sizeof(event_key), "%d|%d|%d|%s|%s|%s", preset, test_state ? 1 : 0, volume ? 1 : 0,
                          fog.valid ? "" : fog.failure, problem ? problem : "", fog_volume_reason_);
            if (status_text_wanted() || fog_status_state_ != event_key) {
                std::snprintf(values, sizeof(values),
                    "air %.4f/m (visibility %.0f m) height %.4f/m @ z %.1f mist %.3f/m | hour %.1f %s %s weather %d->%d rain %.2f wet %.2f morning %.2f%s",
                    air, 3.f / (std::max)(air, 1e-6f), height, fog_terrain_low_z_, mist, hour,
                    env.valid ? (env.cave ? "cave" : "outdoor") : "clock?",
                    env.region == fog_region_gipat ? "Gipat" : env.region == fog_region_ingos ? "Ingos" :
                    env.region == fog_region_suslanger ? "Suslanger" : "region?",
                    env.weather_current, env.weather_next, precipitation, wetness, morning, test_state ? " TEST" : "");
                const size_t used = std::strlen(values);
                std::snprintf(values + used, sizeof(values) - used, " | shafts %s sun %.2f | fog hides past %.0f m",
                    volume_shafts ? "volume" : sun_rays_frame_ == native_frame_index_ ? "screen" : "off", sun_share,
                    fog_cull_distance_);
            }
            // Sky, clouds and cloud shadows share this frame's camera, environment and haze colour.
            prepare_owned_sky(frame, env, eye);
        }
        if (!scene_composited_) {
            static const char* const names[3] = {"faithful", "atmospheric", "full"};
            const char* look = names[std::clamp(preset, 0, 2)];
            // One event per state change: the look, volume, stock read and problem.
            char state[256];
            // The region and outdoor state are in the key too, so each map's
            // fog look is logged once (core mode compares them per map).
            std::snprintf(state, sizeof(state), "%d|%d|%d|%s|%s|%s|%d|%d", preset, test_state ? 1 : 0, volume ? 1 : 0,
                          fog.valid ? "" : fog.failure, problem ? problem : "", fog_volume_reason_,
                          fog.environment.valid ? fog.environment.region : -1, fog.environment.valid && !fog.environment.cave ? 1 : 0);
            const bool event_changed = fog_status_state_ != state;
            if (event_changed || status_text_wanted())
                std::snprintf(fog_status_, sizeof(fog_status_), "Fog %s%s | volume %s | stock %s | %s%s%s", look,
                    test_state ? " (TEST dense)" : "",
                    preset < 2 ? "off" : volume ? "ON" : "FAILED",
                    fog.valid ? "read" : fog.failure, values, problem ? " | PROBLEM: " : "", problem ? problem : "");
            fog_status_problem_ = problem != nullptr;
            if (event_changed) {
                fog_status_state_ = state;
                bridge_report_event("world_fog", fog_status_);
            }
        }
        if (!fog_frame_constants_) {
            D3D11_BUFFER_DESC desc{};desc.ByteWidth = sizeof(FogFrameGpu);desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            HRESULT hr = device_->CreateBuffer(&desc, nullptr, &fog_frame_constants_);if (FAILED(hr)) return hr;
            D3D11_SAMPLER_DESC sampler{};sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;sampler.MaxLOD = D3D11_FLOAT32_MAX;
            if (FAILED(hr = device_->CreateSamplerState(&sampler, &fog_sampler_))) return hr;
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        HRESULT hr = context_->Map(fog_frame_constants_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);if (FAILED(hr)) return hr;
        std::memcpy(mapped.pData, &frame, sizeof(frame));context_->Unmap(fog_frame_constants_.Get(), 0);
        // Native specialisation key bits 7-8 (NATIVE_FOG_VOLUME_QUALITY 0, 1) as uploaded.
        native_spec_fog_bits_ = frame.eye[3] <= .5f ? 128u : frame.volume[3] <= .5f ? 256u : 0u;
        fog_cloud_visible_ = frame.cloud[0] > 0.f; // ground cover selects its cloud-shadow permutation from it
        fog_volume_active_ = volume;
        if (volume) {
            transition_profile_pass("fog_volume");
            ID3D11ShaderResourceView* none[1]{};context_->PSSetShaderResources(11, 1, none);
            const auto grid = fog_volume_grid(fog_volume_quality_);
            const unsigned current = fog_scatter_index_, previous = 1u - current;
            context_->CSSetShader(fog_inject_cs_.Get(), nullptr, 0);
            ID3D11Buffer* buffers[1] = {fog_frame_constants_.Get()};context_->CSSetConstantBuffers(10, 1, buffers);
            context_->CSSetConstantBuffers(2, 1, native_shadow_constants_.GetAddressOf());
            context_->CSSetConstantBuffers(5, 1, native_point_light_constants_.GetAddressOf());
            ID3D11ShaderResourceView* shadow = native_shadow_depth_srv_.Get();context_->CSSetShaderResources(9, 1, &shadow);
            ID3D11ShaderResourceView* lights = native_point_lights_.empty() ? nullptr : native_point_light_srv_.Get();
            context_->CSSetShaderResources(18, 1, &lights);
            ID3D11ShaderResourceView* light_grid = native_point_lights_.empty() ? nullptr : native_point_light_grid_srv_.Get();
            context_->CSSetShaderResources(17, 1, &light_grid);
            std::array<ID3D11ShaderResourceView*, PointShadowTextureLayout::array_bindings> pages{};
            for (std::size_t i = 0; i < native_point_shadow_pages_.size() && i < pages.size(); ++i) pages[i] = native_point_shadow_pages_[i].srv.Get();
            context_->CSSetShaderResources(PointShadowTextureLayout::first_srv, static_cast<UINT>(pages.size()), pages.data());
            ID3D11ShaderResourceView* history = fog_scatter_srv_[previous].Get();context_->CSSetShaderResources(14, 1, &history);
            ID3D11ShaderResourceView* mist = fog_mist_srv_.Get();context_->CSSetShaderResources(15, 1, &mist);
            ID3D11ShaderResourceView* clouds = sky_inputs_active_ ? sky_shadow_srv_.Get() : nullptr;
            context_->CSSetShaderResources(10, 1, &clouds);
            ID3D11SamplerState* samplers[2] = {native_shadow_sampler_.Get(), native_point_shadow_sampler_.Get()};
            context_->CSSetSamplers(1, 2, samplers);context_->CSSetSamplers(7, 1, fog_sampler_.GetAddressOf());
            context_->CSSetUnorderedAccessViews(0, 1, fog_scatter_uav_[current].GetAddressOf(), nullptr);
            context_->Dispatch((grid.width + 3) / 4, (grid.height + 3) / 4, (grid.slices + 3) / 4);
            ID3D11UnorderedAccessView* no_uav = nullptr;context_->CSSetUnorderedAccessViews(0, 1, &no_uav, nullptr);
            ID3D11ShaderResourceView* scatter = fog_scatter_srv_[current].Get();context_->CSSetShaderResources(12, 1, &scatter);
            context_->CSSetShader(fog_integrate_cs_.Get(), nullptr, 0);
            context_->CSSetUnorderedAccessViews(0, 1, fog_integrated_uav_.GetAddressOf(), nullptr);
            context_->Dispatch((grid.width + 7) / 8, (grid.height + 7) / 8, 1);
            context_->CSSetUnorderedAccessViews(0, 1, &no_uav, nullptr);
            std::array<ID3D11ShaderResourceView*, PointShadowTextureLayout::array_bindings> clear{};
            context_->CSSetShaderResources(PointShadowTextureLayout::first_srv, static_cast<UINT>(clear.size()), clear.data());
            ID3D11ShaderResourceView* nulls[10]{};context_->CSSetShaderResources(9, 10, nulls);
            ID3D11Buffer* null_buffer = nullptr;
            context_->CSSetConstantBuffers(10, 1, &null_buffer);context_->CSSetConstantBuffers(2, 1, &null_buffer);
            context_->CSSetConstantBuffers(5, 1, &null_buffer);
            ID3D11SamplerState* null_samplers[2]{};context_->CSSetSamplers(1, 2, null_samplers);
            context_->CSSetShader(nullptr, nullptr, 0);
            fog_scatter_index_ = previous;fog_history_valid_ = true;
            profiler_.add_draw(2, 2);
        } else if (!scene_composited_) {
            fog_history_valid_ = false;
        }
    }
    context_->PSSetConstantBuffers(10, 1, fog_frame_constants_.GetAddressOf());
    ID3D11ShaderResourceView* integrated = fog_volume_active_ ? fog_integrated_srv_.Get() : nullptr;
    context_->PSSetShaderResources(11, 1, &integrated);
    context_->PSSetSamplers(7, 1, fog_sampler_.GetAddressOf());
    bind_owned_sky_pixel_inputs();
    return S_OK;
}

#endif
}  // namespace owned
