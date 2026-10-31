#pragma once

// Bounce light volume (#208, renderer_point_shadows.cpp): one irradiance
// volume per map holding the first bounce of the local lights, the sun's
// bounce off the ground and the sky's terrain occlusion outdoors, and the
// light of the cave exits in caves.
//
// Design (no architecture reference implements an indirect term: GD3D11 has
// tiled point lights, SSAO and a sky irradiance map but no GI; the private
// engine reference and OpenEI light caves with the game's fixed light only).
// Independent implementation of published techniques:
//  - an irradiance volume (Greger et al. 1998): a regular 3D grid of probes,
//    each holding the incoming light as first-order spherical harmonics
//    (L1: 4 coefficients per colour channel), interpolated trilinearly and
//    evaluated per pixel for the surface normal (Ramamoorthi and Hanrahan
//    2001, the clamped-cosine convolution of L1 as 0.25 + 0.5 cos);
//  - virtual point lights (as in reflective-shadow-map injection,
//    Dachsbacher and Stamminger 2005): per lamp a fixed set of points of
//    the ground it lights (cave_lamp_virtual_lights, on the CPU from the
//    height grid: a disk of its radius, each point visible to the lamp)
//    re-emit its light with their albedo (the painted ground colour, else a
//    rock albedo) toward every probe in front of them. The game's lamps
//    light terrain without inverse square, so the bounce is the lit area.
//    The point-shadow cubes cannot supply the lit surfaces: terrain casters
//    keep the game's cull mode and store only faces turned away from the
//    lamp (L-086; the 0e1fd887 run found no surface within reach in 5 of 8
//    baked zone5 cubes, every one a torch above open floor). Objects are
//    not in the height grid and do not bounce. No visibility between a
//    virtual light and a probe; the lamp radius bounds the reach;
// Emissive-surface lights (lava) add a direction-free bounce from their
// direct light at the probe, so lava caves glow on their walls and the lava
// glow option is never required.
// Every lamp within 2.5 of its radii of a probe contributes.
//
// Outdoors (approved 2026-10-26):
//  - Sun bounce: 8 (Low) or 16 (High) ground points on a disk around each
//    probe, read from the terrain height grid, each lit by the sun (n.l,
//    terrain self-shadow by a march toward the sun, and the sun cascades
//    where one holds the point), with the painted ground colour as albedo,
//    re-emitted to the probe when the terrain does not block the path.
//  - No sky term (user decision 2026-10-27: the bounce only ever adds
//    light; the terrain sky-occlusion grid that scaled the ambient darkened
//    foliage and is removed).
// Caves (GMS_CAVE): no sun and no sky. Each map.txt exit whose neighbour has
// normal sky (an edge zone counts as its other zone) is an area light: the
// outer side of its #deploy rectangle (toward the nearest map edge, widened
// 1 m each way, kCavePortalHeight high above the ground there) floods the
// outdoor colour of the hour (0.5 x sun + ambient of
// Config\lights<region>.ini, interpolated between hours) inward only:
// Lambertian, cos at the opening over distance squared, sampled 6 x 2,
// fading to zero over kCavePortalReach metres, each sample behind a
// heightfield visibility test (user correction 2026-10-26: a flood through
// the mouth, not a point). The same volume lights the cave fog
// (nativeCaveFogLight in the native pixel shader), so the haze at a mouth
// takes the outdoor light and the border fog past it is not a black wall.
//
// The volume spans the map's terrain bounds (plus headroom above the highest
// ground) with cubic cells sized by the quality: Low 4 m, 16 virtual lights
// per lamp; High 2 m, 32. Grid size follows the map (demand). After a full
// injection (a new map or quality), one slab of 1/16 (High) or 1/32 (Low) of
// the volume is re-injected per frame, round-robin, so the hour, moving
// lights and the sun cascades follow at a fixed per-frame cost.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace owned {

