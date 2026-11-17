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
inline constexpr std::uint32_t kTerrainPrintTag=0x46505254u;

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

// Game lightning ribbons (CVisualizatorD3D::Draw3DLine, slot 16, 0x00501D90).
// The game keeps the bolt simulation (CLightning builds its leaf points and
// picks the texture strip); the renderer replaces the stock CPU transform,
// screen-space strip expansion and TL draw. Stock rules (level 1, shipped
// bytes): a clip flag of TRV_INVISIBLE (2) or fewer than 2 points draws
// nothing; point j has u = u0 + j * L / (n - 1) with u0 = 0, or 0.875 when the
// texture length L is 0, and v from vstart to vstart + vwidth across the
// strip; diffuse white, alpha 0 at both end points and 255 between; specular
// alpha 255; blend SRCALPHA/ONE for the call. Each point gets a pair of
// vertices offset by 15 * width / W pixels along the screen perpendicular of
// its direction: (p1 - p0) at the first point, (p_last - p_last-1) at the
// last, (p_j+1 - p_j-1) between; the pair (+, -) carries (vstart, vstart +
// vwidth); each segment is triangles (a+, a-, b+) (b+, a-, b-).
// Deliberate difference, as for particles: hardware clipping replaces the
// stock per-segment reject, so segments with both ends off screen still draw
// their visible part.
struct GameRibbonPoint {
    float x = 0, y = 0, z = 0;
};
struct GameRibbonStrip {
    float u0 = 0, du = 0, v0 = 0, v1 = 0;
};
inline void game_ribbon_strip(float texture_length, float v_start, float v_width, std::uint32_t count,
                              GameRibbonStrip& strip) noexcept {
    strip.u0 = texture_length != 0.0f ? 0.0f : 0.875f;
    strip.du = count > 1 ? texture_length / static_cast<float>(count - 1) : 0.0f;
    strip.v0 = v_start;
    strip.v1 = v_start + v_width;
}
inline void game_ribbon_point_gpu(const GameRibbonPoint* points, std::uint32_t count, std::uint32_t j,
                                  const GameRibbonStrip& strip, float half_width_w, GameParticleGpu& g) noexcept {
    g.center_radius[0] = points[j].x;
    g.center_radius[1] = points[j].y;
    g.center_radius[2] = points[j].z;
    g.center_radius[3] = half_width_w;
    g.uv[0] = strip.u0 + strip.du * static_cast<float>(j);
    g.uv[1] = strip.v0;
    g.uv[2] = strip.v1;
    g.uv[3] = 0.0f;
    g.color = (j == 0 || j + 1 == count) ? 0x00ffffffu : 0xffffffffu;
    g.padding[0] = g.padding[1] = g.padding[2] = 0;
}

// The game's selection marker (CVisualizatorD3D::DrawSelection, 0x00503A40,
// called only by Draw3DScene at 0x0056BF12 when the selection type is the
// marker). The game supplies the unit point (its "hp" figure position at the
// unit's ground height), the size and the unit's x/y axes; the stock body
// builds six world points 0.3 above that height from r = size * (float)1/3 and the
// axes' x/y components:
//   0: cp + 3r X + 2r Y   1: cp - 3r X + 2r Y   2: cp - 5r Y
//   3: cp + 2r X + 1.4r Y 4: cp - 2r X + 1.4r Y 5: cp - 4r Y
// and draws triangles (0,4,3) (0,1,4) (1,5,4) (1,2,5) (2,3,5) (2,0,3),
// untextured, diffuse RGBA (0,255,0,128), specular alpha 255 (no fog), with
// the render state Draw3DScene set. The renderer generates the same mesh in
// the vertex shader from those inputs.
struct GameSelectionMarker {
    float x = 0, y = 0, z = 0, size = 0;
    float x_axis[2]{}, y_axis[2]{};
};
inline void game_selection_gpu(const GameSelectionMarker& m, GameParticleGpu& g) noexcept {
    g.center_radius[0] = m.x;
    g.center_radius[1] = m.y;
    g.center_radius[2] = m.z + 0.3f;
    g.center_radius[3] = m.size * 0.33333334f;  // stock multiplies by the float 1/3
    g.uv[0] = m.x_axis[0];
    g.uv[1] = m.x_axis[1];
    g.uv[2] = m.y_axis[0];
    g.uv[3] = m.y_axis[1];
    g.color = 0x8000ff00u;
    g.padding[0] = g.padding[1] = g.padding[2] = 0;
}

