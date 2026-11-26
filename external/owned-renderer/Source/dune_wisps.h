#pragma once
// Dune wisps (#256): thin sand plumes blowing off the crests of Suslanger
// dunes in the wind direction. Pure crest, schedule and shape rules (no D3D);
// the pass is render_dune_wisps (renderer_wind_trails.cpp).
//
// Crests come from the map's own height grid (the shared 1 m ambient grid):
// a sand vertex that is the highest point along the map's wind within 1.5 m,
// with ground rising toward it upwind and a lee face dropping away downwind.
// No hand-placed data. They are found once per map.
//
// Every wisp is a fixed slot with a repeating game-time cycle (the wind
// trails' schedule): pause freezes the plumes, game speed scales them. The
// renderer latches a crest point at the spawn; the plume grows downwind from
// it while the crest emits, then its tail leaves the crest and it thins away.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include "ambient_particles.h"
#include "weather_wind.h"
#include "world_fog.h"

namespace owned::dune_wisps {

inline constexpr unsigned kMaxSlots = 48;
inline constexpr unsigned kSegments = 24;      // ribbon subdivisions along the plume
inline constexpr unsigned kWispVectors = 5;    // float4 constants per wisp
inline constexpr unsigned kStrands = 5;        // thin strands drawn per wisp (one instance each)

struct Quality { unsigned slots = 0; float count_scale = 0.0f; };
// Levels of the "dune_wisps" switch: 0 off, 1 normal, 2 dense.
inline Quality quality_level(int level) noexcept {
    if (level <= 0) return {};
    if (level == 1) return {24u, 1.0f};
    return {kMaxSlots, 2.0f};
}

// One crest vertex: world position (metres) and how strongly its lee drops (0..1).
struct CrestPoint { float x = 0, y = 0, z = 0, strength = 0; };

// Crest rule tuning (metres).
inline constexpr float kNearReach = 1.5f;   // the crest is the highest point within this along the wind
inline constexpr float kFarReach = 4.0f;    // windward rise and lee drop are measured this far out
inline constexpr float kWindRise = .1f;     // the windward side must rise at least this much to the crest
inline constexpr float kLeeDrop = .5f;      // the lee must drop at least this much within kFarReach

// Crest points of a map: heights at (verts_x x verts_y) 1 m vertices, the
// area class per 2 m tile (low four bits), the map's downwind direction.
// lee_drop: the smallest drop over kFarReach downwind (kLeeDrop).
inline std::vector<CrestPoint> find_crests(const float* height, std::uint32_t verts_x, std::uint32_t verts_y,
                                           const std::uint8_t* area, std::uint32_t tiles_x, std::uint32_t tiles_y,
                                           const float direction[2], float lee_drop) {
    std::vector<CrestPoint> out;
    if (!height || !area || verts_x < 3 || verts_y < 3) return out;
    const auto at = [&](double x, double y) {
        x = std::clamp(x, 0.0, double(verts_x - 1)); y = std::clamp(y, 0.0, double(verts_y - 1));
        const auto ix = std::min<std::uint32_t>(std::uint32_t(x), verts_x - 2), iy = std::min<std::uint32_t>(std::uint32_t(y), verts_y - 2);
        const double fx = x - ix, fy = y - iy;
        const auto h = [&](std::uint32_t a, std::uint32_t b) { return double(height[std::size_t(b) * verts_x + a]); };
        return (h(ix, iy) * (1 - fx) + h(ix + 1, iy) * fx) * (1 - fy) + (h(ix, iy + 1) * (1 - fx) + h(ix + 1, iy + 1) * fx) * fy;
    };
    const double dx = direction[0], dy = direction[1];
    const auto sand = [&](double x, double y) {
        const auto tx = std::int64_t(std::floor(x * .5)), ty = std::int64_t(std::floor(y * .5));
        if (tx < 0 || ty < 0 || tx >= std::int64_t(tiles_x) || ty >= std::int64_t(tiles_y)) return false;
        return (area[std::size_t(ty) * tiles_x + std::size_t(tx)] & 15u) == ambient::area_sand;
    };
    for (std::uint32_t y = 2; y + 2 < verts_y; ++y)
        for (std::uint32_t x = 2; x + 2 < verts_x; ++x) {
            const double h0 = height[std::size_t(y) * verts_x + x];
            // Sand at the crest and on the lee face the plume blows over.
            if (!sand(x, y) || !sand(x + dx * kFarReach, y + dy * kFarReach)) continue;
            if (at(x - dx * kNearReach, y - dy * kNearReach) > h0 || at(x + dx * kNearReach, y + dy * kNearReach) > h0) continue;
            if (at(x - dx * .7, y - dy * .7) > h0 || at(x + dx * .7, y + dy * .7) > h0) continue;
            const double drop = h0 - at(x + dx * kFarReach, y + dy * kFarReach);
            const double rise = h0 - at(x - dx * kFarReach, y - dy * kFarReach);
            if (drop < lee_drop || rise < kWindRise) continue;
            CrestPoint p;
            p.x = float(x); p.y = float(y); p.z = float(h0);
            p.strength = float(std::clamp((drop - lee_drop) / (2.0 * lee_drop + .5) + .35, .35, 1.0));
            out.push_back(p);
        }
    return out;
}

// Wisps the wind wants at a quality level: a few on calm spells, more as the
// wind strengthens and gusts, fewer in rain (wet sand stays put).
inline float target_count(const WeatherWindGpu& w, const Quality& q) noexcept {
    if (q.slots == 0) return 0.0f;
    const float e = w.force * w.speed * w.pulse;
    const float intensity = std::clamp((e - .1f) / .9f, 0.0f, 1.0f);
    return (5.0f + 9.0f * intensity) * (1.0f - .85f * std::clamp(w.rain, 0.0f, 1.0f)) * q.count_scale;
}

// Slot schedule (game seconds).
inline constexpr double kCycleSeconds = 12.0;
inline constexpr float kEmitMin = 3.5f, kEmitMax = 6.5f;
inline constexpr float kVisibleShare = .7f;  // mean life over the cycle (emit + plume crossing time)

inline float slot_random(std::uint32_t seed, unsigned slot, std::int64_t cycle, std::uint32_t salt) noexcept {
    return weather_wind_hash(seed ^ salt, cycle * 64 + static_cast<std::int64_t>(slot));
}

struct Cycle { std::int64_t index = 0; double spawn = 0.0; };
inline Cycle slot_cycle(std::uint32_t seed, unsigned slot, double seconds) noexcept {
    const double offset = weather_wind_hash(seed ^ 0x51ed270bu, slot) * kCycleSeconds;
    const double k = std::floor((seconds + offset) / kCycleSeconds);
    Cycle c;
    c.index = static_cast<std::int64_t>(k);
    c.spawn = k * kCycleSeconds - offset + slot_random(seed, slot, c.index, 0x7f4a7c15u) * kCycleSeconds * .3;
    return c;
}
inline bool slot_active(std::uint32_t seed, unsigned slot, std::int64_t cycle, float target, const Quality& q) noexcept {
    if (q.slots == 0 || slot >= q.slots || !(target > 0.0f)) return false;
    const float chance = target / (static_cast<float>(q.slots) * kVisibleShare);
    return slot_random(seed, slot, cycle, 0x94d049bbu) < chance;
}

// Shape of one plume, world metres and game seconds.
struct Shape {
    float speed = 0.0f;   // plume travel downwind
    float emit = 0.0f;    // seconds the crest feeds it
    float length = 0.0f;  // where it has thinned away
    float width = 0.0f;   // crest-line width at the crest
    float spread = 0.0f;  // width gained per metre downwind
    float life = 0.0f;    // emit + the tail's crossing
    float amp = 0.0f, rate = 0.0f, phase = 0.0f;  // lateral sway
    float rise = 0.0f;    // climb per metre downwind
    float alpha = 0.0f;
};
inline Shape make_shape(std::uint32_t seed, unsigned slot, std::int64_t cycle, const WeatherWindGpu& w, float strength) noexcept {
    const auto r = [&](std::uint32_t salt) { return slot_random(seed, slot, cycle, salt); };
    Shape s;
    s.speed = (.65f + .8f * w.force) * std::max(w.speed * w.pulse, .6f) * (.85f + .3f * r(0x11u));
    s.emit = kEmitMin + (kEmitMax - kEmitMin) * r(0x12u);
    s.length = (3.5f + 5.0f * r(0x13u)) * (.7f + .3f * strength) * std::clamp(.75f + .35f * w.speed, .9f, 1.4f);
    s.width = 1.2f + 2.6f * r(0x14u);
    s.spread = .03f + .04f * r(0x15u);  // strands stay within ~4 degrees of the wind
    s.life = s.emit + s.length / s.speed;
    s.amp = .08f + .17f * r(0x16u);
    s.rate = .22f + .18f * r(0x17u);
    s.phase = 6.2831853f * r(0x18u);
    s.rise = .06f + .12f * r(0x19u);
    s.alpha = (.55f + .45f * strength) * (.7f + .3f * r(0x1au));
    return s;
}

struct Slot {
    std::int64_t cycle = INT64_MIN;
    bool live = false;
    double spawn = 0.0;
    float origin[3]{};
    float direction[2]{};
    Shape shape{};
};

// The plume ribbon. T0 origin.xyz, age; T1 wind.xy, speed, emit; T2 length,
// width, spread, life; T3 amp, rate, phase, rise; T4 alpha, seed.
inline constexpr char kDuneWispShader[] = R"(
cbuffer DuneWisps:register(b0){
 row_major float4x4 dwClip;float4 dwEye;float4 dwStyle;float4 dwColor;
 float4 dwFogRange;float4 dwFogControl;float4 dwFogColor;float4 dwDepth;
 float4 dwFogSeam;float4 dwFogCurve;float4 dwFogSpanLo;float4 dwFogSpanHi;
 float4 dwWisp[240];
};
Texture2D<float> dwHeight:register(t0);
Texture2D<float> dwSceneDepth:register(t2);
struct DwOut{float4 position:SV_Position;float4 color:COLOR0;float4 uv:TEXCOORD0;float viewW:TEXCOORD1;float border:TEXCOORD2;};
float dwGround(float2 p){
 uint w,h;dwHeight.GetDimensions(w,h);
 float2 c=clamp(p,0,float2(w-1,h-1));int2 i=int2(floor(c));int2 j=min(i+1,int2(w-1,h-1));float2 f=c-i;
 return lerp(lerp(dwHeight.Load(int3(i,0)),dwHeight.Load(int3(j.x,i.y,0)),f.x),
             lerp(dwHeight.Load(int3(i.x,j.y,0)),dwHeight.Load(int3(j,0)),f.x),f.y);
}
float dwHash(float2 p){float3 q=frac(p.xyx*.1031);q+=dot(q,q.yzx+33.33);return frac((q.x+q.y)*q.z);}
float dwNoise(float2 p){
 float2 i=floor(p),f=frac(p);f=f*f*(3-2*f);
 return lerp(lerp(dwHash(i),dwHash(i+float2(1,0)),f.x),lerp(dwHash(i+float2(0,1)),dwHash(i+1),f.x),f.y);
}
// One wisp is a bundle of thin strands leaving one stretch of crest. Strand
// k of a wisp: its own place along the crest, sway phase, climb and length.
// r: x place on the crest (-.5..+.5), y sway phase, z climb scale, w length scale.
float4 dwStrand(float seed,uint k){
 float h0=dwHash(float2(seed*97.1+k,k*1.73+.5)),h1=dwHash(float2(k*3.1+.2,seed*53.3+1.7));
 float h2=dwHash(float2(seed*17.9+k*5.3,2.9)),h3=dwHash(float2(k*7.7+seed*11.1,6.1));
 return float4(((k+.2+.6*h0)/5.0)-.5,h1*6.2831853,.6+.8*h2,.55+.45*h3);
}
// Strand centre s metres downwind of its root on the crest. The root sits on
// the sand under it; the strand hugs the surface for its first metres, then
// keeps the root's height plus a slow climb, so it lifts off as the lee face
// drops away. It curls more the further it travels; never into the ground.
float3 dwPath(uint b,float4 r,float s){
 float4 t0=dwWisp[b],t1=dwWisp[b+1],t2=dwWisp[b+2],t3=dwWisp[b+3];
 float2 f=t1.xy,n=float2(-f.y,f.x);
 float phase=t3.z+r.y,curl=saturate(s/2.5);
 float lateral=r.x*(t2.y+t2.z*s)+t3.x*(1+curl)*(sin(t3.y*s+phase)-sin(phase));
 float2 root=t0.xy+n*r.x*t2.y,xy=t0.xy+f*s+n*lateral;
 float rise=t3.w*r.z,ground=dwGround(xy);
 float lifted=dwGround(root)+rise*s+.25*rise*s*s/(1+s);
 float z=lerp(ground+.03,max(lifted,ground+.03),smoothstep(.3,2.5,s));
 return float3(xy,z);
}
DwOut duneWispVS(uint vertex:SV_VertexID,uint instance:SV_InstanceID){
 DwOut o=(DwOut)0;
 uint b=(instance/5u)*5u,k=instance%5u;
 float4 t0=dwWisp[b],t1=dwWisp[b+1],t2=dwWisp[b+2],t4=dwWisp[b+4];
 float4 r=dwStrand(t4.y,k);
 float u=(vertex>>1)/24.0,side=(vertex&1u)?1:-1;
 float age=t0.w,reach=t2.x*r.w;
 // The head runs downwind at the plume speed up to its length; once the
 // crest stops feeding it, the tail follows.
 float head=min(age*t1.z,reach),tail=min(max(age-t1.w,0)*t1.z,reach);
 float s=lerp(tail,head,u);
 float3 p=dwPath(b,r,s),tangent=dwPath(b,r,s+.05)-p;
 if(dot(tangent,tangent)<1e-10)tangent=float3(t1.xy,0);
 float3 view=p-dwEye.xyz;
 float3 across=cross(tangent,view);float l=length(across);
 across=l>1e-6?across/l:float3(-t1.y,t1.x,0);
 // A thin strand widening a little downwind, never below dwStyle.z pixels;
 // a wider-than-real strand keeps its coverage by fading.
 float4 clipP=mul(dwClip,float4(p,1));
 float width=dwStyle.y*(1+.12*s);
 float shown=max(width,dwStyle.z*max(clipP.w,.05)/max(dwEye.w,1e-3));
 o.position=mul(dwClip,float4(p+across*side*shown*.5,1));o.viewW=o.position.w;
 float dist=length(view);
 // Thins with distance from the crest and is soft at its head. While the
 // crest feeds it the root is solid on the sand; once it lets go, the whole
 // strand thins out within a metre of travel instead of drifting off whole.
 float fade=(1-smoothstep(.2,1,s/max(reach,.01)))*smoothstep(0,1.2,head-s)*max(smoothstep(0,.3,s-tail),1-step(.001,tail));
 fade*=(1-smoothstep(0,1.5,tail))*smoothstep(0,1.2,age)*(width/shown);
 fade*=saturate((dist-3)/4)*(1-smoothstep(.6*dwDepth.w,dwDepth.w,dist));
 if(clipP.w<=.05)fade=0;
 float fog=1;
 if(dwFogControl.y!=0&&dwFogControl.x>0)
  fog=1-borderFogDensity(p.xy,dwFogRange,dwFogControl.x,dwFogSeam,dwFogCurve,dwFogSpanLo,dwFogSpanHi);
 o.border=fog;
 o.color=float4(dwColor.rgb,dwStyle.x*t4.x*fade);
 // Break-up coordinates: metres along the flow (moving with the sand) and the strand.
 o.uv=float4(s-age*t1.z,0,side,t4.y*31.7+k*7.3);
 return o;
}
float4 duneWispPS(DwOut i):SV_Target{
 // Each strand thickens and breaks up along its length as the sand travels.
 float n=.85*dwNoise(float2(i.uv.x*.4,i.uv.w))+.15*dwNoise(float2(i.uv.x*1.1,i.uv.w+13.1));
 float body=smoothstep(.15,.7,n);
 float edge=saturate(1-i.uv.z*i.uv.z);
 float a=i.color.a*body*edge*edge;
 float d=dwSceneDepth.Load(int3(i.position.xy,0));
 float denom=d-dwDepth.x,sceneW=abs(denom)>1e-7?dwDepth.y/denom:1e9;
 if(sceneW<0)sceneW=1e9;
 a*=saturate((sceneW-i.viewW)/dwDepth.z);
 float4 c=float4(i.color.rgb*a,a);
 float f=nativeFogFactor(i.border,i.position);
 c.rgb=c.rgb*f+borderFogColor(i.position,dwFogColor.rgb)*(c.a*(1-f));
 float3 added=0;float t=nativeAtmosphere(added,i.position,dwFogColor.rgb);
 c.rgb=c.rgb*t+added*c.a;
 return c;
}
)";

} // namespace owned::dune_wisps