inline constexpr float kCaveBounceAlbedo = 0.30f;     // rock and earth without a painted colour
// Default F10 cave_gi_strength (the local lights' bounce). At 1 the c27
// lava-cave run raised the frame by about 1/255 (visible only amplified);
// game lamps reach only 4-7 m, so the physical one-bounce estimate stays
// inside their lit pool. The virtual lights cover the lit ground area and
// take the painted albedo (snow about 0.9), so 1 is the physical estimate;
// 2 blew the snow around Ingos torches out to white disks at dawn (user,
// 08638187 frames 046061/048981).
inline constexpr float kDefaultCaveBounceStrength = 1.0f;
inline constexpr float kDefaultSunBounceStrength = 1.0f;  // F10 sun_bounce_strength
// A source bounces up to this many of its radii (CAVE_BOUNCE_REACH in the
// injection shader): game lamps reach only 4-7 m.
inline constexpr float kCaveBounceReach = 2.5f;
inline constexpr float kCavePortalReach = 50.0f;     // metres a cave exit's light reaches inward
inline constexpr float kCavePortalHeight = 4.0f;     // art: the opening's height above the ground
inline constexpr float kMushroomGlow = 0.35f;        // art: a giant mushroom's glow (no game light)

struct CaveVolumeLayout {
    std::array<float, 3> origin{};   // world position of the grid's low corner
    float cell = 0.0f;               // metres per cell (cubic)
    std::array<std::uint32_t, 3> size{};
    bool valid() const noexcept { return cell > 0.0f && size[0] && size[1] && size[2]; }
};

// The grid over terrain bounds [low, high] for a quality (1 Low, 2 High).
inline CaveVolumeLayout cave_volume_layout(const std::array<float, 3>& low, const std::array<float, 3>& high,
                                           int quality) noexcept {
    CaveVolumeLayout layout{};
    if (quality <= 0) return layout;
    const float cell = quality >= 2 ? 2.0f : 4.0f;
    const float headroom = 4.0f;  // lamps and figures above the highest ground
    std::array<float, 3> top = high;
    top[2] += headroom;
    for (int k = 0; k < 3; ++k) {
        const float extent = top[k] - low[k];
        if (!std::isfinite(extent) || extent <= 0.0f) return {};
        layout.size[k] = static_cast<std::uint32_t>(std::ceil(extent / cell)) + 1u;
        layout.origin[k] = low[k] - 0.5f * cell;
    }
    layout.cell = cell;
    return layout;
}

// Slabs per full refresh (round-robin, one per frame), along y.
inline std::uint32_t cave_volume_slabs(int quality) noexcept { return quality >= 2 ? 16u : 32u; }

// Constants of the injection (cs, b0).
struct CaveInjectConstants {
    float origin[3]; float cell;
    std::uint32_t size[3]; std::uint32_t vpl_count;
    float grid_origin[2]; float grid_inverse_cell; float albedo;
    std::uint32_t grid_width, grid_height, pad0, pad1;
    std::uint32_t pad2, light_count, slab_first, mode;  // slab_first in cells; mode: kCaveMode*
    float sun_toward[3]; float point_strength;
    float sun_color[3]; float sun_strength;
    std::uint32_t height_width, height_height, sun_samples; float sun_radius;
};
static_assert(sizeof(CaveInjectConstants) == 128, "cave inject constant layout");
inline constexpr std::uint32_t kCaveModeOutdoor = 1, kCaveModeSunMaps = 2, kCaveModeGroundColor = 4,
                               kCaveModeHeights = 8;

// Constants of the sampling (the tail of the native pixel shader's point-light
// constants, b5): world to volume uvw, strength (0: no volume) and flags
// (bit 0 the F10 bounce_debug view, bit 1 outdoors, bits 8+ the exit count).
struct CaveSampleConstants {
    float origin[3]; float strength;
    float inverse_extent[3]; float flags;
};
static_assert(sizeof(CaveSampleConstants) == 32, "cave sample constant layout");

// ---- Cave exits (map.txt) and the outdoor light table ----------------------

struct CavePortalRect { float x1 = 0, y1 = 0, x2 = 0, y2 = 0; };