// Game terrain prints (blood, foot and fire prints; CTerrainPrints::Draw
// 0x006CEDE0). The game keeps the print lists, lifetimes, fade and the mesh
// each print projects once onto the terrain and bridges (CastShadowToTerrain,
// cached in the print, lifted 0.01). Its per-frame CDXDirect3D::Render call
// (0x006CF2F6: plots +0x5C, count +0x60, maps +0x6C, indices +0x7C, index
// count +0x80, diffuse +0x54 with alpha MINT(fAlpha * 255), specular alpha
// the print's fog byte) hands that mesh to the renderer instead of the stock
// CPU transform/clip/TL draw. Vertex i of the mesh is lane i % 4 of the
// 64-byte SXYZW4 group i / 4 (x +0, y +16, z +32) with UV maps[i]. Each list
// is drawn as one batch per texture/state run when its last print has drawn.
// Fog: world fog like the terrain under the print (the stock fog byte was the
// terrain fog at the print centre).
inline void game_print_vertex_gpu(const float* group, std::uint32_t lane, float u, float v,
                                  std::uint32_t color, GameParticleGpu& g) noexcept {
    g.center_radius[0] = group[lane];
    g.center_radius[1] = group[4 + lane];
    g.center_radius[2] = group[8 + lane];
    g.center_radius[3] = 0.0f;
    g.uv[0] = u;
    g.uv[1] = v;
    g.uv[2] = g.uv[3] = 0.0f;
    g.color = color;
    g.padding[0] = g.padding[1] = g.padding[2] = 0;
}

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
 uint gpCoverageMode;uint gpCount;uint2 gpPadding;float4 gpSoft;};
struct GameParticle{float4 centerRadius;float4 uv;uint color;uint3 padding;};
StructuredBuffer<GameParticle> gpParticles:register(t7);
Texture2D gpTexture:register(t0);SamplerState gpSampler:register(s0);
// Soft particles: the world depth (tested read-only by the same draw).
Texture2D<float> gpSceneDepth:register(t2);
// r: the particle's clip-scaled radius (gameParticleVS; 0 for the others).
struct GameParticleOut{float4 p:SV_Position;float4 d:COLOR0;float2 uv:TEXCOORD0;float r:TEXCOORD1;nointerpolation float2 printData:TEXCOORD2;};
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
 GameParticleOut o=(GameParticleOut)0;
 o.p=float4((sx-gpViewport.x*k.w)/gpViewport.z*2-k.w,k.w-(sy-gpViewport.y*k.w)/gpViewport.w*2,k.z,k.w);
 o.d=float4((q.color>>16)&255,(q.color>>8)&255,q.color&255,q.color>>24)/255.0;
 o.uv=float2(right>0?q.uv.z:q.uv.x,down>0?q.uv.w:q.uv.y);
 o.r=r;
 return o;
}
// Stock screen point times W (sx, sy) to the D3D11 clip position.
float4 gpClipFromScreen(float sx,float sy,float4 k){
 return float4((sx-gpViewport.x*k.w)/gpViewport.z*2-k.w,k.w-(sy-gpViewport.y*k.w)/gpViewport.w*2,k.z,k.w);}
float2 gpScreenPoint(float3 p){float4 k=mul(gpObjectToClip,float4(p,1));float w=abs(k.w)>1e-6?k.w:1e-6;
 return float2((k.x/w+1)*gpClipHalf.x,(1-k.y/w)*gpClipHalf.y);}
