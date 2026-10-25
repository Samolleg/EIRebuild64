// Owned sky, volumetric clouds and cloud shadows (sky_clouds.h). Prepared
// once per world frame from bind_world_fog (it shares the fog's camera,
// environment and haze colour), drawn behind the first world batch.
#include "renderer.h"
#include "sky_clouds.h"
#include "world_fog.h"

#include <d3dcompiler.h>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace owned {
#if !defined(EID3D11_SHADER_EMBED)
namespace {
struct SkyQuality {
    unsigned cloud_divisor, max_cloud_width, steps, light_steps, shadow_size, probe_width;
};
// Reconstruction target size; the march runs at half of it per axis (one
// sub-pixel of each 2x2 block per frame). 1 low: half-resolution
// reconstruction (march as before: quarter resolution, at most 640 wide);
// 2 high: full-resolution reconstruction up to 2560 wide (march at most
// 1280 wide as before) with more steps for long paths and one more light step.
// The march takes up to 4/3 of the nominal steps on the longest (20 km)
// paths through the 4 km layer: low at most 26, high at most 64. Samples
// above a column's cloud top skip the 3D noise; cirrus is texture taps only.
constexpr SkyQuality sky_quality(int level) noexcept {
    return level <= 1 ? SkyQuality{2, 1280, 20, 3, 256, 128} : SkyQuality{1, 2560, 48, 5, 512, 256};
}
constexpr float kSkyShadowExtent = 768.f;  // metres of ground around the view focus
float sky_mix(float a, float b, float t) noexcept { return a + (b - a) * t; }
float sky_linear(float c) noexcept { return std::pow(std::clamp(c, 0.f, 1.f), 2.2f); }
}  // namespace

void Renderer::release_owned_sky() noexcept {
    sky_shape_cs_.Reset();sky_detail_cs_.Reset();sky_weather_cs_.Reset();sky_lut_cs_.Reset();
    sky_shadow_cs_.Reset();sky_shadow_blur_cs_.Reset();sky_clouds_cs_.Reset();sky_resolve_cs_.Reset();sky_probe_cs_.Reset();sky_vs_.Reset();sky_ps_.Reset();
    sky_constants_.Reset();sky_wrap_sampler_.Reset();sky_clamp_sampler_.Reset();
    sky_depth_state_.Reset();sky_blend_state_.Reset();sky_raster_state_.Reset();
    sky_shape_srv_.Reset();sky_detail_srv_.Reset();sky_weather_srv_.Reset();sky_lut_srv_.Reset();sky_shadow_srv_.Reset();
    sky_lut_uav_.Reset();sky_shadow_uav_.Reset();sky_shadow_raw_srv_.Reset();sky_shadow_raw_uav_.Reset();sky_march_srv_.Reset();sky_march_uav_.Reset();
    for (auto& value : sky_cloud_srv_) value.Reset();
    for (auto& value : sky_cloud_uav_) value.Reset();
    for (auto& value : sky_probe_srv_) value.Reset();
    for (auto& value : sky_probe_uav_) value.Reset();
    sky_cloud_width_=sky_cloud_height_=sky_march_width_=sky_march_height_=sky_shadow_size_=sky_probe_width_=0;sky_quality_=-1;
    sky_history_valid_=sky_failed_=sky_draw_pending_=sky_inputs_active_=false;
    sky_reason_="";sky_status_state_.clear();
    sky_time_ = -1.0;
    sky_lut_inputs_.clear();sky_shadow_inputs_.clear();
}