// The exits of the zone whose #res terrain is `mpr` (lower case, no
// extension) that lead to a normal-sky zone, from the game's map.txt (the
// zone list; its header documents #zone, #res, #sky, #exit and #deploy). A
// #deploy belongs to the latest #exit; an edge zone leads to its other zone.
inline std::vector<CavePortalRect> parse_cave_portals(const std::string& text, const std::string& mpr) {
    struct Exit { std::string neighbour; bool deploy = false; CavePortalRect rect; };
    struct Zone { std::string mpr, type; bool cave = false; std::vector<Exit> exits; };
    std::map<std::string, Zone> zones;
    std::vector<std::string> order;
    const auto lower = [](std::string s) { for (auto& c : s) if (c >= 'A' && c <= 'Z') c = char(c + 32); return s; };
    std::vector<std::vector<std::string>> lines;
    for (std::size_t at = 0; at < text.size();) {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        const std::string line = text.substr(at, end - at);
        at = end + 1;
        const std::size_t first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos || line.compare(first, 2, "##") == 0) continue;
        std::vector<std::string> tokens;
        for (std::size_t p = first; p < line.size();) {
            const std::size_t s = line.find_first_not_of(" \t\r", p);
            if (s == std::string::npos) break;
            std::size_t e = line.find_first_of(" \t\r", s);
            if (e == std::string::npos) e = line.size();
            tokens.push_back(line.substr(s, e - s));
            p = e;
        }
        if (!tokens.empty()) lines.push_back(std::move(tokens));
    }
    Zone* zone = nullptr;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto& t = lines[i];
        const std::string key = lower(t[0]);
        const auto* next = i + 1 < lines.size() && lines[i + 1][0][0] != '#' ? &lines[i + 1] : nullptr;
        if (key == "#zone" && t.size() >= 2) {
            const std::string name = lower(t[1]);
            if (!zones.count(name)) order.push_back(name);
            zone = &zones[name];
            *zone = {};
            zone->type = t.size() >= 4 ? lower(t[3]) : std::string();
        } else if (!zone) {
            continue;
        } else if (key == "#res" && next) {
            zone->mpr = lower((*next)[0]);
        } else if (key == "#sky" && next) {
            zone->cave = lower((*next)[0]) == "cave";
        } else if (key == "#exit" && next) {
            Exit exit;
            exit.neighbour = lower((*next)[0]);
            zone->exits.push_back(exit);
        } else if (key == "#deploy" && next && next->size() >= 4 && !zone->exits.empty()) {
            float v[4];
            bool ok = true;
            for (int k = 0; k < 4; ++k) {
                char* end = nullptr;
                v[k] = std::strtof((*next)[k].c_str(), &end);
                ok = ok && end && *end == 0 && std::isfinite(v[k]);
            }
            if (ok) {
                auto& exit = zone->exits.back();
                exit.deploy = true;
                exit.rect = {std::min(v[0], v[2]), std::min(v[1], v[3]), std::max(v[0], v[2]), std::max(v[1], v[3])};
            }
        }
    }
    std::vector<CavePortalRect> portals;
    std::string own;
    for (const auto& name : order) {
        const auto& z = zones[name];
        if (z.mpr == mpr && z.type == "game") { own = name; break; }
    }
    if (own.empty()) return portals;
    for (const auto& exit : zones[own].exits) {
        if (!exit.deploy) continue;
        auto target = zones.find(exit.neighbour);
        if (target == zones.end()) continue;
        if (target->second.type == "edge") {
            std::string other;
            for (const auto& e : target->second.exits) if (e.neighbour != own) { other = e.neighbour; break; }
            target = zones.find(other);
            if (target == zones.end()) continue;
        }
        if (target->second.cave || target->first == own) continue;
        portals.push_back(exit.rect);
    }
    return portals;
}

// [sunlight] and [ambient] "timeHH = r, g, b" (0-255) of a lights<region>.ini.
struct OutdoorLightTable {
    std::array<std::array<float, 3>, 24> sun{}, ambient{};
    bool valid = false;
};
inline OutdoorLightTable parse_outdoor_light_table(const std::string& text) {
    OutdoorLightTable table;
    std::array<std::array<float, 3>, 24>* section = nullptr;
    unsigned sun_rows = 0, ambient_rows = 0;
    for (std::size_t at = 0; at < text.size();) {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(at, end - at);
        at = end + 1;
        for (auto& c : line) if (c >= 'A' && c <= 'Z') c = char(c + 32);
        const std::size_t first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos) continue;
        if (line[first] == '[') {
            section = line.compare(first, 10, "[sunlight]") == 0 ? &table.sun
                    : line.compare(first, 9, "[ambient]") == 0 ? &table.ambient : nullptr;
            continue;
        }
        if (!section || line.compare(first, 4, "time") != 0) continue;
        const int hour = std::atoi(line.c_str() + first + 4);
        const std::size_t equals = line.find('=', first);
        if (hour < 0 || hour > 23 || equals == std::string::npos) continue;
        float v[3];
        const char* p = line.c_str() + equals + 1;
        bool ok = true;
        for (int k = 0; k < 3 && ok; ++k) {
            char* e = nullptr;
            v[k] = std::strtof(p, &e);
            ok = e != p && std::isfinite(v[k]);
            p = e;
            while (ok && (*p == ',' || *p == ' ' || *p == '\t')) ++p;
        }
        if (!ok) continue;
        for (int k = 0; k < 3; ++k) (*section)[hour][k] = v[k] / 255.0f;
        if (section == &table.sun) ++sun_rows; else ++ambient_rows;
    }
    table.valid = sun_rows >= 24 && ambient_rows >= 24;
    return table;
}