// Lightning ribbon (game_particles.h, Draw3DLine): element j is point j with
// w = stock 15 * width, uv = (u_j, v0, v1), colour = white with end alpha 0.
// Six vertices per segment: (a+, a-, b+) (b+, a-, b-). gpCount points.
GameParticleOut gameRibbonVS(uint id:SV_VertexID){
 uint local=id%6;uint j=id/6+(local==2||local==3||local==5?1:0);
 float side=local==1||local==4||local==5?-1:1;
 GameParticle q=gpParticles[gpFirst+j];
 uint i0=j==0?0:j-1,i1=j+1<gpCount?j+1:j;
 float2 d=gpScreenPoint(gpParticles[gpFirst+i1].centerRadius.xyz)-gpScreenPoint(gpParticles[gpFirst+i0].centerRadius.xyz);
 float len=length(d);d=len>0?d/len:0;
 float4 k=mul(gpObjectToClip,float4(q.centerRadius.xyz,1));
 // Stock offset 15 * width / W pixels; the world clip scale as for particles.
 float s=side*q.centerRadius.w*gpClipHalf.z;
 GameParticleOut o=(GameParticleOut)0;
 o.p=gpClipFromScreen((k.x+k.w)*gpClipHalf.x+s*d.y,(k.w-k.y)*gpClipHalf.y-s*d.x,k);
 o.d=float4((q.color>>16)&255,(q.color>>8)&255,q.color&255,q.color>>24)/255.0;
 o.uv=float2(q.uv.x,side>0?q.uv.y:q.uv.z);o.r=0;
 return o;
}
// Selection marker (game_particles.h, DrawSelection): element = (point at the
// stock height, r) and the unit's x/y axes; 18 vertices per marker.
static const uint gsIndex[18]={0,4,3,0,1,4,1,5,4,1,2,5,2,3,5,2,0,3};
GameParticleOut gameSelectionVS(uint id:SV_VertexID){
 GameParticle q=gpParticles[gpFirst+id/18];uint v=gsIndex[id%18];
 float r=q.centerRadius.w;float2 X=q.uv.xy,Y=q.uv.zw;
 float2 a=v==0?3*r*X+2*r*Y:(v==1?-3*r*X+2*r*Y:(v==2?-5*r*Y:(v==3?2*r*X+1.4*r*Y:(v==4?-2*r*X+1.4*r*Y:-4*r*Y))));
 float4 k=mul(gpObjectToClip,float4(q.centerRadius.xy+a,q.centerRadius.z,1));
 GameParticleOut o=(GameParticleOut)0;
 o.p=gpClipFromScreen((k.x+k.w)*gpClipHalf.x,(k.w-k.y)*gpClipHalf.y,k);
 o.d=float4((q.color>>16)&255,(q.color>>8)&255,q.color&255,q.color>>24)/255.0;
 o.uv=0;o.r=0;
 return o;
}
// World-space triangle list (terrain prints): element = vertex (xyz, uv, colour).
GameParticleOut gameMeshVS(uint id:SV_VertexID){
 GameParticle q=gpParticles[gpFirst+id];
 float4 k=mul(gpObjectToClip,float4(q.centerRadius.xyz,1));
 GameParticleOut o=(GameParticleOut)0;
 o.p=gpClipFromScreen((k.x+k.w)*gpClipHalf.x,(k.w-k.y)*gpClipHalf.y,k);
 o.d=float4((q.color>>16)&255,(q.color>>8)&255,q.color&255,q.color>>24)/255.0;
 o.uv=q.uv.xy;o.r=0;
 return o;
}
// Screen-space glyph quads (combat text, combat_text.h): element = (x, y, w, h)
// in D3D7 viewport pixels, (u0, v0, u1, v1), colour; stock triangles
// (0,2,1) (1,2,3); depth 1 like the stock TL vertices (sz = rhw = 1).
GameParticleOut gameGlyphVS(uint id:SV_VertexID){
 GameParticle q=gpParticles[gpFirst+id/6];
 uint local=id%6;uint corner=local==0?0:(local==1||local==4?2:(local==5?3:1));
 float right=corner&1,down=corner>>1;
 float2 s=q.centerRadius.xy+float2(right,down)*q.centerRadius.zw;
 GameParticleOut o=(GameParticleOut)0;
 o.p=float4((s.x-gpViewport.x)/gpViewport.z*2-1,1-(s.y-gpViewport.y)/gpViewport.w*2,1,1);
 o.d=float4((q.color>>16)&255,(q.color>>8)&255,q.color&255,q.color>>24)/255.0;
 o.uv=float2(right>0?q.uv.z:q.uv.x,down>0?q.uv.w:q.uv.y);o.r=0;
 return o;
}
float4 gpArg(uint selector,float4 diffuse,float4 textureSample){
 uint source=selector&15;float4 v=source<=1?diffuse:(source==2?textureSample:(source==3?gpTextureFactor:(source==4?float4(0,0,0,1):0)));
 if((selector&32)!=0)v=v.aaaa;if((selector&16)!=0)v=1-v;return v;}
