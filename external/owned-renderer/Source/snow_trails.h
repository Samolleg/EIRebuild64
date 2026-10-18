#pragma once
// Snow trails: a world-anchored deformation field around the view that
// ground units cut while they walk on snow. Each frame every shown, grounded,
// non-levitating unit adds one capsule from its previous to its current
// position (MAX-composed), so a path is a continuous trough with a flat
// compacted floor, smooth walls and soft raised berms on both sides, never a
// chain of separate stamps. The field fills back in over game seconds (a
// paused game keeps it; game speed scales it). Only snow ground (base tile
// types 9 and 12) takes a trail. Terrain shades it (and displaces it when
// cliff tessellation runs); snow piles are cut down and pushed up by it.
// Soft sand by material identity (terrain_tile_soft_sand: the Suslanger
// desert sand families) takes the same trails, shallower.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace owned::snow_trails {

// 12.5 cm texels over an 80 m square (R16G16_FLOAT: trough depth, berm height).
constexpr float kTexel = .125f;
constexpr unsigned kSide = 640;
// A unit moving further than this in one frame was placed, not walking.
constexpr float kLongestStep = 4.f;
// Shape of one unit's trough (metres) from the larger half extent of the
// unit's native local bounding box (CUnit+0x144, the box the game also uses
// for death targeting): a human-sized box (~0.4 m) gives a ~0.6 m wide,
// ~8 cm deep trough, a troll's far more, small creatures less. The animated
// envelope radius is a conservative culling sphere over every clip and root
// route, nearly the same for most bodies, so it is only the fallback when
// the box is unavailable.
struct Shape { float half_width = 0, depth = 0, berm = 0; };
inline Shape body_shape(float half_extent, float envelope_radius) noexcept {
    const float body = half_extent > .05f ? half_extent
        : (envelope_radius > 0 ? std::clamp(envelope_radius * .25f, .2f, 2.f) : .4f);
    const float depth = std::clamp(.045f + .09f * body, .04f, .2f);
    return {std::clamp(body * .72f, .14f, 1.4f), depth, .3f * depth};
}
// Game footprints of one maker further apart than this are separate walks.
constexpr float kLongestPrintGap = 3.f;
// One capsule; mirrors GcSnowSegment in the stamp shader. along: path
// distance walked at a (metres, per unit); seed: from the unit's lifecycle
// id. Both key the edge wobble, width and footstep dimples, so a trail keeps
// its shape from frame to frame.
struct Segment { float a[2], b[2]; float half_width, depth, berm, along; std::uint32_t seed, padding[3]; };
static_assert(sizeof(Segment) == 48);
inline std::uint32_t unit_seed(std::uint64_t id) noexcept {
    std::uint32_t h = std::uint32_t(id) ^ std::uint32_t(id >> 32);
    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15;
    return h;
}

