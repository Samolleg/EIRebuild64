// Wind trails: thin translucent streaks that trace gusts near the camera in
// windy weather, drawn into the world colour after the ambient particles and
// before SMAA and the interface. Driven by the shared renderer wind
// (weather_wind.h); schedule, presence and shapes in wind_trails.h. One draw:
// each trail is a camera-facing 32-segment ribbon expanded on the GPU from a
// few constants per trail, following its head's path back in time.
#include "wind_trails.h"
#include "wind_trails_shader.h"
#include "dune_wisps.h"
#include "renderer.h"
#include "engine_bridge.h"
#include "world_fog.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>

namespace owned {
namespace {
using namespace wind_trails;

constexpr unsigned kTrailVectors = 4;     // float4 constants per trail

struct WindTrailConstants {
    float clip[16];
    float eye[4], style[4], color[4];
    float fog_range[4], fog_control[4], fog_color[4], depth[4];
    float fog_seam[4], fog_curve[4], fog_span_lo[4], fog_span_hi[4];
    float trail[kMaxSlots * kTrailVectors][4];
};
static_assert(sizeof(WindTrailConstants) % 16 == 0);
static_assert(kMaxSlots * kTrailVectors == 192, "HLSL wtTrail array size");
static_assert(kRibbonSegments == 32, "HLSL ribbon subdivision");

std::array<double,4> trail_multiply(const std::array<float,16>& m, double x, double y, double z, double w) {
    std::array<double,4> r{};
    for (unsigned row = 0; row < 4; ++row)
        r[row] = m[row*4]*x + m[row*4+1]*y + m[row*4+2]*z + m[row*4+3]*w;
    return r;
}

bool trail_invert(const std::array<float,16>& input, std::array<float,16>& output) {
    double m[4][8]{};
    for (unsigned r = 0; r < 4; ++r) for (unsigned c = 0; c < 4; ++c) { m[r][c] = input[r*4+c]; m[r][c+4] = r == c ? 1 : 0; }
    for (unsigned c = 0; c < 4; ++c) {
        unsigned pivot = c;
        for (unsigned r = c+1; r < 4; ++r) if (std::abs(m[r][c]) > std::abs(m[pivot][c])) pivot = r;
        if (std::abs(m[pivot][c]) < 1e-12) return false;
        for (unsigned k = 0; k < 8; ++k) std::swap(m[pivot][k], m[c][k]);
        const double d = m[c][c];
        for (unsigned k = 0; k < 8; ++k) m[c][k] /= d;
        for (unsigned r = 0; r < 4; ++r) if (r != c) { const double f = m[r][c]; for (unsigned k = 0; k < 8; ++k) m[r][k] -= f*m[c][k]; }
    }
    for (unsigned r = 0; r < 4; ++r) for (unsigned c = 0; c < 4; ++c) output[r*4+c] = static_cast<float>(m[r][c+4]);
    return true;
}

// The world camera of an effect pass (wind trails, dune wisps): eye, pixel
// scale, depth linearisation and where screen rays meet the CPU height grid
// (1 m vertices).
struct EffectCamera {
    std::array<float,16> inverse{};
    std::array<double,3> eye{};
    double pixel_scale = 0, depth_a = 0, depth_b = 0, focus_distance = 40;
    std::array<double,3> focus{};bool focus_hit = false;
    const float* height = nullptr;std::uint32_t verts_x = 0, verts_y = 0;
    bool unproject(double x, double y, double z, std::array<double,3>& out) const {
        const auto h = trail_multiply(inverse,x,y,z,1);
        if (std::abs(h[3]) < 1e-12) return false;
        out = {h[0]/h[3],h[1]/h[3],h[2]/h[3]};return true;
    }
    double ground(double x, double y) const {
        x = std::clamp(x,0.0,double(verts_x-1));y = std::clamp(y,0.0,double(verts_y-1));
        const auto ix = std::min<std::uint32_t>(std::uint32_t(x),verts_x-2), iy = std::min<std::uint32_t>(std::uint32_t(y),verts_y-2);
        const double fx = x-ix, fy = y-iy;
        const auto at = [&](std::uint32_t a, std::uint32_t b) { return double(height[std::size_t(b)*verts_x+a]); };
        return (at(ix,iy)*(1-fx)+at(ix+1,iy)*fx)*(1-fy)+(at(ix,iy+1)*(1-fx)+at(ix+1,iy+1)*fx)*fy;
    }
    // Where a screen ray meets the ground, if it does.
    bool ground_hit(double sx, double sy, std::array<double,3>& hit) const {
        std::array<double,3> a{}, b{};
        if (!unproject(sx,sy,0,a) || !unproject(sx,sy,1,b)) return false;
        std::array<double,3> previous = a;
        for (int step = 1; step <= 64; ++step) {
            const double s = double(step)/64;
            std::array<double,3> p{a[0]+(b[0]-a[0])*s,a[1]+(b[1]-a[1])*s,a[2]+(b[2]-a[2])*s};
            if (p[2] > ground(p[0],p[1])) { previous = p; continue; }
            for (int k = 0; k < 10; ++k) {
                const std::array<double,3> m{(previous[0]+p[0])*.5,(previous[1]+p[1])*.5,(previous[2]+p[2])*.5};
                if (m[2] <= ground(m[0],m[1])) p = m; else previous = m;
            }
            hit = p;return true;
        }
        return false;
    }
};

// Camera from the world clip matrix (column vectors).
bool make_effect_camera(const std::array<float,16>& clip, double width, const float* height,
                        std::uint32_t verts_x, std::uint32_t verts_y, EffectCamera& c) {
    if (!height || verts_x < 2 || verts_y < 2) return false;
    c.height = height;c.verts_x = verts_x;c.verts_y = verts_y;
    if (!trail_invert(clip,c.inverse) || std::abs(c.inverse[14]) < 1e-12) return false;
    std::array<double,3> near_point{}, far_point{}, left{}, right{};
    if (!c.unproject(0,0,0,near_point) || !c.unproject(0,0,1,far_point) || !c.unproject(-.5,0,.5,left) || !c.unproject(.5,0,.5,right)) return false;
    c.eye = {c.inverse[2]/c.inverse[14],c.inverse[6]/c.inverse[14],c.inverse[10]/c.inverse[14]};
    double axis_x[3]{right[0]-left[0],right[1]-left[1],right[2]-left[2]};
    const double axis_length = std::sqrt(axis_x[0]*axis_x[0]+axis_x[1]*axis_x[1]+axis_x[2]*axis_x[2]);
    if (axis_length < 1e-12) return false;
    for (auto& v : axis_x) v /= axis_length;
    c.pixel_scale = std::abs(clip[0]*axis_x[0]+clip[1]*axis_x[1]+clip[2]*axis_x[2])*width*.5;
    // Depth linearisation z_ndc = A + B / w from two points on the centre ray.
    const auto clip_near = trail_multiply(clip,near_point[0]*.8+far_point[0]*.2,near_point[1]*.8+far_point[1]*.2,near_point[2]*.8+far_point[2]*.2,1);
    const auto clip_far = trail_multiply(clip,near_point[0]*.2+far_point[0]*.8,near_point[1]*.2+far_point[1]*.8,near_point[2]*.2+far_point[2]*.8,1);
    if (clip_near[3] <= 0 || clip_far[3] <= 0 || std::abs(1/clip_near[3]-1/clip_far[3]) < 1e-12) return false;
    c.depth_b = (clip_near[2]/clip_near[3]-clip_far[2]/clip_far[3])/(1/clip_near[3]-1/clip_far[3]);
    c.depth_a = clip_near[2]/clip_near[3]-c.depth_b/clip_near[3];
    c.focus_hit = c.ground_hit(0,0,c.focus);
    c.focus_distance = c.focus_hit ? std::sqrt((c.focus[0]-c.eye[0])*(c.focus[0]-c.eye[0])+
        (c.focus[1]-c.eye[1])*(c.focus[1]-c.eye[1])+(c.focus[2]-c.eye[2])*(c.focus[2]-c.eye[2])) : 40.0;
    return true;
}
} // namespace

void append_wind_trail_shader_programs(std::vector<ShaderProgram>& out) {
    static const std::string source = std::string(kAtmosphereShader) + kNeighbourFogHlsl + kPathHlsl + kWindTrailShader;
    out.push_back({source, "wind_trails", nullptr, "windTrailVS", "vs_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
    out.push_back({source, "wind_trails", nullptr, "windTrailPS", "ps_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
}

void append_dune_wisp_shader_programs(std::vector<ShaderProgram>& out) {
    static const std::string source = std::string(kAtmosphereShader) + kNeighbourFogHlsl + dune_wisps::kDuneWispShader;
    out.push_back({source, "dune_wisps", nullptr, "duneWispVS", "vs_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
    out.push_back({source, "dune_wisps", nullptr, "duneWispPS", "ps_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
}

#if !defined(EID3D11_SHADER_EMBED)
void Renderer::render_wind_trails() {
    const auto& config = debug_overlay_.config();
    const int level = configured_integer(config,"wind_trails",1);
    const Quality quality = quality_level(level);
    if (!quality.slots || wind_trails_failed_) return;
    // Only a frame whose world terrain was drawn has a world camera and depth.
    if (ambient_terrain_frame_ != native_frame_index_ || !native_world_clip_valid_ || !world_effect_targets()) return;
    AmbientWorldSource inputs;
    if (!bridge_ambient_world_source(inputs) || !inputs.terrain) return;
    WorldEnvironment environment = bridge_world_fog_snapshot().environment;
    if (!environment.valid) bridge_read_world_environment(environment);
    if (!environment.valid) return;
    // The shared area/height grid (ambient particles, ground cover) gives the
    // zone class and the ground under every trail.
    if (!rebuild_ambient_particle_map(inputs,environment) || ambient_menu_) return;
    const auto fail = [&](const char* reason) { wind_trails_failed_ = true; OutputDebugStringA(reason); };
    if (!d3d12_ && !wind_trails_vs_) {
        ComPtr<ID3DBlob> vs, ps, errors;
        std::vector<ShaderProgram> programs;
        append_wind_trail_shader_programs(programs);
        if (FAILED(compile_shader(programs[0], &vs, &errors)) || FAILED(compile_shader(programs[1], &ps, &errors))) {
            if (errors) OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
            return fail("EID3D11 wind trails: shaders unavailable; wind trails omitted.\n");
        }
        D3D11_BUFFER_DESC b{};b.ByteWidth=sizeof(WindTrailConstants);b.Usage=D3D11_USAGE_DYNAMIC;
        b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;b.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        D3D11_BLEND_DESC blend{};auto& t=blend.RenderTarget[0];t.BlendEnable=TRUE;
        t.SrcBlend=t.SrcBlendAlpha=D3D11_BLEND_ONE;t.DestBlend=t.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;
        t.BlendOp=t.BlendOpAlpha=D3D11_BLEND_OP_ADD;t.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;
        if (FAILED(create_shader(device_.Get(),vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&wind_trails_vs_)) ||
            FAILED(create_shader(device_.Get(),ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&wind_trails_ps_)) ||
            FAILED(device_->CreateBuffer(&b,nullptr,&wind_trails_constants_)) ||
            FAILED(device_->CreateBlendState(&blend,&wind_trails_blend_)) ||
            FAILED(device_->CreateRasterizerState(&raster,&wind_trails_raster_))) {
            wind_trails_vs_.Reset();
            return fail("EID3D11 wind trails: pipeline objects unavailable; wind trails omitted.\n");
        }
    }

    // Game time: frozen while paused, faster at fast speed. A load, map change,
    // level change or time jump clears the latched trails.
    const double seconds = environment.world_time*kWeatherSecondsPerSegment;
    const double dt = seconds - wind_trails_time_;
    if (wind_trails_time_ < 0 || !(dt >= 0 && dt < 2) || wind_trails_generation_ != ambient_map_generation_ ||
        wind_trails_level_ != level)
        wind_trails_slots_ = {};
    wind_trails_time_ = seconds;wind_trails_generation_ = ambient_map_generation_;wind_trails_level_ = level;
    const std::uint32_t seed = 0x6a09e667u ^ static_cast<std::uint32_t>(ambient_map_generation_*0x85ebca6bu);

    const WeatherWindGpu& wind = weather_wind();
    const auto zone = static_cast<ambient::ZoneClass>(ambient_zone_);
    const float target = target_count(wind,zone,quality);

    EffectCamera cam;
    if (!make_effect_camera(native_world_clip_,double(width_),ambient_height_.data(),ambient_tiles_x_*2u+1u,ambient_tiles_y_*2u+1u,cam)) return;
    const std::array<double,3>& eye = cam.eye;
    const double pixel_scale = cam.pixel_scale, depth_a = cam.depth_a, depth_b = cam.depth_b, focus_distance = cam.focus_distance;
    const auto ground_hit = [&](double sx, double sy, std::array<double,3>& hit) { return cam.ground_hit(sx,sy,hit); };
    const double gust_seconds = std::fmod(seconds,16384.0);  // the grass gust clock

    // Latch each slot's trail at its spawn: a few screen samples biased to
    // the lower (near) part of the view, kept by gust-front weight, started
    // upwind so the trail crosses the sampled point mid-life.
    for (unsigned i = 0; i < quality.slots; ++i) {
        auto& slot = wind_trails_slots_[i];
        const Cycle cycle = slot_cycle(seed,i,seconds);
        if (slot.cycle == cycle.index || seconds < cycle.spawn) {
            if (slot.cycle != cycle.index) slot.live = false;
            continue;
        }
        slot.cycle = cycle.index;slot.live = false;
        // A cycle first seen well after its spawn (after a reset) is skipped: no mid-life pop-in.
        if (seconds-cycle.spawn > .25 || !slot_active(seed,i,cycle.index,target,quality)) continue;
        float best = -1;std::array<double,3> chosen{};
        for (unsigned attempt = 0; attempt < 6; ++attempt) {
            const float rx = slot_random(seed,i,cycle.index,0x100u+attempt), ry = slot_random(seed,i,cycle.index,0x200u+attempt);
            std::array<double,3> hit{};
            if (!ground_hit((2.0*rx-1.0)*.95,-.95+1.45*std::pow(double(ry),1.6),hit)) continue;
            const float score = spawn_weight(gust_front(float(hit[0]),float(hit[1]),wind,gust_seconds))*
                (.5f+slot_random(seed,i,cycle.index,0x300u+attempt));
            if (score > best) { best = score; chosen = hit; }
        }
        if (best < 0) continue;
        slot.shape = make_shape(seed,i,cycle.index,wind,cycle.life);
        slot.direction[0] = wind.direction[0];slot.direction[1] = wind.direction[1];
        const double back = slot.shape.speed*cycle.life*.45;
        slot.origin[0] = float(chosen[0]-wind.direction[0]*back);slot.origin[1] = float(chosen[1]-wind.direction[1]*back);
        slot.spawn = cycle.spawn;slot.life = cycle.life;slot.live = true;
    }

    WindTrailConstants c{};
    unsigned count = 0;
    for (unsigned i = 0; i < quality.slots; ++i) {
        const auto& slot = wind_trails_slots_[i];
        const double age = seconds-slot.spawn;
        if (!slot.live || age < 0 || age >= slot.life) continue;
        const Shape& s = slot.shape;
        float (*t)[4] = c.trail+count*kTrailVectors;
        const float values[kTrailVectors][4] = {
            {slot.origin[0],slot.origin[1],s.height,float(age)},
            {slot.direction[0],slot.direction[1],s.speed,s.trail_time},
            {s.amp,s.rate,s.phase,slot.life},
            {s.lift,s.loop_radius,s.width,float(i)/float(kMaxSlots)}};
        std::memcpy(t,values,sizeof(values));
        ++count;
    }
    if (!count) return;

    std::memcpy(c.clip,native_world_clip_.data(),sizeof(c.clip));
    jitter_clip(c.clip);
    for (unsigned k = 0; k < 3; ++k) c.eye[k] = float(eye[k]);
    c.eye[3] = float(pixel_scale);
    // Region tint: desert dust, snow-pale specks, pale air elsewhere; lit by
    // the world light (so night dims it), thinner in rain and at night.
    float albedo[3]{.95f,.97f,1.f};
    if (zone == ambient::zone_desert) { albedo[0] = .9f; albedo[1] = .78f; albedo[2] = .6f; }
    else if (zone == ambient::zone_snow) { albedo[0] = .93f; albedo[1] = .96f; albedo[2] = 1.f; }
    else if (zone == ambient::zone_dead_city) { albedo[0] = .8f; albedo[1] = .8f; albedo[2] = .78f; }
    float ambient_light[3]{.2f,.2f,.2f}, sun[3]{.6f,.6f,.6f};float sun_z = 1;
    if (native_world_lighting_valid_) {
        for (unsigned k = 0; k < 3; ++k) { ambient_light[k] = native_world_lighting_.ambient[k]; sun[k] = native_world_lighting_.sunlight_diffuse[k]; }
        const auto& d = native_world_lighting_.sunlight_direction;
        const float l = std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
        sun_z = l > 1e-6f ? std::clamp(d[2]/l,0.f,1.f) : 1.f;
    }
    for (unsigned k = 0; k < 3; ++k) c.color[k] = albedo[k]*std::min(std::max(ambient_light[k],sun[k]*(.6f+.4f*sun_z))+.1f,1.2f);
    const float night = fog_night(environment.hour);
    c.style[0] = .32f*(1-.45f*night)*(1-.3f*wind.rain)*(zone == ambient::zone_desert ? 1.25f : 1.f);
    c.style[1] = .025f;  // world width, metres
    c.style[2] = 1.0f;   // minimum width, pixels
    c.style[3] = zone == ambient::zone_snow ? .7f : 0.f;
    const auto& fog = ambient_terrain_state_;
    c.fog_range[0] = fog.fog_min[0];c.fog_range[1] = fog.fog_min[1];c.fog_range[2] = fog.fog_max[0];c.fog_range[3] = fog.fog_max[1];
    c.fog_control[0] = fog.fog_distance;c.fog_control[1] = fog.fog_enabled ? 1.f : 0.f;
    c.fog_color[0] = ((ambient_fog_color_>>16)&255)/255.f;c.fog_color[1] = ((ambient_fog_color_>>8)&255)/255.f;
    c.fog_color[2] = (ambient_fog_color_&255)/255.f;
    neighbour_fog_constants(neighbour_fog_, c.fog_seam, c.fog_curve, c.fog_span_lo, c.fog_span_hi);
    c.depth[0] = float(depth_a);c.depth[1] = float(depth_b);c.depth[2] = .25f;c.depth[3] = float(focus_distance*1.6+10.0);

    // Trails take the shared world atmosphere; without it they are omitted.
    if (FAILED(bind_world_fog())) return;
    if (d3d12_) {
        // The same programs and constants on D3D12: height at t0, world depth at t2.
        auto& programs = ambient12_.wind;
        if (programs.empty()) append_wind_trail_shader_programs(programs);
        if (programs.size() < 2 || !ambient12_.height) { d3d12_->omit("wind trails (programs or height grid missing)",false); return; }
        transition_profile_pass("wind_trails");
        bind_world_effect_d3d12(false);
        d3d12_->set_constants(0,&c,sizeof(c));
        d3d12_->set_resource(0,ambient12_.height.get());
        const HRESULT hr = d3d12_->draw_instanced(programs[0],programs[1],world_effect_state_d3d12(false),nullptr,
                                                  2*(kRibbonSegments+1),count,true);
        d3d12_->clear_slots();bind_world_targets_d3d12();
        if (FAILED(hr)) d3d12_->omit("wind trails (draw failed)",false);
        else if (hr == S_FALSE) d3d12_->omit("wind trails (pipeline not ready)",false);
        else if (hr == S_OK) { d3d12_->count("wind trail draws");profiler_.add_draw(1,1);profiler_.add_batch(1); }
        return;
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context_->Map(wind_trails_constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped))) return;
    std::memcpy(mapped.pData,&c,sizeof(c));context_->Unmap(wind_trails_constants_.Get(),0);

    // Runs inside composite_world. It binds every state it reads, restores
    // nothing and unbinds its views after (every following pass binds its own).
    transition_profile_pass("wind_trails");
    bind_plan_targets(plan_pass("wind_trails",rtv_.Get(),nullptr,plan_read(PlanTarget::world_depth))); // depth is read as a texture
    D3D11_VIEWPORT viewport = target_viewport();context_->RSSetViewports(1,&viewport);
    context_->RSSetState(wind_trails_raster_.Get());context_->OMSetDepthStencilState(nullptr,0);
    const float factor[4]{1,1,1,1};context_->OMSetBlendState(wind_trails_blend_.Get(),factor,~0u);
    context_->IASetInputLayout(nullptr);context_->IASetVertexBuffers(0,0,nullptr,nullptr,nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    context_->VSSetShader(wind_trails_vs_.Get(),nullptr,0);context_->PSSetShader(wind_trails_ps_.Get(),nullptr,0);
    context_->GSSetShader(nullptr,nullptr,0);
    context_->VSSetConstantBuffers(0,1,wind_trails_constants_.GetAddressOf());context_->PSSetConstantBuffers(0,1,wind_trails_constants_.GetAddressOf());
    context_->VSSetShaderResources(0,1,ambient_height_srv_.GetAddressOf());context_->PSSetShaderResources(2,1,depth_srv_.GetAddressOf());
    bind_owned_sky_pixel_inputs();  // borderFogColor reads the sky probe
    context_->DrawInstanced(2*(kRibbonSegments+1),count,0,0);
    ID3D11ShaderResourceView* no_view=nullptr;
    context_->VSSetShaderResources(0,1,&no_view);context_->PSSetShaderResources(2,1,&no_view);
    profiler_.add_draw(1,1);profiler_.add_batch(1);
}



namespace {
struct DuneWispConstants {
    float clip[16];
    float eye[4], style[4], color[4];
    float fog_range[4], fog_control[4], fog_color[4], depth[4];
    float fog_seam[4], fog_curve[4], fog_span_lo[4], fog_span_hi[4];
    float wisp[dune_wisps::kMaxSlots * dune_wisps::kWispVectors][4];
};
static_assert(sizeof(DuneWispConstants) % 16 == 0);
static_assert(dune_wisps::kMaxSlots * dune_wisps::kWispVectors == 240, "HLSL dwWisp array size");
static_assert(dune_wisps::kSegments == 24, "HLSL ribbon subdivision");
static_assert(dune_wisps::kStrands == 5, "HLSL strands per wisp");
} // namespace

// Dune wisps (#256): sand plumes off the Suslanger dune crests, drawn after
// the wind trails into the world colour. Desert zone only.
void Renderer::render_dune_wisps() {
    using namespace dune_wisps;
    const auto& config = debug_overlay_.config();
    const int level = configured_integer(config,"dune_wisps",1);
    const dune_wisps::Quality quality = dune_wisps::quality_level(level);
    if (!quality.slots || dune_wisps_failed_) return;
    if (ambient_terrain_frame_ != native_frame_index_ || !native_world_clip_valid_ || !world_effect_targets()) return;
    if (ambient_zone_ != ambient::zone_desert) return;
    AmbientWorldSource inputs;
    if (!bridge_ambient_world_source(inputs) || !inputs.terrain) return;
    WorldEnvironment environment = bridge_world_fog_snapshot().environment;
    if (!environment.valid) bridge_read_world_environment(environment);
    if (!environment.valid || environment.cave) return;
    if (!rebuild_ambient_particle_map(inputs,environment) || ambient_menu_ || ambient_zone_ != ambient::zone_desert) return;
    const auto fail = [&](const char* reason) { dune_wisps_failed_ = true; OutputDebugStringA(reason); };
    if (!d3d12_ && !dune_wisps_vs_) {
        ComPtr<ID3DBlob> vs, ps, errors;
        std::vector<ShaderProgram> programs;
        append_dune_wisp_shader_programs(programs);
        if (FAILED(compile_shader(programs[0], &vs, &errors)) || FAILED(compile_shader(programs[1], &ps, &errors))) {
            if (errors) OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
            return fail("EID3D11 dune wisps: shaders unavailable; dune wisps omitted.\n");
        }
        D3D11_BUFFER_DESC b{};b.ByteWidth=sizeof(DuneWispConstants);b.Usage=D3D11_USAGE_DYNAMIC;
        b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;b.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        D3D11_BLEND_DESC blend{};auto& t=blend.RenderTarget[0];t.BlendEnable=TRUE;
        t.SrcBlend=t.SrcBlendAlpha=D3D11_BLEND_ONE;t.DestBlend=t.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;
        t.BlendOp=t.BlendOpAlpha=D3D11_BLEND_OP_ADD;t.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;
        if (FAILED(create_shader(device_.Get(),vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&dune_wisps_vs_)) ||
            FAILED(create_shader(device_.Get(),ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&dune_wisps_ps_)) ||
            FAILED(device_->CreateBuffer(&b,nullptr,&dune_wisps_constants_)) ||
            FAILED(device_->CreateBlendState(&blend,&dune_wisps_blend_)) ||
            FAILED(device_->CreateRasterizerState(&raster,&dune_wisps_raster_))) {
            dune_wisps_vs_.Reset();
            return fail("EID3D11 dune wisps: pipeline objects unavailable; dune wisps omitted.\n");
        }
    }

    const std::uint32_t verts_x = ambient_tiles_x_*2u+1u, verts_y = ambient_tiles_y_*2u+1u;
    if (ambient_height_.size() != std::size_t(verts_x)*verts_y || ambient_area_.size() != std::size_t(ambient_tiles_x_)*ambient_tiles_y_) return;
    // The map's crests along its own wind (fixed per map), found once.
    const float* base = weather_wind_.base;
    if (dune_crest_generation_ != ambient_map_generation_ || dune_crest_base_[0] != base[0] || dune_crest_base_[1] != base[1]) {
        dune_crests_ = find_crests(ambient_height_.data(),verts_x,verts_y,ambient_area_.data(),ambient_tiles_x_,ambient_tiles_y_,base,kLeeDrop);
        dune_crest_generation_ = ambient_map_generation_;dune_crest_base_[0] = base[0];dune_crest_base_[1] = base[1];
        dune_wisps_slots_ = {};dune_crest_near_.clear();dune_crest_near_focus_[0] = dune_crest_near_focus_[1] = -1e9f;
        char detail[160];
        std::snprintf(detail,sizeof(detail),"%s: %u crest points",ambient_map_name_.c_str(),unsigned(dune_crests_.size()));
        bridge_append_probe_event("dune_wisps",detail);
    }
    if (dune_crests_.empty()) return;

    // Game time: frozen while paused, faster at fast speed. A jump clears the latched wisps.
    const double seconds = environment.world_time*kWeatherSecondsPerSegment;
    const double dt = seconds - dune_wisps_time_;
    if (dune_wisps_time_ < 0 || !(dt >= 0 && dt < 2) || dune_wisps_level_ != level) dune_wisps_slots_ = {};
    dune_wisps_time_ = seconds;dune_wisps_level_ = level;
    const std::uint32_t seed = 0x3c6ef372u ^ static_cast<std::uint32_t>(ambient_map_generation_*0x85ebca6bu);

    EffectCamera cam;
    if (!make_effect_camera(native_world_clip_,double(width_),ambient_height_.data(),verts_x,verts_y,cam)) return;
    // Wisps are kept within reach of the view focus and fade out before it.
    const double reach = std::clamp(cam.focus_distance*1.8+20.0,50.0,160.0);
    const double centre[2]{cam.focus_hit ? cam.focus[0] : cam.eye[0],cam.focus_hit ? cam.focus[1] : cam.eye[1]};
    // Crest points near the focus, refreshed as the view moves.
    const double moved = std::hypot(centre[0]-dune_crest_near_focus_[0],centre[1]-dune_crest_near_focus_[1]);
    if (moved > 8.0 || std::abs(reach-dune_crest_near_reach_) > 10.0) {
        dune_crest_near_.clear();
        const double r = reach+12.0;
        for (std::uint32_t k = 0; k < dune_crests_.size(); ++k) {
            const auto& p = dune_crests_[k];
            if ((p.x-centre[0])*(p.x-centre[0])+(p.y-centre[1])*(p.y-centre[1]) < r*r) dune_crest_near_.push_back(k);
        }
        dune_crest_near_focus_[0] = float(centre[0]);dune_crest_near_focus_[1] = float(centre[1]);dune_crest_near_reach_ = float(reach);
    }

    const WeatherWindGpu& wind = weather_wind();
    const float target = dune_wisps::target_count(wind,quality);
    const double gust_seconds = std::fmod(seconds,16384.0);
    for (unsigned i = 0; i < quality.slots; ++i) {
        auto& slot = dune_wisps_slots_[i];
        const dune_wisps::Cycle cycle = dune_wisps::slot_cycle(seed,i,seconds);
        if (slot.cycle == cycle.index || seconds < cycle.spawn) {
            if (slot.cycle != cycle.index) slot.live = false;
            continue;
        }
        slot.cycle = cycle.index;slot.live = false;
        // A cycle first seen well after its spawn (after a reset) is skipped: no mid-life pop-in.
        if (seconds-cycle.spawn > .25 || dune_crest_near_.empty() || !dune_wisps::slot_active(seed,i,cycle.index,target,quality)) continue;
        float best = -1;const CrestPoint* chosen = nullptr;
        for (unsigned attempt = 0; attempt < 8; ++attempt) {
            const float rk = dune_wisps::slot_random(seed,i,cycle.index,0x100u+attempt);
            const auto& p = dune_crests_[dune_crest_near_[std::min<std::size_t>(std::size_t(rk*dune_crest_near_.size()),dune_crest_near_.size()-1)]];
            const auto h = trail_multiply(native_world_clip_,p.x,p.y,p.z,1);
            if (h[3] <= .1 || std::abs(h[0]/h[3]) > 1.05 || std::abs(h[1]/h[3]) > 1.05) continue;
            const double d = std::sqrt((p.x-cam.eye[0])*(p.x-cam.eye[0])+(p.y-cam.eye[1])*(p.y-cam.eye[1])+(p.z-cam.eye[2])*(p.z-cam.eye[2]));
            if (d > reach*.85) continue;
            const float score = wind_trails::spawn_weight(wind_trails::gust_front(p.x,p.y,wind,gust_seconds))*p.strength*
                (.5f+dune_wisps::slot_random(seed,i,cycle.index,0x300u+attempt));
            if (score > best) { best = score; chosen = &p; }
        }
        if (!chosen) continue;
        slot.shape = dune_wisps::make_shape(seed,i,cycle.index,wind,chosen->strength);
        slot.direction[0] = wind.direction[0];slot.direction[1] = wind.direction[1];
        slot.origin[0] = chosen->x;slot.origin[1] = chosen->y;slot.origin[2] = chosen->z;
        slot.spawn = cycle.spawn;slot.live = true;
    }

    DuneWispConstants c{};
    unsigned count = 0;
    for (unsigned i = 0; i < quality.slots; ++i) {
        const auto& slot = dune_wisps_slots_[i];
        const double age = seconds-slot.spawn;
        if (!slot.live || age < 0 || age >= slot.shape.life) continue;
        const auto& s = slot.shape;
        float (*w)[4] = c.wisp+count*kWispVectors;
        const float values[kWispVectors][4] = {
            {slot.origin[0],slot.origin[1],slot.origin[2],float(age)},
            {slot.direction[0],slot.direction[1],s.speed,s.emit},
            {s.length,s.width,s.spread,s.life},
            {s.amp,s.rate,s.phase,s.rise},
            {s.alpha,float(i)/float(dune_wisps::kMaxSlots),0,0}};
        std::memcpy(w,values,sizeof(values));
        ++count;
    }
    dune_wisps_drawn_ = count;
    if (!count) return;

    std::memcpy(c.clip,native_world_clip_.data(),sizeof(c.clip));
    jitter_clip(c.clip);
    for (unsigned k = 0; k < 3; ++k) c.eye[k] = float(cam.eye[k]);
    c.eye[3] = float(cam.pixel_scale);
    // Sand lit like the dune it blew from: ambient plus most of the sun.
    const float albedo[3]{.86f,.74f,.56f};
    float ambient_light[3]{.2f,.2f,.2f}, sun[3]{.6f,.6f,.6f};float sun_z = 1;
    if (native_world_lighting_valid_) {
        for (unsigned k = 0; k < 3; ++k) { ambient_light[k] = native_world_lighting_.ambient[k]; sun[k] = native_world_lighting_.sunlight_diffuse[k]; }
        const auto& d = native_world_lighting_.sunlight_direction;
        const float l = std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
        sun_z = l > 1e-6f ? std::clamp(d[2]/l,0.f,1.f) : 1.f;
    }
    for (unsigned k = 0; k < 3; ++k) c.color[k] = albedo[k]*std::min(ambient_light[k]+sun[k]*(.5f+.4f*sun_z),1.2f);
    const float night = fog_night(environment.hour);
    c.style[0] = .75f*(1-.4f*night);
    c.style[1] = .16f;  // strand width, metres
    c.style[2] = 1.2f;  // minimum strand width, pixels
    const auto& fog = ambient_terrain_state_;
    c.fog_range[0] = fog.fog_min[0];c.fog_range[1] = fog.fog_min[1];c.fog_range[2] = fog.fog_max[0];c.fog_range[3] = fog.fog_max[1];
    c.fog_control[0] = fog.fog_distance;c.fog_control[1] = fog.fog_enabled ? 1.f : 0.f;
    c.fog_color[0] = ((ambient_fog_color_>>16)&255)/255.f;c.fog_color[1] = ((ambient_fog_color_>>8)&255)/255.f;
    c.fog_color[2] = (ambient_fog_color_&255)/255.f;
    neighbour_fog_constants(neighbour_fog_, c.fog_seam, c.fog_curve, c.fog_span_lo, c.fog_span_hi);
    c.depth[0] = float(cam.depth_a);c.depth[1] = float(cam.depth_b);c.depth[2] = .4f;c.depth[3] = float(reach);

    if (FAILED(bind_world_fog())) return;
    if (d3d12_) {
        auto& programs = ambient12_.wisps;
        if (programs.empty()) append_dune_wisp_shader_programs(programs);
        if (programs.size() < 2 || !ambient12_.height) { d3d12_->omit("dune wisps (programs or height grid missing)",false); return; }
        transition_profile_pass("dune_wisps");
        bind_world_effect_d3d12(false);
        d3d12_->set_constants(0,&c,sizeof(c));
        d3d12_->set_resource(0,ambient12_.height.get());
        const HRESULT hr = d3d12_->draw_instanced(programs[0],programs[1],world_effect_state_d3d12(false),nullptr,
                                                  2*(kSegments+1),count*kStrands,true);
        d3d12_->clear_slots();bind_world_targets_d3d12();
        if (FAILED(hr)) d3d12_->omit("dune wisps (draw failed)",false);
        else if (hr == S_FALSE) d3d12_->omit("dune wisps (pipeline not ready)",false);
        else if (hr == S_OK) { d3d12_->count("dune wisp draws");profiler_.add_draw(1,1);profiler_.add_batch(1); }
        return;
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context_->Map(dune_wisps_constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped))) return;
    std::memcpy(mapped.pData,&c,sizeof(c));context_->Unmap(dune_wisps_constants_.Get(),0);
    // Runs inside composite_world like the wind trails: binds what it reads, unbinds its views after.
    transition_profile_pass("dune_wisps");
    bind_plan_targets(plan_pass("dune_wisps",rtv_.Get(),nullptr,plan_read(PlanTarget::world_depth)));
    D3D11_VIEWPORT viewport = target_viewport();context_->RSSetViewports(1,&viewport);
    context_->RSSetState(dune_wisps_raster_.Get());context_->OMSetDepthStencilState(nullptr,0);
    const float factor[4]{1,1,1,1};context_->OMSetBlendState(dune_wisps_blend_.Get(),factor,~0u);
    context_->IASetInputLayout(nullptr);context_->IASetVertexBuffers(0,0,nullptr,nullptr,nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    context_->VSSetShader(dune_wisps_vs_.Get(),nullptr,0);context_->PSSetShader(dune_wisps_ps_.Get(),nullptr,0);
    context_->GSSetShader(nullptr,nullptr,0);
    context_->VSSetConstantBuffers(0,1,dune_wisps_constants_.GetAddressOf());context_->PSSetConstantBuffers(0,1,dune_wisps_constants_.GetAddressOf());
    context_->VSSetShaderResources(0,1,ambient_height_srv_.GetAddressOf());context_->PSSetShaderResources(2,1,depth_srv_.GetAddressOf());
    bind_owned_sky_pixel_inputs();
    context_->DrawInstanced(2*(kSegments+1),count*kStrands,0,0);
    ID3D11ShaderResourceView* no_view=nullptr;
    context_->VSSetShaderResources(0,1,&no_view);context_->PSSetShaderResources(2,1,&no_view);
    profiler_.add_draw(1,1);profiler_.add_batch(1);
}

#endif
} // namespace owned
