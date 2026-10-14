#pragma once

// Game particle billboards (CVisualizatorD3D::DrawParticles, 0x00502CA0 on the
// supported executable). The game keeps particle simulation, emitter lifetime
// and manager ordering; it hands over completed points (x, y, z, radius),
// colours, UV rectangles and the additive flag. The renderer expands, clips,
// fogs and draws the quads on the GPU instead of the stock CPU TL path.
//
// Stock behaviour reproduced here (fingerprinted game.exe and 3DfpFPU.dll
// a7b11bf5... / f298f790..., slot 24 at RVA 0x6F00 / 0x9E40):
// - clip = M * p with M the top of the game's projection-space matrix stack;
// - the quad is a pixel square of half size r * hx / W around the projected
//   centre, hx/hy being the fingerprint-selected clip-rect half extents;
//   corners 0..3 = (-,-) (+,-) (+,+) (-,+) in screen space with UVs
//   (u0,v0) (u1,v0) (u1,v1) (u0,v1), drawn as triangles (0,2,1) (0,3,2);
// - particles are drawn in batches of 511 in input order; inside a batch they
//   are sorted far to near by the bit pattern of screen z = Z * rhw (rhw = 1/W,
//   or 1e6 when W == 0) and a particle whose screen z has the sign bit set is
//   dropped. More than 128 survivors use a stable sort; fewer use an
//   interleaved bottom-up merge whose tie order is reproduced exactly.
// - vertex colour is the particle colour, specular is black with alpha 255,
//   so stock particles never received vertex fog.
//
// Deliberate difference: the stock trivial reject used corner offsets of
// r * rhw in clip units (not r), so particles at the screen edge could vanish
// before their quad left the screen. Hardware clipping keeps every visible
// part of the quad.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <vector>