// Activity tiles: 8 m (64 texel) squares of the window, 10 x 10. A tile is
// active while a stamp touched it within kTileLife game seconds (longer
// than any trail takes to fill in). Readers test the tile's bit before any
// texture read, and the update writes only active tiles and their
// neighbours, so ground without a recent trail costs a bit test.
constexpr unsigned kTileTexels = 64, kTiles = kSide / kTileTexels;
static_assert(kTiles * kTileTexels == kSide && kTiles * kTiles <= 128, "the tile bits fit a uint4");
constexpr float kTileLife = 300.f;
// Tile-aligned window origin (texels from the world origin) centred on a
// world point; the window moves in whole tiles, so tiles keep their texels.
inline std::array<int, 2> window_origin(float x, float y) noexcept {
    const float tile = kTexel * kTileTexels, half = .5f * float(kTiles) * tile;
    return {int(std::floor((x - half) / tile)) * int(kTileTexels), int(std::floor((y - half) / tile)) * int(kTileTexels)};
}
// Per tile: game second until which it stays active.
struct ActiveTiles {
    std::array<double, kTiles * kTiles> until{};
    // The window moved by whole tiles: keep each world tile's clock.
    void shift(int tiles_x, int tiles_y) noexcept {
        std::array<double, kTiles * kTiles> moved{};
        for (int y = 0; y < int(kTiles); ++y)
            for (int x = 0; x < int(kTiles); ++x) {
                const int sx = x + tiles_x, sy = y + tiles_y;
                if (sx >= 0 && sy >= 0 && sx < int(kTiles) && sy < int(kTiles)) moved[y * kTiles + x] = until[sy * kTiles + sx];
            }
        until = moved;
    }
    // Every tile within reach (metres) of segment a-b, window origin in texels.
    void touch(const Segment& s, const std::array<int, 2>& origin, double now) noexcept {
        const float reach = 2.3f * s.half_width + 2 * kTexel, tile = kTexel * kTileTexels;
        const float ox = origin[0] * kTexel, oy = origin[1] * kTexel;
        const int x0 = int(std::floor((std::min(s.a[0], s.b[0]) - reach - ox) / tile)), x1 = int(std::floor((std::max(s.a[0], s.b[0]) + reach - ox) / tile));
        const int y0 = int(std::floor((std::min(s.a[1], s.b[1]) - reach - oy) / tile)), y1 = int(std::floor((std::max(s.a[1], s.b[1]) + reach - oy) / tile));
        for (int y = std::max(y0, 0); y <= std::min(y1, int(kTiles) - 1); ++y)
            for (int x = std::max(x0, 0); x <= std::min(x1, int(kTiles) - 1); ++x) until[y * kTiles + x] = now + kTileLife;
    }
    // Bit (y * kTiles + x) of the active tiles; dilated: plus their neighbours.
    std::array<std::uint32_t, 4> mask(double now, bool dilated) const noexcept {
        std::array<std::uint32_t, 4> bits{};
        for (int y = 0; y < int(kTiles); ++y)
            for (int x = 0; x < int(kTiles); ++x) {
                bool on = false;
                for (int dy = dilated ? -1 : 0; dy <= (dilated ? 1 : 0) && !on; ++dy)
                    for (int dx = dilated ? -1 : 0; dx <= (dilated ? 1 : 0) && !on; ++dx) {
                        const int sx = x + dx, sy = y + dy;
                        on = sx >= 0 && sy >= 0 && sx < int(kTiles) && sy < int(kTiles) && until[sy * kTiles + sx] > now;
                    }
                if (on) bits[(y * kTiles + x) >> 5] |= 1u << ((y * kTiles + x) & 31);
            }
        return bits;
    }
};

