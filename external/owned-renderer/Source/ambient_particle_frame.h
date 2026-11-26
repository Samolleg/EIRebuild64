#pragma once
// Ambient particle frame setup shared by the renderer
// (renderer_ambient_particles.cpp) and Prototypes/shader-lab: the shader text,
// its constant block, the camera/window constants of one frame, one emitter's
// constants, and the waterfall sources of a terrain. Header-only so the lab
// builds it from the same source.
#include "ambient_particles.h"
#include "ambient_terrain_grids.h"
#include "neighbour_fog.h"
#include "owned_terrain_front_end.h"
#include "procedural_falls.h"
#include "world_fog.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace owned::ambient {

inline constexpr char kAmbientParticleShader[] = "#if 0"
#include "shaders/ambient_particles.hlsl"
"\n#endif\n";

// Game segment clock to seconds (same visual scale as liquid_surface_shader.h).
inline constexpr double kAmbientSecondsPerSegment = .055;

// The shared world atmosphere (b10/t11/s7) is prepended so particles use the
// exact fog functions of world surfaces.
inline std::string ambient_particle_shader_source(std::string_view particles = kAmbientParticleShader) {
    return std::string(kAtmosphereShader) + kNeighbourFogHlsl + std::string(particles);
}

struct AmbientConstants {
    float clip[16];
    float right[4], up[4], eye[4], focus[4], displacement[4];
    float band[4], color_a[4], color_b[4], motion[4], misc[4], velocity[4];
    float affinity_a[4], affinity_b[4], grid[4], ambient[4], sun_direction[4], sun_color[4];
    float fog_range[4], fog_control[4], fog_color[4], depth[4], window[4], reach[4];
    float gust[4];
    float fog_seam[4], fog_curve[4], fog_span_lo[4], fog_span_hi[4];
};
static_assert(sizeof(AmbientConstants) % 16 == 0);

inline std::array<double,4> ambient_multiply(const std::array<float,16>& m, double x, double y, double z, double w) {
    std::array<double,4> r{};
    for (unsigned row = 0; row < 4; ++row)
        r[row] = m[row*4]*x + m[row*4+1]*y + m[row*4+2]*z + m[row*4+3]*w;
    return r;
}