// The portal colour at a game hour (0-24): 0.5 x sun + ambient, interpolated
// between the table's whole hours.
inline std::array<float, 3> outdoor_portal_color(const OutdoorLightTable& table, float hour) noexcept {
    std::array<float, 3> colour{};
    if (!table.valid || !std::isfinite(hour)) return colour;
    hour = hour - 24.0f * std::floor(hour / 24.0f);
    const int a = static_cast<int>(hour) % 24, b = (a + 1) % 24;
    const float t = hour - std::floor(hour);
    for (int k = 0; k < 3; ++k) {
        const float sun = table.sun[a][k] + (table.sun[b][k] - table.sun[a][k]) * t;
        const float ambient = table.ambient[a][k] + (table.ambient[b][k] - table.ambient[a][k]) * t;
        colour[k] = 0.5f * sun + ambient;
    }
    return colour;
}

// A lamp's virtual lights (CPU, rebuilt when the sources or the height grid
// change): `count` points of the ground it lights, on a disk of its radius
// (golden-angle spiral) on the terrain height grid. The game lights terrain
// with colour x (1 - d^2/r^2)^2 x wrap facing and no inverse square, so a
// lamp's bounced light is the ground area it lights; a steep wall counts its
// true area, up to 3x its plan. A point counts when the terrain does not cut
// the way to the lamp. Flux is before the albedo (read on the GPU at the
// point). Returns the points found. (The point-shadow cubes cannot supply
// the lit surfaces: terrain casters keep the game's cull mode and store only
// faces turned away from the lamp, L-086.)
struct CaveVirtualLight { float position[4]; float normal[4]; float flux[4]; };
static_assert(sizeof(CaveVirtualLight) == 48, "cave virtual light layout");
inline unsigned cave_lamp_virtual_lights(const float centre_in[3], float radius, const float colour[3], float strength,
                                         const float* heights, std::uint32_t width, std::uint32_t height, unsigned count,
                                         CaveVirtualLight* out) noexcept {
    const auto at = [&](float x, float y) {
        x = (std::min)((std::max)(x, 0.0f), float(width) - 1.001f);
        y = (std::min)((std::max)(y, 0.0f), float(height) - 1.001f);
        const auto ix = std::uint32_t(x), iy = std::uint32_t(y);
        const float fx = x - float(ix), fy = y - float(iy);
        const float* row = heights + std::size_t(iy) * width + ix;
        const float a = row[0], b = row[1], c = row[width], d = row[width + 1];
        return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
    };
    float centre[3] = {centre_in[0], centre_in[1], (std::max)(centre_in[2], at(centre_in[0], centre_in[1]) + 0.2f)};
    unsigned found = 0;
    for (unsigned k = 0; k < count; ++k) {
        CaveVirtualLight& v = out[k];
        v = {};
        const float plan = radius * std::sqrt((k + 0.5f) / count), angle = k * 2.39996323f;
        const float qx = centre[0] + plan * std::cos(angle), qy = centre[1] + plan * std::sin(angle);
        const float surface[3] = {qx, qy, at(qx, qy)};
        float n[3] = {at(qx - 1, qy) - at(qx + 1, qy), at(qx, qy - 1) - at(qx, qy + 1), 2.0f};
        const float nl = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        for (float& value : n) value /= nl;
        const float to[3] = {centre[0] - surface[0], centre[1] - surface[1], centre[2] - surface[2]};
        const float squared = to[0] * to[0] + to[1] * to[1] + to[2] * to[2];
        const float reach = 1.0f - squared / (radius * radius);
        if (!(reach > 0.0f)) continue;
        const float incidence = (n[0] * to[0] + n[1] * to[1] + n[2] * to[2]) / std::sqrt((std::max)(squared, 1e-6f));
        const float facing = (std::min)((std::max)(1.0f + (std::min)(incidence, 0.0f), 0.0f), 1.0f);  // the game's terrain wrap
        if (!(facing > 0.0f)) continue;
        bool clear = true;
        const float from[3] = {surface[0] + n[0] * 0.3f, surface[1] + n[1] * 0.3f, surface[2] + n[2] * 0.3f};
        for (int s = 1; s < 6 && clear; ++s) {
            const float t = s / 6.0f;
            const float m[3] = {from[0] + (centre[0] - from[0]) * t, from[1] + (centre[1] - from[1]) * t,
                                from[2] + (centre[2] - from[2]) * t};
            clear = at(m[0], m[1]) <= m[2] + 0.25f;
        }
        if (!clear) continue;
        const float area = 3.14159265f * radius * radius / count / (std::max)(n[2], 0.33f);
        const float scale = strength * reach * reach * facing * area;
        for (int c = 0; c < 3; ++c) {
            v.position[c] = surface[c] + n[c] * 0.05f;
            v.normal[c] = n[c];
            v.flux[c] = (std::max)(colour[c], 0.0f) * scale;
        }
        v.position[3] = 1.0f;
        ++found;
    }
    return found;
}