#endif
void append_sky_shader_programs(std::vector<ShaderProgram>& out) {
    for (const char* entry : {"skyShapeCS", "skyDetailCS", "skyWeatherCS", "skyLutCS", "skyShadowCS", "skyShadowBlurCS", "skyCloudsCS",
                              "skyResolveCS", "skyProbeCS"})
        out.push_back({{kSkyShader, sizeof(kSkyShader) - 1}, nullptr, nullptr, entry, "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
    out.push_back({{kSkyShader, sizeof(kSkyShader) - 1}, nullptr, nullptr, "skyVS", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
    out.push_back({{kSkyShader, sizeof(kSkyShader) - 1}, nullptr, nullptr, "skyPS", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
}

#if !defined(EID3D11_SHADER_EMBED)
HRESULT Renderer::ensure_owned_sky(int quality) noexcept {
    HRESULT hr = S_OK;
    auto fail = [&](const char* reason, HRESULT result) { sky_reason_ = reason; return result; };
    if (!sky_ps_) {
        std::vector<ShaderProgram> programs;
        append_sky_shader_programs(programs);  // 9 compute stages, then skyVS and skyPS
        ComPtr<ID3D11ComputeShader>* const targets[9] = {&sky_shape_cs_, &sky_detail_cs_, &sky_weather_cs_, &sky_lut_cs_,
                                                          &sky_shadow_cs_, &sky_shadow_blur_cs_, &sky_clouds_cs_, &sky_resolve_cs_, &sky_probe_cs_};
        for (int i = 0; i < 9; ++i) {
            ComPtr<ID3DBlob> code, error;
            if (FAILED(hr = compile_shader(programs[i], &code, &error))) {
                if (error) OutputDebugStringA(static_cast<const char*>(error->GetBufferPointer()));
                return fail("sky shader compilation failed: sky omitted", hr);
            }
            if (FAILED(hr = create_shader(device_.Get(),code->GetBufferPointer(), code->GetBufferSize(), nullptr,
                                                         targets[i]->ReleaseAndGetAddressOf())))
                return fail("sky shader creation failed: sky omitted", hr);
        }
        ComPtr<ID3DBlob> vs, ps, error;
        if (FAILED(hr = compile_shader(programs[9], &vs, &error)) || FAILED(hr = compile_shader(programs[10], &ps, &error))) {
            if (error) OutputDebugStringA(static_cast<const char*>(error->GetBufferPointer()));
            return fail("sky shader compilation failed: sky omitted", hr);
        }
        D3D11_BUFFER_DESC buffer{};buffer.ByteWidth = sizeof(SkyFrameGpu);buffer.Usage = D3D11_USAGE_DYNAMIC;
        buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        D3D11_SAMPLER_DESC sampler{};sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;sampler.MaxLOD = D3D11_FLOAT32_MAX;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        D3D11_SAMPLER_DESC clamp = sampler;
        clamp.AddressU = clamp.AddressV = clamp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        // Behind everything: only pixels still at the cleared far plane, no depth write.
        D3D11_DEPTH_STENCIL_DESC depth{};depth.DepthEnable = TRUE;depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        D3D11_BLEND_DESC blend{};blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        D3D11_RASTERIZER_DESC raster{};raster.FillMode = D3D11_FILL_SOLID;raster.CullMode = D3D11_CULL_NONE;raster.DepthClipEnable = FALSE;
        if (FAILED(hr = create_shader(device_.Get(),vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &sky_vs_)) ||
            FAILED(hr = create_shader(device_.Get(),ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &sky_ps_)) ||
            FAILED(hr = device_->CreateBuffer(&buffer, nullptr, &sky_constants_)) ||
            FAILED(hr = device_->CreateSamplerState(&sampler, &sky_wrap_sampler_)) ||
            FAILED(hr = device_->CreateSamplerState(&clamp, &sky_clamp_sampler_)) ||
            FAILED(hr = device_->CreateDepthStencilState(&depth, &sky_depth_state_)) ||
            FAILED(hr = device_->CreateBlendState(&blend, &sky_blend_state_)) ||
            FAILED(hr = device_->CreateRasterizerState(&raster, &sky_raster_state_))) {
            sky_ps_.Reset();
            return fail("sky pipeline objects unavailable: sky omitted", hr);
        }
    }
    auto texture2d = [&](UINT width, UINT height, DXGI_FORMAT format, ComPtr<ID3D11ShaderResourceView>& srv,
                         ComPtr<ID3D11UnorderedAccessView>& uav) {
        D3D11_TEXTURE2D_DESC desc{};desc.Width = width;desc.Height = height;desc.MipLevels = desc.ArraySize = 1;
        desc.SampleDesc.Count = 1;desc.Format = format;desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        ComPtr<ID3D11Texture2D> texture;
        HRESULT result = device_->CreateTexture2D(&desc, nullptr, &texture);
        if (SUCCEEDED(result)) result = device_->CreateShaderResourceView(texture.Get(), nullptr, srv.ReleaseAndGetAddressOf());
        if (SUCCEEDED(result)) result = device_->CreateUnorderedAccessView(texture.Get(), nullptr, uav.ReleaseAndGetAddressOf());
        return result;
    };
    if (!sky_shape_srv_) {
        // Tiling noise volumes and the weather field, generated once on the GPU.
        auto volume = [&](UINT size, UINT depth, ID3D11ComputeShader* shader, ComPtr<ID3D11ShaderResourceView>& srv) {
            D3D11_TEXTURE3D_DESC desc{};desc.Width = desc.Height = size;desc.Depth = depth;desc.MipLevels = 1;
            desc.Format = DXGI_FORMAT_R8_UNORM;desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
            ComPtr<ID3D11Texture3D> texture;ComPtr<ID3D11UnorderedAccessView> uav;
            HRESULT result = device_->CreateTexture3D(&desc, nullptr, &texture);
            if (SUCCEEDED(result)) result = device_->CreateShaderResourceView(texture.Get(), nullptr, srv.ReleaseAndGetAddressOf());
            if (SUCCEEDED(result)) result = device_->CreateUnorderedAccessView(texture.Get(), nullptr, &uav);
            if (FAILED(result)) return result;
            context_->CSSetShader(shader, nullptr, 0);
            context_->CSSetUnorderedAccessViews(0, 1, uav.GetAddressOf(), nullptr);
            context_->Dispatch(size / 4, size / 4, depth / 4);
            ID3D11UnorderedAccessView* none = nullptr;context_->CSSetUnorderedAccessViews(0, 1, &none, nullptr);
            return S_OK;
        };
        // The weather field keeps a mip chain: the volume samples mip 0, the
        // distant cirrus streaks pick a mip by their footprint.
        auto weather_map = [&](ComPtr<ID3D11ShaderResourceView>& srv, ComPtr<ID3D11UnorderedAccessView>& uav) {
            D3D11_TEXTURE2D_DESC desc{};desc.Width = desc.Height = 512;desc.MipLevels = 0;desc.ArraySize = 1;
            desc.SampleDesc.Count = 1;desc.Format = DXGI_FORMAT_R8G8_UNORM;desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_RENDER_TARGET;
            D3D11_UNORDERED_ACCESS_VIEW_DESC view{};view.Format = desc.Format;view.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
            ComPtr<ID3D11Texture2D> texture;
            HRESULT result = device_->CreateTexture2D(&desc, nullptr, &texture);
            if (SUCCEEDED(result)) result = device_->CreateShaderResourceView(texture.Get(), nullptr, srv.ReleaseAndGetAddressOf());
            if (SUCCEEDED(result)) result = device_->CreateUnorderedAccessView(texture.Get(), &view, uav.ReleaseAndGetAddressOf());
            return result;
        };
        ComPtr<ID3D11ShaderResourceView> shape, detail, weather;ComPtr<ID3D11UnorderedAccessView> weather_uav;
        // The shape volume is kSkyShapeDepthTiles tiles tall (issue #115).
        if (FAILED(hr = volume(128, 128 * kSkyShapeDepthTiles, sky_shape_cs_.Get(), shape)) ||
            FAILED(hr = volume(32, 32, sky_detail_cs_.Get(), detail)) ||
            FAILED(hr = weather_map(weather, weather_uav)))
            return fail("sky noise textures could not be created: sky omitted", hr);
        context_->CSSetShader(sky_weather_cs_.Get(), nullptr, 0);
        context_->CSSetUnorderedAccessViews(1, 1, weather_uav.GetAddressOf(), nullptr);
        context_->Dispatch(64, 64, 1);
        ID3D11UnorderedAccessView* none = nullptr;context_->CSSetUnorderedAccessViews(1, 1, &none, nullptr);
        context_->CSSetShader(nullptr, nullptr, 0);
        context_->GenerateMips(weather.Get());
        if (FAILED(hr = texture2d(kSkyLutWidth, kSkyLutHeight, DXGI_FORMAT_R16G16B16A16_FLOAT, sky_lut_srv_, sky_lut_uav_)))
            return fail("sky LUT could not be created: sky omitted", hr);
        sky_lut_inputs_.clear();
        sky_shape_srv_ = std::move(shape);sky_detail_srv_ = std::move(detail);sky_weather_srv_ = std::move(weather);
    }
    const auto q = sky_quality(quality);
    const unsigned divided = (width_ + q.cloud_divisor - 1) / q.cloud_divisor;
    const unsigned cloud_width = std::max(1u, std::min(divided, q.max_cloud_width));
    const unsigned cloud_height = std::max(1u, static_cast<unsigned>(std::ceil(double(height_) * cloud_width / width_)));
    const unsigned march_width = (cloud_width + 1) / 2, march_height = (cloud_height + 1) / 2;
    if (sky_quality_ != quality || sky_cloud_width_ != cloud_width || sky_cloud_height_ != cloud_height) {
        sky_history_valid_ = false;sky_quality_ = -1;sky_shadow_inputs_.clear();
        sky_cloud_width_ = sky_cloud_height_ = sky_march_width_ = sky_march_height_ = sky_shadow_size_ = sky_probe_width_ = 0;
        if (FAILED(hr = texture2d(march_width, march_height, DXGI_FORMAT_R16G16B16A16_FLOAT, sky_march_srv_, sky_march_uav_)))
            return fail("sky cloud targets could not be created: sky omitted", hr);
        for (unsigned i = 0; i < 2; ++i)
            if (FAILED(hr = texture2d(cloud_width, cloud_height, DXGI_FORMAT_R16G16B16A16_FLOAT, sky_cloud_srv_[i], sky_cloud_uav_[i])) ||
                FAILED(hr = texture2d(q.probe_width, q.probe_width / 2, DXGI_FORMAT_R16G16B16A16_FLOAT, sky_probe_srv_[i], sky_probe_uav_[i])))
                return fail("sky cloud targets could not be created: sky omitted", hr);
        if (FAILED(hr = texture2d(q.shadow_size, q.shadow_size, DXGI_FORMAT_R16_FLOAT, sky_shadow_srv_, sky_shadow_uav_)) ||
            FAILED(hr = texture2d(q.shadow_size, q.shadow_size, DXGI_FORMAT_R16_FLOAT, sky_shadow_raw_srv_, sky_shadow_raw_uav_)))
            return fail("cloud shadow map could not be created: sky omitted", hr);
        sky_quality_ = quality;sky_cloud_width_ = cloud_width;sky_cloud_height_ = cloud_height;
        sky_march_width_ = march_width;sky_march_height_ = march_height;
        sky_shadow_size_ = q.shadow_size;sky_probe_width_ = q.probe_width;
    }
    return S_OK;
}

// The native world pixels sample the cloud shadow (t10) and the probe (t12).
void Renderer::bind_owned_sky_pixel_inputs() noexcept {
    ID3D11ShaderResourceView* shadow = sky_inputs_active_ ? sky_shadow_srv_.Get() : nullptr;
    ID3D11ShaderResourceView* probe = sky_inputs_active_ ? sky_probe_srv_[sky_index_].Get() : nullptr;
    context_->PSSetShaderResources(10, 1, &shadow);
    context_->PSSetShaderResources(12, 1, &probe);
}

void Renderer::prepare_owned_sky(FogFrameGpu& fog, const WorldEnvironment& env, const float eye[3]) noexcept {
    sky_draw_pending_ = false;sky_inputs_active_ = false;
    const auto& config = debug_overlay_.config();
    const int quality = std::clamp(configured_integer(config, "sky_clouds", 2), 0, 2);
    const char* problem = nullptr;const char* state = nullptr;char values[512] = "";
    if (quality == 0) state = "stock sky (sky_clouds 0)";
    else if (!bridge_owned_sky_installed()) problem = "owned sky hook rejected: stock sky omitted";
    else if (!bridge_owned_sky_requested()) state = "the game drew no sky this frame";
    else if (env.valid && env.cave) state = "cave: no sky";
    else if (feature_level_ < D3D_FEATURE_LEVEL_11_0) problem = "feature level below 11_0: sky omitted";
    else if (sky_failed_) problem = sky_reason_;
    else if (FAILED(ensure_owned_sky(quality))) {sky_failed_ = true;problem = sky_reason_;}
    if (state || problem) {sky_history_valid_ = false;sky_time_ = -1.0;}
    if (!state && !problem) {
        const auto q = sky_quality(quality);
        const float hour = env.valid ? env.hour : 13.f, night = env.valid ? fog_night(hour) : 0.f;
        const float light[3] = {fog.sun[0], fog.sun[1], fog.sun[2]};
        const auto sky = sky_celestial(hour, light);
        const auto look = sky_region_look(env.valid ? env.region : 0);
        // Diagnostics `sky_weather_test` (render only): 1 forces a rain storm,
        // 2 a clear sky, for the sky alone; the game's weather is untouched.
        const int weather_test = configured_integer(config, "sky_weather_test", 0);
        // The renderer's ramped rain and snow (weather_wind.h, the same ramp as
        // the rain fog and particles), refreshed once per world frame by the
        // terrain constants: here it can lag that frame by one.
        const float game_storm = std::clamp(std::max(weather_wind_.gpu.rain, .8f * weather_wind_.snow), 0.f, 1.f);
        const double previous_time = sky_time_;
        // A step over 2 game seconds is a load or jump, not frame time: the new
        // scene seeds its sky from its own weather (as a backwards step does).
        const double step = env.valid && previous_time >= 0 ? env.world_time * .055 - previous_time : -1.0;
        const double elapsed = step >= 0.0 && step <= 2.0 ? step : -1.0;
        const float closed_target = weather_test == 1 ? 1.f : weather_test == 2 ? 0.f
                                  : env.valid && !env.cave ? sky_closure_target(sky_overcast_target(env), game_storm,
                                                                                 elapsed < 0.0 ? -1.f : sky_closure_) : 0.f;
        // The game switches weather within a second; the storm deck moves in
        // as a front from the upwind horizon and clears from the upwind side
        // (sky_step_front; issue #69: it morphed instantly, #115: in place).
        SkyFront front{sky_front_lead_, sky_front_rear_, sky_front_pre_};
        sky_step_front(front, closed_target > .5f, elapsed);
        sky_front_lead_ = front.lead;sky_front_rear_ = front.rear;sky_front_pre_ = front.pre;
        // Camera-local weather (dome, haze, light, rain shafts, lightning)
        // follows the deck over the eye; the clouds take theirs per sample.
        const float closed = sky_front_deck(front, 0.f);
        sky_closure_ = closed;
        sky_front_arrival_ = closed;sky_local_cover_ = front.pre;
        const double seconds = env.world_time * .055;
        const float fair_target = look.coverage + .08f * (env.valid ? sky_day_variation(env.game_hours) : 0.f) +
                                  .12f * (env.valid ? sky_spell_variation(seconds) : 0.f);
        sky_fair_coverage_ = sky_step_coverage(sky_fair_coverage_, fair_target, elapsed);
        // Fair-sky coverage and density; the deck's (1, 1.3) go in deck_cloud.
        const float coverage = sky_fair_coverage_;
        const float density = look.density, haze = sky_mix(look.haze, .55f, closed);
        const float overcast = .85f * closed;
        // All density consumers share the paused/scaled game-clock motion.
        float wx = .91f, wy = .41f, force = .5f;
        if (ambient_terrain_frame_ != UINT64_MAX) {
            const auto& t = ambient_terrain_state_;
            const float l = std::sqrt(t.wind_direction[0] * t.wind_direction[0] + t.wind_direction[1] * t.wind_direction[1]);
            if (l > 1e-3f) {wx = t.wind_direction[0] / l;wy = t.wind_direction[1] / l;}
            force = t.wind_force_normalized;
        }
        // Cloud types and cirrus follow the sky closure, the ramped rain and
        // snow, and the windy spells of the shared renderer wind.
        const float storm = weather_test == 1 ? closed : weather_test == 2 ? 0.f : game_storm;
        // Rain shafts and lightning only in rain (the game strikes only in
        // EW_RAIN), and only as far as the deck has closed.
        const float rain_level = std::min(closed, weather_test == 1 ? closed : weather_test == 2 ? 0.f
                                                                 : std::clamp(weather_wind_.gpu.rain, 0.f, 1.f));
        sky_step_motion(env, sky_time_, sky_wind_offset_, wx, wy, force, storm,
                        configured_floating_point(config, "sky_cloud_motion", 1.f),
                        configured_floating_point(config, "sky_cloud_wind_speed", kSkyCloudWindSpeed),
                        sky_shadow_offset_, configured_floating_point(config, "sky_cloud_shadow_speed", kSkyCloudShadowSpeed));
        // The deck's cloud type: a storm deck of the current precipitation kind (sky_deck_storm).
        if (env.valid && (env.weather_current == 1 || env.weather_current == 2))
            sky_deck_kind_ = env.weather_current == 1 ? 1.f : .8f;
        // Light: the game's hourly sun colour drives the sky sun; the moon is a cool, dim light.
        const float night_brightness = configured_floating_point(config, "sky_night_brightness", 1.5f);
        const auto& sd = native_world_lighting_.sunlight_diffuse;
        const float sun_linear[3] = {sky_linear(sd[0]), sky_linear(sd[1]), sky_linear(sd[2])};
        const float dim = 1.f - overcast * .8f;
        float sun_t[3];
        sky_light_transmittance(sky.sun, haze, sun_t);
        SkyFrameGpu frame{};
        auto set4 = [](float* d, float a, float b, float c, float e) { d[0] = a; d[1] = b; d[2] = c; d[3] = e; };
        std::memcpy(frame.clip_to_world, fog.clip_to_world, sizeof(frame.clip_to_world));
        std::memcpy(frame.previous_world_to_clip, sky_previous_clip_.data(), sizeof(frame.previous_world_to_clip));
        set4(frame.eye, eye[0], eye[1], eye[2], 0.f);
        set4(frame.previous, 0.f, 0.f, 0.f, sky_history_valid_ ? 1.f : 0.f);
        set4(frame.sun, sky.sun[0], sky.sun[1], sky.sun[2], look.sun);
        set4(frame.moon, sky.moon[0], sky.moon[1], sky.moon[2], night);
        sky_disc_constants(sky.moon_visibility, frame.sun_disc, frame.moon_disc);
        float radiance[3];
        for (int k = 0; k < 3; ++k) {
            radiance[k] = sun_t[k] * (.5f + .5f * sun_linear[k]) * 1.6f * dim * look.sun;
        }
        const float day_exposure = 1.f + .25f * (1.f - std::clamp(sky.sun_elevation / 20.f, 0.f, 1.f));
        const float exposure = sky_mix(day_exposure, 1.35f, night) * (1.f + overcast * .25f) *
                               configured_floating_point(config, "sky_brightness", 1.f);
        set4(frame.light_radiance, radiance[0], radiance[1], radiance[2], exposure);
        const float moon_color[3] = {.6f, .68f, .85f};
        sky_cloud_irradiance(sky.sun, haze, sun_linear, 1.6f * dim * look.sun, kSkyCloudTop, frame.cloud_top);
        const bool sun_lights_clouds = frame.cloud_top[0] > 0.f || frame.cloud_top[1] > 0.f || frame.cloud_top[2] > 0.f;
        const float* cloud_light = sun_lights_clouds ? sky.sun : sky.moon;
        set4(frame.cloud_light, cloud_light[0], cloud_light[1], cloud_light[2], 0.f);
        const float cloud_gain = sun_lights_clouds ? 1.6f * dim * look.sun
            : .3f * dim * night * sky.moon_visibility * night_brightness;
        const float* cloud_color = sun_lights_clouds ? sun_linear : moon_color;
        sky_cloud_irradiance(cloud_light, haze, cloud_color, cloud_gain, kSkyCloudBase, frame.cloud_bottom);
        if (!sun_lights_clouds)
            sky_cloud_irradiance(cloud_light, haze, cloud_color, cloud_gain, kSkyCloudTop, frame.cloud_top);
        // The high ice layer sees sunset after the lower clouds enter the
        // planet's shadow. Its light direction is selected independently.
        sky_cloud_irradiance(sky.sun, haze, sun_linear, 1.6f * dim * look.sun, kSkyCirrusAltitude, frame.cirrus_light);
        const bool sun_lights_cirrus = frame.cirrus_light[0] > 0.f || frame.cirrus_light[1] > 0.f || frame.cirrus_light[2] > 0.f;
        if (!sun_lights_cirrus)
            sky_cloud_irradiance(sky.moon, haze, moon_color, .3f * dim * night * sky.moon_visibility * night_brightness,
                                kSkyCirrusAltitude, frame.cirrus_light);
        frame.cirrus_light[3] = sun_lights_cirrus ? 1.f : 0.f;
        // Storm deck (issue #69 reference): it ends 16 km out, so the low sky
        // stays bright; a low sun shines in under it and lights the base
        // orange (undimmed: that light never crosses the deck).
        // 45 km (was 16): at 16 km the low deck's edge sat a few degrees above
        // the horizon and cut a hard line with a bright band below it
        // (runtime, db97b88); now the deck reaches the horizon haze and fades
        // there through its aerial perspective.
        constexpr float kStormEdge = 45000.f;
        sky_cloud_irradiance(sky.sun, haze, sun_linear, 3.2f * look.sun, kSkyCloudBase, frame.storm_light);
        const float low_sun = fog_smoothstep(14.f, 3.f, sky.sun_elevation) * closed;
        for (int k = 0; k < 3; ++k) frame.storm_light[k] *= low_sun;
        frame.storm_light[3] = closed > 0.f ? kStormEdge : 0.f;
        float flash_offset[3];
        const float flash = sky_lightning(seconds, rain_level, flash_offset);
        set4(frame.flash, eye[0] + flash_offset[0], eye[1] + flash_offset[1], flash_offset[2], flash);
        // A game strike lights the deck right above its bolt; it takes the one
        // flash slot over a weaker distant flash.
        const float strike = sky_strike_seconds_ >= 0.0 ?
            kSkyStrikeFlash * sky_flash_pulses(static_cast<float>(seconds - sky_strike_seconds_)) : 0.f;
        if (strike > flash)
            set4(frame.flash, sky_strike_xy_[0], sky_strike_xy_[1], kSkyStrikeFlashAltitude, strike);
        set4(frame.rain, rain_level, wx, wy, .8f * closed);
        const float lut_sun = 7.f * dim * (sky.sun_elevation > -10.f ? 1.f : 0.f);
        const float lut_moon = .55f * dim * night * sky.moon_visibility * night_brightness;
        set4(frame.lut_sun, lut_sun, lut_sun, lut_sun, 3.f * look.stars * night_brightness);
        set4(frame.lut_moon, lut_moon, lut_moon, lut_moon, .01f);
        // Cloud shadows: along the game's light (sun or moonlight).
        const float shadow_strength = configured_floating_point(config, "sky_cloud_shadows", 1.f) * sky_mix(1.f, .6f, night);
        // Always the game's light (continuous, at least 30 degrees up). The
        // visible sun eased to the horizon at dawn and dusk and the shadow
        // light switched to the game light when the clouds went dark: the
        // footprint swept kilometres, then jumped (play-2 "super fast").
        const float* shadow_light = fog.sun;
        set4(frame.light, shadow_light[0], shadow_light[1], shadow_light[2], shadow_strength);
        set4(frame.cloud, kSkyCloudBase, kSkyCloudTop, coverage, density);
        const float windy = std::clamp((weather_wind_.gpu.speed - kWindCalmSpeed - kWindStormSpeed * storm) / kWindWindySpeed, 0.f, 1.f);
        const float day = env.valid ? sky_day_variation(env.game_hours) : 0.f;
        const auto types = sky_cloud_weather(0.f, 0.f, windy, day);
        set4(frame.types, types.type_base, types.type_spread, 0.f, 0.f);
        // The arriving deck is a storm deck before the rain reaches the eye.
        const auto deck = sky_cloud_weather(1.f, weather_test ? storm : sky_deck_storm(1.f, storm, sky_deck_kind_), windy, day);
        set4(frame.deck_types, deck.type_base, deck.type_spread, deck.storm, 1.f);
        set4(frame.deck_cloud, 1.f, 1.3f, front.lead, front.rear);
        set4(frame.deck_axis, -wx, -wy, kSkyFrontBuild, front.pre);
        float cirrus_direction[2];
        sky_cirrus_motion(sky_wind_offset_, wx, wy, cirrus_direction, frame.cirrus_phase);
        set4(frame.cirrus, cirrus_direction[0], cirrus_direction[1], types.cirrus * (1.f - closed), kSkyCirrusAltitude);
        set4(frame.weather, haze, overcast, fog_wetness_, sky_history_valid_ ? .9f : 0.f);
        sky_noise_phases(sky_wind_offset_, frame.noise_phase);
        // Mid altocumulus sheet: part of the fair sky, hidden as the deck closes.
        sky_mid_phases(sky_wind_offset_, frame.mid, frame.mid_shape);
        frame.mid[2] = look.coverage * .7f * (1.f - closed);
        frame.mid[3] = .5f * density;
        // Shadow map: a world-anchored grid around the view focus, snapped to
        // whole texels so the map never crawls as the camera moves.
        const float* m = fog.clip_to_world;
        const float cw = m[14] + m[15];
        float focus[2] = {eye[0], eye[1]};
        if (std::abs(cw) > 1e-12f) {
            float d[3] = {(m[2] + m[3]) / cw - eye[0], (m[6] + m[7]) / cw - eye[1], (m[10] + m[11]) / cw - eye[2]};
            const float l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (l > 1e-6f) {
                for (float& v : d) v /= l;
                const float reach = std::min(200.f, std::max(0.f, eye[2] - fog_terrain_low_z_) / std::max(-d[2], .2f));
                focus[0] += d[0] * reach;focus[1] += d[1] * reach;
            }
        }
        const float texel = kSkyShadowExtent / float(sky_shadow_size_);
        const float origin[2] = {std::floor((focus[0] - kSkyShadowExtent * .5f) / texel) * texel,
                                 std::floor((focus[1] - kSkyShadowExtent * .5f) / texel) * texel};
        set4(frame.shadow, origin[0], origin[1], kSkyShadowExtent, .15f * overcast);
        // Beyond the shadow map the ground takes the closed deck's shadow
        // (skyCloudShadowResponse of a fully dense column, .6, through the
        // strength and overcast lift of skyShadowCS) as far as the deck has
        // closed; fair skies keep 1 there as before.
        const float closed_t = std::clamp(1.f - .4f * shadow_strength, .05f, 1.f);
        const float far_cloud_visibility = sky_mix(1.f, closed_t + (1.f - closed_t) * .15f * overcast, closed);
        // Horizon: the fog's haze colour (the same formula as fogHazeColor),
        // so distant terrain and the sky meet in one colour.
        const float* g = sky_fog_color_;
        float haze_color[3];
        fog_lit_haze_color(g, fog.tint, fog.light, haze_color);
        // Under a closing deck the horizon haze turns to a slightly lighter
        // grey of its own brightness, never the game fog's teal (runtime:
        // a cyan stripe under the dark storm deck).
        {
            const float lum = .3f * haze_color[0] + .5f * haze_color[1] + .2f * haze_color[2];
            const float grey[3] = {lum * .9f, lum * .93f, lum * .98f};
            for (int k = 0; k < 3; ++k) haze_color[k] = sky_mix(haze_color[k], grey[k], .85f * closed);
        }
        set4(frame.horizon, haze_color[0], haze_color[1], haze_color[2], std::clamp(.3f + haze * .5f + look.dust * .15f, 0.f, .8f));
        set4(frame.game, sky_linear(g[0]), sky_linear(g[1]), sky_linear(g[2]), night * 1.2f * night_brightness);
        set4(frame.stars, look.star_tint[0], look.star_tint[1], look.star_tint[2], look.dust);
        frame.size[0] = sky_march_width_;frame.size[1] = sky_march_height_;frame.size[2] = width_;frame.size[3] = height_;
        // The sub-pixel cycles every rendered frame (also while paused) so a
        // still view converges. Sampling jitter advances with reconstruction;
        // physical cloud motion alone follows game time.
        frame.reconstruct[0] = sky_cloud_width_;frame.reconstruct[1] = sky_cloud_height_;
        sky_reconstruct_offset(sky_jitter_++, frame.reconstruct + 2);
        ++sky_frame_;
        frame.control[0] = sky_frame_;frame.control[1] = q.steps;frame.control[2] = q.light_steps;frame.control[3] = sky_probe_width_;

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context_->Map(sky_constants_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            problem = "sky constant upload failed: sky omitted this frame";
        } else {
            std::memcpy(mapped.pData, &frame, sizeof(frame));context_->Unmap(sky_constants_.Get(), 0);
            transition_profile_pass("sky_clouds");
            // Outputs must not stay bound as pixel inputs.
            ID3D11ShaderResourceView* none[6]{};
            context_->PSSetShaderResources(10, 1, none);context_->PSSetShaderResources(12, 1, none);
            const unsigned current = 1u - sky_index_, previous = sky_index_;
            ID3D11Buffer* constants = sky_constants_.Get();context_->CSSetConstantBuffers(0, 1, &constants);
            ID3D11SamplerState* samplers[2] = {sky_wrap_sampler_.Get(), sky_clamp_sampler_.Get()};
            context_->CSSetSamplers(0, 2, samplers);
            ID3D11ShaderResourceView* noise[3] = {sky_shape_srv_.Get(), sky_detail_srv_.Get(), sky_weather_srv_.Get()};
            context_->CSSetShaderResources(0, 3, noise);
            auto run = [&](ID3D11ComputeShader* shader, UINT slot, ID3D11UnorderedAccessView* output, UINT x, UINT y) {
                context_->CSSetShader(shader, nullptr, 0);
                context_->CSSetUnorderedAccessViews(slot, 1, &output, nullptr);
                context_->Dispatch(x, y, 1);
                ID3D11UnorderedAccessView* empty = nullptr;context_->CSSetUnorderedAccessViews(slot, 1, &empty, nullptr);
            };
            // skyLutCS reads only the eye height, haze, sun and moon
            // directions and their LUT scales: unchanged (e.g. paused), the
            // LUT it wrote last is the same.
            {
                const float* const parts[6] = {&frame.eye[2], &frame.weather[0], frame.sun, frame.moon, frame.lut_sun, frame.lut_moon};
                const std::size_t sizes[6] = {1, 1, 3, 3, 3, 3};
                unsigned char key[14 * sizeof(float)];std::size_t used = 0;
                for (int k = 0; k < 6; ++k) {std::memcpy(key + used, parts[k], sizes[k] * sizeof(float));used += sizes[k] * sizeof(float);}
                if (sky_lut_inputs_.size() != used || std::memcmp(sky_lut_inputs_.data(), key, used) != 0) {
                    run(sky_lut_cs_.Get(), 2, sky_lut_uav_.Get(), (kSkyLutWidth + 7) / 8, (kSkyLutHeight + 7) / 8);
                    try { sky_lut_inputs_.assign(key, key + used); } catch (...) { sky_lut_inputs_.clear(); }
                }
            }
            ID3D11ShaderResourceView* lut = sky_lut_srv_.Get();context_->CSSetShaderResources(3, 1, &lut);
            // The shadow field samples the same density at its own, slower
            // drift offset (sky_cloud_shadow_speed): a second upload of the
            // frame with only the layer phases changed, before the sky passes.
            SkyFrameGpu shadow_frame = frame;
            const double shadow_wind[3] = {sky_shadow_offset_[0], sky_shadow_offset_[1], sky_wind_offset_[2]};
            sky_noise_phases(shadow_wind, shadow_frame.noise_phase);
            sky_mid_phases(shadow_wind, shadow_frame.mid, shadow_frame.mid_shape);
            // The shadow and blur programs read no frame counter or jitter
            // (skyControl, skyReconstruct): with every other constant equal
            // (e.g. paused, camera still), the field they wrote last is the same.
            SkyFrameGpu shadow_key = shadow_frame;
            shadow_key.control[0] = 0;shadow_key.reconstruct[2] = shadow_key.reconstruct[3] = 0;
            const auto* shadow_bytes = reinterpret_cast<const unsigned char*>(&shadow_key);
            const bool shadow_current = sky_shadow_inputs_.size() == sizeof(shadow_key) &&
                std::memcmp(sky_shadow_inputs_.data(), shadow_bytes, sizeof(shadow_key)) == 0;
            D3D11_MAPPED_SUBRESOURCE shadow_mapped{};
            const bool shadow_uploaded = shadow_current ||
                SUCCEEDED(context_->Map(sky_constants_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &shadow_mapped));
            if (shadow_uploaded && !shadow_current) {
                std::memcpy(shadow_mapped.pData, &shadow_frame, sizeof(shadow_frame));context_->Unmap(sky_constants_.Get(), 0);
                run(sky_shadow_cs_.Get(), 3, sky_shadow_raw_uav_.Get(), (sky_shadow_size_ + 7) / 8, (sky_shadow_size_ + 7) / 8);
                // The receivers' five-tap blur once per texel (skyShadowBlurCS), so
                // skyCloudVisibility takes one sample per pixel.
                ID3D11ShaderResourceView* raw = sky_shadow_raw_srv_.Get();context_->CSSetShaderResources(6, 1, &raw);
                run(sky_shadow_blur_cs_.Get(), 3, sky_shadow_uav_.Get(), (sky_shadow_size_ + 7) / 8, (sky_shadow_size_ + 7) / 8);
                context_->CSSetShaderResources(6, 1, none);
                try { sky_shadow_inputs_.assign(shadow_bytes, shadow_bytes + sizeof(shadow_key)); } catch (...) { sky_shadow_inputs_.clear(); }
            }
            // Skipped: the buffer still holds this frame's sky constants.
            if (shadow_current) {
            } else if (!shadow_uploaded || FAILED(context_->Map(sky_constants_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                problem = "sky constant re-upload failed: sky clouds drawn at the shadow drift this frame";
            } else {
                std::memcpy(mapped.pData, &frame, sizeof(frame));context_->Unmap(sky_constants_.Get(), 0);
            }
            ID3D11ShaderResourceView* history = sky_probe_srv_[previous].Get();context_->CSSetShaderResources(5, 1, &history);
            run(sky_probe_cs_.Get(), 2, sky_probe_uav_[current].Get(), (sky_probe_width_ + 7) / 8, (sky_probe_width_ / 2 + 7) / 8);
            run(sky_clouds_cs_.Get(), 2, sky_march_uav_.Get(), (sky_march_width_ + 7) / 8, (sky_march_height_ + 7) / 8);
            ID3D11ShaderResourceView* resolve[2] = {sky_march_srv_.Get(), sky_cloud_srv_[previous].Get()};
            context_->CSSetShaderResources(4, 2, resolve);
            run(sky_resolve_cs_.Get(), 2, sky_cloud_uav_[current].Get(), (sky_cloud_width_ + 7) / 8, (sky_cloud_height_ + 7) / 8);
            context_->CSSetShaderResources(0, 6, none);
            ID3D11Buffer* no_buffer = nullptr;context_->CSSetConstantBuffers(0, 1, &no_buffer);
            ID3D11SamplerState* no_samplers[2]{};context_->CSSetSamplers(0, 2, no_samplers);
            context_->CSSetShader(nullptr, nullptr, 0);
            profiler_.add_draw(5, 5);
            sky_index_ = current;sky_history_valid_ = true;sky_draw_pending_ = sky_inputs_active_ = true;
            fog.cloud[0] = far_cloud_visibility;fog.cloud[1] = origin[0];fog.cloud[2] = origin[1];fog.cloud[3] = 1.f / kSkyShadowExtent;
            fog.temporal[2] = 1.f;
            static const char* const regions[4] = {"region?", "Gipat", "Ingos", "Suslanger"};
            // The values feed the F10 line and the owned_sky event (on a state change).
            char event_key[160];
            std::snprintf(event_key, sizeof(event_key), "%d|%s|%s", quality, problem ? problem : "", state ? state : "");
            if (status_text_wanted() || sky_status_state_ != event_key) std::snprintf(values, sizeof(values),
                "%s %s | %s | sun %.0f moon %.0f deg night %.2f | cover %.2f overcast %.2f haze %.2f deck %.2f->%.2f rain %.2f%s | type %.2f+-%.2f deck storm %.2f front %.1f/%.1f km pre %.2f cirrus %.2f | clouds %ux%u->%ux%u shadow %u probe %u | drift %.2f km at %.0f s",
                quality == 1 ? "low" : "high", env.valid ? "" : "(clock? noon used)",
                regions[std::clamp(env.valid ? env.region : 0, 0, 3)], sky.sun_elevation, sky.moon_elevation, night,
                coverage, overcast, haze, closed, closed_target, rain_level, weather_test ? " (sky_weather_test)" : "", types.type_base, types.type_spread, deck.storm, front.lead * .001f, std::min(front.rear, 999000.f) * .001f, front.pre, types.cirrus * (1.f - closed), sky_march_width_, sky_march_height_, sky_cloud_width_, sky_cloud_height_, sky_shadow_size_, sky_probe_width_,
                std::hypot(sky_wind_offset_[0], sky_wind_offset_[1]) * .001, sky_time_);
        }
    }
    // Reprojection input of the next frame.
    const auto clip = bridge_scene_world_to_clip_snapshot();
    if (clip.valid) sky_previous_clip_ = clip.world_to_clip;
    char event_state[160];
    std::snprintf(event_state, sizeof(event_state), "%d|%s|%s", quality, problem ? problem : "", state ? state : "");
    const bool event_changed = sky_status_state_ != event_state;
    if (event_changed || status_text_wanted())
        std::snprintf(sky_status_, sizeof(sky_status_), "Sky %s%s%s", problem ? "PROBLEM: " : state ? "| " : "| owned ",
                      problem ? problem : state ? state : "", values);
    sky_status_problem_ = problem != nullptr;
    if (event_changed) {
        sky_status_state_ = event_state;
        bridge_report_event("owned_sky", sky_status_);
    }
}

// Behind the first world batch of the frame: the game would draw its sky
// first, so emitting it before the first native batch keeps the game's order.
void Renderer::draw_owned_sky() noexcept {
    if (!sky_draw_pending_ || scene_composited_ || rtv_.Get() != scene_composite_.target()) return;
    sky_draw_pending_ = false;
    transition_profile_pass("sky_draw");
    context_->IASetInputLayout(nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(sky_vs_.Get(), nullptr, 0);context_->PSSetShader(sky_ps_.Get(), nullptr, 0);
    ID3D11Buffer* constants = sky_constants_.Get();context_->PSSetConstantBuffers(0, 1, &constants);
    ID3D11SamplerState* samplers[2] = {sky_wrap_sampler_.Get(), sky_clamp_sampler_.Get()};
    context_->PSSetSamplers(0, 2, samplers);
    ID3D11ShaderResourceView* views[2] = {sky_lut_srv_.Get(), sky_cloud_srv_[sky_index_].Get()};
    context_->PSSetShaderResources(3, 2, views);
    context_->OMSetRenderTargets(1, rtv_.GetAddressOf(), dsv_.Get());
    context_->OMSetDepthStencilState(sky_depth_state_.Get(), 0);
    const float factor[4] = {1, 1, 1, 1};context_->OMSetBlendState(sky_blend_state_.Get(), factor, 0xffffffffu);
    context_->RSSetState(sky_raster_state_.Get());
    D3D11_VIEWPORT viewport{0, 0, float(target_width()), float(target_height()), 0, 1};context_->RSSetViewports(1, &viewport);
    context_->Draw(3, 0);
    ID3D11ShaderResourceView* none[2]{};context_->PSSetShaderResources(3, 2, none);
    profiler_.add_draw(1, 1);profiler_.add_batch();
}

#endif
}  // namespace owned