inline bool ambient_invert(const std::array<float,16>& input, std::array<float,16>& output) {
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

// One frame's camera and particle window: the constants every emitter shares
// (clip, screen axes, eye, focus, window, reach, depth linearisation, grid
// size) plus the window extent the presence scan reads.
struct AmbientView {
    AmbientConstants constants{};
    std::array<double,3> focus{};
    bool focus_found = false;
    double reach = 0, window_min[2]{}, window_max[2]{};
    unsigned columns = 0, rows = 0;
};
// height: liquid-surface/ground heights per vertex ((tiles*2+1)^2). Returns
// the reason on failure.
inline const char* ambient_view(const std::array<float,16>& clip, unsigned width, const std::vector<float>& height,
    std::uint32_t tiles_x, std::uint32_t tiles_y, const Quality& quality, double seconds, float size_scale, AmbientView& out) {
    std::array<float,16> inverse{};
    if (!ambient_invert(clip, inverse)) return "Particles: world camera not invertible";
    const auto unproject = [&](double x, double y, double z, std::array<double,3>& p) {
        const auto h = ambient_multiply(inverse,x,y,z,1);
        if (std::abs(h[3]) < 1e-12) return false;
        p = {h[0]/h[3],h[1]/h[3],h[2]/h[3]};return true;
    };
    std::array<double,3> near_point{}, far_point{}, left{}, right{}, down{}, up{};
    if (!unproject(0,0,0,near_point) || !unproject(0,0,1,far_point) || !unproject(-.5,0,.5,left) ||
        !unproject(.5,0,.5,right) || !unproject(0,-.5,.5,down) || !unproject(0,.5,.5,up))
        return "Particles: world camera degenerate";
    if (std::abs(inverse[14]) < 1e-12) return "Particles: orthographic camera"; // no world camera
    const std::array<double,3> eye{inverse[2]/inverse[14],inverse[6]/inverse[14],inverse[10]/inverse[14]};
    const auto unit = [](std::array<double,3> a, const std::array<double,3>& b) {
        for (unsigned k = 0; k < 3; ++k) a[k] = b[k]-a[k];
        const double l = std::sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]);
        if (l > 1e-12) for (auto& v : a) v /= l;
        return a;
    };
    const auto axis_x = unit(left,right), axis_y = unit(down,up);
    const double pixel_scale = std::abs(clip[0]*axis_x[0]+clip[1]*axis_x[1]+clip[2]*axis_x[2])*width*.5;
    // Depth linearisation z_ndc = A + B / w from two points on the centre ray.
    const auto clip_near = ambient_multiply(clip,near_point[0]*.8+far_point[0]*.2,near_point[1]*.8+far_point[1]*.2,near_point[2]*.8+far_point[2]*.2,1);
    const auto clip_far = ambient_multiply(clip,near_point[0]*.2+far_point[0]*.8,near_point[1]*.2+far_point[1]*.8,near_point[2]*.2+far_point[2]*.8,1);
    if (clip_near[3] <= 0 || clip_far[3] <= 0 || std::abs(1/clip_near[3]-1/clip_far[3]) < 1e-12)
        return "Particles: depth range degenerate";
    const double depth_b = (clip_near[2]/clip_near[3]-clip_far[2]/clip_far[3])/(1/clip_near[3]-1/clip_far[3]);
    const double depth_a = clip_near[2]/clip_near[3]-depth_b/clip_near[3];
    // Focus: where the screen-centre ray meets the ground (CPU height grid).
    const std::uint32_t verts_x = tiles_x*2u+1u, verts_y = tiles_y*2u+1u;
    const auto ground = [&](double x, double y) {
        x = std::clamp(x,0.0,double(verts_x-1));y = std::clamp(y,0.0,double(verts_y-1));
        const auto ix = std::min<std::uint32_t>(std::uint32_t(x),verts_x-2), iy = std::min<std::uint32_t>(std::uint32_t(y),verts_y-2);
        const double fx = x-ix, fy = y-iy;
        const auto at = [&](std::uint32_t a, std::uint32_t b) { return double(height[std::size_t(b)*verts_x+a]); };
        return (at(ix,iy)*(1-fx)+at(ix+1,iy)*fx)*(1-fy)+(at(ix,iy+1)*(1-fx)+at(ix+1,iy+1)*fx)*fy;
    };
    std::array<double,3> focus = far_point;bool found = false;
    {
        std::array<double,3> a = near_point;
        for (int step = 1; step <= 96 && !found; ++step) {
            const double s = double(step)/96;
            std::array<double,3> b{near_point[0]+(far_point[0]-near_point[0])*s,near_point[1]+(far_point[1]-near_point[1])*s,
                                   near_point[2]+(far_point[2]-near_point[2])*s};
            if (b[2] <= ground(b[0],b[1])) {
                for (int k = 0; k < 12; ++k) {
                    std::array<double,3> m{(a[0]+b[0])*.5,(a[1]+b[1])*.5,(a[2]+b[2])*.5};
                    if (m[2] <= ground(m[0],m[1])) b = m; else a = m;
                }
                focus = b;found = true;
            } else a = b;
        }
    }
    auto& c = out.constants;
    c = {};
    std::memcpy(c.clip,clip.data(),sizeof(c.clip));
    for (unsigned k = 0; k < 3; ++k) { c.right[k] = float(axis_x[k]); c.up[k] = float(axis_y[k]); c.eye[k] = float(eye[k]); }
    c.right[3] = float(pixel_scale);
    // The field is one world-periodic tile (period = 2 * radius) repeated over
    // a window covering the view frustum up to `reach` from the eye: the focus
    // distance plus the quality depth.
    const double period = 2.0*quality.radius;
    const double focus_distance = found ? std::sqrt((focus[0]-eye[0])*(focus[0]-eye[0])+
        (focus[1]-eye[1])*(focus[1]-eye[1])+(focus[2]-eye[2])*(focus[2]-eye[2])) : 0.0;
    const double reach = focus_distance+quality.radius;
    double window_min[2]{eye[0],eye[1]}, window_max[2]{eye[0],eye[1]};
    for (int sy = -1; sy <= 1; ++sy) for (int sx = -1; sx <= 1; ++sx) {
        std::array<double,3> a{}, b{};
        if (!unproject(sx,sy,0,a) || !unproject(sx,sy,1,b)) continue;
        const auto direction = unit(a,b);
        for (unsigned k = 0; k < 2; ++k) {
            const double v = eye[k]+direction[k]*reach;
            window_min[k] = std::min(window_min[k],v);window_max[k] = std::max(window_max[k],v);
        }
    }
    const double tile_x0 = std::floor((window_min[0]-1)/period), tile_y0 = std::floor((window_min[1]-1)/period);
    out.columns = unsigned(std::floor((window_max[0]+1)/period)-tile_x0)+1u;
    out.rows = unsigned(std::floor((window_max[1]+1)/period)-tile_y0)+1u;
    c.focus[0] = float(focus[0]);c.focus[1] = float(focus[1]);c.focus[2] = float(period);
    c.focus[3] = float(std::fmod(seconds,3600.0));
    c.window[0] = float(tile_x0*period);c.window[1] = float(tile_y0*period);c.window[2] = float(out.columns);
    c.reach[0] = float(reach);c.reach[1] = .6f*quality.radius;c.reach[2] = 1.5f;c.reach[3] = size_scale;
    c.grid[2] = float(tiles_x);c.grid[3] = float(tiles_y);
    c.depth[0] = float(depth_a);c.depth[1] = float(depth_b);c.depth[2] = .25f;
    out.focus = focus;out.focus_found = found;out.reach = reach;
    for (unsigned k = 0; k < 2; ++k) { out.window_min[k] = window_min[k]; out.window_max[k] = window_max[k]; }
    return nullptr;
}