constexpr char kCaveInjectShader[] = R"(
cbuffer CaveInject:register(b0){
 float3 caveOrigin;float caveCell;uint3 caveSize;uint caveVplCount;
 float2 caveGridOrigin;float caveGridInverseCell;float caveAlbedo;
 uint caveGridWidth;uint caveGridHeight;uint2 cavePad0;
 uint cavePad1;uint caveLightCount;uint caveSlabFirst;uint caveMode;
 float3 caveSunToward;float cavePointStrength;
 float3 caveSunColor;float caveSunStrength;
 uint caveHeightWidth;uint caveHeightHeight;uint caveSunSamples;float caveSunRadius;};
// The native sun shadow constants (native_shader.h b2), bound as they are.
cbuffer CaveSunShadow:register(b1){row_major float4x4 caveShadowLight[6];row_major float4x4 caveShadowCamera[6];float4 caveShadowBias[6];uint4 caveShadowControl;};
struct CaveLight{float4 centerInverseRadiusSquared;float4 color;float4 shadowCenter;float4 shadowSlots;};
StructuredBuffer<CaveLight> caveLights:register(t0);
StructuredBuffer<uint> caveGrid:register(t1);
Texture2D<float> caveHeights:register(t3);       // terrain/liquid surface height, 1 m vertices
Texture2D<float4> caveGroundColor:register(t4);  // painted ground colour (mip 1: 1 texel per metre)
Texture2DArray<float> caveSunDepth:register(t5); // sun cascades
RWTexture3D<float4> caveOutR:register(u0);
RWTexture3D<float4> caveOutG:register(u1);
RWTexture3D<float4> caveOutB:register(u2);
// Virtual point lights (cave_lamp_virtual_lights on the CPU): per lamp and
// sample the lit ground point (w 1, 0 none), its normal and its re-emitted
// flux before the albedo, which is read here at the point.
struct CaveVpl{float4 position;float4 normal;float4 flux;};
StructuredBuffer<CaveVpl> caveVpls:register(t6);
static const float caveY0=.282095,caveY1=.488603,cavePi=3.14159265;
#define CAVE_BOUNCE_REACH 2.5
void caveAdd(inout float4 r,inout float4 g,inout float4 b,float3 light,float3 from){
 float4 basis=float4(caveY0,caveY1*from.y,caveY1*from.z,caveY1*from.x);
 r+=light.r*basis;g+=light.g*basis;b+=light.b*basis;
}
// The active map's height grid (map-local metres are world metres).
float caveHeight(float2 p){
 p=clamp(p,0,float2(caveHeightWidth,caveHeightHeight)-1.001);int2 i=int2(floor(p));float2 f=p-i;
 float a=caveHeights.Load(int3(i,0)),b=caveHeights.Load(int3(i+int2(1,0),0));
 float c=caveHeights.Load(int3(i+int2(0,1),0)),d=caveHeights.Load(int3(i+int2(1,1),0));
 return lerp(lerp(a,b,f.x),lerp(c,d,f.x),f.y);
}
float3 caveGroundNormal(float2 p){
 return normalize(float3(caveHeight(p-float2(1,0))-caveHeight(p+float2(1,0)),
                         caveHeight(p-float2(0,1))-caveHeight(p+float2(0,1)),2));
}
float3 caveGroundAlbedo(float2 q){
 float3 albedo=caveAlbedo;
 [branch]if((caveMode&4u)!=0){
  uint w,h,levels;caveGroundColor.GetDimensions(1,w,h,levels);
  float4 v=caveGroundColor.Load(int3(clamp(int2(floor(q)),0,int2(w,h)-1),1));
  if(v.a>=.99)albedo=v.rgb;
 }
 return albedo;
}
// The albedo a bounce takes: the painted ground colour, capped at 0.7 (art:
// fresh snow at about 0.9 made every bounce on snowfields glare).
float3 caveBounceAlbedo(float2 q){return min(caveGroundAlbedo(q),.7);}
// Sun visibility of a ground point: the terrain toward the sun, then the
// finest sun cascade that holds the point (first match, no continue: see
// nativeShadowSlice).
float caveSunVisibility(float3 p){
 [loop]for(uint s=0;s<6;++s){
  float3 m=p+caveSunToward*(1.5*exp2((float)s));
  if(caveHeight(m.xy)>m.z)return 0;
 }
 float visibility=1;
 [branch]if((caveMode&2u)!=0){
  bool found=false;float side=max(1.0,(float)caveShadowControl.z);
  [unroll]for(uint slice=0;slice<5;++slice){
   [branch]if(!found&&(caveShadowControl.x&(1u<<slice))!=0){
    float3 l=mul((float3x4)caveShadowLight[slice],float4(p,1));
    [branch]if(abs(l.x)<=.98&&abs(l.y)<=.98&&l.z>=0&&l.z<=1){
     found=true;
     int2 t=clamp(int2(float2(l.x*.5+.5,.5-l.y*.5)*side),0,int(side)-1);
     visibility=l.z<=caveSunDepth.Load(int4(t,slice,0))?1:0;
    }
   }
  }
 }
 return visibility;
}
// The terrain does not cut the segment between two points.
bool caveClear(float3 a,float3 b,uint steps,float tolerance){
 [loop]for(uint s=1;s<steps;++s){
  float3 m=lerp(a,b,s/(float)steps);
  if(caveHeight(m.xy)>m.z+tolerance)return false;
 }
 return true;
}
[numthreads(4,4,4)]
void caveInjectCS(uint3 id:SV_DispatchThreadID){
 id.y+=caveSlabFirst;
 if(any(id>=caveSize))return;
 float3 probe=caveOrigin+(float3(id)+.5)*caveCell;
 float4 r=0,g=0,b=0;
 // Sky, sun and portal terms read the light at the probe lifted out of
 // the ground: a probe inside the terrain would otherwise darken or light
 // the surface pixels that interpolate it.
 bool heights=(caveMode&8u)!=0;
 float3 at=probe;
 if(heights)at.z=max(at.z,caveHeight(at.xy)+.75);
 float rotation=frac(dot(float3(id),float3(.7548776662,.56984029,.41421356)))*6.2831853;
 // Sun bounce: lit ground points on a disk around the probe re-emit the sun.
 [branch]if((caveMode&1u)!=0&&heights&&caveSunToward.z>.02&&caveSunSamples>0){
  float3 sunLight=caveSunColor*caveSunStrength;
  float area=cavePi*caveSunRadius*caveSunRadius/caveSunSamples;
  float minimum=.25*caveCell*caveCell;
  [loop]for(uint k=0;k<caveSunSamples;++k){
   float radius=caveSunRadius*sqrt((k+.5)/caveSunSamples),angle=k*2.39996323+rotation;
   float2 q=at.xy+radius*float2(cos(angle),sin(angle));
   float3 s=float3(q,caveHeight(q));float3 n=caveGroundNormal(q);
   float lit=dot(n,caveSunToward);
   if(lit<=0)continue;
   float3 toProbe=at-s;float squared=max(dot(toProbe,toProbe),minimum);
   float3 outward=toProbe*rsqrt(squared);
   float emit=dot(n,outward);
   if(emit<=0||!caveClear(s+n*.3,at,4,.25))continue;
   float visibility=caveSunVisibility(s+n*.3+caveSunToward*.5);
   if(visibility<=0)continue;
   float3 bounce=sunLight*caveBounceAlbedo(q)*(lit*visibility*emit*area/(cavePi*squared));
   caveAdd(r,g,b,bounce,-outward);
  }
 }
 float2 cell=floor((probe.xy-caveGridOrigin)*caveGridInverseCell);
 [branch]if(caveLightCount>0&&all(cell>=0)&&all(cell<float2(caveGridWidth,caveGridHeight))){
  float minimum=caveCell*caveCell;
  // The bounce grid lists every source whose bounce reach (CAVE_BOUNCE_REACH
  // radii) touches this probe's cell.
  uint index=(uint)cell.y*caveGridWidth+(uint)cell.x;
  uint first=caveGrid[index],last=caveGrid[index+1];
  [loop]for(uint entry=first;entry<last;++entry){
   uint n=caveGrid[entry];
   CaveLight light=caveLights[n];
   float3 centre=light.centerInverseRadiusSquared.xyz;float inverseSquared=light.centerInverseRadiusSquared.w;
   float3 offset=probe-centre;float lampDistance=dot(offset,offset)*inverseSquared; // in radii, squared
   if(lampDistance>=CAVE_BOUNCE_REACH*CAVE_BOUNCE_REACH)continue;
   float3 colour=max(light.color.rgb,0);
   // A cave exit (colour.w -2): the opening as an area light flooding
   // inward. Its emitter is the opening's outer side, a strip along the map
   // edge (centre, half-width vector shadowCenter.xy, height shadowCenter.z,
   // inward normal shadowSlots.xy), sampled 6 across x 2 up from the ground
   // there. Each sample is a soft patch: only probes in front of the
   // opening get light, by (0.4 + 0.6 cos) at the opening times
   // patch / (8 m x (d + 8 m)) (art: about 0.75 x the outdoor colour at the
   // mouth, a gentle 1/d tail instead of the inverse square's fast near-field
   // drop; d3704000 user: the mouth was white glare), fading linearly to zero
   // at the reach (50 m), where the terrain does not block the way to that
   // sample (2 m tolerance: the mouth's rim does not cut the flood).
   [branch]if(light.color.w<-1.5){
    float2 across=light.shadowCenter.xy;float height=light.shadowCenter.z;
    float2 inward=light.shadowSlots.xy;
    float patch=max(2*length(across)*height/12,.5);
    float fade=1-sqrt(lampDistance)/CAVE_BOUNCE_REACH;
    [loop]for(uint i=0;i<6;++i){
     float2 xy=centre.xy+across*((i+.5)/3-1);
     float ground=heights?caveHeight(xy):centre.z;
     [unroll]for(uint j=0;j<2;++j){
      float3 emitter=float3(xy,ground+height*(j+.5)*.5);
      float3 toProbe=at-emitter;float along=dot(toProbe.xy,inward);
      if(along<=0)continue;
      float squared=dot(toProbe,toProbe);float distance=sqrt(max(squared,1e-4));
      if(heights&&!caveClear(emitter,at,6,2))continue;
      caveAdd(r,g,b,colour*(fade*(.4+.6*along/distance)*patch/(8*(distance+8))),-toProbe/distance);
     }
    }
    continue;
   }
   // A lamp: its virtual lights, each a lit surface patch re-emitting
   // toward the probe (Lambertian, n.l at the patch). Inside the lamp's own
   // pool the bounce keeps only 1 - its direct falloff: the pool's virtual
   // lights lie in the ground the probes there interpolate onto, and L1 lets
   // a flat floor take light from its own neighbouring patches, so the pool
   // the lamp already lights got lit again (white disks on snow, user,
   // eb203bd6 savea torches). The bounce shows on walls and undersides and
   // beyond the radius, where the direct light does not reach.
   [branch]if(light.color.w>=0){
    float direct=saturate(1-lampDistance);float keep=1-direct*direct;
    if(keep<=0)continue;
    uint base=n*caveVplCount;
    [loop]for(uint k=0;k<caveVplCount;++k){
     CaveVpl v=caveVpls[base+k];
     if(v.position.w<=0)continue;
     float3 toProbe=probe-v.position.xyz;float squared=max(dot(toProbe,toProbe),minimum);
     float3 outward=toProbe*rsqrt(squared);
     float emit=dot(v.normal.xyz,outward);
     if(emit<=0)continue;
     caveAdd(r,g,b,keep*caveBounceAlbedo(v.position.xy)*v.flux.rgb*(emit/(cavePi*squared)),-outward);
    }
    continue;
   }
   // An emissive surface (lava; colour.w -1): its light returned without
   // direction, falling off over the bounce reach.
   float spread=1-lampDistance/(CAVE_BOUNCE_REACH*CAVE_BOUNCE_REACH);
   float3 uniformBounce=caveAlbedo*colour*cavePointStrength*(spread*spread)*.5;
   r.x+=uniformBounce.r*caveY0*4;g.x+=uniformBounce.g*caveY0*4;b.x+=uniformBounce.b*caveY0*4;
  }
 }
 caveOutR[id]=r;caveOutG[id]=g;caveOutB[id]=b;
}
)";