float3 gpColor(uint op,float4 a,float4 b,float3 c){if(op==2)return a.rgb;if(op==3)return b.rgb;if(op==4)return a.rgb*b.rgb;if(op==5)return saturate(2*a.rgb*b.rgb);if(op==7)return saturate(a.rgb+b.rgb);return c;}
float gpAlpha(uint op,float4 a,float4 b,float c){if(op==2)return a.a;if(op==3)return b.a;if(op==4)return a.a*b.a;if(op==5)return saturate(2*a.a*b.a);if(op==7)return saturate(a.a+b.a);return c;}
bool gpCompare(float a){if(gpAlphaFunc==1)return false;if(gpAlphaFunc==2)return a<gpAlphaRef;if(gpAlphaFunc==3)return a==gpAlphaRef;if(gpAlphaFunc==4)return a<=gpAlphaRef;if(gpAlphaFunc==5)return a>gpAlphaRef;if(gpAlphaFunc==6)return a!=gpAlphaRef;if(gpAlphaFunc==7)return a>=gpAlphaRef;return true;}
// Target 1 is the bloom glow target, bound for blended world particles that
// are not modulating. Additive particles (light) write the same fogged,
// blended light. Alpha-blended ones write their colour weighted by
// gpFlameKey under their own alpha: the game's torch and campfire flames
// (PTID_CAMPFIRE/FIRE, TendriSmoke, not additive) glow, while their grey
// smoke, blood and dust do not and cover the glow behind them.
float gpFlameKey(float3 c){float hi=max(c.r,max(c.g,c.b)),lo=min(c.r,min(c.g,c.b));
 return smoothstep(.5,.9,hi)*smoothstep(.35,.65,(hi-lo)/max(hi,1e-4));}