// Shader kind of an emitter (ambVelocity.w): 0 plain, 1 sand, 2 impact
// splashes, 3 drips, 4 cave spores, 5 base mist, 6 fall foam, 7 lip spray.
inline float emitter_kind(Emitter e, bool mushroom) noexcept {
    return e == em_dust ? 1.f : e == em_spray ? 2.f : e == em_drips ? 3.f : mushroom ? 4.f : e == em_mist ? 5.f :
        e == em_fall_foam ? 6.f : e == em_lip_spray ? 7.f : 0.f;
}
// One emitter's constants over the frame's (c). displacement: integrated
// drift (x, y) and rise (z); velocity: its drift per second.
inline void ambient_emitter_constants(AmbientConstants& c, Emitter e, const EmitterStyle& style, const AffinityTable& table,
    bool mushroom, unsigned pool, float scale, const double displacement[3], const double velocity[2]) {
    const double band = style.height_max-style.height_min;
    c.displacement[0] = float(displacement[0]);c.displacement[1] = float(displacement[1]);c.displacement[2] = float(displacement[2]);
    c.band[0] = style.height_min;c.band[1] = float(band);c.band[2] = style.size_min;c.band[3] = style.size_max;
    for (unsigned k = 0; k < 3; ++k) { c.color_a[k] = style.color_a[k]; c.color_b[k] = style.color_b[k]; }
    c.color_a[3] = style.alpha;c.color_b[3] = style.emissive;
    c.motion[0] = style.flutter;c.motion[1] = style.flutter_rate;c.motion[2] = style.stretch;c.motion[3] = style.spin;
    c.window[3] = float(pool);
    c.misc[0] = style.twinkle;c.misc[1] = float(unsigned(e)*7919u);c.misc[2] = scale;c.misc[3] = float(unsigned(e));
    c.velocity[0] = float(velocity[0]);c.velocity[1] = float(velocity[1]);c.velocity[2] = style.vertical_speed;
    c.velocity[3] = emitter_kind(e, mushroom);
    for (unsigned k = 0; k < 4; ++k) { c.affinity_a[k] = table.area[k]; c.affinity_b[k] = table.area[k+4]; }
    c.grid[0] = table.tree_mul;c.grid[1] = table.tree_add;
}