// Cave mouths seen from inside (renderer_point_shadows.cpp
// draw_cave_exit_glow): a cave has no sky, so past an exit at the map edge
// the view meets the cleared far plane, black. This pass draws behind
// everything (only pixels still at the far plane, like the owned sky) the
// haze of daylight outside each exit: along the pixel's view ray, the
// exit's outdoor colour of the hour at the ray's closest approach to a
// point 6 m outside the opening, a Gaussian of the opening's half-width
// plus 8 m, so the glow is a soft halo around the mouth and every other
// map edge stays dark; it fades over 120 m of eye distance.
// One cave exit for the glow (CPU: renderer_point_shadows.cpp): the emitter
// centre on the ground, its half-width vector, inward normal and height,
// and the outdoor colour of the hour.
struct CaveExitGpu {
    float centre[4]; float across[4]; float inward_height[4]; float colour[4];
};
static_assert(sizeof(CaveExitGpu) == 64, "cave exit layout");
struct CaveGlowConstants {
    std::uint32_t count; float scale; float pad[2];
};
static_assert(sizeof(CaveGlowConstants) == 16, "cave glow constant layout");
constexpr char kCaveExitGlowHlsl[] = R"(
struct CaveExit{float4 centre;float4 across;float4 inwardHeight;float4 colour;};
// The daylight haze of the cave exits seen along the view ray from the eye
// toward p (to the far plane for the glow pass): per exit its outdoor colour
// of the hour at the ray's closest approach to a point 6 m outside the
// opening (at mid-opening height), a Gaussian of the opening's half-width
// + 8 m, fading over 120 m of eye distance. One formula for the glow pass
// and the border fog on surfaces (kCaveGlowShader, native_shader.h), so the
// ground strip at a mouth blends into the glowing opening.
float3 caveExitGlow(StructuredBuffer<CaveExit> exits,uint count,float3 eye,float3 d){
 float3 glow=0;
 [loop]for(uint n=0;n<count;++n){
  CaveExit e=exits[n];
  float2 inward=e.inwardHeight.xy;float halfWidth=length(e.across.xy);
  float3 outside=e.centre.xyz+float3(-inward*6,.5*e.inwardHeight.z);
  float t=max(dot(outside-eye,d),0);float3 q=eye+d*t-outside;
  float width=halfWidth+8;
  // .6: the haze at the mouth stays below the lit snow there (no glare).
  glow+=.6*max(e.colour.rgb,0)*exp(-dot(q,q)/(width*width))*saturate(1-length(outside-eye)/120);
 }
 return glow;
}
)";
constexpr char kCaveGlowShader[] = R"(
cbuffer FogFrame:register(b10){row_major float4x4 fogClipToWorld;row_major float4x4 fogPreviousWorldToClip;float4 fogEye;};
cbuffer CaveGlow:register(b0){uint glowCount;float glowScale;float2 glowPad;};
StructuredBuffer<CaveExit> glowExits:register(t0);
struct GlowVertex{float4 p:SV_Position;float2 uv:TEXCOORD0;};
GlowVertex caveGlowVS(uint id:SV_VertexID){
 GlowVertex o;float2 t=float2((id<<1)&2,id&2);o.uv=t;o.p=float4(t*float2(2,-2)+float2(-1,1),1,1);return o;
}
float4 caveGlowPS(GlowVertex i):SV_Target{
 float4 far=mul(fogClipToWorld,float4(i.uv.x*2-1,1-i.uv.y*2,1,1));
 float3 eye=fogEye.xyz,d=normalize(far.xyz/far.w-eye);
 return float4(caveExitGlow(glowExits,glowCount,eye,d)*glowScale,0);
}
)";

}  // namespace owned