constexpr char kShader[] = R"(
#ifndef SNOW_TRAIL_SHADER
#define SNOW_TRAIL_SHADER
// The field (snow_trails.h): R trough depth, G berm height, metres. t8 is
// free in the pixel, hull and domain stages (the world vertices are a
// vertex-stage input; t10-t15 carry fog, sky and contact maps).
Texture2D<float2> nativeSnowTrail:register(t8);
// window: xy world origin of texel (0,0), z texels per metre, w side in texels (0: no field).
// tiles: the active 8 m tiles (bit y*10+x, snow_trails.h ActiveTiles); any
// other point returns 0 before a texture read. Manual bilinear (no sampler
// in any stage that reads it). The trail fades from 24 to 31 m (the larger
// axis distance) from the window's smooth centre (nativeSnowValue.yz of the
// native terrain frame this text is compiled into): the window itself moves
// in whole 8 m tiles, and a fade by its edge jumped with every move, so
// trails and their displacement popped with distance (2bea38da user note).
// The centre sits 32 m or more inside every edge.
float2 snowTrailRaw(Texture2D<float2> field,float4 window,uint4 tiles,float2 p){
 if(window.w<=0)return 0;
 float2 q=(p-window.xy)*window.z-.5;
 if(any(q<0)||any(q>window.w-1.001))return 0;
 uint2 tile=uint2(q+.5)/64u;uint bit=tile.y*10u+tile.x;
 if(((tiles[bit>>5]>>(bit&31u))&1u)==0)return 0;
 int2 i=int2(floor(q));float2 f=q-i;
 float2 v=lerp(lerp(field.Load(int3(i,0)),field.Load(int3(i+int2(1,0),0)),f.x),
               lerp(field.Load(int3(i+int2(0,1),0)),field.Load(int3(i+int2(1,1),0)),f.x),f.y);
 float2 off=abs(p-nativeTerrainFrame[0].nativeSnowValue.yz);
 return v*(1-smoothstep(24,31,max(off.x,off.y)));
}
// Height relative to untouched snow: berms up, troughs down.
float snowTrailHeight(float2 raw){return raw.y-raw.x;}
// Height gradient per metre (central differences one texel apart).
float2 snowTrailSlope(Texture2D<float2> field,float4 window,uint4 tiles,float2 p){
 float d=1/window.z;
 return float2(snowTrailHeight(snowTrailRaw(field,window,tiles,p+float2(d,0)))-snowTrailHeight(snowTrailRaw(field,window,tiles,p-float2(d,0))),
               snowTrailHeight(snowTrailRaw(field,window,tiles,p+float2(0,d)))-snowTrailHeight(snowTrailRaw(field,window,tiles,p-float2(0,d))))*(.5*window.z);
}
// Packed trough floor, 0 untouched to 1.
float snowTrailCompaction(float2 raw){return saturate(raw.x/.03);}
// Smooth 1D noise in [-1,1] along a path, per unit seed.
float snowTrailNoise(float x,uint seed){
 float i=floor(x),f=x-i;f=f*f*(3-2*f);
 uint a=uint(int(i))*0x9e3779b1u^seed,b=uint(int(i)+1)*0x9e3779b1u^seed;
 a^=a>>15;a*=0x2c1b3c6du;a^=a>>12;b^=b>>15;b*=0x2c1b3c6du;b^=b>>12;
 return lerp(float(a&0xffffu),float(b&0xffffu),f)/32767.5-1;
}
// One capsule a-b of shape (half width, depth, berm): a flat floor to 35% of
// the half width, smooth walls to the edge, and a soft lip centred just
// outside it (C1, zero beyond 2.3 half widths). path: x the distance walked
// at a, y the unit seed. Along the path the centre line wobbles by up to 12%
// of the half width, the width breathes by +-12%, and footstep dimples sink
// the floor a little more, alternating left and right at a stride of 2.4
// half widths.
float2 snowTrailStamp(float2 q,float2 a,float2 b,float3 shape,float2 path){
 float2 ab=b-a;float l2=dot(ab,ab),l=sqrt(l2);
 float2 u=l>1e-5?ab/l:float2(1,0),v=float2(-u.y,u.x);
 // A standing unit's step has no length: its stamp is the round cap alone.
 float raw=dot(q-a,u),t=clamp(raw,0,l);
 uint seed=asuint(path.y);float s=path.x+t;
 float w=shape.x*(1+.12*snowTrailNoise(s*.45,seed^0x51ed27u));
 float side=dot(q-a,v)-shape.x*.12*snowTrailNoise(s*.8,seed);
 float d=sqrt(side*side+(raw-t)*(raw-t))/w;
 float trough=1-smoothstep(.35,1,d);
 float lip=abs(d-1.15)<.75?(1-((d-1.15)/.75)*((d-1.15)/.75))*(1-((d-1.15)/.75)*((d-1.15)/.75)):0;
 float stride=2.4*shape.x,n=round(s/stride);
 float2 step=float2(s-n*stride,side-(fmod(abs(n),2)<.5?-.25:.25)*w)/(.32*w);
 float dimple=saturate(1-dot(step,step));
 return float2(shape.y*trough*(1+.35*dimple*dimple),shape.z*lip);
}
// Fill-in over game seconds: about 90 s to fade, gone within ~2.5 minutes
// for a human trail. dt 0 (paused) keeps the field unchanged.
float2 snowTrailRecover(float2 v,float dt){return max(v*exp(-dt/90.)-dt*.0002,0);}
// Packed trough floor colour: barely darker, a touch cooler than fresh snow.
float3 snowTrailPackedTone(float2 raw){return lerp(1,float3(.975,.98,.995),snowTrailCompaction(raw));}
#endif
)";

} // namespace owned::snow_trails