// Waterfall sources per 2 m tile, from the flow of the shown liquid surface
// (as the liquid decode reads it) at each water tile's centre and corner
// vertices: [0] impact (flat receiving water below a nearby fall: splashes and
// mist), [3] fall (steep falling water: its foam clumps, and lip spray where
// the shader finds the flat water above). Channels 1 and 2 are left as they are.
inline void ambient_fall_sources(const TerrainCpuAsset& terrain, const OwnedTerrainGeometryValues* geometry,
    const std::vector<std::uint8_t>& liquid_area, std::uint32_t tiles_x, std::vector<std::array<std::uint8_t,4>>& sources) {
    auto liquid_flow = std::make_unique<OwnedLiquidFlowSector>();
    for (std::uint32_t sy = 0; sy < terrain.sector_count_y; ++sy)
    for (std::uint32_t sx = 0; sx < terrain.sector_count_x; ++sx) {
        bool prepared = false;
        for (std::uint32_t y = sy*16u; y < sy*16u+16u; ++y)
        for (std::uint32_t x = sx*16u; x < sx*16u+16u; ++x) {
            if ((liquid_area[std::size_t(y)*tiles_x+x]&15u) != area_water) continue;
            if (!prepared) {
                prepared = true;
                // A rejected sector leaves the field unready: its queries fail and add nothing.
                (void)liquid_flow->prepare(terrain,sy*terrain.sector_count_x+sx,geometry);
            }
            float impact = 0, lip = 0;
            for (unsigned corner = 0; corner < 5; ++corner) {
                const unsigned vx = (x%16u)*2u+(corner == 4 ? 1u : (corner&1u)*2u);
                const unsigned vy = (y%16u)*2u+(corner == 4 ? 1u : (corner>>1)*2u);
                OwnedLiquidFlow flow;
                if (!liquid_flow->flow(vy*33u+vx,flow)) continue;
                impact = std::max(impact,std::clamp(flow.impact,0.f,1.f));
                const float run = std::hypot(flow.current[0],flow.current[1]);
                const float steep = std::clamp((run-.55f)/(1.1f-.55f),0.f,1.f);
                lip = std::max(lip,steep);
            }
            auto& s = sources[std::size_t(y)*tiles_x+x];
            s[0] = static_cast<std::uint8_t>(std::lround(impact*255.f));
            s[3] = static_cast<std::uint8_t>(std::lround(lip*255.f));
        }
    }
}

// The waterfall tiles waterfall emitters fill: tiles with an impact source,
// then tiles with a fall source (packed x | y << 16). impact_count: how many
// of the list are impact tiles.
inline std::vector<std::uint32_t> ambient_fall_tiles(const std::vector<std::array<std::uint8_t,4>>& sources,
    std::uint32_t tiles_x, std::uint32_t& impact_count) {
    std::vector<std::uint32_t> tiles;
    for (const int channel : {0, 3}) {
        for (std::size_t i = 0; i < sources.size(); ++i)
            if (sources[i][channel] > 0)
                tiles.push_back(std::uint32_t(i % tiles_x) | std::uint32_t(i / tiles_x) << 16);
        if (channel == 0) impact_count = std::uint32_t(tiles.size());
    }
    return tiles;
}
// Splashes fill the impact tiles; fall foam, lip spray and mist fill the
// procedural falls (one slot per fall, ambient_fall_list).
inline void ambient_fall_range(Emitter e, std::uint32_t impact_count, std::uint32_t falls,
    std::uint32_t& first, std::uint32_t& count) noexcept {
    first = 0;count = e == em_spray ? impact_count : falls;
}
// The procedural falls as the particle shader reads them (ambFalls, t5):
// lip xy, flow direction xy; width, top, bottom, lip speed.
inline std::vector<std::array<float,4>> ambient_fall_list(const std::vector<ProceduralFall>& falls) {
    std::vector<std::array<float,4>> list;
    for (const auto& f : falls) {
        list.push_back({f.lip[0], f.lip[1], f.dir[0], f.dir[1]});
        list.push_back({f.width, f.top, f.bottom, kFallFlowSpeed});
    }
    if (list.empty()) list.assign(2, {}); // a valid buffer, never read
    return list;
}

} // namespace owned::ambient