namespace owned {

struct GameParticle {
    float x = 0, y = 0, z = 0, radius = 0;
    std::uint32_t color = 0;
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
};

// Structured-buffer element of the GPU pass.
struct GameParticleGpu {
    float center_radius[4];
    float uv[4];
    std::uint32_t color;
    std::uint32_t padding[3];
};
static_assert(sizeof(GameParticleGpu) == 48, "game particle GPU layout must match HLSL");

constexpr std::uint32_t kGameParticleBatch = 511;

// Screen z with the stock operand order (m is row-major, clip = m * p).
inline float game_particle_screen_z(const float* m, const GameParticle& p) noexcept {
    const float z = p.z * m[10] + p.y * m[9] + p.x * m[8] + m[11];
    const float w = p.z * m[14] + p.y * m[13] + p.x * m[12] + m[15];
    const float rhw = w == 0.0f ? 1.0e6f : 1.0f / w;
    return rhw * z;
}

// Appends the stock draw order (indices into particles) to order.
inline void game_particle_draw_order(const float* m, const GameParticle* particles, std::uint32_t count,
                                     std::vector<std::uint32_t>& order) {
    struct Key { std::int32_t bits; std::uint32_t index; };
    std::vector<Key> keys, run, a, b;
    keys.reserve((std::min)(count, kGameParticleBatch));
    const auto farther = [](const Key& a, const Key& b) { return a.bits > b.bits; };
    for (std::uint32_t first = 0; first < count; first += kGameParticleBatch) {
        const std::uint32_t end = (std::min)(count, first + kGameParticleBatch);
        keys.clear();
        for (std::uint32_t i = first; i < end; ++i) {
            const float z = game_particle_screen_z(m, particles[i]);
            std::int32_t bits = 0;
            std::memcpy(&bits, &z, sizeof(bits));
            if (bits >= 0) keys.push_back({bits, i});
        }
        const std::size_t n = keys.size();
        if (n > 128) {
            std::stable_sort(keys.begin(), keys.end(), farther);
        } else if (n > 1) {
            // Runs interleaved with stride 2s are merged into stride s, ties
            // taken from the run that starts first, down to s = 1.
            std::size_t s = 1;
            while (s * 2 < n) s *= 2;
            for (; s > 0; s /= 2) {
                for (std::size_t start = 0; start < s && start + s < n; ++start) {
                    a.clear();
                    b.clear();
                    for (std::size_t i = start; i < n; i += 2 * s) a.push_back(keys[i]);
                    for (std::size_t i = start + s; i < n; i += 2 * s) b.push_back(keys[i]);
                    run.clear();
                    std::merge(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(run), farther);
                    for (std::size_t k = 0; k < run.size(); ++k) keys[start + k * s] = run[k];
                }
            }
        }
        for (const Key& key : keys) order.push_back(key.index);
    }
}

// HLSL. Prepend kAtmosphereShader (world_fog.h): fog matches world surfaces.
constexpr char kGameParticleShader[] = R"(
cbuffer GameParticleFrame:register(b0){row_major float4x4 gpObjectToClip;float4 gpClipHalf;float4 gpViewport;
 float4 gpFogColor;float4 gpTextureFactor;uint gpColorOp;uint gpAlphaOp;uint gpColorArg1;uint gpColorArg2;
 uint gpAlphaArg1;uint gpAlphaArg2;uint gpAlphaTest;uint gpAlphaFunc;float gpAlphaRef;uint gpTextured;uint gpFogMode;uint gpFirst;
 uint gpCoverageMode;uint3 gpPadding;};
struct GameParticle{float4 centerRadius;float4 uv;uint color;uint3 padding;};
StructuredBuffer<GameParticle> gpParticles:register(t7);
Texture2D gpTexture:register(t0);SamplerState gpSampler:register(s0);
struct GameParticleOut{float4 p:SV_Position;float4 d:COLOR0;float2 uv:TEXCOORD0;};
GameParticleOut gameParticleVS(uint id:SV_VertexID){
 GameParticle q=gpParticles[gpFirst+id/6];
 uint local=id%6;uint corner=local==0||local==3?0:(local==1?2:(local==2?1:(local==4?3:2)));
 float right=corner==1||corner==2?1:-1,down=corner>=2?1:-1;
 // The radius is a world size: on a hor+ screen the clip scale that narrows
 // world x narrows it too, so particles keep their 4:3 size against the world.
 float4 k=mul(gpObjectToClip,float4(q.centerRadius.xyz,1));float r=q.centerRadius.w*gpClipHalf.z;
 // Stock screen point times W: linear in clip space, so hardware clipping
 // replaces the stock screen-space clipper without changing visible pixels.
 float sx=(k.x+k.w+right*r)*gpClipHalf.x,sy=(k.w-k.y)*gpClipHalf.y+down*r*gpClipHalf.x;
 GameParticleOut o;
 o.p=float4((sx-gpViewport.x*k.w)/gpViewport.z*2-k.w,k.w-(sy-gpViewport.y*k.w)/gpViewport.w*2,k.z,k.w);
 o.d=float4((q.color>>16)&255,(q.color>>8)&255,q.color&255,q.color>>24)/255.0;
 o.uv=float2(right>0?q.uv.z:q.uv.x,down>0?q.uv.w:q.uv.y);
 return o;
}
float4 gpArg(uint selector,float4 diffuse,float4 textureSample){
 uint source=selector&15;float4 v=source<=1?diffuse:(source==2?textureSample:(source==3?gpTextureFactor:(source==4?float4(0,0,0,1):0)));
 if((selector&32)!=0)v=v.aaaa;if((selector&16)!=0)v=1-v;return v;}
float3 gpColor(uint op,float4 a,float4 b,float3 c){if(op==2)return a.rgb;if(op==3)return b.rgb;if(op==4)return a.rgb*b.rgb;if(op==5)return saturate(2*a.rgb*b.rgb);if(op==7)return saturate(a.rgb+b.rgb);return c;}
float gpAlpha(uint op,float4 a,float4 b,float c){if(op==2)return a.a;if(op==3)return b.a;if(op==4)return a.a*b.a;if(op==5)return saturate(2*a.a*b.a);if(op==7)return saturate(a.a+b.a);return c;}
bool gpCompare(float a){if(gpAlphaFunc==1)return false;if(gpAlphaFunc==2)return a<gpAlphaRef;if(gpAlphaFunc==3)return a==gpAlphaRef;if(gpAlphaFunc==4)return a<=gpAlphaRef;if(gpAlphaFunc==5)return a>gpAlphaRef;if(gpAlphaFunc==6)return a!=gpAlphaRef;if(gpAlphaFunc==7)return a>=gpAlphaRef;return true;}
// Target 1 is the bloom glow target, bound only for additive world
// particles (light): it receives the same fogged, blended light.
struct GameParticlePixel{float4 color:SV_Target0;float4 glow:SV_Target1;};
GameParticlePixel gameParticlePS(GameParticleOut i){
 float4 t=gpTextured!=0?gpTexture.Sample(gpSampler,i.uv):float4(1,1,1,1),c=i.d;
 c.rgb=gpColor(gpColorOp,gpArg(gpColorArg1,i.d,t),gpArg(gpColorArg2,i.d,t),c.rgb);
 c.a=gpAlpha(gpAlphaOp,gpArg(gpAlphaArg1,i.d,t),gpArg(gpAlphaArg2,i.d,t),c.a);
 if(gpAlphaTest!=0&&!gpCompare(c.a))discard;
 // 1: blended like a surface (takes fog colour and in-scatter); 2: additive
 // light, only attenuated.
 [branch]if(gpFogMode==1){c.rgb=lerp(gpFogColor.rgb,c.rgb,nativeFogFactor(1,i.p));nativeAtmosphere(c.rgb,i.p,gpFogColor.rgb);}
 else if(gpFogMode==2){float3 added=0;c.rgb*=nativeFogFactor(1,i.p)*nativeAtmosphere(added,i.p,gpFogColor.rgb);}
 GameParticlePixel o;o.color=c;o.glow=c;return o;
}
// Reflection depth (#140): the particle's screen depth where it covers the
// pixel visibly, for the next frame's water reflections; min-blended into a
// far-cleared R32F target, depth-tested against the scene (no depth writes).
// Coverage: 0 alpha-blended or alpha-tested (alpha), 1 additive by source
// alpha (brightest channel times alpha), 2 additive (brightest channel).
static const float gpReflectCoverage=.15; // coverage below which a particle stays out of reflections @param 0 1
float gameParticleDepthPS(GameParticleOut i):SV_Target0{
 float4 t=gpTextured!=0?gpTexture.Sample(gpSampler,i.uv):float4(1,1,1,1),c=i.d;
 c.rgb=gpColor(gpColorOp,gpArg(gpColorArg1,i.d,t),gpArg(gpColorArg2,i.d,t),c.rgb);
 c.a=gpAlpha(gpAlphaOp,gpArg(gpAlphaArg1,i.d,t),gpArg(gpAlphaArg2,i.d,t),c.a);
 if(gpAlphaTest!=0&&!gpCompare(c.a))discard;
 float light=max(c.r,max(c.g,c.b));
 float coverage=gpCoverageMode==0?c.a:(gpCoverageMode==1?light*c.a:light);
 if(coverage<gpReflectCoverage)discard;
 return i.p.z;
}
)";

struct GameParticleConstants {
    float object_to_clip[16];
    float clip_half[4];   // pipeline clip-rect half width, half height; z: world clip scale x (widescreen)
    float viewport[4];    // D3D7 viewport x, y, width, height
    float fog_color[4];
    float texture_factor[4];
    std::uint32_t color_op, alpha_op, color_arg1, color_arg2;
    std::uint32_t alpha_arg1, alpha_arg2, alpha_test, alpha_func;
    float alpha_ref;
    std::uint32_t textured, fog_mode, first;
    std::uint32_t coverage_mode, padding[3]; // reflection depth coverage (gameParticleDepthPS)
};
static_assert(sizeof(GameParticleConstants) == 192, "game particle constants must match HLSL");

}  // namespace owned