struct GameParticlePixel{float4 color:SV_Target0;float4 glow:SV_Target1;};
GameParticlePixel gameParticlePS(GameParticleOut i){
 if(gpPadding.y==5u){
  uint cell=(uint)i.printData.x;float2 lo=float2(cell&1u,cell>>1)*.5;
  clip(i.uv-lo);clip(lo+.5-i.uv);clip(i.printData.y-i.r);
 }
 float4 t=gpTextured!=0?gpTexture.Sample(gpSampler,i.uv):float4(1,1,1,1),c=i.d;
 c.rgb=gpColor(gpColorOp,gpArg(gpColorArg1,i.d,t),gpArg(gpColorArg2,i.d,t),c.rgb);
 c.a=gpAlpha(gpAlphaOp,gpArg(gpAlphaArg1,i.d,t),gpArg(gpAlphaArg2,i.d,t),c.a);
 if(gpAlphaTest!=0&&!gpCompare(c.a))discard;
 // Opt-in untextured UI ribbon: uv.y is the signed cross-section. The
 // derivative makes a one-pixel inner coverage ramp at either edge. Its
 // caller uses ONE/INVSRCALPHA, so colour and alpha are premultiplied here.
 [branch]if((gpPadding.x&1u)!=0){
  float coverage=saturate((1-abs(i.uv.y))/max(fwidth(i.uv.y),1e-6));
  c*=coverage;
 }
 const float flame=gpCoverageMode==0?gpFlameKey(c.rgb):1; // authored colour, before fog
 // 1: blended like a surface (takes fog colour and in-scatter); 2: additive
 // light, only attenuated.
 // A blended effect is light and smoke, not a surface: past the draw distance
 // it fades out with the cull (its colour already eased to the sky's) instead
 // of standing as a sky-coloured puff, and takes its glow with it.
 [branch]if(gpFogMode==1){c.rgb=lerp(gpFogColor.rgb,c.rgb,nativeFogFactor(1,i.p));nativeAtmosphere(c.rgb,i.p,gpFogColor.rgb);c.a*=nativeDrawFade(i.p);}
 else if(gpFogMode==2){float3 added=0;c.rgb*=nativeFogFactor(1,i.p)*nativeAtmosphere(added,i.p,gpFogColor.rgb);}
 // Soft particles (world particle calls, gpSoft.w: 1 the blend weighs the
 // colour by alpha, 2 additive light): where the world behind the particle
 // is nearer than gpSoft.z times its radius, the particle fades out there
 // instead of cutting a hard line into the ground or a wall. View distance
 // W = B / (z - A) (gpSoft.xy, from the call's own projection) for the world
 // depth and the particle's depth alike.
 float fade=1,scene=1;
 [branch]if(gpPadding.y!=5u&&gpSoft.w!=0){
  scene=gpSceneDepth.Load(int3(i.p.xy,0));
  fade=saturate((gpSoft.y/(scene-gpSoft.x)-gpSoft.y/(i.p.z-gpSoft.x))/max(i.r*gpSoft.z,1e-5));
  if(gpSoft.w>1.5)c*=fade;else c.a*=fade;
 }
 GameParticlePixel o;o.color=c;o.glow=float4(c.rgb*flame,c.a);
#ifdef GP_SOFT_DEBUG
 // Diagnostic (soft_particles_debug): one flat colour per effect class.
 // Soft world particles: red where fully faded to green where unfaded.
 // Self-checks: orange = the depth read is 0 (no depth bound); purple = the
 // read depth lies in front of a pixel the depth test passed (the read and
 // tested depths differ). Other classes through this program: white =
 // particle call left hard (blend, depth write, interface), blue = ribbon,
 // magenta = selection, cyan = glyph, yellow = ground print.
 float shape=gpCoverageMode==2?max(t.r,max(t.g,t.b))*t.a:t.a;
 if(gpTextured!=0&&shape<.05)discard;
 float3 m=gpPadding.y==1?float3(0,0,1):gpPadding.y==2?float3(1,0,1):gpPadding.y==3?float3(0,1,1):gpPadding.y==4?float3(1,1,0):float3(1,1,1);
 if(gpPadding.y!=5u&&gpSoft.w!=0)m=scene<=0?float3(1,.5,0):scene<i.p.z-1e-6?float3(.5,0,1):lerp(float3(1,0,0),float3(0,1,0),fade);
 o.color=float4(m,1);o.glow=0;
#endif
 return o;
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
    std::uint32_t coverage_mode;  // reflection depth coverage (gameParticleDepthPS)
    std::uint32_t count;          // ribbon point count (gameRibbonVS)
    std::uint32_t padding[2];     // x: opt-in UI ribbon analytic coverage; y: effect class (soft particle diagnostic)
    float soft[4];                // soft particles: depth A, B (W = B / (z - A)), range per clip radius, mode (0 off)
};
static_assert(sizeof(GameParticleConstants) == 208, "game particle constants must match HLSL");

}  // namespace owned
