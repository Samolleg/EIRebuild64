// Renderer-owned ambient critters (issue #68). Nothing in the game draws or
// owns them; the game stays authoritative for every unit.
//
// Look and behaviour: the approved offline prototype (claude/critters-
// prototype, Prototypes/critters). Two kinds of creature, both rigid-part rigs
// like the game's own models (one matrix per part):
//  * Miniatures of the game's own creatures, loaded at runtime from the game
//    archives (figures.res models and clips, textures.res skins): rat, snow
//    mouse, spider, frog, the crawler larva and the cave bat. Parts are
//    decimated by a silhouette-preserving quadric collapse to the
//    prototype's pass-3 budgets (larva undecimated). Clips are baked once
//    into a GPU matrix buffer; the larva's peristaltic crawl and burrow run
//    in the vertex shader.
//  * Procedural rigs for what figures.res lacks (birds, lizard, snake,
//    scolopendra, dung beetle and ball, shore crab): rigid smooth parts with
//    authored UVs and vertex normals on painted slices over game-skin grain.
//
// Simulation: a small CPU state (at most two critters, one ambient
// flock and two carrion flocks of up to eight birds, nearby tracked corpses),
// stepped by the game clock (frozen while paused, faster at fast speed).
// Besides the time-based director, the world holds critter spots: at most one
// per 32 m terrain sector, fixed by the map (sector_spot), biased by the
// sector's game soundscape (sector_sound) and kept clear of campfires. A spot
// is found occupied as it approaches the view edge, so the player runs into
// creatures already there; it rests on game time after they were disturbed.
// Director events and residents share one sparse on-screen budget.
// Per frame the CPU writes only instance roots and the procedural part
// matrices of the live critters into small dynamic buffers; every mesh,
// clip and skin is GPU-resident and the CPU build data is freed after upload.
//
// Pass: opaque world colour in flush_native_static after the ground cover,
// depth-tested and depth-writing, lit by the shared world light and point
// lights, receives the sun cascades, writes the AO ambient share and takes the
// border fog and world atmosphere. Fading uses a screen-door dither. Casts no
// shadows.
#include "critters.h"
#include "ambient_occlusion.h"
#include "owned_asset_loader.h"
#include "owned_resource_runtime.h"
#include "owned_resource_store.h"
#include "renderer.h"
#include "engine_bridge.h"
#include "world_fog.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <limits>
#include <future>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace owned {
namespace {
using namespace critters;

constexpr double kSecondsPerSegment = .055;   // the ambient particles' game-clock scale
constexpr float TAU = 6.28318531f, PI = 3.14159265f;

constexpr char kCritterShader[] = R"(
struct CrVertex{float3 p;uint bone;float2 uv;float s;uint flags;};
struct CrInstance{float4 r0;float4 r1;float4 r2;uint4 a;float4 b;float4 c;};
struct CrBone{float4 r0;float4 r1;float4 r2;};
cbuffer CrFrame:register(b11){
 row_major float4x4 crClip;float4 crEye;float4 crFogRange;float4 crFogControl;float4 crFogColor;float4 crSwitches;
 float4 crLarva0;float4 crLarva1;float4 crLarva2;
 float4 crFogSeam;float4 crFogCurve;float4 crFogSpanLo;float4 crFogSpanHi; // neighbour seam fog (neighbour_fog.h)
};
// Draw without vertex buffers: SV_VertexID starts at 0 whatever the
// StartVertexLocation, so each mesh range passes its first vertex here.
cbuffer CrDraw:register(b12){uint crBase;uint crFirst;uint2 crDrawPadding;};
StructuredBuffer<CrVertex> crVertices:register(t27);
StructuredBuffer<CrInstance> crInstances:register(t28);
StructuredBuffer<CrBone> crBones:register(t29);
StructuredBuffer<CrBone> crBaked:register(t30);
Texture2DArray crSkins:register(t31);
SamplerState crWrap:register(s8);
float3 crApply(CrBone b,float3 p){float4 h=float4(p,1);return float3(dot(b.r0,h),dot(b.r1,h),dot(b.r2,h));}
struct CrOut{float4 p:SV_Position;float3 wp:TEXCOORD0;float2 uv:TEXCOORD1;nointerpolation uint slice:TEXCOORD2;
 nointerpolation float alpha:TEXCOORD3;float border:TEXCOORD4;float3 n:TEXCOORD5;};
// Smooth parts carry an octahedral normal (flags bit 1, 2 x 12 bits).
float3 crNormal(uint f){float2 e=float2((f>>8)&4095u,(f>>20)&4095u)/4095.0*2-1;float3 n=float3(e,1-abs(e.x)-abs(e.y));
 if(n.z<0)n.xy=(1-abs(n.yx))*float2(n.x>=0?1:-1,n.y>=0?1:-1);return normalize(n);}
CrOut crVS(uint vid:SV_VertexID,uint iid:SV_InstanceID){
 CrVertex v=crVertices[crFirst+vid];CrInstance inst=crInstances[crBase+iid];
 uint mode=inst.a.y&255u;float3 lp;float3 ln=0;
 if(mode==0u){CrBone b=crBones[inst.a.x+v.bone];lp=crApply(b,v.p);
  if((v.flags&2u)!=0u){float3 n=crNormal(v.flags);ln=float3(dot(b.r0.xyz,n),dot(b.r1.xyz,n),dot(b.r2.xyz,n));}}
 else if(mode==1u){
  lp=lerp(crApply(crBaked[inst.a.x+v.bone],v.p),crApply(crBaked[inst.a.z+v.bone],v.p),inst.b.y);
  if((v.flags&4u)!=0u){   // vertex morph on top of the skeleton (bat membrane): baked model-space offsets
   const uint plot=asuint(v.s);
   lp+=lerp(crBaked[asuint(inst.c.x)+plot].r0.xyz,crBaked[asuint(inst.c.y)+plot].r0.xyz,inst.b.y);
  }
 }
 else{
  // Larva: mandibles from the crawler's idle clip while it casts about, then
  // the peristaltic wave tail to head (prototype critters.py larva_mesh).
  float3 w=v.p;const float search=inst.b.w;
  if((v.flags&1u)!=0u&&search>.01){
   float3 j=lerp(crApply(crBaked[inst.a.w],v.p),crApply(crBaked[asuint(inst.c.z)],v.p),inst.c.y);
   w+=(j-w)*search;
  }
  const float L=crLarva0.x,D=crLarva0.y,h=crLarva0.z,zc=crLarva0.w;
  float a=saturate((frac(inst.b.z)*(1+h)-v.s)/h);float g=a*a*(3-2*a);
  float r=rsqrt(max(1-D*6*a*(1-a)/(L*h),.05));
  w.x*=r;w.z=zc+(w.z-zc)*r;w.y+=D*g;w.z+=crLarva1.x*L*sin(3.14159265*g);
  float head=saturate((v.s-.5)/.5);head*=head;
  float sway=(crLarva1.y*(1-search)+crLarva1.z*search)*L*sin(6.2831853*inst.c.x/crLarva2.x);
  w.x+=sway*head;w.z+=crLarva1.w*L*search*head;lp=w;
 }
 CrOut o;
 float4 h=float4(lp,1);
 o.wp=float3(dot(inst.r0,h),dot(inst.r1,h),dot(inst.r2,h));
 o.p=mul(crClip,float4(o.wp,1));o.uv=v.uv;o.slice=inst.a.y>>8;o.alpha=inst.b.x;
 o.n=float3(dot(inst.r0.xyz,ln),dot(inst.r1.xyz,ln),dot(inst.r2.xyz,ln));
 o.border=1;
 if(crFogControl.y!=0&&crFogControl.x>0){
  o.border=1-floor(borderFogDensity(o.wp.xy,crFogRange,crFogControl.x,crFogSeam,crFogCurve,crFogSpanLo,crFogSpanHi)*255)/255;
 }
 return o;
}
struct CrPixel{float4 color:SV_Target0;float4 shadow:SV_Target1;float4 ambient:SV_Target2;};
CrPixel crPS(CrOut i){
 // Fade in/out: screen-door dither (4x4 Bayer), the pass stays opaque. A
 // negative alpha takes the complementary pixels (two meshes swapping).
 if(i.alpha<.999){
  uint2 q=uint2(i.p.xy)&3u;
  const float bayer[16]={0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
  const float t=(bayer[q.y*4+q.x]+.5)/16;
  clip(i.alpha>=0?i.alpha-t:t-(1+i.alpha));
 }
 float3 positionDx=ddx(i.wp),positionDy=ddy(i.wp);
 // Smooth parts use their interpolated normal; the rest are flat facets as
 // the prototype. Two-sided: the normal faces the eye.
 float3 n=dot(i.n,i.n)>1e-12?i.n:cross(positionDy,positionDx);
 n=dot(n,n)>1e-20?normalize(n):float3(0,0,1);
 if(dot(n,crEye.xyz-i.wp)<0)n=-n;
 float3 alb=crSkins.Sample(crWrap,float3(i.uv,i.slice)).rgb;
 float3 sun=dot(nativeWorldSunDirection.xyz,nativeWorldSunDirection.xyz)>1e-12?normalize(nativeWorldSunDirection.xyz):float3(0,0,1);
 float facing=saturate(dot(n,sun));
 float visibility=1;
 [branch]if(all(isfinite(positionDx))&&all(isfinite(positionDy))&&(shadowControl.x!=0||shadowBias[4].z>.5))
  visibility=nativeShadowVisibility(i.wp+n*.02,positionDx,positionDy,i.p.xy,false); // no history here
 float3 local=0,glow=0;
 uint2 lightRange=nativePointLightRange(i.wp);
 [loop]for(uint k=lightRange.x;k<lightRange.y;++k){
  NativePointLight light=nativePointLights[nativePointLightGrid[k]];
  float3 delta=light.centerInverseRadiusSquared.xyz-i.wp;float d2=dot(delta,delta);
  float reachLight=1-d2*light.centerInverseRadiusSquared.w;if(reachLight<=0)continue;
  float f=saturate(dot(n,delta*rsqrt(max(d2,1e-12))));
  if(light.color.w<0){glow+=light.color.rgb*(reachLight*reachLight*(f*.6+.4));continue;} // emissive surfaces
  local+=light.color.rgb*(reachLight*reachLight*f*(light.color.w>0?.65:1));
 }
 CrPixel result;
 if(visibility<0){result.color=float4(1,0,1,1);result.shadow=float4(1,0,0,1);result.ambient=0;return result;}
 float3 directional=nativeWorldSunDiffuse.rgb*facing*visibility;
 float3 c=alb*(saturate(max(nativeWorldAmbient.rgb,directional)+local)+glow);
 float3 ambientShare=max(c-alb*(saturate(directional+local)+glow),0);
 if(crSwitches.y>0){
  float f=nativeFogFactor(i.border,i.p);c=lerp(borderFogColor(i.p,crFogColor.rgb),c,f);
  ambientShare*=f*nativeAtmosphere(c,i.p,crFogColor.rgb);
 }
 result.color=float4(c,1);result.shadow=float4(visibility,0,0,1);result.ambient=float4(ambientShare,1);
 return result;
}
)";

// ---------------------------------------------------------------- math
struct V3 { float x = 0, y = 0, z = 0; };
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot3(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross3(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float len3(V3 a) { return std::sqrt(dot3(a, a)); }

struct M3 { float m[3][3]{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}; };
V3 operator*(const M3& r, V3 v) {
    return {r.m[0][0] * v.x + r.m[0][1] * v.y + r.m[0][2] * v.z, r.m[1][0] * v.x + r.m[1][1] * v.y + r.m[1][2] * v.z,
            r.m[2][0] * v.x + r.m[2][1] * v.y + r.m[2][2] * v.z};
}
M3 operator*(const M3& a, const M3& b) {
    M3 r;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
    return r;
}
M3 rot_x(float a) { const float c = std::cos(a), s = std::sin(a); M3 r; r.m[1][1] = c; r.m[1][2] = -s; r.m[2][1] = s; r.m[2][2] = c; return r; }
M3 rot_y(float a) { const float c = std::cos(a), s = std::sin(a); M3 r; r.m[0][0] = c; r.m[0][2] = s; r.m[2][0] = -s; r.m[2][2] = c; return r; }
M3 rot_z(float a) { const float c = std::cos(a), s = std::sin(a); M3 r; r.m[0][0] = c; r.m[0][1] = -s; r.m[1][0] = s; r.m[1][1] = c; return r; }
M3 columns(V3 a, V3 b, V3 c) {
    M3 r;
    r.m[0][0] = a.x; r.m[1][0] = a.y; r.m[2][0] = a.z;
    r.m[0][1] = b.x; r.m[1][1] = b.y; r.m[2][1] = b.z;
    r.m[0][2] = c.x; r.m[1][2] = c.y; r.m[2][2] = c.z;
    return r;
}

struct Bone { float r[3][4]; };
static_assert(sizeof(Bone) == 48, "Bone must match CrBone");
Bone affine(const M3& m, V3 t) {
    Bone b{};
    const float tv[3]{t.x, t.y, t.z};
    for (int i = 0; i < 3; ++i) { for (int j = 0; j < 3; ++j) b.r[i][j] = m.m[i][j]; b.r[i][3] = tv[i]; }
    return b;
}
V3 apply(const Bone& b, V3 p) {
    return {b.r[0][0] * p.x + b.r[0][1] * p.y + b.r[0][2] * p.z + b.r[0][3],
            b.r[1][0] * p.x + b.r[1][1] * p.y + b.r[1][2] * p.z + b.r[1][3],
            b.r[2][0] * p.x + b.r[2][1] * p.y + b.r[2][2] * p.z + b.r[2][3]};
}

// Quaternions w,x,y,z with the supported hierarchy's conventions
// (owned_visual_evaluation.cpp: multiply is the Hamilton product, a node
// composes its local rotation on the left, points rotate by q p q*).
using Q4 = std::array<float, 4>;
Q4 qmul(const Q4& a, const Q4& b) {
    return {a[0] * b[0] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3], a[0] * b[1] + a[1] * b[0] + a[2] * b[3] - a[3] * b[2],
            a[0] * b[2] - a[1] * b[3] + a[2] * b[0] + a[3] * b[1], a[0] * b[3] + a[1] * b[2] - a[2] * b[1] + a[3] * b[0]};
}
V3 qrot(const Q4& q, V3 p) {
    const Q4 v = qmul(qmul(q, {0, p.x, p.y, p.z}), {q[0], -q[1], -q[2], -q[3]});
    return {v[1], v[2], v[3]};
}
M3 qmatrix(const Q4& q) { return columns(qrot(q, {1, 0, 0}), qrot(q, {0, 1, 0}), qrot(q, {0, 0, 1})); }

// ---------------------------------------------------------------- GPU layouts
struct CrVertex { float p[3]; std::uint32_t bone; float uv[2]; float s; std::uint32_t flags; };
static_assert(sizeof(CrVertex) == 32, "CrVertex must match the HLSL layout");
struct CrInstance { float r[3][4]; std::uint32_t a[4]; float b[4]; float c[4]; };
static_assert(sizeof(CrInstance) == 96, "CrInstance must match the HLSL layout");
struct CrFrame {
    float clip[16]; float eye[4], fog_range[4], fog_control[4], fog_color[4], switches[4], larva0[4], larva1[4], larva2[4];
    float fog_seam[4], fog_curve[4], fog_span_lo[4], fog_span_hi[4];
};
static_assert(sizeof(CrFrame) % 16 == 0);

// ---------------------------------------------------------------- meshes
enum Mesh : std::uint8_t {
    m_rat = 0, m_snowmouse, m_spider, m_frog, m_larva, m_bat,
    m_lizard, m_snake, m_crab, m_scolopendra, m_dung, m_ball,
    m_bird, m_corvid, m_bird_far, m_vulture, m_vulture_perch, m_count,
};
enum Slice : std::uint8_t {
    s_rat = 0, s_snowmouse, s_spider, s_frog, s_larva, s_bat, s_lizard, s_snake, s_finch, s_sparrow, s_bunting, s_snowbird,
    s_crow, s_raven, s_vulture, s_crab, s_dung, s_scolopendra, s_count,
};
constexpr unsigned kSkin = 128;

// Bird variants: painted slice and the near / far meshes (lengths: bird_length).
struct BirdLook { Slice slice; Mesh near_mesh, far_mesh; };
const BirdLook& bird_look(Bird b) {
    static const BirdLook looks[bird_count] = {{s_finch, m_bird, m_bird_far}, {s_sparrow, m_bird, m_bird_far},
        {s_bunting, m_bird, m_bird_far}, {s_snowbird, m_bird, m_bird_far}, {s_crow, m_corvid, m_bird_far},
        {s_raven, m_corvid, m_bird_far}, {s_vulture, m_vulture, m_vulture}, {s_bat, m_bat, m_bat}};
    return looks[b < bird_count ? b : 0];
}

// Procedural pose parameters (prototype Pose.p; birds: bird_model).
struct PoseP {
    float t = 0, gait = 0, speed = 0, pushup = 0, peck = 0, caw = 0, wing = 0, soar = 0, claw = 0, push = 0;
    float fold = 1, crouch = 0, tail = 0, look = 0;
};

// A rig is evaluated twice with the same structure: once at load in a rest
// pose to record canonical pieces and the prototype's per-part planar UVs,
// then per frame to emit only the part matrices.
// A smooth part: triangle soup in its own rigid frame with authored UVs
// (0..1 of its atlas tile, or of the whole slice for region -1) and a
// smoothing group per surface; normals are averaged within a group.
struct PartMesh {
    std::vector<V3> p;
    std::vector<std::array<float, 2>> uv;
    std::vector<int> smooth;   // per triangle
    std::vector<std::uint8_t> sub;   // optional per-vertex bone of a multi-bone part
    std::vector<V3> n;               // optional authored normals (else smoothed per group)
    void tri(V3 a, V3 b, V3 c, std::array<float, 2> ta, std::array<float, 2> tb, std::array<float, 2> tc, int g) {
        p.insert(p.end(), {a, b, c}); uv.insert(uv.end(), {ta, tb, tc}); smooth.push_back(g);
    }
};
// Octahedral unit normal in 2 x 12 bits (flags bit 1 marks it present).
std::uint32_t pack_normal(V3 n) {
    const float l = std::abs(n.x) + std::abs(n.y) + std::abs(n.z);
    if (!(l > 1e-12f)) return 0;
    float x = n.x / l, y = n.y / l;
    if (n.z < 0) {
        const float ox = (1 - std::abs(y)) * (x >= 0 ? 1.f : -1.f), oy = (1 - std::abs(x)) * (y >= 0 ? 1.f : -1.f);
        x = ox; y = oy;
    }
    const auto q = [](float v) { return std::uint32_t(std::lround(std::clamp(v * .5f + .5f, 0.f, 1.f) * 4095)); };
    return 2u | (q(x) << 8) | (q(y) << 20);
}
std::vector<std::uint32_t> smooth_normals(const PartMesh& m) {
    std::map<std::array<long long, 4>, V3> sum;
    const auto key = [&](std::size_t i) {
        return std::array<long long, 4>{std::llround(m.p[i].x * 1e5), std::llround(m.p[i].y * 1e5), std::llround(m.p[i].z * 1e5),
                                        m.smooth[i / 3]};
    };
    for (std::size_t t = 0; t + 2 < m.p.size(); t += 3) {
        const V3 n = cross3(m.p[t + 1] - m.p[t], m.p[t + 2] - m.p[t]);   // area weighted
        for (int c = 0; c < 3; ++c) { V3& s = sum[key(t + c)]; s = s + n; }
    }
    std::vector<std::uint32_t> out(m.p.size());
    for (std::size_t i = 0; i < m.p.size(); ++i) out[i] = pack_normal(sum[key(i)]);
    return out;
}

struct Rig {
    std::vector<Bone>* bones = nullptr;
    bool record = false;
    struct RecordVertex { V3 p; std::uint32_t bone; int group; bool authored = false; float u = 0, v = 0; std::uint32_t normal = 0; };
    std::vector<RecordVertex> vertices;
    std::vector<int> regions;
    int group = -1;
    std::uint32_t base = 0;
    std::uint32_t bone(const Bone& b) { bones->push_back(b); return std::uint32_t(bones->size() - 1 - base); }
    void begin(int region) { ++group; if (record) regions.push_back(region); }
    void emit(V3 p, std::uint32_t b) { if (record) vertices.push_back({p, b, group}); }
    // One rigid smooth part; build() runs only while recording.
    template <class Build> void part(const Bone& b, int region, Build build) { part(&b, 1, region, build); }
    // A part over several rigid bones (per-vertex sub index), e.g. a game figure hinged at its joints.
    template <class Build> void part(const Bone* b, int count, int region, Build build) {
        begin(region);
        const auto id = bone(b[0]);
        for (int k = 1; k < count; ++k) bone(b[k]);
        if (!record) return;
        const PartMesh m = build();
        std::vector<std::uint32_t> normals;
        if (m.n.size() == m.p.size()) for (const V3& v : m.n) normals.push_back(pack_normal(v));
        else normals = smooth_normals(m);
        for (std::size_t i = 0; i < m.p.size(); ++i)
            vertices.push_back({m.p[i], id + (m.sub.empty() ? 0u : m.sub[i]), group, true, m.uv[i][0], m.uv[i][1], normals[i]});
    }
    const struct StatueBird* statue = nullptr;   // the vulture's game figure (vulture_fly / vulture_perch)
};

void limb(Rig& rig, V3 p0, V3 p1, float r0, float r1, int seg, int region) {
    rig.begin(region);
    V3 ax = p1 - p0;
    const float L = len3(ax);
    ax = L > 1e-9f ? ax * (1 / L) : V3{0, 0, 1};
    const V3 t = std::abs(ax.z) < .9f ? V3{0, 0, 1} : V3{1, 0, 0};
    V3 u = cross3(ax, t); u = u * (1 / std::max(len3(u), 1e-12f));
    const V3 w = cross3(ax, u);
    const auto a = rig.bone(affine(columns(u * r0, ax * L, w * r0), p0));
    const auto b = rig.bone(affine(columns(u * r1, ax * L, w * r1), p0));
    if (!rig.record) return;
    for (int k = 0; k < seg; ++k) {
        const int k2 = (k + 1) % seg;
        const float c0 = std::cos(TAU * k / seg), s0 = std::sin(TAU * k / seg), c1 = std::cos(TAU * k2 / seg), s1 = std::sin(TAU * k2 / seg);
        rig.emit({c0, 0, s0}, a); rig.emit({c1, 0, s1}, a); rig.emit({c1, 1, s1}, b);
        rig.emit({c0, 0, s0}, a); rig.emit({c1, 1, s1}, b); rig.emit({c0, 1, s0}, b);
    }
}
// Triangles with independently moving corners: each is the image of the
// unit basis under its own matrix.
void triangles(Rig& rig, std::initializer_list<V3> corners, int region, V3 offset = {}) {
    rig.begin(region);
    const V3* p = corners.begin();
    for (std::size_t k = 0; k + 2 < corners.size(); k += 3) {
        const auto b = rig.bone(affine(columns(p[k] + offset, p[k + 1] + offset, p[k + 2] + offset), {}));
        rig.emit({1, 0, 0}, b); rig.emit({0, 1, 0}, b); rig.emit({0, 0, 1}, b);
    }
}

void legs(Rig& rig, const V3* hips, const V3* feet, float gait, float speed, const float* phases, float lift, float stride,
          float r, int region, float knee_up, int seg) {
    for (int k = 0; k < 4; ++k) {
        const float s = std::sin(TAU * (gait + phases[k])), c = std::cos(TAU * (gait + phases[k]));
        V3 f = feet[k];
        f.y += stride * speed * s;
        f.z += lift * speed * std::max(0.f, c);
        const V3 knee = (hips[k] + f) * .5f + V3{0, 0, knee_up};
        limb(rig, hips[k], knee, r, r * .8f, seg, region);
        limb(rig, knee, f, r * .8f, r * .4f, seg, region);
    }
}

// ---- smooth part building blocks
// A loft section: centre (y, zc) and half axes; u is the texture coordinate
// along the part. Rings start at the belly and wind over the back, so both
// flanks share one painted strip (v 0 belly .. vmax back).
struct Section { float y, rx, rz, zc, u; };
using UV = std::array<float, 2>;
void loft(PartMesh& m, const std::vector<Section>& s, int around, float v0, float v1, int g, bool cap_back, bool cap_front,
          float x0 = 0) {
    const auto ring = [&](const Section& c, int k) {
        const float th = TAU * k / around;
        return V3{x0 + c.rx * std::sin(th), c.y, c.zc - c.rz * std::cos(th)};
    };
    const auto vv = [&](int k) {
        const float th = TAU * float(k % around) / around, f = th <= PI ? th / PI : (TAU - th) / PI;
        return v0 + (v1 - v0) * f;
    };
    for (std::size_t i = 0; i + 1 < s.size(); ++i)
        for (int k = 0; k < around; ++k) {
            const V3 a = ring(s[i], k), b = ring(s[i], k + 1), c = ring(s[i + 1], k + 1), d = ring(s[i + 1], k);
            const UV ta{s[i].u, vv(k)}, tb{s[i].u, vv(k + 1)}, tc{s[i + 1].u, vv(k + 1)}, td{s[i + 1].u, vv(k)};
            m.tri(a, b, c, ta, tb, tc, g); m.tri(a, c, d, ta, tc, td, g);
        }
    const auto cap = [&](const Section& c, V3 tip, float u, bool reverse) {
        for (int k = 0; k < around; ++k) {
            const V3 a = ring(c, k), b = ring(c, k + 1);
            const UV ta{c.u, vv(k)}, tb{c.u, vv(k + 1)}, tt{u, .5f * (vv(k) + vv(k + 1))};
            if (reverse) m.tri(b, a, tip, tb, ta, tt, g); else m.tri(a, b, tip, ta, tb, tt, g);
        }
    };
    if (cap_back) { const Section& c = s.front(); cap(c, {x0, c.y - .35f * std::min(c.rx, c.rz), c.zc}, c.u, true); }
    if (cap_front) { const Section& c = s.back(); cap(c, {x0, c.y + .35f * std::min(c.rx, c.rz), c.zc}, c.u, false); }
}
// A thin cambered surface (wing, tail fan) over a span grid: leading edge
// le(x) and trailing edge te(x) along local y, thickness th(x). Top and
// bottom are separate smoothing groups joined at both edges; side mirrors x.
template <class Edge>
void airfoil(PartMesh& m, const std::vector<float>& xs, Edge edge, float side, UV top0, UV top1, UV bot0, UV bot1, int g) {
    static const float cs[4]{0, .22f, .58f, 1};
    const auto point = [&](float x, int c, bool top) {
        float le, te, th; edge(x, le, te, th);
        const float f = cs[c], y = le + (te - le) * f;
        const float hump = std::sin(PI * std::pow(f, .55f));
        return V3{side * x, y, top ? th * hump : -.3f * th * hump};
    };
    const float span = std::max(xs.back(), 1e-6f);
    for (std::size_t i = 0; i + 1 < xs.size(); ++i)
        for (int c = 0; c < 3; ++c)
            for (int top = 0; top < 2; ++top) {
                const UV a0 = top ? top0 : bot0, a1 = top ? top1 : bot1;
                const auto t = [&](float x, int cc) { return UV{a0[0] + (a1[0] - a0[0]) * x / span, a0[1] + (a1[1] - a0[1]) * cs[cc]}; };
                const V3 p00 = point(xs[i], c, top), p10 = point(xs[i + 1], c, top), p11 = point(xs[i + 1], c + 1, top), p01 = point(xs[i], c + 1, top);
                const UV t00 = t(xs[i], c), t10 = t(xs[i + 1], c), t11 = t(xs[i + 1], c + 1), t01 = t(xs[i], c + 1);
                m.tri(p00, p10, p11, t00, t10, t11, g + top); m.tri(p00, p11, p01, t00, t11, t01, g + top);
            }
}

// ---- birds
// A bird is a set of rigid smooth parts like the game's own figures (one
// matrix per part): body, head with upper mandible, lower mandible, tail,
// arm and hand of each wing, legs. Unit length (beak tip to tail tip),
// forward +Y, feet on z=0; the instance root applies the variant's length
// and display scale. Painted UV layout of a bird slice (bird_paint): body
// u 0-.6 / head u .6-1 over v 0-.5 (belly to back), wing top v .5-.75 (arm
// u 0-.4, hand .4-1; leading to trailing edge), wing underside v .75-.875,
// tail u 0-.5 / beak .5-.75 / legs .75-1 over v .875-1.
struct BirdShape {
    float body_z, body_w, body_h;
    float head_y, head_z, head_r, neck, beak, beak_r, hook;
    float shoulder_x, shoulder_y, shoulder_z;
    float arm, hand, chord, hand_chord;
    int fingers; float finger_depth;
    float tail_y, tail, tail_w;
    float leg, leg_x, leg_y;
    float ground_pitch;
};
enum BirdKind : std::uint8_t { bk_small = 0, bk_corvid };
const BirdShape& bird_shape(BirdKind k) {
    static const BirdShape shapes[2] = {
        // finch / sparrow / bunting / snow bunting: round body, short conical beak, rounded wings
        {.24f, .115f, .11f, .19f, .33f, .078f, 0, .10f, .032f, 0, .085f, .10f, .32f, .28f, .42f, .17f, .15f, 0, 0,
         -.20f, .32f, .09f, .16f, .04f, .0f, .32f},
        // crow / raven: longer heavier beak, longer fingered wings, wedge tail
        {.22f, .11f, .10f, .19f, .31f, .072f, 0, .15f, .036f, .2f, .08f, .10f, .30f, .36f, .50f, .19f, .17f, 5, .3f,
         -.20f, .30f, .10f, .17f, .04f, .0f, .22f},
    };
    return shapes[k];
}

PartMesh bird_body(const BirdShape& b, int around, bool coarse) {
    // Egg-shaped body: (y, half width, half height, centre lift) fractions.
    static const float f[7][4]{{-.22f, .26f, .25f, 0}, {-.15f, .65f, .64f, 0}, {-.06f, .91f, .91f, .005f},
                               {.03f, 1, 1, .015f}, {.11f, .91f, .95f, .03f}, {.17f, .70f, .77f, .05f}, {.21f, .48f, .55f, .075f}};
    std::vector<Section> s;
    for (int i = 0; i < 7; ++i) {
        if (coarse && (i == 2 || i == 4)) continue;
        s.push_back({f[i][0], f[i][1] * b.body_w, f[i][2] * b.body_h, b.body_z + f[i][3] * b.body_h / .11f, .6f * (f[i][0] + .22f) / .43f});
    }
    PartMesh m; loft(m, s, around, 0, .5f, 0, true, true);
    return m;
}
// Head in its own frame (origin = neck pivot): optional bare neck, skull and
// upper mandible.
PartMesh bird_head(const BirdShape& b, int around, bool coarse) {
    const float r = b.head_r / .078f, ny = b.neck, nz = b.neck * .4f;
    std::vector<Section> s;
    if (b.neck > 0) {
        s.push_back({-.03f, .5f * b.head_r, .5f * b.head_r, 0, .6f});
        s.push_back({ny * .5f, .42f * b.head_r, .42f * b.head_r, nz * .55f, .66f});
    }
    static const float f[6][4]{{-.03f, .05f, .05f, 0}, {.02f, .065f, .066f, .012f}, {.06f, .078f, .075f, .02f},
                               {.10f, .07f, .068f, .02f}, {.14f, .045f, .045f, .012f}, {.155f, .03f, .03f, .008f}};
    const float u0 = b.neck > 0 ? .7f : .6f;
    for (int i = b.neck > 0 ? 1 : 0; i < 6; ++i) {
        if (coarse && (i == 1 || i == 3)) continue;
        s.push_back({ny + f[i][0] * r, f[i][1] * r, f[i][2] * r, nz + f[i][3] * r, u0 + (1 - u0) * (f[i][0] + .03f) / .185f});
    }
    PartMesh m; loft(m, s, around, 0, .5f, 0, true, false);
    // upper mandible: from inside the skull front to the (hooked) tip
    const float y0 = ny + .13f * r, z0 = nz + .02f * r, L = b.beak, br = b.beak_r;
    std::vector<Section> beak{{y0, br, .75f * br, z0 + .2f * br, .5f}, {y0 + .5f * L, .55f * br, .45f * br, z0 + .05f * br - .3f * b.hook * br, .62f},
                              {y0 + L, .08f * br, .08f * br, z0 - .2f * br - 1.2f * b.hook * br, .75f}};
    loft(m, beak, coarse ? 4 : 6, .875f, 1, 1, false, true);
    return m;
}
PartMesh bird_lower_beak(const BirdShape& b, bool coarse) {
    const float L = .85f * b.beak, br = b.beak_r;
    std::vector<Section> s{{0, .85f * br, .45f * br, -.35f * br, .5f}, {.5f * L, .45f * br, .25f * br, -.3f * br, .62f},
                           {L, .06f * br, .05f * br, -.25f * br, .75f}};
    PartMesh m; loft(m, s, coarse ? 4 : 6, .875f, 1, 0, false, true);
    return m;
}
PartMesh bird_tail(const BirdShape& b, bool coarse) {
    PartMesh m;
    std::vector<float> xs{0, .5f, 1};
    if (!coarse) xs = {0, .25f, .5f, .75f, 1};
    // airfoil along the tail's length (x), width as the chord: a fan that
    // widens to tail_w, tip slightly notched for the small birds
    const float L = b.tail, w = b.tail_w;
    airfoil(m, xs, [&](float x, float& le, float& te, float& th) {
        const float t = x;
        le = .45f * w * (.4f + .6f * t); te = -le; th = .02f * (1 - .6f * t);
    }, 1, {0, .875f}, {.5f, 1}, {0, .875f}, {.5f, 1}, 0);
    // the airfoil runs along +x; turn it to run back along -y
    for (auto& p : m.p) p = V3{p.y, -p.x * L, p.z};
    return m;
}
// One wing: arm (shoulder to wrist) or hand (wrist to tip), span along
// side*x, leading edge +y.
PartMesh bird_wing(const BirdShape& b, bool hand, float side, bool coarse) {
    PartMesh m;
    std::vector<float> xs;
    if (!hand) {
        const float L = b.arm;
        xs = coarse ? std::vector<float>{0, L} : std::vector<float>{0, .5f * L, L};
        airfoil(m, xs, [&](float x, float& le, float& te, float& th) {
            const float t = x / L;
            le = .045f * b.chord / .17f * (1 - .2f * t); te = -b.chord * (1 - .12f * t); th = .028f * b.chord / .17f * (1 - .35f * t);
        }, side, {0, .5f}, {.4f, .75f}, {0, .75f}, {.4f, .875f}, 0);
        return m;
    }
    const float L = b.hand;
    const int n = b.fingers;
    xs = {0, .2f * L, .4f * L};
    if (n > 0 && !coarse) { for (int k = 1; k <= 4 * n; ++k) xs.push_back(L * (.4f + .6f * k / (4.f * n))); }
    else if (coarse) xs = {0, .45f * L, .8f * L, L};
    else for (float t : {.55f, .7f, .82f, .92f, 1.f}) xs.push_back(t * L);
    airfoil(m, xs, [&](float x, float& le, float& te, float& th) {
        const float t = std::min(x / L, 1.f);
        const float width = b.hand_chord * (1 - .35f * t) * std::sqrt(std::max(0.f, 1 - std::pow(t, 6.f))) + .002f;
        le = .04f * b.hand_chord / .15f * (1 - t * t) - .35f * b.hand_chord * t * t;
        float w = width;
        if (n > 0 && t > .4f) {   // slotted primaries: the trailing edge steps in between feather tips
            const float k = std::abs(std::sin(PI * n * (t - .4f) / .6f));
            w *= 1 - b.finger_depth * (1 - std::sqrt(k)) * std::min(1.f, (t - .4f) * 4);
        }
        te = le - w; th = .022f * b.hand_chord / .15f * (1 - .7f * t);
    }, side, {.4f, .5f}, {1, .75f}, {.4f, .75f}, {1, .875f}, 0);
    return m;
}
PartMesh bird_leg(const BirdShape& b, bool coarse) {
    PartMesh m;
    const float L = b.leg, r0 = .012f * L / .16f, r1 = .008f * L / .16f;
    const int around = coarse ? 3 : 4;
    std::vector<Section> s{{0, r0, r0, 0, .75f}, {L, r1, r1, 0, 1}};
    loft(m, s, around, .875f, 1, 0, false, false);
    for (auto& p : m.p) p = V3{p.x, -p.z, -p.y};   // the loft runs down from the hip
    if (coarse) return m;
    const V3 foot{0, 0, -L};
    const float toe = .45f * L, w = .006f * L / .16f;
    for (const float a : {-.45f, 0.f, .45f, PI}) {
        const float len = a == PI ? .55f * toe : toe;
        const V3 d{std::sin(a) * len, std::cos(a) * len, 0}, sd{std::cos(a) * w, -std::sin(a) * w, 0};
        const UV t0{.8f, .9f}, t1{1, .95f};
        m.tri(foot + sd, foot + d, foot - sd, t0, t1, t0, 1);
        m.tri(foot - sd + V3{0, 0, w}, foot + d, foot + sd + V3{0, 0, w}, t0, t1, t0, 2);
    }
    return m;
}

// Rigid transforms and their blend (folded <-> spread wings).
struct Xf { M3 r; V3 t; };
Xf operator*(const Xf& a, const Xf& b) { return {a.r * b.r, a.r * b.t + a.t}; }
Xf xf(V3 t, const M3& r = M3{}) { return {r, t}; }
Q4 qfrom(const M3& m) {
    const float tr = m.m[0][0] + m.m[1][1] + m.m[2][2];
    Q4 q;
    if (tr > 0) { const float s = std::sqrt(tr + 1) * 2; q = {.25f * s, (m.m[2][1] - m.m[1][2]) / s, (m.m[0][2] - m.m[2][0]) / s, (m.m[1][0] - m.m[0][1]) / s}; }
    else if (m.m[0][0] > m.m[1][1] && m.m[0][0] > m.m[2][2]) {
        const float s = std::sqrt(1 + m.m[0][0] - m.m[1][1] - m.m[2][2]) * 2;
        q = {(m.m[2][1] - m.m[1][2]) / s, .25f * s, (m.m[0][1] + m.m[1][0]) / s, (m.m[0][2] + m.m[2][0]) / s};
    } else if (m.m[1][1] > m.m[2][2]) {
        const float s = std::sqrt(1 + m.m[1][1] - m.m[0][0] - m.m[2][2]) * 2;
        q = {(m.m[0][2] - m.m[2][0]) / s, (m.m[0][1] + m.m[1][0]) / s, .25f * s, (m.m[1][2] + m.m[2][1]) / s};
    } else {
        const float s = std::sqrt(1 + m.m[2][2] - m.m[0][0] - m.m[1][1]) * 2;
        q = {(m.m[1][0] - m.m[0][1]) / s, (m.m[0][2] + m.m[2][0]) / s, (m.m[1][2] + m.m[2][1]) / s, .25f * s};
    }
    return q;
}
Xf blend(const Xf& a, const Xf& b, float t) {
    Q4 qa = qfrom(a.r), qb = qfrom(b.r);
    if (qa[0] * qb[0] + qa[1] * qb[1] + qa[2] * qb[2] + qa[3] * qb[3] < 0) for (float& v : qb) v = -v;
    // smoothstep-eased slerp
    float d = std::clamp(qa[0] * qb[0] + qa[1] * qb[1] + qa[2] * qb[2] + qa[3] * qb[3], -1.f, 1.f);
    const float ang = std::acos(d), sn = std::sin(ang);
    const float wa = sn > 1e-4f ? std::sin((1 - t) * ang) / sn : 1 - t, wb = sn > 1e-4f ? std::sin(t * ang) / sn : t;
    Q4 q;
    for (int k = 0; k < 4; ++k) q[k] = wa * qa[k] + wb * qb[k];
    return {qmatrix(q), a.t * (1 - t) + b.t * t};
}
float smooth01(float x) { x = std::clamp(x, 0.f, 1.f); return x * x * (3 - 2 * x); }
// Frame whose local span axis (side*x) points along `span` and top (z) along `up`.
M3 wing_frame(float side, V3 span, V3 up) {
    const auto unit = [](V3 v) { return v * (1 / std::max(len3(v), 1e-9f)); };
    const V3 X = unit(span * side);
    V3 Z = up - X * dot3(up, X); Z = unit(Z);
    return columns(X, cross3(Z, X), Z);
}

// Pose: fold (1 folded on the ground, 0 wings spread), wing (flap cycles),
// soar, peck, caw, crouch (anticipation / landing squash), tail (kick),
// look (head turn), t (age for idle motion).
void bird_model(Rig& rig, const BirdShape& b, const PoseP& p, bool coarse) {
    const int around = coarse ? 6 : 10, head_around = coarse ? 5 : 8;
    const float fold = smooth01(p.fold), spread = 1 - fold, soar = p.soar;
    const float ph = TAU * p.wing, beat = spread * (1 - soar);
    // body: pitched up on the ground, crouch lowers and tips it forward,
    // flapping bobs it (up on the downstroke)
    const V3 hip{0, b.leg_y, b.body_z - .8f * b.body_h};
    const float body_pitch = b.ground_pitch * fold - .3f * p.crouch - .25f * p.peck * fold + .05f * beat * std::cos(ph);
    const float drop = .45f * b.leg * p.crouch;
    const Xf body = xf(hip + V3{0, 0, -drop - .02f * beat * std::cos(ph)}, rot_x(body_pitch)) * xf(V3{} - hip);
    rig.part(affine(body.r, body.t), -1, [&] { return bird_body(b, around, coarse); });
    // head: level against the body pitch, pecks, caws, looks about
    const float look = p.look * fold * (1 - p.peck);
    const Xf head = body * xf({0, b.head_y + .05f * p.peck, b.head_z - .07f * p.peck},
        rot_x(-body_pitch * .7f - 1.05f * p.peck + .5f * p.caw) * rot_z(look));
    rig.part(affine(head.r, head.t), -1, [&] { return bird_head(b, head_around, coarse); });
    const float r = b.head_r / .078f;
    const Xf jaw = head * xf({0, b.neck + .13f * r, b.neck * .4f + .02f * r}, rot_x(-.5f * p.caw));
    rig.part(affine(jaw.r, jaw.t), -1, [&] { return bird_lower_beak(b, coarse); });
    // tail: lifts as the head goes down, kicks on landing, counters the beat
    const float tail_pitch = .12f * fold + .3f * p.peck * fold + p.tail - .12f * beat * std::cos(ph) - .1f * spread;
    const Xf tail = body * xf({0, b.tail_y, b.body_z + .02f}, rot_x(tail_pitch));
    rig.part(affine(tail.r, tail.t), -1, [&] { return bird_tail(b, coarse); });
    // wings: spread pose (flap, wrist lag, hand swept back on the upstroke,
    // glide dihedral when soaring) blended with the folded pose on the flank
    const float amp = .95f;
    const float flap = amp * std::sin(ph) * (1 - soar) + .12f * soar;
    const float hflap = .5f * amp * std::sin(ph - .9f) * (1 - soar) + .06f * soar;
    const float sweep = .12f + .35f * std::max(0.f, std::cos(ph)) * (1 - soar);
    for (const float sx : {-1.f, 1.f}) {
        const Xf arm_spread = xf({sx * b.shoulder_x, b.shoulder_y, b.shoulder_z}, rot_y(-sx * flap) * rot_z(sx * .05f));
        const Xf hand_spread = arm_spread * xf({sx * b.arm, 0, 0}, rot_y(-sx * hflap) * rot_z(-sx * sweep));
        // folded: span back along the upper flank, dorsal side out and up,
        // leading edge low at the bend of the wing, flight feathers over the back
        const M3 folded = wing_frame(sx, {sx * .12f, -1, -.1f}, {sx * .38f, 0, .92f});
        const Xf arm_fold = xf({sx * .4f * b.body_w, b.shoulder_y + .02f, b.body_z + .68f * b.body_h}, folded);
        const Xf hand_fold = xf({sx * .62f * b.body_w, b.shoulder_y + .05f, b.body_z + .62f * b.body_h}, folded);
        const float f = std::clamp(p.fold, 0.f, 1.f);
        const Xf arm = body * blend(arm_spread, arm_fold, smooth01(f * 1.3f));
        const Xf hand = body * blend(hand_spread, hand_fold, smooth01(f * 1.3f - .3f));
        rig.part(affine(arm.r, arm.t), -1, [&] { return bird_wing(b, false, sx, coarse); });
        rig.part(affine(hand.r, hand.t), -1, [&] { return bird_wing(b, true, sx, coarse); });
    }
    // legs: feet on the ground (the leg tilts forward as the hip lowers),
    // tucked back in flight
    for (const float sx : {-1.f, 1.f}) {
        const V3 h = body.r * V3{sx * b.leg_x, b.leg_y, hip.z} + body.t;
        const float reach = std::clamp(h.z / b.leg, -1.f, 1.f);
        const float stand = std::acos(reach), tilt = stand * fold + (-1.35f) * (1 - fold);
        const Xf leg = xf(h, rot_x(tilt));
        rig.part(affine(leg.r, leg.t), -1, [&] { return bird_leg(b, coarse); });
    }
}
void bird_small(Rig& rig, const PoseP& p) { bird_model(rig, bird_shape(bk_small), p, false); }
void bird_corvid(Rig& rig, const PoseP& p) { bird_model(rig, bird_shape(bk_corvid), p, false); }
void bird_far(Rig& rig, const PoseP& p) { bird_model(rig, bird_shape(bk_small), p, true); }

// Shore crab, 10 cm across the legs: a domed carapace wider than long with
// stalked eyes (smooth part), four pairs of three-segment walking legs and
// two chelipeds with a palm, fixed finger and opening dactyl. Walks sideways.
PartMesh crab_shell() {
    PartMesh m;
    const std::vector<Section> shell{{-.021f, .012f, .004f, .015f, 0}, {-.014f, .024f, .008f, .016f, .17f}, {-.004f, .030f, .010f, .016f, .4f},
                                     {.007f, .029f, .0095f, .016f, .66f}, {.015f, .023f, .0065f, .0165f, .85f}, {.019f, .016f, .004f, .0165f, 1}};
    loft(m, shell, 12, 0, 1, 0, true, true);
    for (const float sx : {-1.f, 1.f}) {
        const std::vector<Section> eye{{.016f, .0016f, .0016f, .021f, .9f}, {.021f, .0022f, .0022f, .0225f, 1}};
        loft(m, eye, 5, .6f, 1, 1, false, true, sx * .0075f);
    }
    return m;
}
PartMesh crab_palm() {   // palm and fixed finger, pointing +y from the wrist
    PartMesh m;
    const std::vector<Section> s{{0, .0035f, .003f, 0, 0}, {.006f, .0055f, .0045f, .0004f, .35f}, {.012f, .0042f, .0036f, -.0004f, .65f},
                                 {.020f, .0009f, .0008f, -.0018f, 1}};
    loft(m, s, 8, 0, 1, 0, true, true);
    return m;
}
PartMesh crab_finger() {   // movable dactyl, hinged at the top of the palm front
    PartMesh m;
    const std::vector<Section> s{{0, .0018f, .0016f, 0, 0}, {.005f, .0014f, .0012f, .0004f, .5f}, {.009f, .0004f, .0004f, -.0004f, 1}};
    loft(m, s, 6, 0, 1, 0, true, true);
    return m;
}
void crab(Rig& rig, const PoseP& p) {
    const float run = std::min(p.speed, 1.f);
    const float bz = .0015f * std::sin(TAU * p.gait * 2) * run;
    rig.part(affine(M3{}, {0, 0, bz}), 0, [] { return crab_shell(); });
    for (const float sx : {-1.f, 1.f}) {
        const float wave = p.claw * (.5f + .5f * std::sin(p.t * 4 + sx));   // raised and working while it displays
        const V3 a0{sx * .017f, .013f, .014f + bz}, a1 = a0 + V3{sx * .010f, .009f, .004f + .008f * wave};
        limb(rig, a0, a1, .0028f, .0026f, 4, 1);
        const Xf palm = xf(a1, rot_z(sx * .55f) * rot_x(.3f * wave));
        rig.part(affine(palm.r, palm.t), 3, [] { return crab_palm(); });
        const Xf finger = palm * xf({0, .011f, .003f}, rot_x(.15f + .45f * wave));
        rig.part(affine(finger.r, finger.t), 3, [] { return crab_finger(); });
        const float ys[4]{.009f, .003f, -.004f, -.011f};
        for (int i = 0; i < 4; ++i) {
            const int g = (i + (sx > 0)) % 2;
            const float s = std::sin(TAU * (p.gait + .5f * g)), c = std::cos(TAU * (p.gait + .5f * g));
            const float fan = (i - 1.5f) * .004f;
            const V3 hip{sx * .023f, ys[i], .014f + bz}, knee{sx * .036f, ys[i] + .5f * fan, .025f + bz};
            const V3 ankle{sx * (.046f + .004f * s * run), ys[i] + fan, .012f + .004f * std::max(0.f, c) * run};
            const V3 foot = ankle + V3{sx * .005f, .4f * fan, -.012f + .004f * std::max(0.f, c) * run};
            limb(rig, hip, knee, .0026f, .0021f, 4, 2);
            limb(rig, knee, ankle, .0021f, .0015f, 4, 2);
            limb(rig, ankle, foot, .0015f, .0005f, 3, 1);
        }
    }
}

// ---- spined bodies (lizard, snake, centipede) and the dung beetle
// Body segments are rigid smooth parts between consecutive spine points
// (head first), each a short loft overlapping its neighbours so the bend
// shows no gaps; painted slices (critter_paint) put the body strip on the
// top half (u tail 0 .. head 1, v belly .. back), the accent tile bottom
// left and the legs tile bottom right (atlas regions 2 and 3).
Bone segment_bone(V3 tail, V3 head) {
    V3 d = head - tail;
    const float l = len3(d);
    d = l > 1e-9f ? d * (1 / l) : V3{0, 1, 0};
    V3 x = cross3(d, {0, 0, 1});
    x = len3(x) > 1e-6f ? x * (1 / len3(x)) : V3{1, 0, 0};
    return affine(columns(x, d, cross3(x, d)), tail);
}
void spine(Rig& rig, const V3* pts, const float* r, int n, float flat, int around, float u_tail, float u_head,
           float snout = 0) {
    for (int i = 0; i + 1 < n; ++i) {
        rig.part(segment_bone(pts[i + 1], pts[i]), -1, [&] {
            const float len = len3(pts[i] - pts[i + 1]), lap = .14f * len;
            const float ua = u_tail + (u_head - u_tail) * (1 - float(i + 1) / (n - 1)), ub = u_tail + (u_head - u_tail) * (1 - float(i) / (n - 1));
            const std::vector<Section> s{{-lap, r[i + 1] * .96f, r[i + 1] * flat * .96f, 0, ua},
                                         {.5f * len, .5f * (r[i] + r[i + 1]), .5f * (r[i] + r[i + 1]) * flat, 0, .5f * (ua + ub)},
                                         {len + (i ? lap : 0.f), r[i] * .96f, r[i] * flat * .96f, 0, ub}};
            PartMesh m; loft(m, s, around, 0, .5f, 0, i + 2 == n, i == 0 && snout > 0);
            return m;
        });
    }
}

void lizard(Rig& rig, const PoseP& p) {
    const float run = std::min(p.speed, 1.f);
    const float ys[7]{.097f, .07f, .035f, 0, -.035f, -.07f, -.098f}, rr[7]{.0045f, .010f, .013f, .012f, .007f, .0035f, .0012f};
    V3 pts[7];
    for (int i = 0; i < 7; ++i) {
        const float x = (.012f * run + .002f) * std::sin(TAU * p.gait - i * .8f) * (i < 2 ? .5f : 1.f);
        const float z = .009f + p.pushup * .012f * std::max(0.f, 1 - i / 3.f);
        pts[i] = {x, ys[i], z};
    }
    spine(rig, pts, rr, 7, .7f, 8, 0, 1, 1);
    const V3 hips[4]{pts[2] + V3{-.008f, 0, 0}, pts[2] + V3{.008f, 0, 0}, pts[4] + V3{-.007f, 0, 0}, pts[4] + V3{.007f, 0, 0}};
    const float sx[4]{-1, 1, -1, 1}, dy[4]{.012f, .012f, -.01f, -.01f}, phases[4]{0, .5f, .5f, 0};
    V3 feet[4];
    for (int k = 0; k < 4; ++k) feet[k] = {hips[k].x + sx[k] * .022f, hips[k].y + dy[k], 0};
    legs(rig, hips, feet, p.gait, run, phases, .008f, .016f, .003f, 3, .01f, 4);
}

void snake(Rig& rig, const PoseP& p) {
    const float run = std::min(p.speed, 1.f);
    V3 pts[9];
    for (int i = 0; i < 9; ++i) {
        const float amp = .02f + .025f * run;
        const float x = amp * std::sin(TAU * p.gait - i * .9f) * std::min(1.f, i / 2.f) + .01f * std::sin(p.t * .8f + i) * (1 - run);
        const float z = .007f + (i == 0 && run == 0 ? .02f * std::max(0.f, std::sin(p.t * 1.3f)) : 0.f);
        pts[i] = {x, .2f - i * .05f, z};
    }
    const float r[9]{.0065f, .008f, .009f, .009f, .008f, .007f, .005f, .003f, .001f};
    spine(rig, pts, r, 9, .8f, 8, 0, 1, 1);
}

void scolopendra(Rig& rig, const PoseP& p) {
    const float run = std::min(p.speed, 1.f);
    V3 pts[11];
    for (int i = 0; i < 11; ++i) {
        const float amp = .004f + .01f * run;
        const float x = amp * std::sin(TAU * p.gait * 1.5f - i * .7f) + .004f * std::sin(p.t * .9f + i) * (1 - run);
        pts[i] = {x, .1f - i * .02f, .005f};
    }
    float r[11];
    for (int i = 0; i < 11; ++i) r[i] = i == 0 ? .0038f : i == 10 ? .0025f : .0047f;
    spine(rig, pts, r, 11, .55f, 6, 0, 1, 1);
    for (const float sx : {-1.f, 1.f})   // antennae and forcipules
        triangles(rig, {pts[0], pts[0] + V3{sx * .012f, .014f, .004f}, pts[0] + V3{sx * .002f, .004f, 0}}, 2);
    for (int i = 1; i < 10; ++i) {
        const V3 mid = (pts[i] + pts[i + 1]) * .5f, ax = pts[i] - pts[i + 1];
        const float n = std::sqrt(ax.x * ax.x + ax.y * ax.y) + 1e-9f;
        const V3 side{ax.y / n, -ax.x / n, 0};
        for (const float sx : {-1.f, 1.f}) {
            const float w = std::sin(TAU * p.gait * 3 - i * .9f + (sx > 0 ? PI : 0.f));
            const V3 foot = mid + side * (sx * .012f) + V3{0, .004f * w * run, -.005f};
            triangles(rig, {mid + V3{0, .002f, 0}, foot, mid - V3{0, .002f, 0}}, 3);
        }
    }
}

// Dung beetle: smooth domed elytra and pronotum/head, six legs.
PartMesh beetle_shell() {
    PartMesh m;
    const std::vector<Section> elytra{{-.011f, .004f, .003f, 0, 0}, {-.007f, .0075f, .0048f, .0004f, .3f}, {0, .0085f, .0052f, .0006f, .62f},
                                      {.005f, .0078f, .0046f, .0004f, .82f}, {.0075f, .006f, .0035f, 0, .9f}};
    loft(m, elytra, 10, 0, .5f, 0, true, false);
    for (auto& t : m.uv) t[0] *= .85f;
    const std::vector<Section> front{{.006f, .0058f, .0034f, -.0004f, .86f}, {.010f, .0055f, .0030f, -.0006f, .93f},
                                     {.0145f, .0042f, .0018f, -.0012f, 1}};
    loft(m, front, 8, 0, .5f, 1, false, true);
    return m;
}
void dungbeetle(Rig& rig, const PoseP& p) {
    const float run = std::min(p.speed, 1.f);
    const M3 Rb = rot_x(-.35f * p.push);
    const V3 bc{0, 0, .008f + .004f * p.push};
    rig.part(affine(Rb, bc), -1, [] { return beetle_shell(); });
    const float ys[3]{.006f, 0, -.007f}, reach[3]{.012f, .013f, .016f};
    for (const float sx : {-1.f, 1.f})
        for (int i = 0; i < 3; ++i) {
            const int g = (i + (sx > 0)) % 2;
            const float s = std::sin(TAU * (p.gait + .5f * g)), c = std::cos(TAU * (p.gait + .5f * g));
            const V3 hip = bc + Rb * V3{sx * .006f, ys[i], -.002f};
            V3 foot{sx * reach[i], ys[i] * 1.6f + .004f * s * run, .003f * std::max(0.f, c) * run};
            if (i == 2 && p.push > .5f) foot = {sx * .006f, -.022f, .014f};   // hind legs up on the ball
            const V3 knee = (hip + foot) * .5f + V3{0, 0, .004f};
            limb(rig, hip, knee, .0011f, .0009f, 3, 3);
            limb(rig, knee, foot, .0009f, .0004f, 3, 3);
        }
}
PartMesh dung_sphere() {
    PartMesh m;
    std::vector<Section> s;
    for (int i = 0; i <= 6; ++i) {
        const float a = -PI / 2 + PI * i / 6, r = .015f * std::cos(a) + 1e-5f;
        s.push_back({.015f * std::sin(a), r, r, 0, i / 6.f});
    }
    loft(m, s, 10, 0, 1, 0, false, false);
    return m;
}
void dung_ball(Rig& rig, const PoseP&) { rig.part(affine(M3{}, {}), 2, [] { return dung_sphere(); }); }

// ---- vultures from the game's eagle statues
// The Suslanger bridge statues are an eagle with spread wings (stst112, part
// eag1) and a perched eagle with folded wings (stst113, part eag2), 409
// triangles each, figures.res. Vultures fly on eag1 with the wings
// articulated at shoulder and wrist (hinge lines through the vertex columns
// the arm and hand triangles share with their inner neighbour, so the mesh
// stays closed) and the talons tucked; they stand on eag2 with a moving
// head. Both are re-UV'd by region into the painted bird layout at load and
// shaded with crease-angle normals. Unit length head to tail, forward +Y,
// feet on z=0; the flight wings are stretched 15 % for a vulture's span.
struct StatueBird {
    PartMesh fly, perch;   // per-vertex bone: fly body, arm R, hand R, arm L, hand L, legs; perch body, head
    V3 shoulder, shoulder_axis, wrist, wrist_axis, hip, neck, perch_hip;   // right side; the left mirrors x
};
M3 axis_rot(V3 a, float t) {
    const float c = std::cos(t), s = std::sin(t), k = 1 - c;
    M3 r;
    r.m[0][0] = c + a.x * a.x * k;       r.m[0][1] = a.x * a.y * k - a.z * s; r.m[0][2] = a.x * a.z * k + a.y * s;
    r.m[1][0] = a.y * a.x * k + a.z * s; r.m[1][1] = c + a.y * a.y * k;       r.m[1][2] = a.y * a.z * k - a.x * s;
    r.m[2][0] = a.z * a.x * k - a.y * s; r.m[2][1] = a.z * a.y * k + a.x * s; r.m[2][2] = c + a.z * a.z * k;
    return r;
}
V3 mirror_x(V3 v) { return {-v.x, v.y, v.z}; }

// Vulture in flight on the statue eagle: soft body bob, wings beat about the
// shoulder hinge with the hand lagging about the wrist (glide: a shallow
// dihedral lifting the statue's mantled wings), talons tucked back.
void vulture_fly(Rig& rig, const PoseP& p) {
    const StatueBird& s = *rig.statue;
    const float soar = p.soar, ph = TAU * p.wing, beat = 1 - soar;
    const float flap = .6f * std::sin(ph) * beat + .22f + .1f * soar;
    const float hflap = .4f * std::sin(ph - .9f) * beat + .08f * soar;
    const Xf body = xf({0, 0, -.025f * beat * std::cos(ph)}, rot_x(.04f * beat * std::cos(ph)));
    Bone bones[6];
    bones[0] = affine(body.r, body.t);
    for (int side = 0; side < 2; ++side) {
        const float sx = side ? -1.f : 1.f;
        const V3 sp = side ? mirror_x(s.shoulder) : s.shoulder, sa = side ? mirror_x(s.shoulder_axis) : s.shoulder_axis;
        const V3 wp = side ? mirror_x(s.wrist) : s.wrist, wa = side ? mirror_x(s.wrist_axis) : s.wrist_axis;
        const Xf arm = body * xf(sp, axis_rot(sa, -sx * flap));
        const Xf hand = arm * xf(wp - sp, axis_rot(wa, -sx * hflap));
        bones[1 + 2 * side] = affine(arm.r, arm.t);
        bones[2 + 2 * side] = affine(hand.r, hand.t);
    }
    const Xf legs = body * xf(s.hip, rot_x(-1.1f));
    bones[5] = affine(legs.r, legs.t);
    rig.part(bones, 6, -1, [&] { return s.fly; });
}
// Vulture on the ground on the perched statue eagle: crouch and peck tip
// the body forward, the head pecks, calls and looks about.
void vulture_perch(Rig& rig, const PoseP& p) {
    const StatueBird& s = *rig.statue;
    const Xf body = xf(s.perch_hip + V3{0, 0, -.06f * p.crouch}, rot_x(-.2f * p.crouch - .15f * p.peck)) * xf(V3{} - s.perch_hip);
    const Xf head = body * xf(s.neck, rot_x(-.8f * p.peck + .35f * p.caw) * rot_z(p.look * (1 - p.peck)));
    const Bone bones[2]{affine(body.r, body.t), affine(head.r, head.t)};
    rig.part(bones, 2, -1, [&] { return s.perch; });
}

using RigFn = void (*)(Rig&, const PoseP&);
RigFn procedural_rig(Mesh m) {
    switch (m) {
    case m_lizard: return lizard;
    case m_snake: return snake;
    case m_crab: return crab;
    case m_scolopendra: return scolopendra;
    case m_dung: return dungbeetle;
    case m_ball: return dung_ball;
    case m_bird: return bird_small;
    case m_corvid: return bird_corvid;
    case m_bird_far: return bird_far;
    case m_vulture: return vulture_fly;
    case m_vulture_perch: return vulture_perch;
    default: return nullptr;
    }
}
PoseP rest_pose(Mesh) { return PoseP{}; }

// ---------------------------------------------------------------- game assets
// Atlas tile i of 4 in a 128 px atlas (64 px tiles), inset 1 %.
std::array<float, 4> atlas_box(int region) {
    const float x = float((region % 2) * 64), y = float((region / 2) * 64);
    return {x / 128 + .01f, y / 128 + .01f, (x + 64) / 128 - .01f, (y + 64) / 128 - .01f};
}

struct Image { unsigned w = 0, h = 0; std::vector<std::uint8_t> rgb; };

// DXT1 or DXT3 colour (block: 8 or 16 bytes, DXT3's colour after its alpha).
bool decode_dxt(const std::uint8_t* data, std::size_t bytes, unsigned w, unsigned h, unsigned block, Image& out) {
    if (w % 4 || h % 4 || bytes < std::size_t(w / 4) * (h / 4) * block) return false;
    out.w = w; out.h = h; out.rgb.assign(std::size_t(w) * h * 3, 0);
    for (unsigned by = 0; by < h / 4; ++by)
        for (unsigned bx = 0; bx < w / 4; ++bx) {
            const std::uint8_t* b = data + (std::size_t(by) * (w / 4) + bx) * block + (block - 8);
            const unsigned c0 = b[0] | (b[1] << 8), c1 = b[2] | (b[3] << 8);
            float pal[4][3];
            const auto rgb = [](unsigned c, float* o) {
                o[0] = float(((c >> 11) & 31) * 255 / 31); o[1] = float(((c >> 5) & 63) * 255 / 63); o[2] = float((c & 31) * 255 / 31);
            };
            rgb(c0, pal[0]); rgb(c1, pal[1]);
            for (int k = 0; k < 3; ++k) {
                if (c0 > c1) { pal[2][k] = (2 * pal[0][k] + pal[1][k]) / 3; pal[3][k] = (pal[0][k] + 2 * pal[1][k]) / 3; }
                else { pal[2][k] = (pal[0][k] + pal[1][k]) / 2; pal[3][k] = 0; }
            }
            const std::uint32_t bits = b[4] | (b[5] << 8) | (b[6] << 16) | (std::uint32_t(b[7]) << 24);
            for (unsigned i = 0; i < 16; ++i) {
                const unsigned sel = (bits >> (2 * i)) & 3, x = bx * 4 + i % 4, y = by * 4 + i / 4;
                for (int k = 0; k < 3; ++k) out.rgb[(std::size_t(y) * w + x) * 3 + k] = std::uint8_t(pal[sel][k]);
            }
        }
    return true;
}

// Recolour towards rgb keeping the painted light/dark variation (keep 0.9).
// A creature skin's top mip in any MMP format the loader accepts: DXT1,
// DXT3, or packed 16/32-bit with per-channel masks (Dejavu's bat is a
// 512 px ARGB1555). Masks are in the file's A, R, G, B order.
bool decode_skin_texture(const TextureCpuAsset& t, Image& out) {
    if (t.mips.empty() || !t.source) return false;
    const auto& mip = t.mips[0];
    if (mip.byte_offset > t.source->size() || mip.bytes > t.source->size() - mip.byte_offset) return false;
    const std::uint8_t* data = t.source->data() + mip.byte_offset;
    if (t.format == MmpFormat::dxt1) return decode_dxt(data, mip.bytes, mip.width, mip.height, 8, out);
    if (t.format == MmpFormat::dxt3) return decode_dxt(data, mip.bytes, mip.width, mip.height, 16, out);
    const unsigned bpp = t.bit_count / 8;
    if ((bpp != 2 && bpp != 4) || mip.bytes < std::size_t(mip.width) * mip.height * bpp) return false;
    out.w = mip.width; out.h = mip.height; out.rgb.assign(std::size_t(out.w) * out.h * 3, 0);
    for (std::size_t i = 0; i < std::size_t(out.w) * out.h; ++i) {
        const std::uint8_t* q = data + i * bpp;
        const std::uint32_t v = bpp == 2 ? std::uint32_t(q[0] | (q[1] << 8)) : std::uint32_t(q[0] | (q[1] << 8) | (q[2] << 16)) | (std::uint32_t(q[3]) << 24);
        for (int c = 0; c < 3; ++c) {
            const std::uint32_t mask = t.masks[1 + c], bits = t.channel_bits[1 + c];
            if (!mask || !bits || bits > 8) return false;
            out.rgb[i * 3 + c] = std::uint8_t(((v & mask) >> t.shifts[1 + c]) * 255u / ((1u << bits) - 1));
        }
    }
    return true;
}
Image tint(const Image& img, const float rgb[3]) {
    Image out = img;
    double mean = 0;
    std::vector<float> lum(std::size_t(img.w) * img.h);
    for (std::size_t i = 0; i < lum.size(); ++i) {
        lum[i] = (img.rgb[i * 3] + img.rgb[i * 3 + 1] + img.rgb[i * 3 + 2]) / 3.f;
        mean += lum[i];
    }
    mean = std::max(mean / std::max<std::size_t>(lum.size(), 1), 1.0);
    for (std::size_t i = 0; i < lum.size(); ++i)
        for (int k = 0; k < 3; ++k)
            out.rgb[i * 3 + k] = std::uint8_t(std::clamp(rgb[k] * (.9f * lum[i] / float(mean) + .1f), 0.f, 255.f));
    return out;
}
Image crop(const Image& img, const float box[4]) {
    const unsigned x0 = unsigned(box[0] * img.w), y0 = unsigned(box[1] * img.h), x1 = unsigned(box[2] * img.w), y1 = unsigned(box[3] * img.h);
    Image out; out.w = x1 - x0; out.h = y1 - y0; out.rgb.resize(std::size_t(out.w) * out.h * 3);
    for (unsigned y = 0; y < out.h; ++y)
        std::memcpy(&out.rgb[std::size_t(y) * out.w * 3], &img.rgb[(std::size_t(y0 + y) * img.w + x0) * 3], out.w * 3);
    return out;
}
Image resize(const Image& img, unsigned n) {   // nearest, texel centres
    Image out; out.w = out.h = n; out.rgb.resize(std::size_t(n) * n * 3);
    for (unsigned y = 0; y < n; ++y)
        for (unsigned x = 0; x < n; ++x) {
            const unsigned sx = unsigned((x + .5) * img.w / n), sy = unsigned((y + .5) * img.h / n);
            for (int k = 0; k < 3; ++k) out.rgb[(std::size_t(y) * n + x) * 3 + k] = img.rgb[(std::size_t(sy) * img.w + sx) * 3 + k];
        }
    return out;
}
Image downsample(const Image& img, unsigned n) {   // box filter to n x n
    const unsigned s = img.w / n;
    Image out; out.w = out.h = n; out.rgb.resize(std::size_t(n) * n * 3);
    for (unsigned y = 0; y < n; ++y)
        for (unsigned x = 0; x < n; ++x)
            for (int k = 0; k < 3; ++k) {
                unsigned sum = 0;
                for (unsigned v = 0; v < s; ++v)
                    for (unsigned u = 0; u < s; ++u) sum += img.rgb[(std::size_t(y * s + v) * img.w + x * s + u) * 3 + k];
                out.rgb[(std::size_t(y) * n + x) * 3 + k] = std::uint8_t(sum / (s * s));
            }
    return out;
}

// Painted bird slice in the bird UV layout (bird_model): per variant
// colours carried by the feather detail of the game's own painted bird
// (textures.res bird.mmp, a top-down bird with spread wings): each region
// samples the matching part of it, so primaries, coverts and tail feathers
// follow the wing and tail directions. Sheen adds a cool highlight on the
// brighter feathers (corvids).
struct BirdPaint {
    float back[3], belly[3], cap[3], throat[3], cheek[3], coverts[3], bar[3], flight[3], under[3], tail[3], beak[3], legs[3];
    bool streaks, bald;
    float sheen[3];
};
const BirdPaint& bird_paint(Bird b) {
    static const BirdPaint paints[bird_count] = {
        // chaffinch: chestnut back, pink breast, blue-grey cap, white wing bar
        {{120, 92, 70}, {196, 142, 118}, {108, 118, 134}, {190, 128, 104}, {196, 140, 116}, {92, 86, 78}, {226, 222, 210},
         {48, 44, 40}, {176, 164, 152}, {62, 58, 56}, {150, 140, 128}, {124, 104, 94}, false, false},
        // house sparrow: streaked brown back, grey crown and belly, black bib
        {{132, 100, 70}, {172, 168, 158}, {112, 106, 102}, {42, 38, 36}, {196, 190, 180}, {142, 98, 62}, {222, 216, 202},
         {82, 64, 48}, {182, 174, 162}, {92, 74, 58}, {62, 58, 54}, {152, 122, 102}, true, false},
        // yellowhammer: yellow head and breast, streaked back
        {{140, 110, 62}, {214, 190, 82}, {224, 202, 72}, {226, 206, 92}, {200, 170, 62}, {130, 96, 58}, {192, 172, 122},
         {72, 58, 42}, {202, 186, 122}, {82, 68, 50}, {112, 102, 92}, {152, 122, 102}, true, false},
        // snow bunting: white below and on the wing, tawny cheek, black primaries
        {{122, 102, 82}, {236, 234, 230}, {222, 210, 190}, {236, 234, 230}, {194, 162, 122}, {232, 230, 226}, {236, 234, 230},
         {38, 36, 36}, {232, 230, 228}, {52, 50, 50}, {202, 172, 92}, {42, 38, 36}, true, false},
        // carrion crow: brown-black, faint sheen, dark grey beak and legs
        {{60, 54, 52}, {64, 58, 56}, {52, 48, 48}, {58, 54, 52}, {58, 54, 52}, {64, 58, 56}, {64, 58, 56},
         {40, 37, 37}, {70, 66, 64}, {44, 41, 41}, {50, 50, 53}, {48, 47, 49}, false, false, {10, 12, 18}},
        // raven: blue-black with a strong blue sheen
        {{26, 28, 40}, {30, 32, 44}, {22, 24, 36}, {28, 30, 42}, {26, 28, 40}, {28, 30, 44}, {28, 30, 44},
         {18, 20, 30}, {36, 38, 50}, {20, 22, 32}, {44, 44, 50}, {42, 42, 46}, false, false, {26, 36, 70}},
        // griffon vulture: sandy brown, bald pink head, cream ruff, dark flight feathers
        {{122, 94, 64}, {152, 122, 88}, {204, 172, 160}, {172, 142, 132}, {228, 216, 190}, {132, 102, 72}, {132, 102, 72},
         {46, 36, 30}, {112, 88, 64}, {52, 42, 34}, {202, 192, 172}, {152, 142, 132}, false, true},
        // bat: the game's own bat model and skin (unused here)
        {{60, 52, 50}, {60, 52, 50}, {60, 52, 50}, {60, 52, 50}, {60, 52, 50}, {60, 52, 50}, {60, 52, 50},
         {60, 52, 50}, {60, 52, 50}, {60, 52, 50}, {60, 52, 50}, {60, 52, 50}, false, false},
    };
    return paints[b < bird_count ? b : 0];
}
Image paint_bird(const BirdPaint& c, const Image& feathers) {
    Image img; img.w = img.h = kSkin; img.rgb.assign(std::size_t(kSkin) * kSkin * 3, 0);
    const auto ss = [](float a, float b, float x) { x = std::clamp((x - a) / (b - a), 0.f, 1.f); return x * x * (3 - 2 * x); };
    const auto frac = [](float x) { return x - std::floor(x); };
    // feather luminance of the game bird at 256 px coordinates, relative to
    // the mean of its painted (non-background) texels
    const float fscale = feathers.w / 256.f;
    const auto lum_at = [&](float fx, float fy) {
        fx = std::clamp(fx * fscale, 0.f, float(feathers.w - 1)); fy = std::clamp(fy * fscale, 0.f, float(feathers.h - 1));
        const unsigned x0 = unsigned(fx), y0 = unsigned(fy), x1 = std::min(x0 + 1, feathers.w - 1), y1 = std::min(y0 + 1, feathers.h - 1);
        const float ax = fx - x0, ay = fy - y0;
        const auto l = [&](unsigned x, unsigned y) {
            const std::size_t i = (std::size_t(y) * feathers.w + x) * 3;
            return (feathers.rgb[i] + feathers.rgb[i + 1] + feathers.rgb[i + 2]) / 3.f;
        };
        return (l(x0, y0) * (1 - ax) + l(x1, y0) * ax) * (1 - ay) + (l(x0, y1) * (1 - ax) + l(x1, y1) * ax) * ay;
    };
    float mean = 0, n = 0;
    for (std::size_t i = 0; i + 2 < feathers.rgb.size(); i += 3) {
        const float l = (feathers.rgb[i] + feathers.rgb[i + 1] + feathers.rgb[i + 2]) / 3.f;
        if (l > 25) { mean += l; ++n; }
    }
    mean = n > 0 ? mean / n : 128.f;
    for (unsigned y = 0; y < kSkin; ++y)
        for (unsigned x = 0; x < kSkin; ++x) {
            const float u = (x + .5f) / kSkin, v = (y + .5f) / kSkin;
            float col[3], detail = 1, strength = .5f;
            const auto mix = [&](const float* a, const float* b, float t) { for (int k = 0; k < 3; ++k) col[k] = a[k] + (b[k] - a[k]) * t; };
            const auto set = [&](const float* a) { for (int k = 0; k < 3; ++k) col[k] = a[k]; };
            const auto toward = [&](const float* b, float t) { for (int k = 0; k < 3; ++k) col[k] += (b[k] - col[k]) * t; };
            const auto shade = [&](float f) { for (float& k : col) k *= f; };
            if (v < .5f) {
                const float t = v / .5f;   // belly 0 .. back 1
                if (u < .6f) {
                    mix(c.belly, c.back, ss(.3f, .7f, t));
                    if (c.streaks && t > .55f && frac(u * 16 + .35f * std::sin(t * 23)) < .3f) shade(.68f);
                    if (c.bald) toward(c.cheek, ss(.52f, .58f, u));   // ruff at the neck base
                    detail = lum_at(118 + t * 14, 112 + u / .6f * 38); strength = .55f;   // the game bird's body feathers
                } else {
                    mix(c.throat, c.cap, ss(.35f, .65f, t));
                    if (c.bald) { mix(c.throat, c.cap, ss(.62f, .72f, u)); if (u < .64f) set(c.cheek); }
                    const float cu = (u - .84f) / .08f, ct = (t - .45f) / .14f;
                    if (!c.bald) toward(c.cheek, 1 - ss(.6f, 1, cu * cu + ct * ct));
                    detail = lum_at(121 + t * 6, 150 + (u - .6f) / .4f * 22); strength = .35f;
                }
            } else if (v < .75f) {
                const float ch = (v - .5f) / .25f;   // leading 0 .. trailing 1
                set(ch < .45f && u < .75f ? c.coverts : c.flight);
                if (ch > .4f && ch < .5f && u < .7f) set(c.bar);
                if ((ch >= .45f || u >= .75f) && u > .7f) shade(.85f);   // darker primaries
                // the game bird's left wing: span from the body out, chord across
                const float lead = 117 + 7 * u, trail = 150 - 13 * u * u;
                detail = lum_at(110 - u * 106, lead + ch * (trail - lead)); strength = 1.f;
            } else if (v < .875f) {
                mix(c.under, c.flight, ss(.55f, 1, (v - .75f) / .125f));
                const float ch = (v - .75f) / .125f, lead = 117 + 7 * u, trail = 150 - 13 * u * u;
                detail = lum_at(110 - u * 106, lead + ch * (trail - lead)); strength = .7f;
            } else if (u < .5f) {
                set(c.tail);
                const float along = u / .5f, across = (v - .875f) / .125f;
                if (along > .85f) shade(.8f);
                detail = lum_at(125 + (across - .5f) * (18 + 10 * along), 112 - along * 30); strength = 1.f;
            } else if (u < .75f) set(c.beak);
            else set(c.legs);
            // feather detail: the game bird's relative brightness drives the
            // value; the brighter feathers also take the sheen
            const float r = detail / mean, f = std::clamp(1 + strength * (r - 1) * .9f, .45f, 1.6f);
            shade(f);
            const float glow = std::clamp(r - 1, 0.f, 1.f) * strength;
            for (int k = 0; k < 3; ++k) col[k] += c.sheen[k] * glow;
            if (v < .5f && u >= .6f) {   // eye and glint over the detail
                const float t = v / .5f, eu = (u - .885f) / .024f, et = (t - .6f) / .07f, e = eu * eu + et * et;
                if (e < 1) { const float dark[3]{18, 16, 14}; toward(dark, 1 - ss(.6f, 1, e)); }
                const float hu = (u - .89f) / .007f, ht = (t - .63f) / .02f;
                if (hu * hu + ht * ht < 1) { const float glint[3]{210, 210, 200}; toward(glint, .8f); }
            }
            for (int k = 0; k < 3; ++k) img.rgb[(std::size_t(y) * kSkin + x) * 3 + k] = std::uint8_t(std::clamp(col[k], 0.f, 255.f));
        }
    return img;
}

// Painted slice for the spined critters and the beetle (layout: spine()),
// over a game skin's grain (toad for reptiles, scrab for arthropods) so the
// pattern sits in EI's painted texture: body strip, accent tile, legs tile.
enum Pattern : std::uint8_t { pt_blotches = 0, pt_zigzag, pt_bands, pt_shell };
struct StripPaint {
    float belly[3], back[3], mark[3], head[3], accent[3], legs[3];
    Pattern pattern; float marks, head_u, eye_u;
};
Image paint_strip(const StripPaint& c, const Image& grain) {
    Image img; img.w = img.h = kSkin; img.rgb.assign(std::size_t(kSkin) * kSkin * 3, 0);
    const auto ss = [](float a, float b, float x) { x = std::clamp((x - a) / (b - a), 0.f, 1.f); return x * x * (3 - 2 * x); };
    const auto frac = [](float x) { return x - std::floor(x); };
    float mean = 0;
    for (const auto v : grain.rgb) mean += v;
    mean = std::max(mean / std::max<std::size_t>(grain.rgb.size(), 1), 1.f);
    for (unsigned y = 0; y < kSkin; ++y)
        for (unsigned x = 0; x < kSkin; ++x) {
            const float u = (x + .5f) / kSkin, v = (y + .5f) / kSkin;
            float col[3];
            const auto set = [&](const float* a) { for (int k = 0; k < 3; ++k) col[k] = a[k]; };
            const auto toward = [&](const float* b, float t) { for (int k = 0; k < 3; ++k) col[k] += (b[k] - col[k]) * t; };
            const auto shade = [&](float f) { for (float& k : col) k *= f; };
            if (v < .5f) {
                const float t = v / .5f;   // belly 0 .. back 1
                set(c.belly); toward(c.back, ss(.25f, .6f, t));
                const float cell = frac(u * c.marks);
                switch (c.pattern) {
                case pt_blotches: {   // two dorsal rows of dark blotches, a pale flank line
                    const float du = (cell - .5f) / .3f, dt = (t - .84f) / .1f;
                    toward(c.mark, (1 - ss(.5f, 1, du * du + dt * dt)) * (u < c.head_u ? 1.f : 0.f));
                    if (t > .52f && t < .6f) shade(1.18f);
                    break;
                }
                case pt_zigzag: {     // a dark dorsal zigzag
                    const float z = std::abs(cell - .5f) * 2, centre = .8f + .14f * (z - .5f);
                    toward(c.mark, 1 - ss(.04f, .08f, std::abs(t - centre)));
                    break;
                }
                case pt_bands:        // a dark band at every segment joint
                    toward(c.mark, (1 - ss(.08f, .2f, std::min(cell, 1 - cell))) * ss(.2f, .45f, t));
                    break;
                case pt_shell:        // glossy dome: a highlight band and a dark seam down the back
                    if (t > .66f && t < .8f) toward(c.mark, .6f * (1 - std::abs(t - .73f) / .07f));
                    if (t > .95f) shade(.55f);
                    break;
                }
                toward(c.head, ss(c.head_u - .03f, c.head_u + .03f, u));
                if (c.eye_u > 0) {
                    const float eu = (u - c.eye_u) / .012f, et = (t - .62f) / .07f;
                    if (eu * eu + et * et < 1) { const float dark[3]{16, 14, 12}; set(dark); }
                }
            } else if (u < .5f) {
                set(c.accent);
                if (frac(u * 9 + v * 5) < .3f) shade(.85f);
            } else {
                set(c.legs);
                if (frac(v * 12) < .2f) shade(.8f);
            }
            const std::size_t gi = (std::size_t(y % grain.h) * grain.w + x % grain.w) * 3;
            const float g = std::clamp((grain.rgb[gi] + grain.rgb[gi + 1] + grain.rgb[gi + 2]) / (3 * mean), .8f, 1.2f);
            for (int k = 0; k < 3; ++k) img.rgb[(std::size_t(y) * kSkin + x) * 3 + k] = std::uint8_t(std::clamp(col[k] * g, 0.f, 255.f));
        }
    return img;
}

struct Track {
    std::vector<Q4> rotations; std::vector<V3> translations;
    // per-frame vertex morphs (offsets per plot, the figure's geometric vertex), e.g. a bat's wing membrane
    std::uint32_t morph_frames = 0, morph_points = 0; std::vector<V3> morphs;
};
struct Clip { std::string name; std::vector<Track> tracks; unsigned frames = 1; };   // tracks per model node
struct Part { std::vector<V3> p; std::vector<std::array<float, 2>> uv; std::vector<std::array<std::uint32_t, 3>> tris; std::vector<std::uint32_t> plot; };
struct Model {
    std::vector<std::string> nodes;
    std::vector<std::uint32_t> parent;
    std::vector<V3> offset;
    std::vector<Part> parts;   // empty part: no figure
    std::vector<Clip> clips;   // sorted by name
    const Clip* clip(const char* prefix) const {
        for (const auto& c : clips) if (c.name == prefix) return &c;
        for (const auto& c : clips) if (c.name.compare(0, std::strlen(prefix), prefix) == 0) return &c;
        return nullptr;
    }
};

std::string folded(std::string s) { for (char& c : s) if (c >= 'A' && c <= 'Z') c = char(c + 32); return s; }

bool load_model(const ResourceStore& store, const std::string& name, Model& model, std::string& error) {
    ResourceFailure failure;
    ResourceBlob mod, bon, anm;
    ModelCpuAsset skeleton;
    if (!store.resolve(name + ".mod", mod, failure) || !store.resolve(name + ".bon", bon, failure) ||
        !decode_model_skeleton_resources(mod, bon, name, skeleton, failure)) {
        error = name + ": " + failure.message;
        return false;
    }
    if (std::any_of(skeleton.nodes.begin(), skeleton.nodes.end(),
        [](const auto& node) { return !node.transform_available; })) {
        error = name + ": missing BON transform; critter omitted";
        return false;
    }
    // Static figures (the statues) have no clip archive.
    ResourceFailure no_clips;
    const bool animated = store.resolve(name + ".anm", anm, no_clips);
    MemoryResourceArchive figures, clips;
    if (!figures.open(mod, failure) || (animated && !clips.open(anm, failure))) { error = name + ": " + failure.message; return false; }
    const std::size_t count = skeleton.nodes.size();
    if (!count || skeleton.links.size() != count) { error = name + ": skeleton"; return false; }
    model.nodes.resize(count); model.parent.resize(count); model.offset.resize(count); model.parts.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        model.nodes[i] = folded(skeleton.nodes[i].name);
        model.parent[i] = skeleton.links[i].parent_index < i ? skeleton.links[i].parent_index : UINT_MAX;
        const auto& bone = skeleton.nodes[i].bone_positions;
        if (bone.size() >= 3) model.offset[i] = {bone[0], bone[1], bone[2]};
        ResourceIdentity identity;
        ResourceFailure absent;
        if (!figures.locate(skeleton.nodes[i].name, identity, absent)) continue;   // a joint without a figure
        ResourceBlob blob;
        FigureCpuAsset figure;
        if (!figures.resolve(skeleton.nodes[i].name, blob, failure) || !parse_figure_asset(blob, figure, failure)) {
            error = name + "/" + skeleton.nodes[i].name + ": " + failure.message;
            return false;
        }
        // Base shape (first morph endpoint) as the prototype's eimodel.py.
        Part& part = model.parts[i];
        for (const auto& c : figure.components) {
            const std::size_t group = c.position / 4u, lane = c.position % 4u;
            if (group >= figure.position_groups || c.texture >= figure.texture_coordinates.size()) { error = name + ": figure component"; return false; }
            const auto& g = figure.positions[group];
            part.p.push_back({g.x[lane], g.y[lane], g.z[lane]});
            part.plot.push_back(c.position);
            part.uv.push_back({figure.texture_coordinates[c.texture].u, figure.texture_coordinates[c.texture].v});
        }
        for (std::size_t k = 0; k + 2 < figure.indices.size(); k += 3) {
            const std::array<std::uint32_t, 3> t{figure.indices[k], figure.indices[k + 1], figure.indices[k + 2]};
            if (t[0] >= part.p.size() || t[1] >= part.p.size() || t[2] >= part.p.size()) { error = name + ": figure index"; return false; }
            part.tris.push_back(t);
        }
    }
    auto names = animated ? clips.entry_names() : std::vector<std::string>{};
    std::sort(names.begin(), names.end());
    // Some mods store an .anm flat, one "clip_\node" track per entry,
    // instead of one nested archive per clip.
    std::map<std::string, Clip> flat;
    for (const auto& entry : names) {
        const auto split = entry.find("_\\");
        if (split == std::string::npos) continue;
        ResourceBlob member;
        AnimationTrackAsset track;
        if (!clips.resolve(entry, member, failure) || !parse_animation_track(member, track, failure)) {
            error = name + "/" + entry + ": " + failure.message;
            return false;
        }
        Clip& clip = flat[folded(entry.substr(0, split))];
        clip.name = folded(entry.substr(0, split));
        clip.tracks.resize(count);
        const std::string node = folded(entry.substr(split + 2));
        for (std::size_t i = 0; i < count; ++i) {
            if (model.nodes[i] != node) continue;
            for (const auto& q : track.rotations) clip.tracks[i].rotations.push_back(q);
            for (const auto& t : track.translations) clip.tracks[i].translations.push_back({t[0], t[1], t[2]});
            if (track.morph_frames && track.morph_points && track.morphs.size() >= std::size_t(track.morph_frames) * track.morph_points) {
                clip.tracks[i].morph_frames = track.morph_frames; clip.tracks[i].morph_points = track.morph_points;
                for (const auto& d : track.morphs) clip.tracks[i].morphs.push_back({d[0], d[1], d[2]});
            }
            clip.frames = std::max<unsigned>(clip.frames, unsigned(std::max(track.rotations.size(), track.translations.size())));
        }
    }
    for (auto& entry : flat) model.clips.push_back(std::move(entry.second));
    for (const auto& clip_name : names) {
        if (clip_name.find("_\\") != std::string::npos) continue;
        ResourceBlob member;
        AnimationClipAsset asset;
        if (!clips.resolve(clip_name, member, failure) || !decode_animation_clip(member, asset, failure)) {
            error = name + "/" + clip_name + ": " + failure.message;
            return false;
        }
        Clip clip;
        clip.name = folded(clip_name);
        clip.tracks.resize(count);
        for (std::size_t i = 0; i < count; ++i) {
            const auto found = asset.tracks.find(skeleton.nodes[i].name);
            if (found == asset.tracks.end()) continue;
            auto& track = clip.tracks[i];
            for (const auto& q : found->second.rotations) track.rotations.push_back(q);
            for (const auto& t : found->second.translations) track.translations.push_back({t[0], t[1], t[2]});
            const auto& src = found->second;
            if (src.morph_frames && src.morph_points && src.morphs.size() >= std::size_t(src.morph_frames) * src.morph_points) {
                track.morph_frames = src.morph_frames; track.morph_points = src.morph_points;
                for (const auto& d : src.morphs) track.morphs.push_back({d[0], d[1], d[2]});
            }
            clip.frames = std::max<unsigned>(clip.frames, unsigned(std::max(track.rotations.size(), track.translations.size())));
        }
        model.clips.push_back(std::move(clip));
    }
    return true;
}

// Node rotations and positions at a clip frame (prototype eimodel.py world():
// keys interpolated linearly / normalised, wrapping per track).
void model_pose(const Model& m, const Clip* clip, float frame, std::vector<Q4>& Q, std::vector<V3>& P) {
    const std::size_t n = m.nodes.size();
    Q.assign(n, {1, 0, 0, 0}); P.assign(n, {});
    const int whole = int(std::floor(frame));
    const float a = frame - float(whole);
    for (std::size_t i = 0; i < n; ++i) {
        Q4 q{1, 0, 0, 0};
        V3 p{};
        if (clip && !clip->tracks[i].rotations.empty()) {
            const auto& r = clip->tracks[i].rotations;
            const std::size_t f0 = std::size_t(whole) % r.size(), f1 = (f0 + 1) % r.size();
            Q4 q1 = r[f1];
            const float d = r[f0][0] * q1[0] + r[f0][1] * q1[1] + r[f0][2] * q1[2] + r[f0][3] * q1[3];
            if (d < 0) for (float& v : q1) v = -v;
            float l = 0;
            for (int k = 0; k < 4; ++k) { q[k] = r[f0][k] * (1 - a) + q1[k] * a; l += q[k] * q[k]; }
            l = std::sqrt(l);
            if (l > 1e-12f) for (float& v : q) v /= l;
        }
        if (clip && !clip->tracks[i].translations.empty()) {
            const auto& t = clip->tracks[i].translations;
            const std::size_t f0 = std::size_t(whole) % t.size(), f1 = (f0 + 1) % t.size();
            p = t[f0] * (1 - a) + t[f1] * a;
        }
        if (m.parent[i] == UINT_MAX) { Q[i] = q; P[i] = p; }
        else {
            const auto j = m.parent[i];
            P[i] = P[j] + qrot(Q[j], m.offset[i]);
            Q[i] = qmul(q, Q[j]);
        }
    }
}

// Silhouette-preserving quadric edge collapse (prototype decimate.py, pass 3):
// quadrics accumulated from the original surface, feature, boundary and UV
// seam vertices expensive to remove, flips and slivers rejected; collapsed
// corners take the surviving vertex's UV from a face on the collapsed edge.
void decimate(const Part& part, std::size_t target, std::vector<std::array<V3, 3>>& out_p,
              std::vector<std::array<std::array<float, 2>, 3>>& out_uv) {
    std::vector<V3> pos;
    std::vector<std::uint32_t> inv(part.p.size());
    {
        std::map<std::array<long long, 3>, std::uint32_t> weld;
        for (std::size_t i = 0; i < part.p.size(); ++i) {
            const std::array<long long, 3> key{std::llround(part.p[i].x * 1e5), std::llround(part.p[i].y * 1e5), std::llround(part.p[i].z * 1e5)};
            const auto found = weld.emplace(key, std::uint32_t(pos.size()));
            if (found.second) pos.push_back(part.p[i]);
            inv[i] = found.first->second;
        }
    }
    const std::size_t F = part.tris.size();
    std::vector<std::array<std::uint32_t, 3>> faces(F);
    std::vector<std::array<std::array<float, 2>, 3>> fuv(F);
    std::vector<char> alive(F);
    for (std::size_t k = 0; k < F; ++k) {
        for (int c = 0; c < 3; ++c) { faces[k][c] = inv[part.tris[k][c]]; fuv[k][c] = part.uv[part.tris[k][c]]; }
        alive[k] = faces[k][0] != faces[k][1] && faces[k][1] != faces[k][2] && faces[k][0] != faces[k][2];
    }
    const auto normal = [&](const std::array<std::uint32_t, 3>& f, V3& n) {
        n = cross3(pos[f[1]] - pos[f[0]], pos[f[2]] - pos[f[0]]);
        const float a = len3(n);
        if (a <= 1e-12f) return 0.f;
        n = n * (1 / a);
        return a;
    };
    using Q44 = std::array<double, 16>;
    std::vector<Q44> quadric(pos.size(), Q44{});
    std::vector<float> sharp(pos.size(), 0.f);
    std::vector<std::vector<std::size_t>> vf(pos.size());
    std::vector<V3> normals(F);
    std::vector<char> has_normal(F, 0);
    for (std::size_t k = 0; k < F; ++k) {
        if (!alive[k]) continue;
        V3 n;
        const float a = normal(faces[k], n);
        for (const auto v : faces[k]) vf[v].push_back(k);
        if (a <= 0) continue;
        normals[k] = n; has_normal[k] = 1;
        const double pl[4]{n.x, n.y, n.z, -dot3(n, pos[faces[k][0]])};
        for (const auto v : faces[k])
            for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) quadric[v][r * 4 + c] += pl[r] * pl[c] * a;
    }
    for (std::size_t v = 0; v < pos.size(); ++v) {
        float lowest = 2;
        int count = 0;
        for (const auto a : vf[v]) {
            if (!has_normal[a]) continue;
            ++count;
            for (const auto b : vf[v]) if (has_normal[b]) lowest = std::min(lowest, dot3(normals[a], normals[b]));
        }
        if (count > 1) sharp[v] = 1 - lowest;
    }
    std::vector<char> seam(pos.size(), 0);
    {
        std::vector<std::array<long long, 2>> seen(pos.size(), {LLONG_MIN, 0});
        for (std::size_t k = 0; k < F; ++k)
            for (int c = 0; c < 3; ++c) {
                const std::array<long long, 2> t{std::llround(fuv[k][c][0] * 1e4), std::llround(fuv[k][c][1] * 1e4)};
                auto& s = seen[faces[k][c]];
                if (s[0] == LLONG_MIN) s = t;
                else if (s != t) seam[faces[k][c]] = 1;
            }
    }
    const auto flips = [&](std::uint32_t keep, std::uint32_t drop) {
        for (std::size_t k = 0; k < F; ++k) {
            const auto& f = faces[k];
            if (!alive[k] || (f[0] != drop && f[1] != drop && f[2] != drop) || f[0] == keep || f[1] == keep || f[2] == keep) continue;
            V3 n0, n1;
            const float a0 = normal(f, n0);
            auto g = f;
            for (auto& v : g) if (v == drop) v = keep;
            const float a1 = normal(g, n1);
            if (a0 <= 0 || a1 <= 0 || dot3(n0, n1) < .5f || a1 < .05f * a0) return true;
        }
        return false;
    };
    std::size_t live = 0;
    for (const char a : alive) live += a ? 1 : 0;
    struct Candidate { double cost; std::uint32_t keep, drop; };
    std::vector<std::pair<std::uint32_t, std::uint32_t>> edges;
    std::unordered_map<std::uint64_t, int> edge_faces;
    std::vector<Candidate> candidates;
    while (live > target) {
        edges.clear(); edge_faces.clear();
        for (std::size_t k = 0; k < F; ++k) {
            if (!alive[k]) continue;
            for (int i = 0; i < 3; ++i) {
                const std::uint32_t a = std::min(faces[k][i], faces[k][(i + 1) % 3]), b = std::max(faces[k][i], faces[k][(i + 1) % 3]);
                const auto found = edge_faces.emplace((std::uint64_t(a) << 32) | b, 0);
                if (found.second) edges.push_back({a, b});
                ++found.first->second;
            }
        }
        std::vector<char> boundary(pos.size(), 0);
        for (const auto& [a, b] : edges) if (edge_faces[(std::uint64_t(a) << 32) | b] == 1) boundary[a] = boundary[b] = 1;
        candidates.clear();
        for (const auto& [a, b] : edges) {
            Q44 q;
            for (int i = 0; i < 16; ++i) q[i] = quadric[a][i] + quadric[b][i];
            const V3 d = pos[a] - pos[b];
            const double l2 = dot3(d, d);
            for (const auto [keep, drop] : {std::pair{a, b}, std::pair{b, a}}) {
                const double h[4]{pos[keep].x, pos[keep].y, pos[keep].z, 1};
                double c = 0;
                for (int r = 0; r < 4; ++r) for (int cc = 0; cc < 4; ++cc) c += h[r] * q[r * 4 + cc] * h[cc];
                c += 1e-3 * l2 + .02 * sharp[drop] * l2;
                if (boundary[drop]) c += (boundary[keep] ? .2 : 1.0) * l2;
                if (seam[drop]) c += seam[keep] ? .05 : 1.0;
                candidates.push_back({c, keep, drop});
            }
        }
        std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& x, const Candidate& y) { return x.cost < y.cost; });
        const Candidate* pick = nullptr;
        for (const auto& c : candidates) if (!flips(c.keep, c.drop)) { pick = &c; break; }
        if (!pick) break;
        const auto keep = pick->keep, drop = pick->drop;
        for (int i = 0; i < 16; ++i) quadric[keep][i] += quadric[drop][i];
        sharp[keep] = std::max(sharp[keep], sharp[drop]);
        bool have_uv = false;
        std::array<float, 2> uv_keep{};
        for (std::size_t k = 0; k < F && !have_uv; ++k) {
            const auto& f = faces[k];
            if (!alive[k]) continue;
            int ik = -1; bool hd = false;
            for (int c = 0; c < 3; ++c) { if (f[c] == keep) ik = c; if (f[c] == drop) hd = true; }
            if (ik >= 0 && hd) { uv_keep = fuv[k][ik]; have_uv = true; }
        }
        for (std::size_t k = 0; k < F; ++k) {
            if (!alive[k]) continue;
            for (int c = 0; c < 3; ++c)
                if (faces[k][c] == drop) { faces[k][c] = keep; if (have_uv) fuv[k][c] = uv_keep; }
            const auto& f = faces[k];
            if (f[0] == f[1] || f[1] == f[2] || f[0] == f[2]) { alive[k] = 0; --live; }
        }
    }
    for (std::size_t k = 0; k < F; ++k) {
        if (!alive[k]) continue;
        out_p.push_back({pos[faces[k][0]], pos[faces[k][1]], pos[faces[k][2]]});
        out_uv.push_back(fuv[k]);
    }
}

// Region of a statue triangle from its centroid in the figure's own units
// (eag1 1.79 long, eag2 1.85; head +Y): 0 body, 1 head, 2 arm / folded wing,
// 3 tail, 4 legs, 5 hand. The diving eag1 hangs its head as low as its
// talons (beak z 0.1-0.3 at y 0.53-0.80, talons y -0.17-0.27): only the
// talons are legs.
int statue_region(bool flight, V3 c) {
    const float x = std::abs(c.x);
    if (flight) {
        if (c.z < .33f && x < .3f && c.y < .4f) return 4;
        if (c.y < -.5f && x < .42f) return 3;
        if (x > 1.f) return 5;
        if (x > .27f) return 2;
        return c.y > .25f ? 1 : 0;
    }
    if (c.z < .4f && x < .3f && c.y > -.3f) return 4;
    if (c.y < -.55f && c.z < .9f) return 3;
    if (c.y > .2f && c.z > 1.f) return 1;
    return x > .245f && c.z > .3f ? 2 : 0;
}
bool build_statue_mesh(const Model& model, const char* part_name, bool flight, PartMesh& out, std::vector<int>& bone_of,
                       std::string& error) {
    std::size_t node = model.nodes.size();
    for (std::size_t i = 0; i < model.nodes.size(); ++i) if (model.nodes[i] == part_name) node = i;
    if (node == model.nodes.size() || model.parts[node].tris.empty()) { error = std::string("statue part ") + part_name + " missing"; return false; }
    const Part& part = model.parts[node];
    std::vector<Q4> Q; std::vector<V3> P;
    model_pose(model, nullptr, 0, Q, P);
    std::vector<V3> raw(part.p.size());
    float lo[3]{1e9f, 1e9f, 1e9f}, hi[3]{-1e9f, -1e9f, -1e9f};
    for (std::size_t i = 0; i < raw.size(); ++i) {
        raw[i] = qrot(Q[node], part.p[i]) + P[node];
        const float c[3]{raw[i].x, raw[i].y, raw[i].z};
        for (int k = 0; k < 3; ++k) { lo[k] = std::min(lo[k], c[k]); hi[k] = std::max(hi[k], c[k]); }
    }
    // the region thresholds are in the statue's own units, its feet on z=0
    for (auto& v : raw) { v.x -= .5f * (lo[0] + hi[0]); v.z -= lo[2]; }
    const float length = hi[1] - lo[1], ymid = .5f * (lo[1] + hi[1]);
    // the beak tip: the foremost point of the body column (eag1's swept
    // wingtips reach further forward than its head)
    float ymax = -1e9f;
    for (const auto& v : raw) if (std::abs(v.x) < .3f) ymax = std::max(ymax, v.y - ymid);
    if (!(length > .1f)) { error = std::string("statue part ") + part_name + " empty"; return false; }
    const float scale = 1 / length;
    const auto local = [&](V3 v) {
        if (flight && std::abs(v.x) > .27f) v.x = (v.x > 0 ? 1.f : -1.f) * (.27f + (std::abs(v.x) - .27f) * 1.15f);
        return V3{v.x * scale, (v.y - ymid) * scale, v.z * scale};
    };
    const auto clamp01 = [](float v) { return std::clamp(v, 0.f, 1.f); };
    // per-vertex bone from its position
    const auto vertex_bone = [&](V3 v) {
        const float x = std::abs(v.x), side = v.x >= 0 ? 0.f : 2.f;
        if (flight) {
            if (v.z < .33f && x < .3f && v.y - ymid < .4f) return 5;
            if (x > 1.1f) return int(2 + side);
            if (x > .3f && !(v.y - ymid < -.5f && x < .42f)) return int(1 + side);
            return 0;
        }
        return (v.y - ymid > .15f && v.z > .95f) ? 1 : 0;
    };
    // crease-angle normals in the figure's frame
    const std::size_t F = part.tris.size();
    std::vector<V3> fn(F);
    std::map<std::array<long long, 3>, std::vector<std::size_t>> at;
    const auto key = [](V3 v) { return std::array<long long, 3>{std::llround(v.x * 1e4), std::llround(v.y * 1e4), std::llround(v.z * 1e4)}; };
    for (std::size_t f = 0; f < F; ++f) {
        const auto& t = part.tris[f];
        fn[f] = cross3(raw[t[1]] - raw[t[0]], raw[t[2]] - raw[t[0]]);
        for (int c = 0; c < 3; ++c) at[key(raw[t[c]])].push_back(f);
    }
    for (std::size_t f = 0; f < F; ++f) {
        const auto& t = part.tris[f];
        const V3 c = (raw[t[0]] + raw[t[1]] + raw[t[2]]) * (1.f / 3);
        const V3 cy{c.x, c.y - ymid, c.z};
        const int region = statue_region(flight, cy);
        const float fl = len3(fn[f]);
        if (!(fl > 1e-12f)) continue;
        const V3 n0 = fn[f] * (1 / fl);
        for (int k = 0; k < 3; ++k) {
            const V3 v = raw[t[k]], vy{v.x, v.y - ymid, v.z};
            V3 n{};
            for (const auto g : at[key(v)]) {
                const float gl = len3(fn[g]);
                if (gl > 1e-12f && dot3(fn[g] * (1 / gl), n0) > .57f) n = n + fn[g];
            }
            const float x = std::abs(v.x);
            float u = 0, w = 0;
            switch (region) {
            case 0: {   // body strip: along the body, around it belly to back
                const float zc = flight ? .62f : .8f;
                u = .6f * clamp01((vy.y + (flight ? .5f : .6f)) / (flight ? .75f : .8f));
                w = .5f * std::atan2(x, -(v.z - zc)) / PI;
                break;
            }
            case 1:     // head strip; the beak tip on the beak tile
                if (vy.y > ymax - .12f) { u = .5f + .25f * clamp01((vy.y - ymax + .12f) / .12f); w = .875f + .125f * clamp01(v.z / 1.4f); }
                else {
                    u = .6f + .4f * clamp01((vy.y - .2f) / std::max(ymax - .32f, .05f));
                    w = .5f * std::atan2(x, -(v.z - (flight ? .45f : 1.25f))) / PI;
                }
                break;
            case 2: case 5: {   // wing: top or underside, span and chord
                const bool top = flight ? n0.z >= 0 : n0.x * (c.x >= 0 ? 1.f : -1.f) >= 0;
                float span, chord;
                if (flight) {
                    span = region == 2 ? .4f * clamp01((x - .25f) / .8f) : .4f + .6f * clamp01((x - 1.05f) / .89f);
                    chord = clamp01((.8f - vy.y) / 1.65f);
                } else {
                    span = .4f + .6f * clamp01((.25f - vy.y) / 1.35f);
                    chord = clamp01((1.3f - v.z) / 1.05f);
                }
                u = span; w = top ? .5f + .25f * chord : .75f + .125f * chord;
                break;
            }
            case 3:
                u = .5f * clamp01((flight ? (-.45f - vy.y) / .55f : (-.55f - vy.y) / .58f));
                w = .875f + .125f * clamp01((v.x + .45f) / .9f);
                break;
            default:
                u = .75f + .25f * clamp01(x / .3f); w = .875f + .125f * clamp01(v.z / .4f);
                break;
            }
            out.p.push_back(local(v)); out.uv.push_back({u, w}); out.n.push_back(len3(n) > 1e-12f ? n : n0);
            out.sub.push_back(std::uint8_t(vertex_bone(v)));
            bone_of.push_back(region);
        }
        out.smooth.push_back(0);
    }
    return true;
}
bool build_statue(const Model& fly, const Model& perch, StatueBird& s, std::string& error) {
    std::vector<int> fly_region, perch_region;
    if (!build_statue_mesh(fly, "eag1", true, s.fly, fly_region, error) ||
        !build_statue_mesh(perch, "eag2", false, s.perch, perch_region, error)) return false;
    // Hinges: the right arm turns about the line through the body vertices
    // its triangles use, the hand about the arm vertices its triangles use.
    const auto hinge = [&](const PartMesh& m, const std::vector<int>& region, int want_region, int inner_bone, V3& pivot, V3& axis) {
        V3 a{1e9f, 1e9f, 0}, b{-1e9f, -1e9f, 0};
        bool found = false;
        for (std::size_t i = 0; i < m.p.size(); ++i) {
            if (region[i] != want_region || m.sub[i] != inner_bone || m.p[i].x <= 0) continue;
            if (m.p[i].y < a.y) a = m.p[i];
            if (m.p[i].y > b.y) b = m.p[i];
            found = true;
        }
        if (!found || b.y - a.y < 1e-3f) return false;
        pivot = (a + b) * .5f; axis = (b - a) * (1 / len3(b - a));
        return true;
    };
    if (!hinge(s.fly, fly_region, 2, 0, s.shoulder, s.shoulder_axis) || !hinge(s.fly, fly_region, 5, 1, s.wrist, s.wrist_axis)) {
        error = "statue eagle: wing hinges not found"; return false;
    }
    float leg_top = -1e9f, leg_y = 0, leg_n = 0, neck_z = 1e9f, neck_y = 0, neck_n = 0;
    for (std::size_t i = 0; i < s.fly.p.size(); ++i)
        if (s.fly.sub[i] == 5) { leg_top = std::max(leg_top, s.fly.p[i].z); leg_y += s.fly.p[i].y; ++leg_n; }
    for (std::size_t i = 0; i < s.perch.p.size(); ++i)
        if (s.perch.sub[i] == 1 && perch_region[i] == 0) { neck_z = std::min(neck_z, s.perch.p[i].z); neck_y += s.perch.p[i].y; ++neck_n; }
    if (!leg_n || !neck_n) { error = "statue eagle: legs or neck not found"; return false; }
    s.hip = {0, leg_y / leg_n, leg_top};
    s.neck = {0, neck_y / neck_n, neck_z};
    s.perch_hip = {0, 0, .35f};
    // vertices relative to their bone's pivot (bones are rigid about it)
    const auto pivot_fly = [&](int bone) {
        switch (bone) {
        case 1: return s.shoulder; case 2: return s.wrist; case 3: return mirror_x(s.shoulder); case 4: return mirror_x(s.wrist);
        case 5: return s.hip; default: return V3{};
        }
    };
    for (std::size_t i = 0; i < s.fly.p.size(); ++i) s.fly.p[i] = s.fly.p[i] - pivot_fly(s.fly.sub[i]);
    for (std::size_t i = 0; i < s.perch.p.size(); ++i) if (s.perch.sub[i] == 1) s.perch.p[i] = s.perch.p[i] - s.neck;
    return true;
}

struct Mini {
    const char* model; const char* skin; float length; unsigned budget; std::vector<std::string> drop; bool tinted; float tint[3];
    const char* clips[3];   // idle, walk, run (the bat: its two flight loops)
    bool flyer = false;     // authored upright, flying: laid level, posed by its first clip, never decimated, morphs kept
};
const Mini& mini_spec(Mesh m) {
    static const Mini specs[6] = {
        {"unanwira", "rat00", .24f, 240, {"box01", "chelust"}, false, {}, {"cidle", "cwalk", "crun"}},
        {"unanwira", "rat00", .13f, 200, {"box01", "chelust", "tl3", "tl4", "tl5"}, true, {205, 200, 196}, {"cidle", "cwalk", "crun"}},
        {"unmosp", "spider00", .12f, 260, {"box01"}, false, {}, {"cidle", "cwalk", "crun"}},
        {"unmoto", "toad00", .065f, 240, {"box", "deard", "jaw-bone", "baserh3"}, true, {112, 150, 70}, {"cidle", "cwalk", "crun"}},
        // the full 253-triangle crawler body: decimated, the segment rings turned into flat fins
        {"unanwicr", "crab00", .07f, 260, {"lh1", "lh2", "lh3", "rh1", "rh2", "rh3", "ll1", "ll2", "ll3", "rl1", "rl2", "rl3"}, false, {},
         {"cidle", "cwalk", "crun"}},
        // cave bat: the game's bat figure at a quarter metre span, its skin darkened so it never reads as
        // one of the game's attackable bats (1.16 m span, orange-brown)
        {"unanwiba", "bat00", .25f, 4000, {"box"}, true, {92, 76, 66}, {"cflight01", "cflight01", "cflight02"}, true},
    };
    return specs[m <= m_bat ? m : 0];
}
constexpr float kMiniFps = 15.f;

struct MeshRange { std::uint32_t first = 0, count = 0; };
constexpr const char* kMeshNames[m_count] = {"rat", "snow mouse", "spider", "frog", "larva", "bat", "lizard", "snake", "crab",
    "centipede", "dung beetle", "dung ball", "small bird", "crow/raven", "far bird", "vulture", "perched vulture"};
// Species that cannot be drawn because one of their meshes was omitted.
std::uint32_t unavailable_species(const std::array<std::string, m_count>& omitted) {
    static const std::initializer_list<Mesh> uses[sp_count] = {
        {m_rat}, {m_spider}, {m_larva}, {m_frog}, {m_lizard}, {m_snake}, {m_scolopendra}, {m_dung, m_ball},
        {m_bird, m_bird_far}, {m_corvid, m_bird_far}, {m_vulture, m_vulture_perch}, {m_crab}, {m_bird, m_bird_far},
        {m_snowmouse}, {m_bat}};
    std::uint32_t mask = 0;
    for (unsigned s = 0; s < sp_count; ++s)
        for (const Mesh m : uses[s]) if (!omitted[m].empty()) mask |= 1u << s;
    return mask;
}
struct MiniClips {
    std::uint32_t start[3]{}, frames[3]{}; std::uint32_t nodes = 0; float scale = 1;
    std::uint32_t morph_start = 0, morph_points = 0, morph_frames = 0;   // first clip's vertex morphs (baked offsets)
};
struct CritterAssets {
    std::string error;
    std::vector<CrVertex> vertices;
    std::array<MeshRange, m_count> meshes{};
    std::vector<Bone> baked;
    std::array<MiniClips, m_bat + 1> minis{};
    std::uint32_t jaw_start = 0, jaw_frames = 1;
    float larva_length = .07f, larva_center_z = 0, larva_tail_y = 0;
    std::vector<std::uint8_t> texels;
    std::shared_ptr<StatueBird> statue;
    // A mesh whose game asset failed to load is omitted with its reason; its
    // species stays out, every other species keeps going.
    std::array<std::string, m_count> omitted;   // s_count slices of kSkin^2 RGBA
};

void put_slice(CritterAssets& a, Slice s, const Image& img) {
    const Image src = img.w == kSkin ? img : resize(img, kSkin);
    std::uint8_t* out = &a.texels[std::size_t(s) * kSkin * kSkin * 4];
    for (std::size_t i = 0; i < std::size_t(kSkin) * kSkin; ++i) {
        out[i * 4] = src.rgb[i * 3]; out[i * 4 + 1] = src.rgb[i * 3 + 1]; out[i * 4 + 2] = src.rgb[i * 3 + 2]; out[i * 4 + 3] = 255;
    }
}

std::shared_ptr<CritterAssets> build_assets(ResourceStoreConfig mounts) {
    auto assets = std::make_shared<CritterAssets>();
    auto& a = *assets;
    try {
        ResourceStore store;
        ResourceFailure failure;
        if (!store.open(mounts, failure)) { a.error = "archives: " + failure.message; return assets; }
        // The larva is the base game's crawler and skin (the approved
        // prototype): a mod may replace unanwicr with a high-detail figure
        // (Dejavu: 5242 triangles, oversized mandibles) that does not read as
        // a grub. The base archives are the last figures/textures in the
        // mount priority (callers append the base fallback last).
        ResourceStore base_store;
        bool own_base = false;
        {
            const auto last = [&](const wchar_t* file) {
                const std::wstring tail(file);
                for (auto it = mounts.archives.rbegin(); it != mounts.archives.rend(); ++it) {
                    if (it->size() < tail.size()) continue;
                    std::wstring end = it->substr(it->size() - tail.size());
                    for (auto& c : end) c = wchar_t(std::towlower(c));
                    if (end == tail) return *it;
                }
                return std::wstring();
            };
            ResourceStoreConfig base_mounts;
            const std::wstring figures = last(L"\\figures.res"), textures = last(L"\\textures.res");
            if (!figures.empty() && !textures.empty()) {
                base_mounts.archives = {figures, textures};
                ResourceFailure ignored;
                own_base = base_store.open(base_mounts, ignored);
            }
        }
        ResourceStore& larva_store = own_base ? base_store : store;
        // Skins (DXT1 mip 0).
        std::map<std::string, Image> skins;
        const auto decode_skin = [&](ResourceStore& from, const char* name, Image& out, std::string& why) {
            ResourceBlob blob;
            TextureCpuAsset texture;
            if (!from.resolve(std::string(name) + ".mmp", blob, failure) || !parse_texture_asset(blob, texture, failure)) {
                why = std::string(name) + ": " + failure.message; return false;
            }
            if (!decode_skin_texture(texture, out) || out.w < 64 || out.w != out.h || out.w % 64) {
                why = std::string(name) + ": unsupported skin format (MMP " + std::to_string(texture.width) + "x" +
                      std::to_string(texture.height) + ", " + std::to_string(texture.bit_count) + " bit)";
                return false;
            }
            return true;
        };
        const auto omit = [&](Mesh m, const std::string& why) { if (a.omitted[m].empty()) a.omitted[m] = why; };
        const auto ok = [&](Mesh m) { return a.omitted[m].empty(); };
        const struct { const char* skin; std::initializer_list<Mesh> users; } skin_users[] = {
            {"rat00", {m_rat, m_snowmouse}}, {"spider00", {m_spider}}, {"toad00", {m_frog, m_lizard, m_snake}},
            {"crab00", {m_crab, m_scolopendra, m_dung, m_ball}},
            {"bird", {m_bird, m_corvid, m_bird_far, m_vulture, m_vulture_perch}}, {"bat00", {m_bat}}};
        for (const auto& entry : skin_users) {
            std::string why;
            if (!decode_skin(store, entry.skin, skins[entry.skin], why)) { skins.erase(entry.skin); for (const Mesh m : entry.users) omit(m, why); }
        }
        Image larva_skin;
        {
            std::string why;
            if (!decode_skin(larva_store, "crab00", larva_skin, why)) omit(m_larva, why);
        }
        a.texels.assign(std::size_t(s_count) * kSkin * kSkin * 4, 0);
        // Miniature skins at the prototype's 64 px (shown on 128 px slices).
        for (const Mesh m : {m_rat, m_snowmouse, m_spider, m_frog, m_larva, m_bat}) {
            if (!ok(m)) continue;
            const auto& spec = mini_spec(m);
            Image skin = m == m_larva ? larva_skin : skins[spec.skin];
            if (spec.tinted) skin = tint(skin, spec.tint);
            put_slice(a, Slice(m), downsample(skin, 64));
        }
        // Procedural atlases: four tinted 64 px crops of game skins.
        struct Crop { const char* skin; float box[4]; float rgb[3]; };
        const float toad_body[4]{.02f, .52f, .36f, .9f},
                    crab_plate[4]{.45f, .62f, .9f, .86f}, crab_leg[4]{.05f, .8f, .35f, 1.f};
        const auto box = [](const float* b) { return std::array<float, 4>{b[0], b[1], b[2], b[3]}; };
        struct Atlas { Slice slice; std::array<std::pair<const char*, std::array<float, 4>>, 4> crops; float rgb[4][3]; };
        const Atlas atlases[] = {
            {s_crab, {{{"crab00", box(crab_plate)}, {"crab00", box(crab_leg)}, {"crab00", box(crab_leg)}, {"crab00", box(crab_plate)}}},
             {{160, 98, 64}, {176, 112, 72}, {128, 80, 54}, {150, 82, 52}}},
        };
        for (const auto& atlas : atlases) {
            if (!ok(m_crab)) continue;
            Image img; img.w = img.h = kSkin; img.rgb.assign(std::size_t(kSkin) * kSkin * 3, 0);
            for (int i = 0; i < 4; ++i) {
                const Image tile = tint(resize(crop(skins[atlas.crops[i].first], atlas.crops[i].second.data()), 64), atlas.rgb[i]);
                const unsigned ox = (i % 2) * 64, oy = (i / 2) * 64;
                for (unsigned y = 0; y < 64; ++y) std::memcpy(&img.rgb[(std::size_t(oy + y) * kSkin + ox) * 3], &tile.rgb[std::size_t(y) * 64 * 3], 64 * 3);
            }
            put_slice(a, atlas.slice, img);
        }
        // Spined critters and the beetle: painted strips over toad / scrab grain.
        {
            const bool have_toad = skins.count("toad00") != 0, have_scrab = skins.count("crab00") != 0;
            const Image toad = have_toad ? resize(crop(skins["toad00"], toad_body), kSkin) : Image{};
            const Image scrab = have_scrab ? resize(crop(skins["crab00"], crab_plate), kSkin) : Image{};
            const StripPaint lizard{{176, 164, 116}, {118, 112, 66}, {66, 60, 38}, {110, 104, 62}, {104, 96, 60}, {104, 96, 60},
                                    pt_blotches, 7, .9f, .955f};
            const StripPaint snake{{170, 160, 118}, {92, 98, 70}, {44, 42, 32}, {80, 84, 60}, {92, 98, 70}, {92, 98, 70},
                                   pt_zigzag, 14, .93f, .975f};
            const StripPaint centipede{{196, 146, 62}, {136, 60, 30}, {44, 26, 20}, {98, 40, 24}, {200, 150, 60}, {204, 154, 64},
                                       pt_bands, 10, .92f, 0};
            const StripPaint beetle{{24, 22, 20}, {38, 34, 32}, {96, 92, 88}, {30, 28, 26}, {96, 74, 46}, {24, 22, 20},
                                    pt_shell, 1, .86f, 0};
            if (have_toad) { put_slice(a, s_lizard, paint_strip(lizard, toad)); put_slice(a, s_snake, paint_strip(snake, toad)); }
            if (have_scrab) { put_slice(a, s_scolopendra, paint_strip(centipede, scrab)); put_slice(a, s_dung, paint_strip(beetle, scrab)); }
        }
        // Birds: painted slices carried by the game bird's (bird.mmp) feather detail.
        if (skins.count("bird")) {
            const Image& grain = skins["bird"];
            const Slice slices[bird_count]{s_finch, s_sparrow, s_bunting, s_snowbird, s_crow, s_raven, s_vulture, s_bat};
            for (unsigned b = 0; b < bird_count; ++b) if (b != bird_bat) put_slice(a, slices[b], paint_bird(bird_paint(Bird(b)), grain));
        }

        // Miniatures.
        std::map<std::string, Model> models;
        for (const Mesh m : {m_rat, m_snowmouse, m_spider, m_frog, m_larva, m_bat}) {
            const auto& spec = mini_spec(m);
            if (!ok(m) || models.count(spec.model)) continue;
            std::string error;
            if (!load_model(m == m_larva ? larva_store : store, spec.model, models[spec.model], error)) {
                models.erase(spec.model);
                for (const Mesh other : {m_rat, m_snowmouse, m_spider, m_frog, m_larva, m_bat})
                    if (mini_spec(other).model == spec.model) omit(other, error);
            }
        }
        std::vector<Q4> Q; std::vector<V3> P;
        const auto build_mini = [&](Mesh m) -> std::string {
            const auto& spec = mini_spec(m);
            const Model& model = models[spec.model];
            std::size_t total = 0;
            std::vector<char> kept(model.nodes.size(), 0);
            for (std::size_t i = 0; i < model.nodes.size(); ++i) {
                kept[i] = !model.parts[i].tris.empty() && std::find(spec.drop.begin(), spec.drop.end(), model.nodes[i]) == spec.drop.end();
                if (kept[i]) total += model.parts[i].tris.size();
            }
            if (!total) return std::string(spec.model) + ": no parts";
            const float ratio = std::min(1.f, float(spec.budget) / float(total));
            std::vector<std::vector<std::array<V3, 3>>> dec_p(model.nodes.size());
            std::vector<std::vector<std::array<std::array<float, 2>, 3>>> dec_uv(model.nodes.size());
            for (std::size_t i = 0; i < model.nodes.size(); ++i) {
                if (!kept[i]) continue;
                const std::size_t n = model.parts[i].tris.size();
                // a large part keeps at least 40 triangles (a high-detail mod
                // figure must not turn a body ring into a flat fin)
                std::size_t target = std::max<std::size_t>(6, std::size_t(std::lround(n * ratio)));
                if (n >= 80) target = std::max<std::size_t>(target, 40);
                target = std::min(n, target);
                if (spec.flyer) {   // the whole figure: its morphing membrane keeps every plot
                    const Part& part = model.parts[i];
                    for (const auto& t : part.tris) {
                        dec_p[i].push_back({part.p[t[0]], part.p[t[1]], part.p[t[2]]});
                        dec_uv[i].push_back({part.uv[t[0]], part.uv[t[1]], part.uv[t[2]]});
                    }
                } else decimate(model.parts[i], target, dec_p[i], dec_uv[i]);
            }
            // Scale to the target length and stand on z=0 in the idle pose
            // ("cidle", or the bind pose when the model has no such clip).
            model_pose(model, m == m_larva ? nullptr : model.clip(spec.flyer ? spec.clips[0] : "cidle"), 0, Q, P);
            // A flyer whose clip has no vertex morphs is authored upright (head
            // +Z, belly +Y): lay it level, head forward along the figure's -Y.
            // A morphing flyer (Dejavu's bat) is laid level by its morphs.
            M3 pre;
            const Clip* flight = spec.flyer ? model.clip(spec.clips[0]) : nullptr;
            bool morphs = false;
            if (flight) for (const auto& t : flight->tracks) morphs = morphs || t.morph_points;
            if (spec.flyer && !morphs) pre = columns({-1, 0, 0}, {0, 0, -1}, {0, -1, 0});
            float lo[3]{1e9f, 1e9f, 1e9f}, hi[3]{-1e9f, -1e9f, -1e9f};
            for (std::size_t i = 0; i < model.nodes.size(); ++i)
                for (const auto& t : dec_p[i])
                    for (const auto& v : t) {
                        const V3 w = pre * (qrot(Q[i], v) + P[i]);
                        const float c[3]{w.x, w.y, w.z};
                        for (int k = 0; k < 3; ++k) { lo[k] = std::min(lo[k], c[k]); hi[k] = std::max(hi[k], c[k]); }
                    }
            const float span = std::max(hi[0] - lo[0], hi[1] - lo[1]);
            if (span <= 1e-6f) return std::string(spec.model) + ": empty bounds";
            const float scale = spec.length / span, ground = lo[2] * scale;
            M3 flip; flip.m[0][0] = -scale; flip.m[1][1] = -scale; flip.m[2][2] = scale;   // EI models face -Y
            flip = flip * pre;
            auto& range = a.meshes[m];
            range.first = std::uint32_t(a.vertices.size());
            if (m != m_larva) {
                // Morphing parts (the first clip's tracks with vertex morphs): a
                // frame holds every morphing part's plots back to back; the
                // corner keeps its index there in s and flags bit 2.
                const Clip* first = model.clip(spec.clips[0]);
                std::vector<std::uint32_t> morph_base(model.nodes.size(), 0);
                std::uint32_t morph_size = 0, morph_frames = 0;
                if (spec.flyer && first)
                    for (std::size_t i = 0; i < model.nodes.size(); ++i) {
                        const Track& t = first->tracks[i];
                        if (!t.morph_points || !kept[i]) continue;
                        morph_base[i] = morph_size; morph_size += t.morph_points; morph_frames = std::max(morph_frames, t.morph_frames);
                    }
                for (std::size_t i = 0; i < model.nodes.size(); ++i) {
                    const bool morph = morph_size && first->tracks[i].morph_points && kept[i];
                    for (std::size_t k = 0; k < dec_p[i].size(); ++k)
                        for (int c = 0; c < 3; ++c) {
                            CrVertex v{{dec_p[i][k][c].x, dec_p[i][k][c].y, dec_p[i][k][c].z}, std::uint32_t(i),
                                       {dec_uv[i][k][c][0], dec_uv[i][k][c][1]}, 0, 0};
                            if (morph) {
                                const std::uint32_t plot = model.parts[i].plot[model.parts[i].tris[k][c]];
                                if (plot < first->tracks[i].morph_points) { const std::uint32_t at = morph_base[i] + plot; std::memcpy(&v.s, &at, 4); v.flags = 4; }
                            }
                            a.vertices.push_back(v);
                        }
                }
                // Baked clips: one matrix per node and frame.
                auto& clips = a.minis[m];
                clips.nodes = std::uint32_t(model.nodes.size());
                clips.scale = scale;
                for (int c = 0; c < 3; ++c) {
                    const char* const* names = spec.clips;
                    const Clip* clip = model.clip(names[c]);
                    if (!clip) return std::string(spec.model) + ": clip " + names[c];
                    clips.start[c] = std::uint32_t(a.baked.size());
                    clips.frames[c] = clip->frames;
                    for (unsigned f = 0; f < clip->frames; ++f) {
                        model_pose(model, clip, float(f), Q, P);
                        for (std::size_t i = 0; i < model.nodes.size(); ++i)
                            a.baked.push_back(affine(flip * qmatrix(Q[i]), flip * P[i] - V3{0, 0, ground}));
                    }
                }
                // The first clip's vertex morphs: per frame, one baked entry per
                // morphing plot holding its offset in r0.xyz. The game adds them
                // on top of the skeleton, in the figure's frame, so they take the
                // figure's scale and orientation but no part rotation.
                if (morph_size) {
                    clips.morph_start = std::uint32_t(a.baked.size()); clips.morph_points = morph_size; clips.morph_frames = morph_frames;
                    for (std::uint32_t f = 0; f < morph_frames; ++f)
                        for (std::size_t i = 0; i < model.nodes.size(); ++i) {
                            const Track& t = first->tracks[i];
                            if (!t.morph_points || !kept[i]) continue;
                            const std::uint32_t tf = f % t.morph_frames;
                            for (std::uint32_t q = 0; q < t.morph_points; ++q) {
                                const V3 d = flip * t.morphs[std::size_t(tf) * t.morph_points + q];
                                Bone b{}; b.r[0][0] = d.x; b.r[0][1] = d.y; b.r[0][2] = d.z;
                                a.baked.push_back(b);
                            }
                        }
                }
            } else {
                // Larva: rest-pose vertices in its own frame, body coordinate
                // s (tail 0 .. head 1), mandible flag.
                std::size_t jaw_node = model.nodes.size(), head_node = model.nodes.size();
                for (std::size_t i = 0; i < model.nodes.size(); ++i) {
                    if (model.nodes[i] == "chelust") jaw_node = i;
                    if (model.nodes[i] == "hd") head_node = i;
                }
                if (jaw_node == model.nodes.size() || head_node == model.nodes.size()) return "unanwicr: jaw/head nodes";
                const auto local = [&](V3 w) { return flip * w - V3{0, 0, ground}; };
                std::vector<V3> rest;
                for (std::size_t i = 0; i < model.nodes.size(); ++i)
                    for (std::size_t k = 0; k < dec_p[i].size(); ++k)
                        for (int c = 0; c < 3; ++c) {
                            const V3 v = local(qrot(Q[i], dec_p[i][k][c]) + P[i]);
                            rest.push_back(v);
                            a.vertices.push_back({{v.x, v.y, v.z}, 0, {dec_uv[i][k][c][0], dec_uv[i][k][c][1]}, 0, i == jaw_node ? 1u : 0u});
                        }
                float ymin = 1e9f, ymax = -1e9f;
                std::vector<float> zs;
                for (const auto& v : rest) { ymin = std::min(ymin, v.y); ymax = std::max(ymax, v.y); zs.push_back(v.z); }
                std::nth_element(zs.begin(), zs.begin() + zs.size() / 2, zs.end());
                float median = zs[zs.size() / 2];
                if (zs.size() % 2 == 0) {
                    const float below = *std::max_element(zs.begin(), zs.begin() + zs.size() / 2);
                    median = (median + below) / 2;
                }
                a.larva_length = std::max(ymax - ymin, 1e-4f);
                a.larva_center_z = median;
                a.larva_tail_y = ymin + kLarvaStride;
                for (std::size_t k = range.first; k < a.vertices.size(); ++k)
                    a.vertices[k].s = (a.vertices[k].p[1] - ymin) / a.larva_length;
                // Mandibles: the idle clip's jaw motion relative to the head,
                // re-applied to the rest-pose head, as a rest-to-animated map.
                const Clip* jaw = model.clip("cidle01");
                if (!jaw) return "unanwicr: clip cidle01";
                const std::vector<Q4> Q0 = Q; const std::vector<V3> P0 = P;
                a.jaw_start = std::uint32_t(a.baked.size());
                a.jaw_frames = jaw->frames;
                M3 unflip; unflip.m[0][0] = -1 / scale; unflip.m[1][1] = -1 / scale; unflip.m[2][2] = 1 / scale;
                const Q4 q0j = Q0[jaw_node], q0j_inv{q0j[0], -q0j[1], -q0j[2], -q0j[3]};
                for (unsigned f = 0; f < jaw->frames; ++f) {
                    model_pose(model, jaw, float(f), Q, P);
                    const Q4 qh_inv{Q[head_node][0], -Q[head_node][1], -Q[head_node][2], -Q[head_node][3]};
                    const auto animate = [&](V3 y) {
                        const V3 tp = qrot(q0j_inv, unflip * (y + V3{0, 0, ground}) - P0[jaw_node]);
                        const V3 in_head = qrot(qh_inv, qrot(Q[jaw_node], tp) + P[jaw_node] - P[head_node]);
                        return local(qrot(Q0[head_node], in_head) + P0[head_node]);
                    };
                    const V3 o = animate({});
                    a.baked.push_back(affine(columns(animate({1, 0, 0}) - o, animate({0, 1, 0}) - o, animate({0, 0, 1}) - o), o));
                }
            }
            range.count = std::uint32_t(a.vertices.size()) - range.first;
            return std::string();
        };
        for (const Mesh m : {m_rat, m_snowmouse, m_spider, m_frog, m_larva, m_bat}) {
            if (!ok(m)) continue;
            const std::size_t vertices = a.vertices.size(), baked = a.baked.size();
            const std::string why = build_mini(m);
            if (!why.empty()) { a.vertices.resize(vertices); a.baked.resize(baked); a.meshes[m] = {}; omit(m, why); }
        }

        // Vultures: the game's eagle statues (stst112 spread, stst113 perched).
        {
            Model fly, perch;
            std::string error;
            a.statue = std::make_shared<StatueBird>();
            if (!load_model(store, "stst112", fly, error) || !load_model(store, "stst113", perch, error) ||
                !build_statue(fly, perch, *a.statue, error)) { omit(m_vulture, error); omit(m_vulture_perch, error); }
        }
        // Procedural meshes: record canonical pieces at the rest pose and the
        // prototype's per-part planar UVs into atlas tiles.
        for (unsigned mi = m_lizard; mi < m_count; ++mi) {
            const Mesh m = Mesh(mi);
            if (!procedural_rig(m) || !ok(m)) continue;
            std::vector<Bone> bones;
            Rig rig; rig.bones = &bones; rig.record = true; rig.statue = a.statue.get();
            const PoseP pose = rest_pose(m);
            procedural_rig(m)(rig, pose);
            auto& range = a.meshes[m];
            range.first = std::uint32_t(a.vertices.size());
            for (int g = 0; g <= rig.group; ++g) {
                float lo[3]{1e9f, 1e9f, 1e9f}, hi[3]{-1e9f, -1e9f, -1e9f};
                for (const auto& v : rig.vertices) {
                    if (v.group != g) continue;
                    const V3 w = apply(bones[v.bone], v.p);
                    const float c[3]{w.x, w.y, w.z};
                    for (int k = 0; k < 3; ++k) { lo[k] = std::min(lo[k], c[k]); hi[k] = std::max(hi[k], c[k]); }
                }
                float span[3];
                for (int k = 0; k < 3; ++k) span[k] = std::max(hi[k] - lo[k], 1e-6f);
                const int region = rig.regions[g];
                const auto b = region >= 0 ? atlas_box(region) : std::array<float, 4>{0, 0, 1, 1};
                for (const auto& v : rig.vertices) {
                    if (v.group != g) continue;
                    if (v.authored) {   // smooth part: authored UV in its tile, packed normal
                        a.vertices.push_back({{v.p.x, v.p.y, v.p.z}, v.bone, {b[0] + v.u * (b[2] - b[0]), b[1] + v.v * (b[3] - b[1])}, 0, v.normal});
                        continue;
                    }
                    const V3 w = apply(bones[v.bone], v.p);
                    const float u = (w.y - lo[1]) / span[1], vv = .5f * (w.x - lo[0]) / span[0] + .5f * (w.z - lo[2]) / span[2];
                    a.vertices.push_back({{v.p.x, v.p.y, v.p.z}, v.bone, {b[0] + u * (b[2] - b[0]), b[1] + vv * (b[3] - b[1])}, 0, 0});
                }
            }
            range.count = std::uint32_t(a.vertices.size()) - range.first;
        }
        // Every mesh must have triangles: one without geometry is omitted
        // (named, logged), never a silently skipped draw.
        for (unsigned m = 0; m < m_count; ++m)
            if (ok(Mesh(m)) && (!a.meshes[m].count || a.meshes[m].count % 3)) {
                omit(Mesh(m), "no triangle geometry (" + std::to_string(a.meshes[m].count) + " vertices)");
                a.meshes[m] = {};
            }
    } catch (...) {
        a.error = "allocation failed while building critter assets";
    }
    return assets;
}

// ---------------------------------------------------------------- simulation
constexpr float kFirstDelay = 3.f, kAttemptEvery = 1.f, kAttemptChance = .5f, kSpeciesCooldown = 60.f;
constexpr float kCooldownMin = 8.f, kCooldownMax = 18.f, kIdleMin = 15.f, kIdleMax = 35.f;
constexpr float kCombatQuiet = 20.f, kUndeadExclusion = 35.f, kDeadZoneWeight = .4f;
// Every living critter avoids the undead: none appears within kUndeadExclusion
// of one, and one coming within kUndeadReach (or its own sight/hearing reach)
// sends it off, whatever its size.
constexpr float kUndeadReach = 12.f;
// The camera is a render-only input: no critter or bird path depends on it
// (birds near the lens are faded by the draw, never steered).
constexpr unsigned kFlockBirds = 8;
constexpr float kBatPath = 36.f;   // metres a bat fly-by travels, centred on its spawn spot

struct BirdFlight { float speed, radius, altitude, flee, hz, soar; };
const BirdFlight& bird_flight(Bird b) {
    static const BirdFlight table[bird_count] = {{5, 3, 2.5f, 5, 13, 0}, {5, 3, 2.5f, 5, 13, 0}, {5, 3, 2.5f, 5, 13, 0},
        {5, 3, 2.5f, 5, 13, 0}, {6.5f, 4.5f, 4, 7, 5, .2f}, {6.5f, 5, 4.5f, 7.5f, 4.5f, .35f},
        {7, 12, 20, 9, 2, .9f},                                        // vultures: a 20 m thermal at scale 1
        {3.5f, 2.5f, 2.6f, 4, 9, 0}};                                  // cave bats: a loose swarm 2-3 m up
    return table[b < bird_count ? b : 0];
}
// A flock's flight at its display scale: circles and altitude grow with the
// bird (the vulture thermal keeps a fixed 4 m base: 4 + 16 m x scale), larger
// birds fly a little faster and beat slower. Threat reach (flee) stays the species'.
BirdFlight bird_flight(Bird b, float scale) {
    BirdFlight fl = bird_flight(b);
    const float root = std::sqrt(scale);
    fl.radius *= scale; fl.speed *= root; fl.hz /= root;
    fl.altitude = b == bird_vulture ? 4 + (fl.altitude - 4) * scale : fl.altitude * scale;
    return fl;
}
bool big_bird(Bird b) { return b == bird_crow || b == bird_raven || b == bird_vulture; }
// Each bird's own cruise speed (+-18 %), wingbeat rate (+-12 %) and wingbeat
// phase, stable per bird (its key), so a flock never flies or beats in step.
float bird_pace(std::uint64_t bird) { return 1 + .18f * (2 * hash01(bird, key("pace")) - 1); }
float bird_beat(std::uint64_t bird) { return 1 + .12f * (2 * hash01(bird, key("beat")) - 1); }
float bird_wing_phase(std::uint64_t bird) { return hash01(bird, key("wph")); }

enum AgentState : std::uint8_t { st_appear, st_idle, st_flee, st_leave, st_vanish, st_gone };
struct Agent {
    Species species = sp_rat;
    Bird bird = bird_none;
    AgentState state = st_appear;
    bool flying = false, has_target = false, has_ball = false, hop_clip = false;
    std::uint64_t key = 0;
    float x = 0, y = 0, z = 0, yaw = 0, pitch = 0, t = 0, age = 0, gait = 0, speed = 0, alpha = 0;
    float seen = 0;   // party sight: 1 inside a controlled unit's vision, fading on game time
    float pushup = 0, peck = 0, caw = 0, wing = 0, soar = 0, claw = 0, push = 0, search = 0;
    float scale = 1; // immutable display scale, chosen using the spawn camera
    float idle_limit = 0, tx = 0, ty = 0, fx = 0, fy = 1;
    float vx = 0, vy = 0, vz = 0, slot_x = 0, slot_y = 0, delay = 0;
    float ball_x = 0, ball_y = 0, ball_roll = 0, ball_yaw = 0;
    int bout = -1, bout_n = 0;
    float pause = 0, bdx = 0, bdy = 1;
    // birds: wing fold (1 folded), hop flick, crouch, tail kick; a hop in
    // progress (seconds since it began, <0 none) from its start point
    float fold = 1, flick = 0, crouch = 0, tail = 0, roll = 0;
    // carrion: game seconds fed and the bird's own appetite; solo 1 = leaving
    // alone (done feeding), 2 = a newcomer arriving alone at a free slot
    float feed = 0, feed_limit = 0;
    std::uint8_t solo = 0;
    // perch: the slot's height above the ground and a hop's start/end heights
    float slot_z = 0, hop_z0 = 0, hop_z1 = 0;
    int perch = -1;   // index of its perch point
    float hop = -1, hop_x = 0, hop_y = 0, hop_dx = 0, hop_dy = 0, hop_yaw0 = 0, hop_yaw1 = 0;
    int hop_slot = -1;
    // landing: the bird's own approach line into its slot (unit, slot to
    // bird), fixed when its descent begins
    bool approach_set = false;
    float approach_x = 0, approach_y = 1;
};
enum FlockState : std::uint8_t { fl_ground, fl_circle, fl_land, fl_leave, fl_gone };
struct Flock {
    bool active = false, carrion = false, land_ok = true;
    float corpse_age = 0, next_newcomer = 0;   // carrion: game seconds since the death; newcomer timer
    float corpse_body = .5f;                   // carrion: half length of the body's footprint (m)
    // perching flock: the tree (its map object position) and its perch points (world)
    bool perching = false, perch_cactus = false;   // vultures perch on cacti
    float tree[2]{};
    std::uint8_t perches = 0;
    float perch[kPerchCount][3]{};
    Bird variant = bird_finch;
    FlockState state = fl_ground;
    std::uint8_t count = 0;
    std::uint64_t key = 0, corpse = 0;
    float t = 0, circle_time = 0, reland = .5f, quiet = 0, scattered = 0;
    float landing_wait = 0, retry_at = 0;
    // carrion diagnostics (F11): landing margin to the nearest threat and the last refusal
    // (1 no slots, 2 threat, 3 quiet wait, 4 slots lost on approach, 5 landing wait expired)
    float margin = 99; std::uint8_t refused = 0;
    float scale = 1;   // immutable display scale, chosen using the spawn camera
    float spot[2]{}, center[2]{}, exit[2]{0, 1};
    Agent birds[kFlockBirds];
};
// A corpse keeps its age by retained entity ID while the body stays in the
// retained scene, near or not; present means within the activity range.
struct Corpse {
    std::uint64_t id = 0; float x = 0, y = 0, envelope = 1; double seen = 0, cooldown = 0; float u = 0;
    bool used = false, in_scene = false, present = false, was_present = false;
};
constexpr float kCarrionFadeIn = 1.f;   // game seconds: every bird fades in (screen-door), none pops
constexpr float kBirdFadeOut = 1.5f;   // a leaving flock fades out once on its way
constexpr float kSightFade = .5f;      // game seconds to fade out of (or into) party sight
// Interest in a corpse fades with its age: appetite shortens, newcomers grow
// rare, and past this age (game seconds after the death) no bird comes.
constexpr float kCarrionInterest = 300.f;

struct SpotMap;
// Flight and landing obstacles: the visible objects' world envelopes
// (bridge_critter_obstacles: buildings, walls, fences, trees, props; cached
// per object, no mesh work) rasterised into a 2 m grid of top heights around
// the view, rebuilt every 2 game seconds or when the view moves 20 m.
struct Obstacles {
    static constexpr float kCell = 2.f, kRadius = 90.f;
    float x0 = 0, y0 = 0, cx = 0, cy = 0;
    int n = 0;
    double built = -1e9;
    std::vector<float> top;
    // What stands on the ground for landing: the same boxes, but a tree is its
    // trunk only (its box spans the whole crown, which a bird lands under).
    std::vector<float> ground_top;
    std::vector<std::array<float, 6>> boxes;
    std::vector<CritterPerchTree> perches;   // bridge_critter_perches: trees and cacti with mesh perches
    float ground_top_near(float x, float y, float r) const {
        float best = -std::numeric_limits<float>::infinity();
        if (!n) return best;
        const int ix0 = int(std::floor((x - r - x0) / kCell)), ix1 = int(std::floor((x + r - x0) / kCell));
        const int iy0 = int(std::floor((y - r - y0) / kCell)), iy1 = int(std::floor((y + r - y0) / kCell));
        for (int iy = std::max(iy0, 0); iy <= std::min(iy1, n - 1); ++iy)
            for (int ix = std::max(ix0, 0); ix <= std::min(ix1, n - 1); ++ix) best = std::max(best, ground_top[std::size_t(iy) * n + ix]);
        return best;
    }
    float top_near(float x, float y, float r) const {
        float best = -std::numeric_limits<float>::infinity();
        if (!n) return best;
        const int ix0 = int(std::floor((x - r - x0) / kCell)), ix1 = int(std::floor((x + r - x0) / kCell));
        const int iy0 = int(std::floor((y - r - y0) / kCell)), iy1 = int(std::floor((y + r - y0) / kCell));
        for (int iy = std::max(iy0, 0); iy <= std::min(iy1, n - 1); ++iy)
            for (int ix = std::max(ix0, 0); ix <= std::min(ix1, n - 1); ++ix) best = std::max(best, top[std::size_t(iy) * n + ix]);
        return best;
    }
    void rebuild(float x, float y, double clock, const std::vector<StaticObjectSource>* objects) {
        built = clock; cx = x; cy = y;
        n = int(2 * kRadius / kCell); x0 = x - kRadius; y0 = y - kRadius;
        top.assign(std::size_t(n) * n, -std::numeric_limits<float>::infinity());
        ground_top.assign(std::size_t(n) * n, -std::numeric_limits<float>::infinity());
        if (!bridge_critter_obstacles(x, y, kRadius, boxes)) boxes.clear();
        if (!bridge_critter_perches(x, y, kRadius, perches)) perches.clear();
        for (const auto& b : boxes) {
            if (b[5] - b[2] < .25f) continue;   // flat decals and pebbles
            const int ix0 = std::max(0, int(std::floor((b[0] - x0) / kCell))), ix1 = std::min(n - 1, int(std::floor((b[3] - x0) / kCell)));
            const int iy0 = std::max(0, int(std::floor((b[1] - y0) / kCell))), iy1 = std::min(n - 1, int(std::floor((b[4] - y0) / kCell)));
            for (int iy = iy0; iy <= iy1; ++iy)
                for (int ix = ix0; ix <= ix1; ++ix) { float& t = top[std::size_t(iy) * n + ix]; t = std::max(t, b[5]); }
            float fx0 = b[0], fx1 = b[3], fy0 = b[1], fy1 = b[4];
            if (objects)
                for (const auto& o : *objects)
                    if (o.position[0] >= b[0] && o.position[0] <= b[3] && o.position[1] >= b[1] && o.position[1] <= b[4] &&
                        ambient::tree_kind(o.visual_reference.c_str()) != ambient::tree_none) {
                        fx0 = o.position[0] - .6f; fx1 = o.position[0] + .6f; fy0 = o.position[1] - .6f; fy1 = o.position[1] + .6f;
                        break;
                    }
            const int gx0 = std::max(0, int(std::floor((fx0 - x0) / kCell))), gx1 = std::min(n - 1, int(std::floor((fx1 - x0) / kCell)));
            const int gy0 = std::max(0, int(std::floor((fy0 - y0) / kCell))), gy1 = std::min(n - 1, int(std::floor((fy1 - y0) / kCell)));
            for (int iy = gy0; iy <= gy1; ++iy)
                for (int ix = gx0; ix <= gx1; ++ix) { float& t = ground_top[std::size_t(iy) * n + ix]; t = std::max(t, b[5]); }
        }
    }
};
// Read-only world view of one tick.
struct WorldView {
    const Obstacles* obstacles = nullptr;
    std::uint32_t unavailable = 0;   // species whose game assets failed to load
    bool party_moving = false;       // a living unit near the view is walking (bats fly past then)
    const SpotMap* spot_map = nullptr;
    ambient::ZoneClass zone = ambient::zone_outdoor;
    bool day = true;
    const std::uint8_t* area = nullptr; std::uint32_t tiles_x = 0, tiles_y = 0;
    const float* height = nullptr; std::uint32_t verts_x = 0, verts_y = 0;
    const std::vector<StaticObjectSource>* objects = nullptr;
    const std::vector<Point>* ring = nullptr;
    const float* clip = nullptr; float width = 0, height_px = 0;
    V3 eye{}, focus{};
    float view_radius = 30;
    const std::vector<std::array<float, 4>>* units = nullptr;
    const std::vector<GroundUnitDetail>* details = nullptr;
    int level = 2;

    // Preserve the former query neighborhood for local population bookkeeping.
    // Threat checks use all retained units and their individual, uncapped reach.
    // The game's sight rule for the player: a point is seen when it lies within
    // the vision range of a live party member (controlled by the local human
    // player). Returns the smallest distance / vision ratio: <= 1 is seen.
    float party_extent(float px, float py) const {
        float best = std::numeric_limits<float>::infinity();
        for (std::size_t i = 0; units && i < units->size(); ++i) {
            const auto& d = (*details)[i];
            if (d.dead || !d.controlled || !(d.vision > 0)) continue;
            best = std::min(best, std::hypot((*units)[i][0] - px, (*units)[i][1] - py) / d.vision);
        }
        return best;
    }
    bool near_activity(const std::array<float, 4>& unit) const {
        const float dx = unit[0] - focus.x, dy = unit[1] - focus.y;
        const float radius = view_radius + 40.f;
        return dx * dx + dy * dy <= radius * radius;
    }
    std::uint8_t area_at(float px, float py) const {
        const int tx = int(std::floor(px * .5f)), ty = int(std::floor(py * .5f));
        if (!area || tx < 0 || ty < 0 || std::uint32_t(tx) >= tiles_x || std::uint32_t(ty) >= tiles_y) return 0xff;
        return area[std::size_t(ty) * tiles_x + tx] & 15u;
    }
    // Ground height as the terrain is drawn: every 1 m cell is split along its
    // (x,y)-(x+1,y+1) diagonal (LOD0 tile template) and each vertex sits at
    // its stored XY offset (up to 0.5 m). A regular bilinear grid differs by
    // centimetres to decimetres on slopes, enough to bury a 5 cm animal.
    const float* offset_x = nullptr, * offset_y = nullptr;   // per vertex, beside height
    float ground(float px, float py) const {
        if (!height || verts_x < 2 || verts_y < 2) return 0;
        if (offset_x && offset_y) {
            const int cx = int(std::floor(px)), cy = int(std::floor(py));
            static constexpr int order[3]{0, -1, 1};   // own cell first
            for (const int oy : order)
                for (const int ox : order) {
                    const int x = cx + ox, y = cy + oy;
                    if (x < 0 || y < 0 || x + 1 >= int(verts_x) || y + 1 >= int(verts_y)) continue;
                    const std::size_t i00 = std::size_t(y) * verts_x + x, i10 = i00 + 1, i01 = i00 + verts_x, i11 = i01 + 1;
                    const auto at = [&](std::size_t i, int gx, int gy) { return V3{float(gx) + offset_x[i], float(gy) + offset_y[i], height[i]}; };
                    const V3 a = at(i00, x, y), b = at(i10, x + 1, y), c = at(i11, x + 1, y + 1), d = at(i01, x, y + 1);
                    for (const auto& t : {std::array<V3, 3>{a, b, c}, std::array<V3, 3>{a, c, d}}) {
                        const float area = (t[1].x - t[0].x) * (t[2].y - t[0].y) - (t[2].x - t[0].x) * (t[1].y - t[0].y);
                        if (std::abs(area) < 1e-9f) continue;
                        const float u = ((t[1].x - px) * (t[2].y - py) - (t[2].x - px) * (t[1].y - py)) / area;
                        const float v = ((t[2].x - px) * (t[0].y - py) - (t[0].x - px) * (t[2].y - py)) / area;
                        const float s = 1 - u - v;
                        if (u >= -1e-5f && v >= -1e-5f && s >= -1e-5f) return u * t[0].z + v * t[1].z + s * t[2].z;
                    }
                }
        }
        const float gx = std::clamp(px, 0.f, float(verts_x - 1) - .001f), gy = std::clamp(py, 0.f, float(verts_y - 1) - .001f);
        const int ix = int(gx), iy = int(gy);
        const float fx = gx - ix, fy = gy - iy;
        const float* r0 = height + std::size_t(iy) * verts_x + ix, * r1 = r0 + verts_x;
        return (r0[0] * (1 - fx) + r0[1] * fx) * (1 - fy) + (r1[0] * (1 - fx) + r1[1] * fx) * fy;
    }
    bool nearest(float px, float py, std::uint32_t mask, float radius, float& ox, float& oy) const {
        float best = radius * radius;
        bool found = false;
        const int r = int(std::ceil(radius / 2)), cx = int(std::floor(px * .5f)), cy = int(std::floor(py * .5f));
        for (int ty = cy - r; ty <= cy + r; ++ty)
            for (int tx = cx - r; tx <= cx + r; ++tx) {
                const float qx = (tx + .5f) * 2, qy = (ty + .5f) * 2;
                const auto a = area_at(qx, qy);
                if (a == 0xff || !((mask >> a) & 1u)) continue;
                const float d = (qx - px) * (qx - px) + (qy - py) * (qy - py);
                if (d < best) { best = d; ox = qx; oy = qy; found = true; }
            }
        return found;
    }
    // Clip-space projection and on-screen pixels per metre at a point.
    bool project(V3 p, float& nx, float& ny, float& ppm) const {
        return project_ground(clip, width, height_px, p.x, p.y, p.z, nx, ny, ppm);
    }
    bool habitat(const SpeciesRule& r, float px, float py) const {
        if (!((r.zones >> zone) & 1u) || (day ? r.day : r.night) <= 0) return false;
        const auto a = area_at(px, py);
        if (a == 0xff || !((r.habitat >> a) & 1u)) return false;
        float ox, oy;
        return !r.near_mask || nearest(px, py, r.near_mask, r.near_r, ox, oy);
    }
    bool outside_walls(float px, float py) const { return !ring || ring->empty() || !inside_ring(*ring, px, py); }
    // Walkable ground as the game's AI map judges it: no neighbour 0.5 m away
    // more than a 40 degree rise (FP_AI_MAX_ANGLE_TO_MOVE, terrain_cliff.h).
    bool walkable(float px, float py) const {
        if (!height) return true;
        const float g = ground(px, py), rise = .5f * .83909963f;   // tan 40
        return std::abs(ground(px + .5f, py) - g) <= rise && std::abs(ground(px - .5f, py) - g) <= rise &&
               std::abs(ground(px, py + .5f) - g) <= rise && std::abs(ground(px, py - .5f) - g) <= rise;
    }
    // Clear ground for a landing bird: no object envelope within r and a
    // slope under ~35 degrees.
    bool clear_ground(float px, float py, float r) const {
        const float g = ground(px, py);
        if (obstacles && obstacles->ground_top_near(px, py, r + .2f) > g + .15f) return false;
        const float d = std::max(r, .3f);
        const float gx = ground(px + d, py) - ground(px - d, py), gy = ground(px, py + d) - ground(px, py - d);
        return std::abs(gx) < 1.4f * d && std::abs(gy) < 1.4f * d && walkable(px, py);
    }
};

// World-seeded critter spots of one map: at most one per 32 m terrain sector
// (sector_spot), with the sector's soundscape (sector_sound). Built once per map.
struct SpotCell { float x = 0, y = 0; bool valid = false; std::uint8_t sound = 0; };
struct SpotMap { std::uint32_t sectors_x = 0, sectors_y = 0; std::vector<SpotCell> cells; };
SpotMap build_spot_map(std::uint64_t map_seed, const std::uint8_t* area, std::uint32_t tiles_x, std::uint32_t tiles_y,
                       const std::vector<std::uint8_t>& sounds, const std::vector<Point>& fires) {
    SpotMap m;
    m.sectors_x = tiles_x / 16; m.sectors_y = tiles_y / 16;
    m.cells.resize(std::size_t(m.sectors_x) * m.sectors_y);
    for (std::uint32_t sy = 0; sy < m.sectors_y; ++sy)
        for (std::uint32_t sx = 0; sx < m.sectors_x; ++sx) {
            auto& c = m.cells[std::size_t(sy) * m.sectors_x + sx];
            Point p;
            const std::size_t i = std::size_t(sy) * m.sectors_x + sx;
            c.sound = i < sounds.size() ? sounds[i] : std::uint8_t(ambient::tt_grass);
            c.valid = c.sound < 16 && sector_spot(map_seed, sx, sy, area, tiles_x, tiles_y, fires, p);
            c.x = p.x; c.y = p.y;
        }
    return m;
}
// The soundscape of every sector of a map (sector_sound); 0xff where the map
// has no sector.
std::vector<std::uint8_t> sector_sounds(const TerrainCpuAsset& t) {
    std::vector<std::uint8_t> out(std::size_t(t.sector_count_x) * t.sector_count_y, 0xff);
    for (const auto& sector : t.sectors) {
        if (sector.map_x >= t.sector_count_x || sector.map_y >= t.sector_count_y) continue;
        std::uint32_t counts[16]{};
        for (const auto& tile : sector.tiles)
            if (tile.tile_index < t.tile_types.size()) ++counts[t.tile_types[tile.tile_index] & 15u];
        out[std::size_t(sector.map_y) * t.sector_count_x + sector.map_x] = sector_sound(counts);
    }
    return out;
}
// Campfires: the map's MOB Sound objects (CC01 records in the object section)
// whose resource names (CC0A) name a fire sound; position CC03 (x, y, z).
std::vector<Point> campfires(const MobDocument& mob) {
    std::vector<Point> out;
    if (!mob.bytes) return out;
    const auto& b = *mob.bytes;
    const auto u32 = [&](std::size_t o) { std::uint32_t v; std::memcpy(&v, b.data() + o, 4); return v; };
    std::vector<const MobChunkRecord*> stack;
    for (const auto& r : mob.roots) stack.push_back(&r);
    while (!stack.empty()) {
        const MobChunkRecord* r = stack.back();
        stack.pop_back();
        for (const auto& c : r->children) stack.push_back(&c);
        if (r->id != 0xcc01u) continue;
        bool fire = false, placed = false;
        Point p;
        for (std::size_t o = r->payload_offset, end = o + r->payload_bytes; end - o >= 8;) {
            const std::uint32_t id = u32(o), total = u32(o + 4);
            if (total < 8 || total > end - o) break;
            if (id == 0xcc03u && total == 20) {
                float v[2]; std::memcpy(v, b.data() + o + 8, 8);
                placed = std::isfinite(v[0]) && std::isfinite(v[1]); p = {v[0], v[1]};
            } else if (id == 0xcc0au) {
                fire = folded(std::string(reinterpret_cast<const char*>(b.data() + o + 8), total - 8)).find("fire") != std::string::npos;
            }
            o += total;
        }
        if (fire && placed) out.push_back(p);
    }
    return out;
}
// Per-sector memory, kept only for sectors near the view (and for disturbed
// ones until they have rested); cleared with the director on a zone change.
struct SpotMemory { std::uint32_t sector = 0; double quiet_until = 0; bool rolled = false, occupied = false; };
// A creature or resting flock found at a spot.
struct Resident { bool active = false, is_flock = false, disturbed = false; std::uint32_t sector = 0; Agent critter; Flock birds; };
constexpr unsigned kResidents = 4;

struct Director {
    std::uint64_t seed = 1;
    double clock = 0, next_attempt = kFirstDelay, last_combat = -1e9;
    std::uint32_t attempt = 0, events = 0;
    // Map-local totals; spot rejects count candidate checks, not random draws.
    std::uint64_t spawn_attempts = 0, no_spot = 0, no_species = 0, quiet_reject = 0;
    std::uint64_t size_reject = 0, habitat_reject = 0, undead_reject = 0, threat_reject = 0;
    std::uint64_t carrion_arrivals = 0, carrion_landed = 0;
    std::uint64_t slots_wanted = 0, slots_found = 0, slots_failed = 0;   // carrion landing slots, reserved once per flock
    std::uint64_t spot_rolls = 0, spot_found = 0, spot_faded = 0, spot_disturbed = 0, budget_reject = 0;
    double last_species[sp_count];
    Agent critters[4];
    unsigned critter_count = 0;
    Flock flock;                // ambient flock (one event)
    Flock carrion[2];
    std::vector<Corpse> corpses;
    Resident residents[kResidents];
    std::vector<SpotMemory> spot_memory;
    Director() { std::fill(std::begin(last_species), std::end(last_species), -1e9); }
};
// Director events and spot residents on screen or about to be.
unsigned on_screen(const Director& d) {
    unsigned n = (d.critter_count ? 1u : 0u) + (d.flock.active ? 1u : 0u);
    for (const auto& r : d.residents) n += r.active ? 1u : 0u;
    return n;
}

float rnd(const Director& d, std::uint64_t a, std::uint64_t b = 0, std::uint64_t c = 0) { return hash01(d.seed, a, b, c); }
float rrange(const Director& d, float lo, float hi, std::uint64_t a, std::uint64_t b = 0, std::uint64_t c = 0) { return lo + (hi - lo) * rnd(d, a, b, c); }

void face(Agent& c, float dx, float dy, float rate, float dt) {
    const float d = std::fmod(std::atan2(dx, dy) - c.yaw + PI + 100 * TAU, TAU) - PI;
    c.yaw += std::clamp(d, -rate * dt, rate * dt);
}
// A ground step never ends on steep ground: blocked, the animal turns along
// the slope (the side that is walkable) or holds still.
void move(const WorldView& w, Agent& c, float v, float dt, bool sideways = false) {
    const float off = sideways ? PI / 2 : 0.f;
    for (const float turn : {0.f, .9f, -.9f, 1.8f, -1.8f}) {
        const float a = c.yaw + off + turn, nx = c.x + std::sin(a) * v * dt, ny = c.y + std::cos(a) * v * dt;
        if (!w.walkable(nx, ny)) continue;
        c.x = nx; c.y = ny; c.yaw += turn * std::min(1.f, dt * 4);
        return;
    }
}

// Larva: one peristaltic wave per period (gait counts waves). The root moves
// only when a wave completes, by one stride; turns pivot on the planted tail.
bool crawl(const WorldView& w, Agent& c, float dt, float period, float tail_y, const float* toward = nullptr, float turn_rate = .8f) {
    tail_y *= c.scale;
    const float wave = std::floor(c.gait);
    c.gait += dt / period;
    c.speed = .4f;
    if (toward && std::fmod(c.gait, 1.f) > .5f) {
        const float tx = c.x + std::sin(c.yaw) * tail_y, ty = c.y + std::cos(c.yaw) * tail_y;
        face(c, toward[0], toward[1], turn_rate, dt);
        c.x = tx - std::sin(c.yaw) * tail_y; c.y = ty - std::cos(c.yaw) * tail_y;
    }
    if (std::floor(c.gait) > wave) {
        c.gait = std::floor(c.gait);
        const float nx = c.x + std::sin(c.yaw) * kLarvaStride * c.scale, ny = c.y + std::cos(c.yaw) * kLarvaStride * c.scale;
        if (w.walkable(nx, ny)) { c.x = nx; c.y = ny; } else c.yaw += PI / 2;   // steep: turn away over the next waves
        return true;
    }
    return false;
}

// Current live crawler rings are the larvae's habitat, not persistent unit IDs.
// A grub is born 15-20 m from a live crawler and lives on while one is within
// 30 m: crawlers wander and charge, and the ring alone would fade grubs out.
bool larva_habitat(const WorldView& w, float x, float y, float near_r = 15.f, float far_r = 20.f) {
    for (std::size_t i = 0; i < w.units->size(); ++i) {
        const auto& d = (*w.details)[i];
        if (d.dead || !d.crawler) continue;
        const auto& u = (*w.units)[i];
        const float distance = std::hypot(u[0] - x, u[1] - y);
        if (distance >= near_r && distance <= far_r) return true;
    }
    return false;
}

// The retained unit row's senses for the critter threat law.
UnitSenses senses_of(const GroundUnitDetail& d) {
    return {d.visibility, d.loudness, d.pose, d.moving, d.stance == stance_attack};
}
struct Threat { bool found = false; float x = 0, y = 0; };
Threat threatened(const Agent& c, const WorldView& w, float flee_r) {
    Threat best; float bd = 1e9f;
    for (std::size_t i = 0; i < w.units->size(); ++i) {
        if ((*w.details)[i].dead || (c.species == sp_larva && (*w.details)[i].crawler)) continue;
        const auto& u = (*w.units)[i];
        const float d = std::hypot(u[0] - c.x, u[1] - c.y);
        const auto& senses = (*w.details)[i];
        float reach = c.species == sp_larva && senses.stance == stance_attack ?
            std::max(12.f, threat_reach(flee_r, senses_of(senses))) : threat_reach(flee_r, senses_of(senses));
        if (senses.crawler && c.species != sp_larva && c.species != sp_bat) reach = std::max(reach, 15.f);   // crawlers hunt small critters
        if (senses.undead) reach = std::max(reach, kUndeadReach);
        if (d < reach && d < bd) { bd = d; best = {true, u[0], u[1]}; }
    }
    return best;
}

void begin_flee(const Director& dir, Agent& c, float threat_x, float threat_y, const WorldView& w, bool calm) {
    const auto& rule = species_rule(c.species);
    c.state = calm ? st_leave : st_flee; c.t = 0;
    float ax = c.x - threat_x, ay = c.y - threat_y;
    float n = std::hypot(ax, ay); if (n <= 0) n = 1;
    ax /= n; ay /= n;
    bool goal = false; float gx = 0, gy = 0;
    if (rule.exit == exit_water) goal = w.nearest(c.x, c.y, kWater | kSwamp, 6, gx, gy);
    else if (w.objects) {
        // Cover: the placed objects of the map (rocks, bushes, trees, walls).
        float best = 1e9f;
        for (const auto& o : *w.objects) {
            const float dx = o.position[0] - c.x, dy = o.position[1] - c.y, d = std::hypot(dx, dy);
            if (d > 6 || d < 1.2f || (dx * ax + dy * ay) / d < -.2f) continue;   // a real dart, never towards the threat
            if (d < best) { best = d; gx = o.position[0]; gy = o.position[1]; goal = true; }
        }
    }
    if (goal) {
        float ex = gx - c.x, ey = gy - c.y, g = std::hypot(ex, ey); if (g <= 0) g = 1;
        ax = .3f * ax + .7f * ex / g; ay = .3f * ay + .7f * ey / g;
        n = std::hypot(ax, ay); if (n > 0) { ax /= n; ay /= n; }
    }
    (void)dir;
    c.has_target = goal; c.tx = gx; c.ty = gy; c.fx = ax; c.fy = ay;
}

void critter_idle(const Director& dir, Agent& c, float dt, const WorldView& w, float tail_y) {
    const Species k = c.species;
    const float t = c.t;
    c.speed = 0;
    const int slot = int(t / 2.5f);
    const float r = rnd(dir, c.key, key("idle"), std::uint64_t(slot));
    if (k == sp_dungbeetle) {
        if (!c.has_ball) { c.has_ball = true; c.ball_x = c.x - std::sin(c.yaw) * .028f * c.scale; c.ball_y = c.y - std::cos(c.yaw) * .028f * c.scale; c.ball_roll = 0; }
        c.push = 1; c.speed = .5f;
        c.gait += dt * 2;
        c.yaw += .15f * std::sin(t * .4f) * dt;
        const float v = .05f, dx = -std::sin(c.yaw) * v * dt, dy = -std::cos(c.yaw) * v * dt;   // walks backwards
        c.x += dx; c.y += dy;
        c.ball_x = c.x - std::sin(c.yaw) * .028f * c.scale; c.ball_y = c.y - std::cos(c.yaw) * .028f * c.scale;
        c.ball_roll += std::hypot(dx, dy) / (.015f * c.scale); c.ball_yaw = c.yaw;
        return;
    }
    if (k == sp_larva) {   // bouts of 2-4 waves, pausing to cast the head about
        const auto search = [&](float target) { c.search += (target - c.search) * std::min(1.f, dt * 2.5f); };
        if (c.bout < 0) { c.bout = 0; c.bout_n = 0; c.pause = rrange(dir, .6f, 2.f, c.key, key("pause"), 0); }
        if (c.bout > 0) {
            search(0);
            const float toward[2]{c.bdx, c.bdy};
            if (crawl(w, c, dt, kLarvaPeriod, tail_y, toward)) --c.bout;
            return;
        }
        c.speed = 0;
        search(1);
        c.pause -= dt;
        if (c.pause <= 0) {
            const int n = ++c.bout_n;
            const float turn = rrange(dir, -.7f, .7f, c.key, key("turn"), std::uint64_t(n));
            c.bout = 2 + int(rnd(dir, c.key, key("waves"), std::uint64_t(n)) * 3);
            c.bdx = std::sin(c.yaw + turn); c.bdy = std::cos(c.yaw + turn);
            c.pause = rrange(dir, 1.2f, 3.f, c.key, key("pause"), std::uint64_t(n));
        }
        return;
    }
    if (k == sp_rat || k == sp_snowmouse || k == sp_spider || k == sp_snake || k == sp_scolopendra) {
        if (r < .3f && std::fmod(t, 2.5f) < .5f) {   // short reposition
            c.speed = .4f; c.gait += dt * 2;
            move(w, c, .12f, dt);
            face(c, std::sin(float(slot)), std::cos(float(slot)), 3, dt);
        } else if (k == sp_snake) c.gait += dt * .3f;
    } else if (k == sp_lizard) {
        const float ph = std::fmod(t, 4.f);
        c.pushup = ph > 2 && ph < 2.55f ? std::max(0.f, std::sin((ph - 2) * 6)) : 0.f;
    } else if (k == sp_crab) {
        c.claw += ((r > .5f ? 1.f : 0.f) - c.claw) * std::min(1.f, dt * 3);
        if (r < .25f && std::fmod(t, 2.5f) < .6f) { c.speed = .7f; c.gait += dt * 5; move(w, c, slot % 2 ? .15f : -.15f, dt, true); }
    }
}

void critter_flee(const Director& dir, Agent& c, float dt, const WorldView& w, bool calm, float tail_y) {
    const auto& rule = species_rule(c.species);
    const Species k = c.species;
    const float pace = calm ? .45f : 1.f;
    // dart: straight and fast towards cover with small jinks; no pathing
    const float jit = (rnd(dir, c.key, key("zig"), std::uint64_t(c.t / .35f)) - .5f) * (k == sp_lizard ? .9f : .4f);
    const float dx = c.fx * std::cos(jit) - c.fy * std::sin(jit), dy = c.fx * std::sin(jit) + c.fy * std::cos(jit);
    if (k == sp_larva) {   // no dart: the waves just come faster
        c.search += (0 - c.search) * std::min(1.f, dt * 2.5f);
        const float toward[2]{dx, dy};
        crawl(w, c, dt, kLarvaFleePeriod / pace, tail_y, toward, 1.2f);
    } else if (k == sp_crab) {
        if (c.t < .25f) face(c, -dy, dx, 6, dt);
        c.speed = 1; c.gait += dt * 7;
        move(w, c, rule.speed * pace, dt, true);
    } else if (k == sp_frog) {
        const float hop = .4f, gap = .2f, ph = std::fmod(c.t, hop + gap);
        face(c, dx, dy, 10, dt);
        if (ph < hop) {
            const float h = ph / hop;
            c.hop_clip = true; c.speed = 1; c.gait = h;
            c.z = .10f * std::sin(PI * h);
            move(w, c, rule.speed * pace, dt);
        } else { c.hop_clip = false; c.z = 0; c.speed = 0; }
    } else {
        face(c, dx, dy, 14, dt);
        c.speed = 1;
        float rate = 6;
        switch (k) { case sp_snowmouse: rate = 7; break; case sp_spider: rate = 5; break; case sp_lizard: rate = 9; break;
                     case sp_snake: rate = 2.5f; break; case sp_scolopendra: rate = 4; break; default: break; }
        c.gait += dt * rate;
        if (k == sp_dungbeetle) c.push = 0;   // abandons the ball where it is
        move(w, c, rule.speed * pace, dt);
    }
    const bool at_goal = c.has_target && std::hypot(c.tx - c.x, c.ty - c.y) < .4f;
    const auto here = w.area_at(c.x, c.y);
    const bool in_water = rule.exit == exit_water && here != 0xff && ((kWater | kSwamp) >> here & 1u);
    const float limit = (k == sp_larva ? 1.2f : k == sp_dungbeetle ? 4.f : (rule.exit == exit_water || rule.exit == exit_mound) ? 3.f : 2.2f) / pace;
    if (at_goal || in_water || c.t > limit) {
        c.hop_clip = false; c.pitch = 0;
        if (k == sp_frog) c.z = 0;
        c.state = st_vanish; c.t = 0;
    }
}

void critter_update(const Director& dir, Agent& c, float dt, const WorldView& w, const Threat& scare, float tail_y) {
    const auto& rule = species_rule(c.species);
    c.t += dt; c.age += dt;
    if (c.species == sp_larva && c.state != st_vanish && c.state != st_gone && !larva_habitat(w, c.x, c.y, 0.f, 30.f)) {
        c.state = st_vanish; c.t = 0;
    }
    if (c.state == st_appear) {   // fade in
        c.alpha = std::min(1.f, c.alpha + dt / .4f);
        if (c.alpha >= 1) { c.state = st_idle; c.t = 0; }
        return;
    }
    if (c.state == st_idle) {
        const Threat u = threatened(c, w, rule.flee_r);
        if (scare.found && c.species != sp_larva) begin_flee(dir, c, scare.x, scare.y, w, false);
        else if (u.found) begin_flee(dir, c, u.x, u.y, w, false);
        else if (c.t > c.idle_limit && (c.species != sp_larva || std::fmod(c.gait, 1.f) == 0)) begin_flee(dir, c, c.x, c.y - 1, w, true);
        else critter_idle(dir, c, dt, w, tail_y);
        return;
    }
    if (c.state == st_flee || c.state == st_leave) { critter_flee(dir, c, dt, w, c.state == st_leave, tail_y); return; }
    if (c.state == st_vanish) {
        c.speed *= std::pow(.9f, dt * 30);
        if (c.species == sp_larva) {   // burrows: sinks into the soil while it keeps crawling
            crawl(w, c, dt, kLarvaFleePeriod, tail_y);
            c.z -= dt * .035f * c.scale;
            c.alpha = std::max(0.f, c.alpha - dt / .9f);
        } else if (rule.exit == exit_water || rule.exit == exit_mound) {
            c.z -= dt * .12f * c.scale;
            c.alpha = std::max(0.f, c.alpha - dt / .6f);
        } else {
            c.alpha = std::max(0.f, c.alpha - dt / .5f);
            if (c.speed > .05f) move(w, c, rule.speed * .5f * c.speed, dt);
        }
        if (c.alpha <= 0) c.state = st_gone;
    }
}

// ---- flocks
void flock_init(Flock& f, Bird variant, unsigned n, float x, float y, const Director& dir, std::uint64_t key0,
                const float* arrive, float circle_lo, float circle_hi, float reland, float scale) {
    f = Flock{};
    f.active = true; f.variant = variant; f.key = key0; f.count = std::uint8_t(std::min(n, kFlockBirds));
    f.scale = scale;
    f.spot[0] = f.center[0] = x; f.spot[1] = f.center[1] = y;
    const BirdFlight fl = bird_flight(variant, scale);
    const float size = (big_bird(variant) ? 2.5f : 1.f) * scale;
    for (unsigned j = 0; j < f.count; ++j) {
        Agent& b = f.birds[j];
        b = Agent{};
        b.bird = variant; b.key = splitmix(key0 ^ (j + 1)); b.alpha = 0; b.state = st_idle;
        const float ang = rrange(dir, 0, TAU, key0, j, key("a")), r = .25f + .6f * std::sqrt(rnd(dir, key0, j, key("r")));
        b.slot_x = std::cos(ang) * r * size; b.slot_y = std::sin(ang) * r * size;
        b.x = x + b.slot_x; b.y = y + b.slot_y;
        b.yaw = rrange(dir, 0, TAU, key0, j, key("yaw"));
    }
    f.state = fl_ground;
    f.circle_time = rrange(dir, circle_lo, circle_hi, key0, key("ct"));
    f.reland = reland;
    if (variant == bird_bat) {
        // A fly-by: the swarm comes in from one side, passes over the spot
        // (by the moving hero) and goes off the other side; it never perches
        // or lingers.
        const float a = rrange(dir, 0, TAU, key0, key("byd"));
        f.exit[0] = std::cos(a); f.exit[1] = std::sin(a);
        f.center[0] = x - f.exit[0] * kBatPath * .5f; f.center[1] = y - f.exit[1] * kBatPath * .5f;
        for (unsigned j = 0; j < f.count; ++j) {
            Agent& b = f.birds[j];
            b.flying = true; b.fold = 0; b.z = fl.altitude * (.8f + .4f * hash01(b.key, 5));
            b.x = f.center[0] + b.slot_x * 2; b.y = f.center[1] + b.slot_y * 2;
            b.vx = f.exit[0] * fl.speed; b.vy = f.exit[1] * fl.speed;
        }
        f.state = fl_circle; f.circle_time = 1e9f;
        return;
    }
    if (arrive) {   // fly in from off-screen towards the spot
        for (unsigned j = 0; j < f.count; ++j) {
            Agent& b = f.birds[j];
            b.x = x + arrive[0] * 22 + b.slot_x * 2; b.y = y + arrive[1] * 22 + b.slot_y * 2; b.z = fl.altitude + 2 + j * .3f;
            b.flying = true; b.vx = -arrive[0] * fl.speed; b.vy = -arrive[1] * fl.speed; b.vz = 0;
        }
        f.state = fl_circle;
    }
}

void flock_centroid(const Flock& f, float& x, float& y) {
    x = y = 0;
    for (unsigned j = 0; j < f.count; ++j) { x += f.birds[j].x; y += f.birds[j].y; }
    if (f.count) { x /= f.count; y /= f.count; }
}
V3 bird_world(const Agent& b, const WorldView& w) { return {b.x, b.y, w.ground(b.x, b.y) + b.z}; }

// Birds on the ground are spooked by units of about human size and larger
// walking up (unit row envelope radius; 0 = unknown counts as large), never
// by the camera or by small creatures.
constexpr float kSpookEnvelope = .7f;
bool spooks_birds(const std::array<float, 4>& unit) { return !(unit[3] > 0) || unit[3] >= kSpookEnvelope; }

Threat flock_threat(const Flock& f, const WorldView& w) {
    const BirdFlight fl = bird_flight(f.variant, f.scale);
    for (std::size_t i = 0; i < w.units->size(); ++i) {
        const auto& d = (*w.details)[i];
        if (d.dead) continue;
        const auto& u = (*w.units)[i];
        if (!spooks_birds(u) && !d.undead) continue;
        for (unsigned j = 0; j < f.count; ++j) {
            const Agent& b = f.birds[j];
            // a carrion bird still coming down is watching its landing slot
            const bool landing = f.carrion && f.state == fl_land && b.flying;
            if (b.state == st_gone || (b.flying && !landing)) continue;
            const float x = landing ? f.spot[0] + b.slot_x : b.x, y = landing ? f.spot[1] + b.slot_y : b.y;
            float reach = f.carrion ? std::min(10.f, threat_reach(fl.flee, senses_of(d))) : threat_reach(fl.flee, senses_of(d));
            if (d.undead) reach = std::max(reach, kUndeadReach);
            if (std::hypot(u[0] - x, u[1] - y) < reach) return {true, u[0], u[1]};
        }
    }
    return {};
}

void flock_alarm(Flock& f, const Director& dir, float tx, float ty) {
    f.state = fl_circle; f.t = 0;
    for (unsigned j = 0; j < f.count; ++j) {
        Agent& b = f.birds[j];
        const float d = std::hypot(b.x - tx, b.y - ty);
        b.delay = std::min(.6f, .05f * d) + .25f * rnd(dir, b.key, key("td"), f.count);
    }
    float cx, cy; flock_centroid(f, cx, cy);
    float ax = cx - tx, ay = cy - ty, n = std::hypot(ax, ay) + 1e-9f;
    f.exit[0] = ax / n; f.exit[1] = ay / n;
    const BirdFlight fl = bird_flight(f.variant, f.scale);
    f.center[0] = cx + f.exit[0] * fl.radius * .8f; f.center[1] = cy + f.exit[1] * fl.radius * .8f;
}

// Ground hop, weighted: an anticipation crouch during which the bird turns
// toward the hop, a ballistic arc (constant horizontal speed, gravity
// parabola, wings flicked), then a landing squash that springs back with a
// tail kick. Pecks and caws ease in and out. All on game time.
constexpr float kHopCrouch = .09f, kHopAir = .17f, kHopLand = .14f;
float angle_lerp(float a, float b, float t) {
    float d = std::fmod(b - a + PI, TAU);
    if (d < 0) d += TAU;
    return a + (d - PI) * t;
}
void bird_hop_step(Agent& b, float dt, float height) {
    b.hop += dt;
    const float h = b.hop;
    b.yaw = angle_lerp(b.hop_yaw0, b.hop_yaw1, smooth01(h / kHopCrouch));
    const float air = std::clamp((h - kHopCrouch) / kHopAir, 0.f, 1.f);
    const float land = std::clamp((h - kHopCrouch - kHopAir) / kHopLand, 0.f, 1.f);
    b.x = b.hop_x + b.hop_dx * air; b.y = b.hop_y + b.hop_dy * air;
    b.z = height * 4 * air * (1 - air);
    b.flick = .3f * std::sin(PI * air);
    if (h < kHopCrouch) b.crouch = .7f * smooth01(h / kHopCrouch);
    else if (air < 1) b.crouch = .7f * (1 - smooth01(air * 3));
    else b.crouch = .75f * std::sin(PI * land) * (1 - .4f * land);
    b.tail = air >= 1 ? .35f * std::sin(PI * land) * (1 - land) : 0.f;
    if (h >= kHopCrouch + kHopAir + kHopLand) { b.hop = -1; b.crouch = b.tail = b.flick = 0; b.z = 0; }
}
// Perches: the nearest tree (or, for vultures, cactus) within reach of a point
// and at least `away` from another point, with the perches chosen once per
// model from its own branches (bridge_critter_perches): branch picks first,
// then the crown's outer edge. Saplings and stumps under 1.5 m (cacti 1 m)
// carry none. Returns the count.
unsigned perch_tree(const WorldView& w, float x, float y, float reach, const float* avoid, float away, bool cactus,
                    float* tree, float (*out)[3]) {
    if (!w.obstacles) return 0;
    float best = reach;
    const CritterPerchTree* pick = nullptr;
    for (const auto& t : w.obstacles->perches) {
        if (t.cactus != cactus || t.count < 2) continue;
        const float d = std::hypot(t.x - x, t.y - y);
        if (d >= best || (avoid && std::hypot(t.x - avoid[0], t.y - avoid[1]) < away)) continue;
        if (t.top - w.ground(t.x, t.y) < (cactus ? 1.f : 1.5f)) continue;
        best = d; pick = &t;
    }
    if (!pick) return 0;
    tree[0] = pick->x; tree[1] = pick->y;
    for (unsigned k = 0; k < pick->count; ++k) for (int c = 0; c < 3; ++c) out[k][c] = pick->points[k][c];
    return pick->count;
}
// Seat a flock on a tree's (or cactus's) perches (birds keep flying when `land`).
bool seat_on_tree(Flock& f, const WorldView& w, float x, float y, float reach, const float* avoid, float away, bool land,
                  bool cactus = false) {
    float tree[2], pts[kPerchCount][3];
    const unsigned n = perch_tree(w, x, y, reach, avoid, away, cactus, tree, pts);
    if (n < 2 || (land && n < f.count)) return false;   // a resettling flock never drops a bird
    f.perching = true; f.perch_cactus = cactus; f.perches = std::uint8_t(n);
    f.tree[0] = tree[0]; f.tree[1] = tree[1]; f.spot[0] = tree[0]; f.spot[1] = tree[1];
    for (unsigned k = 0; k < n; ++k) for (int c = 0; c < 3; ++c) f.perch[k][c] = pts[k][c];
    f.count = std::uint8_t(std::min<unsigned>(f.count, n));
    for (unsigned j = 0; j < f.count; ++j) {
        Agent& b = f.birds[j];
        b.perch = int(j); b.slot_x = pts[j][0] - tree[0]; b.slot_y = pts[j][1] - tree[1];
        b.slot_z = pts[j][2] - w.ground(pts[j][0], pts[j][1]);
        if (!land) { b.x = pts[j][0]; b.y = pts[j][1]; b.z = b.slot_z; b.flying = false; }
    }
    return true;
}
// A feeding bird belongs next to the body: move its slot in along its own
// bearing to the innermost clear point a body away from every other bird's
// slot (it then walks there). Called at touch-down and now and then while
// feeding, so outer birds close in as others leave.
void pull_in(Flock& f, Agent& b, const WorldView* w) {
    const float len = bird_length(f.variant) * f.scale, here = std::hypot(b.slot_x, b.slot_y);
    const float bearing = std::atan2(b.slot_y, b.slot_x);
    for (float r = f.corpse_body + .7f * len; r < here - .3f * len; r += .4f * len)
        for (const float da : {0.f, .25f, -.25f, .5f, -.5f}) {
            const float a = bearing + da, sx = std::cos(a) * r, sy = std::sin(a) * r;
            bool room = !w || w->clear_ground(f.spot[0] + sx, f.spot[1] + sy, .45f * len);
            for (unsigned j = 0; room && j < f.count; ++j) {
                const Agent& o = f.birds[j];
                if (&o == &b || o.state == st_gone || o.solo == 1) continue;
                room = std::hypot(o.slot_x - sx, o.slot_y - sy) >= 1.1f * len;
            }
            if (room) { b.slot_x = sx; b.slot_y = sy; return; }
        }
}
void bird_ground_idle(Flock& f, Agent& b, float dt, const Director& dir, const WorldView* w = nullptr) {
    const float t = f.t + 3 * hash01(b.key, 1);
    b.soar = 0;
    b.fold = std::min(1.f, b.fold + dt / .3f);
    const bool big = big_bird(f.variant);
    // a carrion bird that landed away from its slot walks there first
    const float body = bird_length(f.variant) * f.scale;
    const bool walking = f.carrion && b.hop < 0 &&
        std::hypot(f.spot[0] + b.slot_x - b.x, f.spot[1] + b.slot_y - b.y) > .25f * body;
    // idle beat: vultures slow and heavy, corvids quick and hoppy
    const float beat = f.variant == bird_vulture ? 1.4f : (f.variant == bird_crow || f.variant == bird_raven) ? .8f : 1.2f;
    const int slot = int(t / beat);
    const float r = rnd(dir, b.key, key("g"), std::uint64_t(slot)), ph = std::fmod(t, beat) * 1.2f / beat;
    if (f.carrion && !walking && b.hop < 0 && b.hop_slot != slot && r > .93f) { pull_in(f, b, w); b.hop_slot = slot; }
    // feeding birds peck most of the time; others peck now and then
    const float pk = b.hop < 0 && ph < (f.carrion ? .7f : .4f) && r < (f.carrion ? .85f : .6f) ? std::sin(ph / (f.carrion ? .7f : .4f) * PI) : 0.f;
    b.peck = pk * pk;
    b.caw += ((big && r > .85f && ph < .6f ? 1.f : 0.f) - b.caw) * std::min(1.f, dt * 10);
    // hop, keeping away from neighbours; a feeding bird only rarely shuffles
    const float carrion_hops = f.variant == bird_vulture ? .06f : .16f;
    if (f.perching && b.hop < 0 && ph > .75f && r < .12f && b.hop_slot != slot) {   // flutter-hop to another free perch
        const unsigned to = unsigned(hash01(b.key, std::uint64_t(slot)) * f.perches) % f.perches;
        bool free = int(to) != b.perch;
        for (unsigned j = 0; free && j < f.count; ++j) free = f.birds[j].perch != int(to);
        if (free) {
            const float tx = f.perch[to][0], ty = f.perch[to][1];
            b.hop_slot = slot; b.hop = 0; b.hop_x = b.x; b.hop_y = b.y; b.hop_dx = tx - b.x; b.hop_dy = ty - b.y;
            b.hop_yaw0 = b.yaw; b.hop_yaw1 = std::atan2(b.hop_dx, b.hop_dy);
            b.hop_z0 = b.slot_z; b.hop_z1 = f.perch[to][2] - (w ? w->ground(tx, ty) : 0.f);
            b.perch = int(to); b.slot_x = tx - f.spot[0]; b.slot_y = ty - f.spot[1]; b.slot_z = b.hop_z1;
        }
    }
    if (!f.perching && !walking && b.hop < 0 && ph > .75f && r < (f.carrion ? carrion_hops : .7f) && b.hop_slot != slot) {
        float px = 0, py = 0;
        for (unsigned j = 0; j < f.count; ++j) {
            const Agent& o = f.birds[j];
            const float dx = b.x - o.x, dy = b.y - o.y, d = std::hypot(dx, dy);
            if (&o != &b && d < (big ? .5f : .2f) * f.scale) { px += dx / (d + 1e-3f); py += dy / (d + 1e-3f); }
        }
        const float off = std::floor(5 * hash01(b.key, 2));
        float hx = std::sin(slot * 2.3f + off) + px, hy = std::cos(slot * 2.3f) + py;
        if (f.carrion) {   // a small step along its own spot on the ring, never into the body
            const float rx = b.x - f.spot[0], ry = b.y - f.spot[1], rn = std::hypot(rx, ry) + 1e-6f, side = (slot & 1) ? 1.f : -1.f;
            hx = -ry / rn * side; hy = rx / rn * side;
        }
        const float n = std::hypot(hx, hy) + 1e-9f,
                    dist = (f.carrion ? (f.variant == bird_vulture ? .3f : .45f) : big ? .6f : .9f) * bird_length(f.variant) * f.scale;
        b.hop_slot = slot; b.hop = 0; b.hop_x = b.x; b.hop_y = b.y;
        b.hop_dx = hx / n * dist; b.hop_dy = hy / n * dist;
        if (w && !w->walkable(b.x + b.hop_dx, b.y + b.hop_dy)) b.hop_dx = b.hop_dy = 0;   // hop in place
        b.hop_yaw0 = b.yaw; b.hop_yaw1 = std::atan2(hx, hy);
    }
    if (b.hop >= 0) {
        const float hop_h = (big ? .25f : .35f) * bird_length(f.variant) * f.scale * (f.perching ? 1.6f : 1.f);
        bird_hop_step(b, dt, hop_h);
        if (f.perching) {   // between perches the height glides from one to the other
            const float air = std::clamp((b.hop - kHopCrouch) / kHopAir, 0.f, 1.f);
            b.z += b.hop < 0 ? b.slot_z : b.hop_z0 + (b.hop_z1 - b.hop_z0) * smooth01(air);
        }
        if (f.carrion && b.hop < 0) { b.slot_x = b.x - f.spot[0]; b.slot_y = b.y - f.spot[1]; }   // it stays where it hopped
    }
    if (f.perching && b.hop < 0) b.z = b.slot_z;   // sitting on its perch
    if (walking) {
        // Walk to the slot: face the way it goes, a steady stride (big birds a
        // rolling walk, small ones a hop-step) with a body bob, no pecking;
        // it turns to the body once there.
        const float dx = f.spot[0] + b.slot_x - b.x, dy = f.spot[1] + b.slot_y - b.y, off = std::hypot(dx, dy);
        b.peck = 0; b.caw = 0;
        b.yaw = angle_lerp(b.yaw, std::atan2(dx, dy), std::min(1.f, dt * 4));
        const float facing = std::cos(std::atan2(dx, dy) - b.yaw);   // walks off once roughly turned
        const float v = (big ? .8f : 1.2f) * body * std::clamp(facing, 0.f, 1.f) * std::min(1.f, off / (.5f * body) + .3f);
        const float step = std::min(off, v * dt);
        b.x += dx / off * step; b.y += dy / off * step;
        b.gait += dt * (big ? 1.6f : 3.f);
        b.crouch = .18f * std::abs(std::sin(PI * b.gait));
        b.z = big ? 0.f : .08f * body * std::abs(std::sin(PI * b.gait));
    } else if (f.carrion && b.hop < 0) {   // at its slot: settle and turn smoothly to face the body
        const float dx = f.spot[0] + b.slot_x - b.x, dy = f.spot[1] + b.slot_y - b.y;
        // a shuffle step slowly returns to the slot (a few cm per second), no jitter
        if (std::hypot(dx, dy) > .05f * body) { const float k = std::min(1.f, dt * .7f); b.x += dx * k; b.y += dy * k; }
        b.crouch *= std::max(0.f, 1 - dt * 6); b.z = 0;
        b.yaw = angle_lerp(b.yaw, std::atan2(f.spot[0] - b.x, f.spot[1] - b.y), std::min(1.f, dt * 2));
    }
}

void bird_update(Flock& f, Agent& b, float dt, const WorldView& w, const Director& dir) {
    if (b.state == st_gone) return;
    const BirdFlight fl = bird_flight(f.variant, f.scale);
    // a carrion bird may leave or arrive on its own while the group feeds
    const FlockState state = b.solo == 1 ? fl_leave : b.solo == 2 ? fl_land : f.state;
    if (b.solo == 1) b.feed += dt;   // seconds since it took off
    if (state == fl_leave && b.flying && (b.solo == 1 ? b.feed > 1.5f : f.t > 1.5f)) {
        b.alpha = std::max(0.f, b.alpha - dt / kBirdFadeOut);
        if (b.solo == 1 && b.alpha <= 0) { b.state = st_gone; return; }
    } else b.alpha = std::min(1.f, b.alpha + dt / kCarrionFadeIn);
    if (!b.flying) {
        if (state == fl_circle || state == fl_leave) {
            b.delay -= dt;
            if (b.delay <= 0) {
                b.flying = true; b.vx = f.exit[0] * 1.5f; b.vy = f.exit[1] * 1.5f; b.vz = 2.5f; b.roll = 0;
                b.peck = b.caw = b.crouch = b.tail = b.flick = 0; b.hop = -1;
            }
        } else bird_ground_idle(f, b, dt, dir, &w);
        return;
    }
    // goal velocity, at this bird's own cruise speed
    float gx, gy, gz, flare = 0, brake = 0, approach = 1e9f;
    const float body = bird_length(f.variant) * f.scale;
    const float altitude = fl.altitude, cruise = fl.speed * bird_pace(b.key);
    // landing: steepest sink while braking, and the slowest forward speed kept
    const float sink = .55f * cruise, crawl = .25f * cruise;
    if (state != fl_land) b.approach_set = false;
    if (state == fl_circle || state == fl_ground) {
        const float dx = b.x - f.center[0], dy = b.y - f.center[1], r = std::hypot(dx, dy) + 1e-9f;
        const float radial = (fl.radius - r) * .8f;
        gx = -dy / r * cruise + radial * dx / r; gy = dx / r * cruise + radial * dy / r; gz = (altitude - b.z) * 1.2f;
    } else if (state == fl_land) {
        // A short direct glide. When the descent begins the bird fixes its own
        // approach line into the slot: its bearing from the slot turned by up
        // to 55 degrees (per bird), so a flock comes in from varied sides with
        // at most a small correction turn. It steers onto that line by aiming
        // halfway along it to the slot (the distance along the line only
        // shrinks: no orbit) and heads straight in on the last body and a half.
        const float sx = f.spot[0] + b.slot_x, sy = f.spot[1] + b.slot_y;
        const float rx = b.x - sx, ry = b.y - sy, dist = std::hypot(rx, ry) + 1e-9f;
        if (!b.approach_set) {
            float bearing = dist > body ? std::atan2(ry, rx) : TAU * hash01(b.key, key("appr"));
            bearing += 1.92f * (hash01(b.key, key("apoff")) - .5f);
            b.approach_x = std::cos(bearing); b.approach_y = std::sin(bearing); b.approach_set = true;
        }
        const float along = rx * b.approach_x + ry * b.approach_y;
        float tx = sx - b.x, ty = sy - b.y;   // straight at the slot
        if (dist > 1.5f * body && along > 0) { tx += b.approach_x * .5f * along; ty += b.approach_y * .5f * along; }
        const float tn = std::hypot(tx, ty) + 1e-9f;
        // Speed: cruise, braking over the last metres; a bird too high for its
        // glide brakes further (down to a crawl) and drops steeply with the
        // wings beating instead of circling the height away.
        const float height = b.z - b.slot_z;
        const float glide = std::min(cruise, .3f + dist * 1.2f);
        float v = glide;
        if (height > 0) v = std::clamp(sink * dist / height, std::min(crawl, glide), glide);
        brake = glide > 1e-3f ? std::clamp(1 - v / glide, 0.f, 1.f) : 0.f;
        gx = tx / tn * v; gy = ty / tn * v;
        // Down the straight line to the slot (or up to it), the sink bounded.
        gz = std::clamp(-height * v / std::max(dist, .25f * body), -sink, .5f * cruise);
        if (height > 0 && dist < body) gz = std::min(gz, -.15f);   // settle onto the slot
        // Flare: nose up, beating, over the last two and a half bodies.
        flare = smooth01(1 - dist / (2.5f * body)) * smooth01(1 - height / body);
        gz *= 1 - .5f * flare;
        approach = dist;
    } else { gx = f.exit[0] * cruise * 1.2f; gy = f.exit[1] * cruise * 1.2f; gz = 1.8f; }
    // Terrain and objects: keep a clearance over the highest obstacle here
    // and 0.9 s ahead (not on the last metres of a landing onto clear ground).
    if (w.obstacles && !(state == fl_land && approach < 3 * body)) {
        const float ahead_x = b.x + b.vx * .9f, ahead_y = b.y + b.vy * .9f, reach = .6f * body + .5f;
        const float g = w.ground(b.x, b.y);
        const float top = std::max({w.obstacles->top_near(b.x, b.y, reach), w.obstacles->top_near(ahead_x, ahead_y, reach), w.ground(ahead_x, ahead_y)});
        const float floor = top - g + .4f + 1.2f * body;
        if (b.z < floor) gz = std::max(gz, (floor - b.z) * 3 + .5f);
    }
    const float size = (big_bird(f.variant) ? 3.f : 1.f) * f.scale;
    const float steer = state == fl_land ? 3.f : 2.f;   // a landing bird holds its line tighter
    float ax = (gx - b.vx) * steer, ay = (gy - b.vy) * steer, az = (gz - b.vz) * 2;
    unsigned flyers = 0; float cx = 0, cy = 0, cz = 0, avx = 0, avy = 0, avz = 0;
    for (unsigned j = 0; j < f.count; ++j) {
        const Agent& o = f.birds[j];
        if (&o == &b || !o.flying) continue;
        ++flyers; cx += o.x; cy += o.y; cz += o.z; avx += o.vx; avy += o.vy; avz += o.vz;
        const float dx = b.x - o.x, dy = b.y - o.y, dz = b.z - o.z, dd = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (dd < .6f * size) { const float s = .3f * size / (dd * dd + 1e-3f); ax += dx * s; ay += dy * s; az += dz * s; }   // separation
    }
    if (flyers && state != fl_land) {
        ax += (cx / flyers - b.x) * .4f; ay += (cy / flyers - b.y) * .4f; az += (cz / flyers - b.z) * .4f;   // cohesion
        ax += (avx / flyers - b.vx) * .5f; ay += (avy / flyers - b.vy) * .5f; az += (avz / flyers - b.vz) * .5f;   // alignment
    }
    b.vx += ax * dt; b.vy += ay * dt; b.vz += az * dt;
    const float sp = std::sqrt(b.vx * b.vx + b.vy * b.vy + b.vz * b.vz), vmax = cruise * (state == fl_leave ? 1.3f : 1.f);
    if (sp > vmax) { b.vx *= vmax / sp; b.vy *= vmax / sp; b.vz *= vmax / sp; }
    // Bound the actual descent, after acceleration and separation forces.
    if (state == fl_land) b.vz = std::max(b.vz, -sink);
    const float ground_before = w.ground(b.x, b.y);
    const V3 before{b.x, b.y, b.z + ground_before};
    b.x += b.vx * dt; b.y += b.vy * dt;
    b.z += b.vz * dt + ground_before - w.ground(b.x, b.y);   // keep the world height across terrain
    // Wings never overlap: resolve any flock mate closer than a wingspan.
    for (unsigned j = 0; j < f.count; ++j) {
        const Agent& o = f.birds[j];
        if (&o == &b || !o.flying) continue;
        const float dx = b.x - o.x, dy = b.y - o.y, dz = b.z - o.z, dd = std::sqrt(dx * dx + dy * dy + dz * dz);
        const float span = (state == fl_land ? 1.1f : 2.6f) * body;   // landing slots are a body apart
        if (dd < span && dd > 1e-4f) { const float push = .5f * (span - dd) / dd; b.x += dx * push; b.y += dy * push; b.z += dz * push; }
    }
    // Separation also changes position. Bound the resulting world-space
    // descent, including that correction and terrain-relative height changes.
    if (state == fl_land)
        b.z = std::max(b.z, before.z - w.ground(b.x, b.y) - sink * dt);
    if (state == fl_land && b.z <= b.slot_z + .02f && std::hypot(b.x - f.spot[0] - b.slot_x, b.y - f.spot[1] - b.slot_y) < .5f * f.scale) {
        b.z = b.slot_z; b.flying = false; b.vx = b.vy = b.vz = 0; b.solo = 0; b.feed = 0;
        if (f.perching) { b.x = f.spot[0] + b.slot_x; b.y = f.spot[1] + b.slot_y; }
        // touch-down keeps the reserved slot: landed within a quarter body it
        // stays where it is, farther out it walks over (bird_ground_idle)
        if (std::hypot(b.x - f.spot[0] - b.slot_x, b.y - f.spot[1] - b.slot_y) < .25f * body) { b.slot_x = b.x - f.spot[0]; b.slot_y = b.y - f.spot[1]; }
        if (f.carrion) pull_in(f, b, &w);
        // and play the hop's landing squash in place
        b.hop = kHopCrouch + kHopAir; b.hop_x = b.x; b.hop_y = b.y; b.hop_dx = b.hop_dy = 0; b.hop_yaw0 = b.hop_yaw1 = b.yaw;
    }
    b.z = std::max(b.z, 0.f);
    if (state == fl_land && f.perching && b.z < b.slot_z && std::hypot(b.x - f.spot[0] - b.slot_x, b.y - f.spot[1] - b.slot_y) < .5f * f.scale)
        b.z = b.slot_z;   // never under its branch
    const float hv = std::hypot(b.vx, b.vy);
    if (hv > .05f) {
        // bank into the turn (coordinated: tan roll = v * yaw rate / g), eased
        const float yaw = std::atan2(b.vx, b.vy);
        float turn = std::fmod(yaw - b.yaw + 3 * PI, TAU) - PI;
        const float rate = dt > 1e-5f ? turn / dt : 0.f;
        b.yaw = yaw;
        const float bank = std::clamp(std::atan(hv * rate / 9.8f), -.7f, .7f) * (1 - flare);
        b.roll += (bank - b.roll) * std::min(1.f, dt * 4);
    }
    const float glide_pitch = std::clamp(std::atan2(b.vz, hv + 1e-6f) * .6f, -.5f, .6f);
    b.pitch = glide_pitch + (.55f - glide_pitch) * std::max(flare, .7f * brake);
    const bool soaring = state == fl_land ? approach > 2.5f * body && b.vz < .2f && brake < .3f :
        fl.soar > 0 && b.vz < .6f && rnd(dir, b.key, key("flap"), std::uint64_t(f.t / 1.5f)) < fl.soar;
    // glide and beat blend over ~0.4 s; the wings open over ~0.2 s after take-off
    b.soar += ((soaring ? 1.f : 0.f) - b.soar) * std::min(1.f, dt * 2.5f);
    b.wing += dt * fl.hz * bird_beat(b.key) * (1 - b.soar);
    b.fold = std::max(0.f, b.fold - dt / .2f);
}

// Cave bat swarm: a loose fluttering fly-by a few metres up, the swarm's
// centre travelling straight past the spot; bats veer from every living unit
// and the camera, never perch, and fade out at the far end.
void bat_update(Flock& f, Agent& b, float dt, const WorldView& w) {
    const BirdFlight fl = bird_flight(f.variant, f.scale);
    if (f.state == fl_leave && f.t > 1) b.alpha = std::max(0.f, b.alpha - dt / kBirdFadeOut);
    else b.alpha = std::min(1.f, b.alpha + dt / kCarrionFadeIn);
    const float k = TAU * hash01(b.key, 3), t = f.t;
    float gx, gy;
    if (f.state == fl_leave) { gx = f.exit[0] * fl.speed; gy = f.exit[1] * fl.speed; }
    else {
        const float dx = b.x - f.center[0], dy = b.y - f.center[1], r = std::hypot(dx, dy) + 1e-6f;
        const float want = fl.radius * (.35f + .3f * std::sin(t * .7f + k)), spin = hash01(b.key, 6) < .5f ? -1.f : 1.f;
        gx = -dy / r * fl.speed * .35f * spin + (want - r) * dx / r * 1.5f + f.exit[0] * fl.speed * .8f;
        gy = dx / r * fl.speed * .35f * spin + (want - r) * dy / r * 1.5f + f.exit[1] * fl.speed * .8f;
    }
    const float gz = (fl.altitude * (.8f + .25f * std::sin(t * 1.3f + k)) - b.z) * 1.5f;
    // erratic flutter on top of the steering
    float ax = (gx - b.vx) * 2.5f + fl.speed * 2.2f * std::sin(t * 6.1f + k) * std::sin(t * 2.3f + 2 * k);
    float ay = (gy - b.vy) * 2.5f + fl.speed * 2.2f * std::cos(t * 5.3f + 2 * k) * std::sin(t * 1.9f + k);
    float az = (gz - b.vz) * 2.5f + 1.2f * std::sin(t * 4.7f + 3 * k);
    const float span = bird_length(f.variant) * f.scale;
    for (unsigned j = 0; j < f.count; ++j) {   // separation, light cohesion
        const Agent& o = f.birds[j];
        if (&o == &b) continue;
        const float dx = b.x - o.x, dy = b.y - o.y, dz = b.z - o.z, dd = std::sqrt(dx * dx + dy * dy + dz * dz) + 1e-4f;
        if (dd < 3 * span) { const float s = 2 * (3 * span - dd) / dd; ax += dx * s; ay += dy * s; az += dz * s; }
    }
    for (std::size_t i = 0; i < w.units->size(); ++i) {   // never in anyone's face
        if ((*w.details)[i].dead) continue;
        const auto& u = (*w.units)[i];
        const float dx = b.x - u[0], dy = b.y - u[1], dd = std::hypot(dx, dy) + 1e-4f;
        if (dd < 2.5f) { const float s = 6 * (2.5f - dd) / dd; ax += dx * s; ay += dy * s; az += 2; }
    }
    if (w.obstacles) {   // walls and rock: keep over them
        const float top = std::max(w.obstacles->top_near(b.x + b.vx * .6f, b.y + b.vy * .6f, .6f), w.ground(b.x + b.vx * .6f, b.y + b.vy * .6f));
        const float floor = top - w.ground(b.x, b.y) + .8f;
        if (b.z < floor) az += (floor - b.z) * 6;
    }
    b.vx += ax * dt; b.vy += ay * dt; b.vz += az * dt;
    const float sp = std::sqrt(b.vx * b.vx + b.vy * b.vy + b.vz * b.vz), vmax = fl.speed * 1.4f;
    if (sp > vmax) { b.vx *= vmax / sp; b.vy *= vmax / sp; b.vz *= vmax / sp; }
    const float ground_before = w.ground(b.x, b.y);
    b.x += b.vx * dt; b.y += b.vy * dt;
    b.z = std::max(.6f, b.z + b.vz * dt + ground_before - w.ground(b.x, b.y));
    const float hv = std::hypot(b.vx, b.vy);
    if (hv > .05f) {
        const float yaw = std::atan2(b.vx, b.vy);
        const float turn = std::fmod(yaw - b.yaw + 3 * PI, TAU) - PI;
        b.roll += (std::clamp(std::atan(hv * (dt > 1e-5f ? turn / dt : 0.f) / 9.8f), -.8f, .8f) - b.roll) * std::min(1.f, dt * 5);
        b.yaw = yaw;
    }
    b.pitch = std::clamp(std::atan2(b.vz, hv + 1e-6f) * .5f, -.5f, .5f);
    b.wing += dt * fl.hz;
}
void bat_swarm_update(Flock& f, float dt, const WorldView& w, const Threat& scare) {
    const BirdFlight fl = bird_flight(f.variant, f.scale);
    (void)scare;
    f.t += dt;
    if (f.state != fl_leave) {   // the swarm's centre flies the path; past its end the bats go on and fade
        f.center[0] += f.exit[0] * fl.speed * .8f * dt; f.center[1] += f.exit[1] * fl.speed * .8f * dt;
        if (f.t * fl.speed * .8f > kBatPath) { f.state = fl_leave; f.t = 0; }
    }
    for (unsigned j = 0; j < f.count; ++j) bat_update(f, f.birds[j], dt, w);
    bool gone = f.state == fl_leave;
    for (unsigned j = 0; j < f.count; ++j) gone = gone && f.birds[j].alpha <= 0;
    if (gone) f.state = fl_gone;
}

// Smallest distance by which a living unit's threat reach misses a landing spot.
float landing_margin(const WorldView& w, const float* spot, float flee) {
    float closest = 99;
    for (std::size_t i = 0; i < w.units->size(); ++i) {
        const auto& det = (*w.details)[i];
        if (det.dead || (!spooks_birds((*w.units)[i]) && !det.undead)) continue;
        const auto& u = (*w.units)[i];
        // Landing yields only to someone near the body, never to the party's whole sight range.
        const float reach = std::min(10.f, threat_reach(flee, senses_of(det)));
        closest = std::min(closest, std::hypot(u[0] - spot[0], u[1] - spot[1]) - (det.undead ? std::max(reach, kUndeadReach) : reach));
    }
    return closest;
}

// Landing slots around a corpse: a ring just outside its body footprint
// (corpse_splat: half length = envelope radius / 2, 0.4-4 m), birds a body
// and a bit apart, outer rings when the inner one is full; each slot must be
// clear ground. Returns the slot count (0: nowhere to land).
unsigned carrion_slots(const WorldView& w, float cx, float cy, float envelope, float len, unsigned want, float phase,
                       float* sx, float* sy, bool tight = false) {
    const float body = std::clamp((envelope > 0 ? envelope : 1.f) * .5f, .4f, 4.f);
    unsigned n = 0;
    for (int ring = 0; ring < 5 && n < want; ++ring) {
        const float r = body + len * ((tight ? .5f : .7f) + (tight ? .9f : 1.1f) * ring);   // corvids crowd closer
        const unsigned fit = std::max(3u, unsigned(TAU * r / ((tight ? 1.f : 1.2f) * len)));
        for (unsigned k = 0; k < fit && n < want; ++k) {
            const float a = phase + TAU * (k + .5f * ring) / fit, x = cx + std::cos(a) * r, y = cy + std::sin(a) * r;
            if (!w.clear_ground(x, y, .45f * len)) continue;
            sx[n] = x - cx; sy[n] = y - cy; ++n;
        }
    }
    return n;
}

void flock_update(Flock& f, float dt, const WorldView& w, const Director& dir, const Threat& scare, bool corpse_present) {
    if (f.variant == bird_bat) { bat_swarm_update(f, dt, w, scare); return; }
    const BirdFlight fl = bird_flight(f.variant, f.scale);
    // A landing may be blocked or never converge. Hold on game time,
    // retry clear slots, then depart through the normal fade path.
    bool approaching = f.state == fl_land;
    for (unsigned j = 0; j < f.count; ++j) approaching = approaching || (f.birds[j].flying && f.birds[j].solo == 2);
    if (approaching || (f.state == fl_circle && f.t > f.circle_time)) f.landing_wait += dt;
    if (f.state == fl_ground && !approaching) f.landing_wait = 0;
    if (f.landing_wait > 25.f && f.state != fl_leave && f.state != fl_gone) { f.state = fl_leave; f.t = 0; f.refused = 5; }
    const auto slots_clear = [&] {
        if (!f.land_ok) return false;
        for (unsigned j = 0; j < f.count; ++j) {
            const auto& b = f.birds[j];
            if (b.state == st_gone || b.solo == 1) continue;
            if (!f.perching && !w.clear_ground(f.spot[0] + b.slot_x, f.spot[1] + b.slot_y, .45f * bird_length(f.variant) * f.scale)) return false;
        }
        return true;
    };
    const auto reserve_slots = [&](float x, float y) {
        float sx[kFlockBirds], sy[kFlockBirds];
        const unsigned n = carrion_slots(w, x, y, f.carrion ? f.corpse_body * 2 : 0.f,
            bird_length(f.variant) * f.scale, f.count, rrange(dir, 0, TAU, f.key, key("retry_slot")), sx, sy, f.variant != bird_vulture);
        if (n == 0) return false;
        // Land whoever fits: birds without a clear slot of their own share one,
        // a little further out, instead of keeping the whole flock in the air.
        f.spot[0] = x; f.spot[1] = y;
        for (unsigned j = 0; j < f.count; ++j) {
            const unsigned k = j % n; const float out = j < n ? 1.f : 1.f + .35f * float(j / n);
            f.birds[j].slot_x = sx[k] * out; f.birds[j].slot_y = sy[k] * out; f.birds[j].slot_z = 0;
        }
        f.land_ok = true;
        return true;
    };
    if (approaching && f.state != fl_leave && !f.land_ok) {   // reserved slots are not re-tested mid-descent
        f.state = fl_circle; f.t = 0; f.circle_time = 2; f.retry_at = 0; f.quiet = 0; f.refused = 4;
        for (auto& b : f.birds) b.solo = 0;
    }
    if (f.carrion) {
        // Interest expires in every approach/feeding state: an unreachable slot
        // must not keep a flock alive after this body stops attracting birds.
        if ((!corpse_present || f.corpse_age >= kCarrionInterest) && f.state != fl_leave) {
            f.state = fl_leave; f.t = 0;
            for (unsigned j = 0; j < f.count; ++j) f.birds[j].delay = .2f * rnd(dir, f.birds[j].key, key("cd"));
        }
        if (f.state == fl_circle || f.state == fl_leave)
            for (auto& b : f.birds) b.solo = 0;   // a scare or a departure takes everyone along
        if (f.state == fl_circle && f.t > f.circle_time) {
            const float closest = landing_margin(w, f.spot, fl.flee);
            f.margin = closest;
            if (!slots_clear() && f.t >= f.retry_at) {
                f.land_ok = reserve_slots(f.spot[0], f.spot[1]);
                f.retry_at = f.t + 2;
            }
            if (!f.land_ok) { f.quiet = 0; f.refused = 1; }
            else if (closest > 1) {
                f.quiet += dt; f.refused = 3;
                if (f.quiet > 3) { f.state = fl_land; f.t = 0; f.quiet = 0; }
            } else {
                f.quiet = 0; f.scattered += dt; f.refused = 2;
                if (f.scattered > 25) { f.state = fl_leave; f.t = 0; f.exit[0] = 1 / 1.044f; f.exit[1] = .3f / 1.044f; }
            }
            for (unsigned j = 0; j < f.count; ++j) bird_update(f, f.birds[j], dt, w, dir);
            f.t += dt;
            return;
        }
    }
    f.t += dt;
    if (f.state == fl_ground || (f.carrion && f.state == fl_land)) {
        const Threat u = flock_threat(f, w);
        if (scare.found) flock_alarm(f, dir, scare.x, scare.y);
        else if (u.found) flock_alarm(f, dir, u.x, u.y);
        // Carrion birds give the body up to whoever walks over: they lift off
        // away from the threat and leave instead of circling back.
        if (f.carrion && (scare.found || u.found)) f.state = fl_leave;
    } else if (f.state == fl_circle && f.t > f.circle_time && f.t >= f.retry_at && f.perching &&
               seat_on_tree(f, w, f.center[0] + f.exit[0] * fl.radius * 1.5f, f.center[1] + f.exit[1] * fl.radius * 1.5f, 30, f.tree, 15, true,
                            f.perch_cactus)) {
        f.state = fl_land; f.t = 0;   // resettle on a tree (a vulture's cactus) further on
    } else if (f.state == fl_circle && f.t > f.circle_time && f.t >= f.retry_at && !f.carrion && !f.perching &&
               f.variant == bird_vulture && rnd(dir, f.key, key("settle")) < .5f &&
               seat_on_tree(f, w, f.center[0], f.center[1], 40, nullptr, 0, true, true)) {
        f.state = fl_land; f.t = 0;   // half the thermal flocks come down onto a cactus
    } else if (f.state == fl_circle && f.t > f.circle_time && f.t >= f.retry_at) {
        // re-land a little further on, away from whoever scared them
        if (f.perching) { f.perching = false; for (unsigned j = 0; j < f.count; ++j) { f.birds[j].slot_z = 0; f.birds[j].perch = -1; } }
        float spot[2]{f.spot[0], f.spot[1]};
        if (!f.carrion) { spot[0] = f.center[0] + f.exit[0] * fl.radius * 1.5f; spot[1] = f.center[1] + f.exit[1] * fl.radius * 1.5f; }
        const float closest = landing_margin(w, spot, fl.flee);
        if (rnd(dir, f.key, key("reland"), std::uint64_t(f.t)) < f.reland && closest > 1) {
            if (reserve_slots(spot[0], spot[1])) { f.state = fl_land; f.t = 0; }
            else f.retry_at = f.t + 2;
        } else f.retry_at = f.t + 2;
    }
    if (f.carrion && (f.state == fl_ground || f.state == fl_land)) {
        // Landed birds feed even while a flockmate is still approaching.
        // Natural feeding: each bird eats for its own while (40-120 s, less on
        // an old body) then flies off alone; while the body is fresh, a
        // newcomer now and then takes a free slot. Past kCarrionInterest
        // nobody new comes and the group thins out to nothing.
        const float stale = std::clamp(f.corpse_age / kCarrionInterest, 0.f, 1.f);
        for (unsigned j = 0; j < f.count; ++j) {
            Agent& b = f.birds[j];
            if (b.state == st_gone || b.flying || b.solo) continue;
            if (!(b.feed_limit > 0)) b.feed_limit = rrange(dir, 40, 120, b.key, key("feed")) * (1 - .6f * stale);
            b.feed += dt;
            if (b.feed > b.feed_limit) {
                const float ax = b.x - f.spot[0], ay = b.y - f.spot[1], n = std::hypot(ax, ay) + 1e-6f;
                b.solo = 1; b.feed = 0; b.flying = true; b.peck = b.caw = b.crouch = b.tail = b.flick = 0; b.hop = -1;
                b.vx = ax / n * 1.5f; b.vy = ay / n * 1.5f; b.vz = 2.5f; b.roll = 0;
            }
        }
        if (stale < 1 && f.state == fl_ground) {
            f.next_newcomer -= dt;
            if (f.next_newcomer <= 0) {
                f.next_newcomer = rrange(dir, 15, 45, f.key, key("nc"), std::uint64_t(f.t)) / std::max(1 - stale, .1f);
                for (unsigned j = 0; j < f.count; ++j) {
                    Agent& b = f.birds[j];
                    if (b.state != st_gone) continue;
                    const float ang = rrange(dir, 0, TAU, b.key, key("nca"), std::uint64_t(f.t));
                    const float sx = b.slot_x, sy = b.slot_y;
                    const std::uint64_t k = b.key;
                    b = Agent{};
                    b.bird = f.variant; b.key = splitmix(k); b.state = st_idle; b.solo = 2; b.flying = true; b.fold = 0;
                    b.slot_x = sx; b.slot_y = sy;
                    b.x = f.spot[0] + std::cos(ang) * 22; b.y = f.spot[1] + std::sin(ang) * 22; b.z = fl.altitude;
                    b.vx = -std::cos(ang) * fl.speed; b.vy = -std::sin(ang) * fl.speed;
                    break;
                }
            }
        }
    }
    for (unsigned j = 0; j < f.count; ++j) bird_update(f, f.birds[j], dt, w, dir);
    bool all_ground = true, all_away = true, all_gone = true;
    for (unsigned j = 0; j < f.count; ++j) {
        const Agent& b = f.birds[j];
        if (b.state == st_gone) continue;
        all_gone = false;
        all_ground = all_ground && (!b.flying || b.solo);
        all_away = all_away && (b.alpha <= 0 || std::hypot(b.x - f.spot[0], b.y - f.spot[1]) > 30 || b.z > std::max(16.f, fl.altitude + 10));
    }
    if (f.state == fl_land && all_ground) { f.state = fl_ground; f.t = 0; }
    if (f.state == fl_leave && all_away) f.state = fl_gone;
    if (f.carrion && all_gone && f.corpse_age >= kCarrionInterest) f.state = fl_gone;   // the last one left an old body
}


// Base weight of a species in this zone and hour; 0 when it may not appear.
// Corvids: raven flocks in the Dead City, single wild birds elsewhere (rarer).
float species_weight(const WorldView& w, Species s) {
    const auto& rule = species_rule(s);
    const bool grim = w.zone == ambient::zone_dead_city;
    if ((w.unavailable >> s) & 1u) return 0;   // its game assets failed to load (logged once)
    if (s == sp_bat && !w.party_moving) return 0;   // bats fly past moving heroes, never idle ones
    if (!((rule.zones >> w.zone) & 1u) || (grim && rule.cute)) return 0;
    return rule.weight * (w.day ? rule.day : rule.night) * (s == sp_raven && !grim ? .6f : 1.f);
}
// Dead City dampening (its own ravens excepted) and the recent-species halving.
float weight_now(const Director& d, const WorldView& w, Species s) {
    float f = w.zone == ambient::zone_dead_city && s != sp_raven ? kDeadZoneWeight : 1.f;
    return s != sp_larva && d.clock - d.last_species[s] < kSpeciesCooldown ? f * .5f : f;
}
// A ground animal steps onto open ground of its habitat within kOpenSearch
// when the spot is in dense cover, so grass does not hide it; it stays put
// when none is near. Thermal-riding vultures keep their spot.
void open_spot(const WorldView& w, Species s, float& x, float& y) {
    const auto& rule = species_rule(s);
    const std::uint32_t open = rule.habitat & kOpenGround;
    const auto a = w.area_at(x, y);
    if (rule.exit == exit_soar || !open || a == 0xff || ((open >> a) & 1u)) return;
    float ox, oy;
    if (w.nearest(x, y, open, kOpenSearch, ox, oy) && w.habitat(rule, ox, oy)) { x = ox; y = oy; }
}
// Whether a species may appear at a ground point: habitat, undead and wall
// rules, and no unit close enough to scare it at once. On success, the world
// size it takes (its species band, never the view's).
bool fits_at(Director& d, const WorldView& w, Species s, float x, float y, float& scale) {
    const auto& rule = species_rule(s);
    const bool grim = w.zone == ambient::zone_dead_city;
    scale = species_scale(s);
    if (s == sp_larva && !larva_habitat(w, x, y)) { ++d.habitat_reject; return false; }
    if (!w.habitat(rule, x, y) || (rule.exit != exit_soar && s != sp_bat && !w.walkable(x, y))) { ++d.habitat_reject; return false; }
    bool near_undead = false;
    for (std::size_t k = 0; k < w.units->size(); ++k)
        if ((*w.details)[k].undead && !(*w.details)[k].dead && std::hypot((*w.units)[k][0] - x, (*w.units)[k][1] - y) < kUndeadExclusion)
            near_undead = true;
    // Every living critter keeps away from the undead; Dead City ravens stay outside its wall.
    if (near_undead || (s == sp_raven && grim && !w.outside_walls(x, y))) { ++d.undead_reject; return false; }
    if (s == sp_raven && !grim) {   // a wild corvid sits in a tree
        float tree[2], perches[kPerchCount][3];
        if (!perch_tree(w, x, y, 20, nullptr, 0, false, tree, perches)) { ++d.habitat_reject; return false; }
    }
    float margin = std::numeric_limits<float>::infinity();
    for (std::size_t k = 0; k < w.units->size(); ++k) {
        if ((*w.details)[k].dead || (s == sp_larva && (*w.details)[k].crawler)) continue;
        const auto& u = (*w.units)[k];
        margin = std::min(margin, std::hypot(u[0] - x, u[1] - y) - threat_reach(rule.flee_r, senses_of((*w.details)[k])));
    }
    if (margin < 2) { ++d.threat_reject; return false; }
    // Other ground critters keep well away from crawlers (they are prey).
    if (s != sp_larva && rule.exit != exit_flock && rule.exit != exit_soar && s != sp_bat)
        for (std::size_t k = 0; k < w.units->size(); ++k)
            if ((*w.details)[k].crawler && !(*w.details)[k].dead &&
                std::hypot((*w.units)[k][0] - x, (*w.units)[k][1] - y) < 20.f) { ++d.threat_reject; return false; }
    return true;
}

// A bird group of a flocking species at a ground spot (false: it needs a perch
// and none is near). Small birds sit on a tree 60 % of the time; vultures on a
// cactus 40 %, else they ride a thermal overhead; the Dead City's ravens flock
// on the ground; elsewhere a corvid is one wild raven or crow perched on a tree.
bool start_flock(Flock& f, const Director& d, const WorldView& w, Species s, float x, float y, std::uint64_t k) {
    const auto& rule = species_rule(s);
    unsigned n = rule.group_lo + unsigned(rnd(d, k, key("n")) * (rule.group_hi - rule.group_lo + 1));
    if (w.level < 2) n = std::min(n, 5u);
    unsigned vcount = 0;
    for (const Bird b : rule.variants) vcount += b != bird_none ? 1u : 0u;
    Bird variant = rule.variants[std::min(vcount - 1, unsigned(rnd(d, k, key("v")) * vcount))];
    if (s == sp_raven && w.zone != ambient::zone_dead_city) {
        variant = w.zone == ambient::zone_snow ? bird_crow : variant;
        flock_init(f, variant, 1, x, y, d, k, nullptr, 4, 8, .5f, bird_scale(variant));
        if (seat_on_tree(f, w, x, y, 20, nullptr, 0, false)) return true;
        f.active = false;
        return false;
    }
    if (s == sp_raven) variant = bird_raven;
    if (rule.exit == exit_soar) {
        flock_init(f, variant, n, x, y, d, k, nullptr, 4, 8, 0, bird_scale(variant));
        if (rnd(d, k, key("cactus")) < .4f && seat_on_tree(f, w, x, y, 30, nullptr, 0, false, true)) return true;
        const float ang = rrange(d, 0, TAU, k, key("arr")), arrive[2]{std::cos(ang), std::sin(ang)};
        flock_init(f, variant, n, x, y, d, k, arrive, 40, 60, 0, bird_scale(variant));   // a thermal overhead
        return true;
    }
    flock_init(f, variant, n, x, y, d, k, nullptr, 4, 8, .5f, bird_scale(variant));
    if (!big_bird(variant) && variant != bird_bat && rnd(d, k, key("perch")) < .6f) seat_on_tree(f, w, x, y, 20, nullptr, 0, false);
    return true;
}

// Select only species with a visible, habitat-valid, unthreatened candidate.
// A rare habitat outside the view must not waste the whole spawn opportunity.
void try_spawn(Director& d, const WorldView& w, const std::vector<V3>& spots) {
    ++d.spawn_attempts;
    const std::uint32_t a = d.attempt;
    float weights[sp_count]{}, scales[sp_count]{}, cx[sp_count]{}, cy[sp_count]{}, total = 0;
    bool eligible_species = false;
    // Ground critters appear near the party's own level, not on ledges above or
    // pits below it (caves); a party that reaches a height lets them appear there.
    float party_z = 0; unsigned party_n = 0;
    for (std::size_t k = 0; k < w.units->size(); ++k)
        if (!(*w.details)[k].dead && (*w.details)[k].controlled) { party_z += w.ground((*w.units)[k][0], (*w.units)[k][1]); ++party_n; }
    if (party_n) party_z /= float(party_n);
    for (unsigned s = 0; s < sp_count; ++s) {
        float wt = species_weight(w, Species(s));
        if (!(wt > 0)) continue;
        eligible_species = true;
        // Larvae sample the current crawler rings rather than hoping a terrain
        // spot happens to intersect this narrow habitat. No extra retained list.
        const std::size_t candidates = s == sp_larva ? w.units->size() * (w.units->size() + 1) : spots.size();
        for (std::size_t i = 0; i < candidates; ++i) {
            float x, y;
            if (s == sp_larva) {
                const std::size_t crawler = i / (w.units->size() + 1), member = i % (w.units->size() + 1);
                if ((*w.details)[crawler].dead || !(*w.details)[crawler].crawler) continue;
                float angle = rrange(d, 0, TAU, a, crawler, key("larva_angle"));
                float radius = rrange(d, 15.f, 20.f, a, crawler, key("larva_radius"));
                if (member < w.units->size()) {
                    const auto& unit = (*w.details)[member];
                    if (unit.dead || !unit.controlled || !(unit.vision > 0)) continue;
                    const float dx = (*w.units)[member][0] - (*w.units)[crawler][0];
                    const float dy = (*w.units)[member][1] - (*w.units)[crawler][1];
                    angle = std::atan2(dy, dx);
                    radius = std::clamp(std::hypot(dx, dy), 15.f, 20.f);
                    // A member standing in the ring would have the grub at its feet (a
                    // threat reject every time): move 7 m along the ring, either way.
                    if (std::hypot(dx - radius * std::cos(angle), dy - radius * std::sin(angle)) < 5.f)
                        angle += (a & 1u ? 7.f : -7.f) / radius;
                }
                x = (*w.units)[crawler][0] + radius * std::cos(angle);
                y = (*w.units)[crawler][1] + radius * std::sin(angle);
            } else { x = spots[i].x; y = spots[i].y; }
            float scale;
            if (!(w.party_extent(x, y) <= kSpotBand)) { ++d.size_reject; continue; }   // inside the party's sight
            const auto& level_rule = species_rule(Species(s));
            if (party_n && level_rule.exit != exit_flock && level_rule.exit != exit_soar && s != sp_bat &&
                std::fabs(w.ground(x, y) - party_z) > 4.f) { ++d.habitat_reject; continue; }
            open_spot(w, Species(s), x, y);
            if (!fits_at(d, w, Species(s), x, y, scale)) continue;
            cx[s] = x; cy[s] = y; scales[s] = scale;
            wt *= weight_now(d, w, Species(s));
            weights[s] = wt; total += wt;
            break;
        }
    }
    if (!(total > 0)) {
        if (eligible_species) ++d.no_spot; else ++d.no_species;
        return;
    }
    float pick = rnd(d, a, key("sp")) * total;
    unsigned s = 0;
    if (weights[sp_larva] > 0) s = sp_larva;   // near a live crawler, its larvae come first
    else for (; s < sp_count; ++s) { if (weights[s] <= 0) continue; pick -= weights[s]; if (pick <= 0) break; }
    if (s >= sp_count) return;
    const Species species = Species(s);
    const auto& rule = species_rule(species);
    const float x = cx[s], y = cy[s];
    {
        unsigned n = rule.group_lo + unsigned(rnd(d, a, key("n")) * (rule.group_hi - rule.group_lo + 1));
        if (rule.exit == exit_flock || rule.exit == exit_soar) {
            if (!start_flock(d.flock, d, w, species, x, y, splitmix(d.seed ^ (std::uint64_t(d.events) << 8) ^ key("f")))) return;
        } else {
            n = species == sp_larva ? std::min(n, 4u) : std::min(n, w.level < 2 ? 1u : 2u);   // grubs come in small clutches
            for (unsigned j = 0; j < n; ++j) {
                Agent& c = d.critters[j];
                c = Agent{};
                c.species = species; c.scale = scales[s]; c.key = splitmix(d.seed ^ (std::uint64_t(d.events) << 8) ^ (j + 1));
                c.x = x + (rnd(d, a, j, key("ox")) - .5f) * .6f * j; c.y = y + (rnd(d, a, j, key("oy")) - .5f) * .6f * j;
                c.yaw = rrange(d, 0, TAU, c.key, key("yaw"));
                c.idle_limit = rrange(d, kIdleMin, kIdleMax, c.key, key("idle"));
            }
            d.critter_count = n;
        }
        d.last_species[s] = d.clock;
        ++d.events;
        return;
    }
}

// ---- spots: creatures found where the world put them
SpotMemory* spot_memory(Director& d, std::uint32_t sector, bool create) {
    for (auto& m : d.spot_memory) if (m.sector == sector) return &m;
    if (!create) return nullptr;
    d.spot_memory.push_back({sector});
    return &d.spot_memory.back();
}
// Occupy a spot: a species chosen by zone, hour, habitat and soundscape, found
// already there (outside party sight) or fading in (first seen inside it).
bool populate_spot(Director& d, const WorldView& w, std::uint32_t sector, const SpotCell& cell, bool fade) {
    const std::uint64_t roll = d.spot_rolls;
    float weights[sp_count]{}, scales[sp_count]{}, cx[sp_count]{}, cy[sp_count]{}, total = 0;
    for (unsigned s = 0; s < sp_count; ++s) {
        if (species_rule(Species(s)).exit == exit_soar) continue;   // vultures ride thermals, not spots
        float wt = species_weight(w, Species(s));
        cx[s] = cell.x; cy[s] = cell.y;
        if (wt > 0) open_spot(w, Species(s), cx[s], cy[s]);
        if (!(wt > 0) || !fits_at(d, w, Species(s), cx[s], cy[s], scales[s])) continue;
        wt *= weight_now(d, w, Species(s)) * sound_bias(Species(s), cell.sound);
        weights[s] = wt; total += wt;
    }
    if (!(total > 0)) return false;
    float pick = rnd(d, sector, roll, key("ssp")) * total;
    unsigned s = 0;
    for (; s < sp_count; ++s) { if (weights[s] <= 0) continue; pick -= weights[s]; if (pick <= 0) break; }
    if (s >= sp_count) return false;
    Resident* r = nullptr;
    for (auto& slot : d.residents) if (!slot.active) { r = &slot; break; }
    if (!r) return false;
    const Species species = Species(s);
    const auto& rule = species_rule(species);
    *r = Resident{};
    r->active = true; r->sector = sector;
    const std::uint64_t k = splitmix(d.seed ^ (std::uint64_t(sector) << 20) ^ roll ^ key("spot"));
    if (rule.exit == exit_flock) {
        r->is_flock = true;
        if (!start_flock(r->birds, d, w, species, cx[s], cy[s], k)) { *r = Resident{}; return false; }
        if (fade) for (unsigned j = 0; j < r->birds.count; ++j) r->birds.birds[j].alpha = 0;
    } else {
        Agent& c = r->critter;
        c.species = species; c.scale = scales[s]; c.key = k;
        c.x = cx[s]; c.y = cy[s];
        c.yaw = rrange(d, 0, TAU, k, key("yaw"));
        c.idle_limit = rrange(d, 90, 180, k, key("idle"));   // at home: stays a good while
        if (!fade) { c.state = st_idle; c.alpha = 1; }
    }
    d.last_species[s] = d.clock;
    ++d.spot_found; d.spot_faded += fade ? 1u : 0u;
    return true;
}

// A resident is gone (or its birds went to a death): its spot is free again,
// after a rest on game time when it was disturbed.
void end_resident(Director& d, Resident& r) {
    r.active = false;
    SpotMemory* m = spot_memory(d, r.sector, true);
    m->occupied = false;
    if (r.disturbed) {
        m->quiet_until = d.clock + rrange(d, kSpotQuietMin, kSpotQuietMax, r.sector, d.spot_rolls, key("quiet"));
        ++d.spot_disturbed;
    }
}

// Residents live until they flee, leave or fall far outside party sight; a
// disturbed spot then rests on game time. Spots near the party's sight are
// armed once per approach: just outside its edge they are found occupied (no
// pop in sight); first seen inside it (a teleport, a load) they fade in.
// Extents are in vision ranges: <= 1 is seen by a party member.
void spots_tick(Director& d, float dt, const WorldView& w, const std::vector<std::array<float, 2>>& combat, float tail_y) {
    const auto scare_at = [&](float x, float y) {
        Threat t;
        for (const auto& c : combat) if (std::hypot(c[0] - x, c[1] - y) < 12) t = {true, c[0], c[1]};
        return t;
    };
    for (auto& r : d.residents) {
        if (!r.active) continue;
        float x, y;
        bool gone;
        if (r.is_flock) {
            flock_centroid(r.birds, x, y);
            flock_update(r.birds, dt, w, d, scare_at(x, y), true);
            flock_centroid(r.birds, x, y);
            r.disturbed = r.disturbed || r.birds.state != fl_ground;
            gone = r.birds.state == fl_gone || std::hypot(x - w.focus.x, y - w.focus.y) > w.view_radius + 15;
        } else {
            critter_update(d, r.critter, dt, w, scare_at(r.critter.x, r.critter.y), tail_y);
            x = r.critter.x; y = r.critter.y;
            r.disturbed = r.disturbed || (r.critter.state != st_appear && r.critter.state != st_idle);
            gone = r.critter.state == st_gone;
        }
        if (!gone && !(w.party_extent(x, y) <= kSpotForget)) gone = true;
        if (gone) end_resident(d, r);
    }
    const SpotMap* map = w.spot_map;
    if (!map || map->cells.empty()) return;
    const float reach = w.view_radius * 1.8f + 32;
    // Forget rested, unoccupied sectors once out of reach (or never armed).
    d.spot_memory.erase(std::remove_if(d.spot_memory.begin(), d.spot_memory.end(), [&](const SpotMemory& m) {
        const float cx = (float(m.sector % map->sectors_x) + .5f) * 32, cy = (float(m.sector / map->sectors_x) + .5f) * 32;
        const bool away = std::hypot(cx - w.focus.x, cy - w.focus.y) > reach + 32;
        return !m.occupied && d.clock >= m.quiet_until && (away || !m.rolled);
    }), d.spot_memory.end());
    const bool quiet = d.clock - d.last_combat < kCombatQuiet;
    const int x0 = std::max(0, int(std::floor((w.focus.x - reach) / 32))), x1 = std::min(int(map->sectors_x) - 1, int(std::floor((w.focus.x + reach) / 32)));
    const int y0 = std::max(0, int(std::floor((w.focus.y - reach) / 32))), y1 = std::min(int(map->sectors_y) - 1, int(std::floor((w.focus.y + reach) / 32)));
    for (int sy = y0; sy <= y1; ++sy)
        for (int sx = x0; sx <= x1; ++sx) {
            const std::uint32_t sector = std::uint32_t(sy) * map->sectors_x + std::uint32_t(sx);
            const SpotCell& cell = map->cells[sector];
            if (!cell.valid) continue;
            const float extent = w.party_extent(cell.x, cell.y);
            SpotMemory* m = spot_memory(d, sector, false);
            if (!(extent <= kSpotForget)) { if (m && !m->occupied) m->rolled = false; continue; }
            if (m && (m->occupied || m->rolled || d.clock < m->quiet_until)) continue;
            if (extent > kSpotBand || quiet) continue;   // wait for the approach band; no finds right after a fight
            if (on_screen(d) >= critter_budget(w.level)) { ++d.budget_reject; continue; }
            m = spot_memory(d, sector, true);
            m->rolled = true;
            ++d.spot_rolls;
            if (rnd(d, sector, d.spot_rolls, key("roll")) >= kSpotChance * (w.level < 2 ? .5f : 1.f)) continue;
            if (populate_spot(d, w, sector, cell, extent <= 1)) {
                m->occupied = true;
                return;   // at most one find per tick
            }
        }
}

// Birds already near a death go to it: their flock becomes that body's carrion
// flock (clear slots around it, then the arrival's landing rule: nobody near
// the body), taking off from the ground or its perches towards the body.
void go_to_corpse(Flock& f, const Corpse& c, const WorldView& w, const Director& d) {
    float cx, cy; flock_centroid(f, cx, cy);
    f.carrion = true; f.corpse = c.id; f.corpse_age = float(d.clock - c.seen);
    f.corpse_body = std::clamp((c.envelope > 0 ? c.envelope : 1.f) * .5f, .4f, 4.f);
    f.perching = f.perch_cactus = false;
    f.state = fl_circle; f.t = 0; f.circle_time = rrange(d, 3, 6, f.key, key("near"));
    f.retry_at = f.quiet = f.scattered = f.landing_wait = f.next_newcomer = 0; f.refused = 0; f.margin = 99;
    f.spot[0] = f.center[0] = c.x; f.spot[1] = f.center[1] = c.y;
    const float ax = c.x - cx, ay = c.y - cy, n = std::hypot(ax, ay) + 1e-6f;
    f.exit[0] = ax / n; f.exit[1] = ay / n;
    float sx[kFlockBirds], sy[kFlockBirds];
    const unsigned slots = carrion_slots(w, c.x, c.y, c.envelope, bird_length(f.variant) * f.scale, f.count,
                                         rrange(d, 0, TAU, f.key, key("ring")), sx, sy, f.variant != bird_vulture);
    f.land_ok = slots > 0;
    for (unsigned j = 0; j < f.count; ++j) {
        Agent& b = f.birds[j];
        b.perch = -1; b.solo = 0; b.feed = b.feed_limit = 0; b.hop = -1; b.slot_z = 0;
        if (slots) {
            const unsigned k = j % slots; const float out = j < slots ? 1.f : 1.f + .35f * float(j / slots);
            b.slot_x = sx[k] * out; b.slot_y = sy[k] * out;
        }
        if (!b.flying) b.delay = .6f * rnd(d, b.key, key("go"));
    }
}

void director_tick(Director& d, float dt, const WorldView& w, const std::vector<V3>& spots, float tail_y, double elapsed) {
    if (elapsed <= 0) return; // pause holds state and transitions
    d.clock += elapsed;
    // Combat anywhere near: no spawns for a while; fighting within 12 m
    // sends every critter and flock running at once.
    std::vector<std::array<float, 2>> combat;
    for (std::size_t i = 0; i < w.units->size(); ++i)
        if (!(*w.details)[i].dead && (*w.details)[i].stance == stance_attack && w.near_activity((*w.units)[i]))
            combat.push_back({(*w.units)[i][0], (*w.units)[i][1]});
    if (!combat.empty()) d.last_combat = d.clock;
    const auto scare_at = [&](float x, float y) {
        Threat t;
        for (const auto& c : combat) if (std::hypot(c[0] - x, c[1] - y) < 12) t = {true, c[0], c[1]};
        return t;
    };
    const unsigned before = d.critter_count + (d.flock.active ? 1u : 0u);
    for (unsigned i = 0; i < d.critter_count; ++i) critter_update(d, d.critters[i], dt, w, scare_at(d.critters[i].x, d.critters[i].y), tail_y);
    if (d.flock.active) {
        float cx, cy; flock_centroid(d.flock, cx, cy);
        flock_update(d.flock, dt, w, d, scare_at(cx, cy), true);
    }
    // Leave party sight: critters a little past its edge, flocks well beyond it.
    unsigned kept = 0;
    for (unsigned i = 0; i < d.critter_count; ++i) {
        const Agent& c = d.critters[i];
        if (c.state != st_gone && w.party_extent(c.x, c.y) <= kSpotForget) d.critters[kept++] = c;
    }
    d.critter_count = kept;
    if (d.flock.active) {
        float cx, cy; flock_centroid(d.flock, cx, cy);
        if (d.flock.state == fl_gone || std::hypot(cx - w.focus.x, cy - w.focus.y) > w.view_radius + 15) d.flock.active = false;
    }
    const unsigned after = d.critter_count + (d.flock.active ? 1u : 0u);
    if (after < before && !after) d.next_attempt = d.clock + rrange(d, kCooldownMin, kCooldownMax, d.events, key("cd")) * (w.level < 2 ? 1.5f : 1.f);
    spots_tick(d, dt, w, combat, tail_y);
    if (!after) {
        while (d.clock >= d.next_attempt) {
            ++d.attempt;
            d.next_attempt += kAttemptEvery;
            if (d.clock - d.last_combat < kCombatQuiet) { ++d.quiet_reject; continue; }
            if (on_screen(d) >= critter_budget(w.level)) { ++d.budget_reject; continue; }
            const float rate = kAttemptChance * (w.zone == ambient::zone_dead_city ? kDeadZoneWeight : 1.f) * (w.level < 2 ? .5f : 1.f);
            if (rnd(d, d.attempt, key("p")) < rate) {
                try_spawn(d, w, spots);
                if (d.critter_count || d.flock.active) break;
            }
        }
    }

    // Carrion: a separate budget of corpse flocks.
    // Every corpse row of the retained scene keeps its record (and age); the
    // record ends when the game removes the body or the world resets.
    for (auto& c : d.corpses) { c.was_present = c.present; c.present = c.in_scene = false; }
    for (std::size_t i = 0; i < w.units->size(); ++i) {
        const auto& det = (*w.details)[i];
        if (!det.dead || !carrion_edible(det.blood, det.undead)) continue;
        Corpse* slot = nullptr;
        for (auto& c : d.corpses) if (c.used && c.id == det.id) slot = &c;
        if (!slot) {
            d.corpses.emplace_back();
            slot = &d.corpses.back(); slot->used = true; slot->id = det.id;
            slot->seen = d.clock; slot->u = hash01(d.seed, det.id, key("delay"));
        }
        slot->in_scene = true; slot->present = w.near_activity((*w.units)[i]);
        slot->x = (*w.units)[i][0]; slot->y = (*w.units)[i][1]; slot->envelope = (*w.units)[i][3];
    }
    const unsigned max_flocks = w.level < 2 ? 1u : 2u;
    for (auto& f : d.carrion) {
        if (!f.active) continue;
        bool present = false;
        for (const auto& c : d.corpses) {
            if (!(c.used && c.id == f.corpse)) continue;
            present = present || c.present;
            f.corpse_age = float(d.clock - c.seen);
        }
        flock_update(f, dt, w, d, {}, present);
        if (f.state == fl_gone) {
            f.active = false;
            for (auto& c : d.corpses) if (c.used && c.id == f.corpse) c.cooldown = d.clock + rrange(d, 20, 40, f.corpse, d.events);
        }
    }
    for (auto& c : d.corpses) {
        if (!c.used) continue;
        bool flocked = false;
        unsigned active = 0;
        for (const auto& f : d.carrion) { flocked = flocked || (f.active && f.corpse == c.id); active += f.active ? 1u : 0u; }
        if (!c.in_scene) { if (!flocked) c.used = false; continue; }
        if (!c.present) continue;
        bool near_undead = false;
        for (std::size_t i = 0; i < w.units->size(); ++i) {
            const auto& det = (*w.details)[i];
            if (det.dead) continue;
            const float dd = std::hypot((*w.units)[i][0] - c.x, (*w.units)[i][1] - c.y);
            near_undead = near_undead || (det.undead && dd < kUndeadExclusion);
        }
        if (flocked || active >= max_flocks) continue;
        // Ravens, crows or vultures already near the death come to it first.
        const float age = float(d.clock - c.seen);
        if (d.clock >= c.cooldown && age <= kCarrionInterest && carrion_near_due(age, c.u)) {
            Flock* nearby = nullptr; Resident* owner = nullptr;
            float best = kNearDeath;
            const auto consider = [&](Flock& f, Resident* r) {
                if (!f.active || f.carrion || f.state == fl_leave || f.state == fl_gone ||
                    (f.variant != bird_raven && f.variant != bird_crow && f.variant != bird_vulture)) return;
                float fx, fy; flock_centroid(f, fx, fy);
                const float dd = std::hypot(fx - c.x, fy - c.y);
                if (dd < best) { best = dd; nearby = &f; owner = r; }
            };
            consider(d.flock, nullptr);
            for (auto& r : d.residents) if (r.active && r.is_flock) consider(r.birds, &r);
            Flock* slot = nullptr;
            for (auto& f : d.carrion) if (!f.active) { slot = &f; break; }
            if (nearby && slot) {
                *slot = *nearby;
                nearby->active = false;
                if (owner) { owner->disturbed = true; end_resident(d, *owner); }
                go_to_corpse(*slot, c, w, d);
                ++d.events; ++d.carrion_arrivals;
                continue;
            }
        }
        if (d.clock < c.cooldown || !carrion_due(age, c.u)) continue;
        if (d.clock - c.seen > kCarrionInterest) continue;   // an old body draws no new flock
        // Nearby living units prevent landing in flock_update, not arrival.
        // Carrion can circle above a visible corpse while the party stays near it.
        const Bird variant = carrion_bird(w.zone, near_undead, !w.outside_walls(c.x, c.y));
        if (variant == bird_none || ((w.unavailable >> (variant == bird_vulture ? sp_vulture : sp_raven)) & 1u)) continue;
        const std::uint64_t k = splitmix(c.id ^ (std::uint64_t(d.events) << 16));
        const unsigned lo = variant == bird_vulture ? 2 : 3, hi = variant == bird_vulture ? 5 : 7;
        const unsigned n = lo + unsigned(rnd(d, k, key("n")) * (hi - lo + 1));
        const float ang = rrange(d, 0, TAU, k, key("dir")), arrive[2]{std::cos(ang), std::sin(ang)};
        // An overdue corpse coming back into range is found with its flock
        // already feeding, unless a living unit would keep it up.
        const float spot[2]{c.x, c.y};
        Flock probe; probe.variant = variant; probe.scale = bird_scale(variant);
        const bool landed = !c.was_present && landing_margin(w, spot, bird_flight(variant).flee) > 1;
        for (auto& f : d.carrion) {
            if (f.active) continue;
            flock_init(f, variant, n, c.x, c.y, d, k, landed ? nullptr : arrive, 5, 9, 1, probe.scale);
            f.carrion = true; f.corpse = c.id;
            float sx[kFlockBirds], sy[kFlockBirds];
            const unsigned slots = carrion_slots(w, c.x, c.y, c.envelope, bird_length(variant) * f.scale, f.count,
                                                 rrange(d, 0, TAU, k, key("ring")), sx, sy, variant != bird_vulture);
            f.land_ok = slots > 0;
            f.corpse_body = std::clamp((c.envelope > 0 ? c.envelope : 1.f) * .5f, .4f, 4.f);
            d.slots_wanted += f.count; d.slots_found += slots; d.slots_failed += slots ? 0u : 1u;
            if (slots) f.count = std::uint8_t(slots);
            for (unsigned j = 0; j < f.count; ++j) {   // each bird its own clear slot around the body
                Agent& b = f.birds[j];
                b.slot_x = slots ? sx[j] : 0.f; b.slot_y = slots ? sy[j] : 0.f;
                if (landed && slots) { b.x = c.x + b.slot_x; b.y = c.y + b.slot_y; b.flying = false; b.z = 0;
                                       b.yaw = std::atan2(-b.slot_x, -b.slot_y); }
            }
            if (landed && !slots) for (unsigned j = 0; j < f.count; ++j) { f.birds[j].flying = true; f.birds[j].z = bird_flight(variant, f.scale).altitude; }
            if (landed && !slots) f.state = fl_circle;
            ++d.events; ++d.carrion_arrivals; d.carrion_landed += landed ? 1u : 0u;
            break;
        }
    }
    // Party sight on game time: an animal fades out over kSightFade as it
    // leaves every party member's vision and back in as it enters one.
    const auto sight = [&](Agent& a) {
        const float step = dt / kSightFade;
        a.seen = std::clamp(a.seen + (w.party_extent(a.x, a.y) <= 1 ? step : -step), 0.f, 1.f);
    };
    const auto sight_flock = [&](Flock& f) { if (f.active) for (unsigned j = 0; j < f.count; ++j) sight(f.birds[j]); };
    for (unsigned i = 0; i < d.critter_count; ++i) sight(d.critters[i]);
    sight_flock(d.flock);
    for (auto& f : d.carrion) sight_flock(f);
    for (auto& r : d.residents) if (r.active) { if (r.is_flock) sight_flock(r.birds); else sight(r.critter); }
    d.corpses.erase(std::remove_if(d.corpses.begin(), d.corpses.end(),
        [](const Corpse& c) { return !c.used; }), d.corpses.end());
}

// critter_showcase: idle animation in place (no repositioning, no fleeing).
// Miniatures play their idle clip from age; procedural rigs get their idle
// gestures; the dung beetle treads its ball on the spot.
void showcase_idle(Agent& c, float dt) {
    c.t += dt; c.age += dt; c.speed = 0;
    switch (c.species) {
    case sp_lizard: { const float ph = std::fmod(c.t, 4.f); c.pushup = ph > 2 && ph < 2.55f ? std::max(0.f, std::sin((ph - 2) * 6)) : 0.f; break; }
    case sp_snake: c.gait += dt * .3f; break;
    case sp_crab: c.claw += ((std::fmod(c.t, 5.f) < 2.5f ? 1.f : 0.f) - c.claw) * std::min(1.f, dt * 3); break;
    case sp_larva: {   // crawl waves in place, then a short pause casting about
        const bool crawling = std::fmod(c.t, 7.f) < 5.f;
        if (crawling) c.gait += dt / kLarvaPeriod;
        c.search += ((crawling ? 0.f : 1.f) - c.search) * std::min(1.f, dt * 2);
        break;
    }
    case sp_dungbeetle:
        c.push = 1; c.speed = .5f; c.gait += dt * 2; c.has_ball = true;
        c.ball_x = c.x - std::sin(c.yaw) * .028f * c.scale; c.ball_y = c.y - std::cos(c.yaw) * .028f * c.scale;
        c.ball_yaw = c.yaw; c.ball_roll += dt * 2;
        break;
    default: break;
    }
}

HRESULT make_buffer(ID3D11Device* device, UINT bytes, UINT bind, UINT misc, UINT stride, const void* data,
                    ComPtr<ID3D11Buffer>& buffer, D3D11_USAGE usage = D3D11_USAGE_DEFAULT, UINT cpu = 0) {
    D3D11_BUFFER_DESC d{}; d.ByteWidth = bytes; d.BindFlags = bind; d.MiscFlags = misc; d.StructureByteStride = stride;
    d.Usage = usage; d.CPUAccessFlags = cpu;
    D3D11_SUBRESOURCE_DATA initial{data, 0, 0};
    return device->CreateBuffer(&d, data ? &initial : nullptr, &buffer);
}
HRESULT make_structured_srv(ID3D11Device* device, ID3D11Buffer* buffer, UINT count, ComPtr<ID3D11ShaderResourceView>& srv) {
    D3D11_SHADER_RESOURCE_VIEW_DESC v{}; v.Format = DXGI_FORMAT_UNKNOWN; v.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    v.Buffer.NumElements = count;
    return device->CreateShaderResourceView(buffer, &v, &srv);
}

} // namespace


struct Renderer::CritterState {
    std::future<std::array<ComPtr<ID3DBlob>, 2>> compile;
    std::future<std::shared_ptr<CritterAssets>> assets;
    ComPtr<ID3D11VertexShader> vs; ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11Buffer> frame_constants, draw_constants, vertices, baked, instances, bones;
    ComPtr<ID3D11ShaderResourceView> vertex_srv, baked_srv, instance_srv, bone_srv, skins_srv;
    ComPtr<ID3D11RasterizerState> raster; ComPtr<ID3D11DepthStencilState> depth; ComPtr<ID3D11BlendState> blend;
    ComPtr<ID3D11SamplerState> sampler;
    bool ready = false;
    UINT instance_capacity = 0, bone_capacity = 0;
    // Small metadata kept from the asset build (the data itself is on the GPU).
    std::array<MeshRange, m_count> meshes{};
    std::array<MiniClips, m_bat + 1> minis{};
    std::uint32_t jaw_start = 0, jaw_frames = 1;
    float larva_length = .07f, larva_center_z = 0, larva_tail_y = 0;
    std::shared_ptr<const StatueBird> statue;   // vulture hinges (part matrices per frame)
    std::uint32_t unavailable = 0;              // species omitted at load
    std::string omitted_text;
    // Simulation state and reused per-frame scratch.
    Director director;
    std::weak_ptr<const TerrainCpuAsset> terrain;
    std::vector<Point> ring;
    SpotMap spot_map;
    double world_time = -1;
    std::vector<std::array<float, 4>> units;
    std::vector<GroundUnitDetail> details;
    std::vector<V3> spots;
    std::vector<float> offset_x, offset_y;   // drawn terrain vertex XY offsets beside the height grid
    Obstacles obstacles;
    float obstacle_age = 0;
    std::vector<Bone> bone_scratch;
    std::vector<std::pair<Mesh, CrInstance>> instances_scratch;
    std::vector<CrInstance> instance_upload;
    // critter_showcase (debug): render-only animals in a fixed layout; the
    // director is not ticked while it is on. Re-anchored when the mode
    // changes or the camera focus moves away.
    int showcase_mode = 0;
    V3 showcase_anchor{}, showcase_right{1, 0, 0}, showcase_forward{0, 1, 0};
    std::vector<std::pair<ShowcaseSlot, Agent>> showcase_critters;
    std::vector<std::pair<ShowcaseSlot, Flock>> showcase_flocks;
    Director showcase_director;
    ComPtr<ID3D11DepthStencilState> depth_xray;   // showcase 3: drawn through everything
};

void append_critter_shader_programs(std::vector<ShaderProgram>& out) {
    static const std::string source = owned_native_shader_source() + kCritterShader;
    out.push_back({source, "critters", nullptr, "crVS", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
    out.push_back({source, "critters", nullptr, "crPS", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
}

HRESULT Renderer::render_critters(const FixedState& frame, const std::array<float, 16>& world_to_clip) noexcept {
    const int level = configured_integer(debug_overlay_.config(), "critters", 2);
    const int showcase = std::clamp(configured_integer(debug_overlay_.config(), "critter_showcase", 0), 0, 3);
    if (critters_drawn_frame_ != native_frame_index_) critters_status_frame_ = native_frame_index_;
    const auto status = [&](const char* message) {
        std::snprintf(critters_status_, sizeof(critters_status_), "Critters: %s", message);
        critters_summary_ = {};
        return S_OK;
    };
    if (level <= 0 && !showcase) { critters_.reset(); critters_failed_ = false; return status("off"); }
    const auto fail = [&](const std::string& reason) {
        critters_failed_ = true; critters_.reset();
        OutputDebugStringA(("EID3D11 critters: " + reason + "; critters omitted.\n").c_str());
        bridge_append_probe_event("critters_omitted", reason.c_str());   // once: the failure is preserved
        return status((reason + "; omitted").c_str());
    };
    if (critters_failed_) return S_OK; // preserve the original failure
    if (!device_ || !context_ || !rtv_ || !dsv_ || scene_composited_) return status("world targets unavailable");
    try {
        if (!critters_) {
            // Shaders and the game assets build on workers; no frame waits.
            ResourceStoreConfig mounts;
            if (!owned_resource_runtime_mounts(mounts)) return fail("game resource mounts are not configured");
            auto state = std::make_shared<CritterState>();
            std::vector<ShaderProgram> programs;
            append_critter_shader_programs(programs);
            state->compile = std::async(std::launch::async, [programs = std::move(programs)]() {
                std::array<ComPtr<ID3DBlob>, 2> blobs;
                for (unsigned k = 0; k < 2; ++k) {
                    ComPtr<ID3DBlob> errors;
                    if (FAILED(compile_shader(programs[k], &blobs[k], &errors))) {
                        if (errors) OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
                        blobs = {};
                        break;
                    }
                }
                return blobs;
            });
            state->assets = std::async(std::launch::async, build_assets, std::move(mounts));
            critters_ = std::move(state);
            return status("loading assets/shaders");
        }
        auto& cr = *critters_;
        if (!cr.ready) {
            if (cr.compile.wait_for(std::chrono::seconds(0)) != std::future_status::ready ||
                cr.assets.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return status("loading assets/shaders");
            const auto blobs = cr.compile.get();
            const auto assets = cr.assets.get();
            if (!blobs[0] || !blobs[1]) return fail("shaders unavailable");
            if (!assets || !assets->error.empty()) return fail(assets ? assets->error : std::string("asset build failed"));
            HRESULT hr = S_OK;
            if (FAILED(hr = device_->CreateVertexShader(blobs[0]->GetBufferPointer(), blobs[0]->GetBufferSize(), nullptr, &cr.vs)) ||
                FAILED(hr = device_->CreatePixelShader(blobs[1]->GetBufferPointer(), blobs[1]->GetBufferSize(), nullptr, &cr.ps)))
                return fail("shader objects unavailable");
            const auto& a = *assets;
            if (FAILED(make_buffer(device_.Get(), UINT(a.vertices.size() * sizeof(CrVertex)), D3D11_BIND_SHADER_RESOURCE,
                    D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, sizeof(CrVertex), a.vertices.data(), cr.vertices, D3D11_USAGE_IMMUTABLE)) ||
                FAILED(make_structured_srv(device_.Get(), cr.vertices.Get(), UINT(a.vertices.size()), cr.vertex_srv)) ||
                FAILED(make_buffer(device_.Get(), UINT(a.baked.size() * sizeof(Bone)), D3D11_BIND_SHADER_RESOURCE,
                    D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, sizeof(Bone), a.baked.data(), cr.baked, D3D11_USAGE_IMMUTABLE)) ||
                FAILED(make_structured_srv(device_.Get(), cr.baked.Get(), UINT(a.baked.size()), cr.baked_srv)) ||
                FAILED(make_buffer(device_.Get(), sizeof(CrFrame), D3D11_BIND_CONSTANT_BUFFER, 0, 0, nullptr, cr.frame_constants,
                    D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE)) ||
                FAILED(make_buffer(device_.Get(), 16, D3D11_BIND_CONSTANT_BUFFER, 0, 0, nullptr, cr.draw_constants,
                    D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE)))
                return fail("mesh or clip buffers unavailable");
            // Skins: one texture array with mips.
            {
                D3D11_TEXTURE2D_DESC d{}; d.Width = d.Height = kSkin; d.MipLevels = 0; d.ArraySize = s_count;
                d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_DEFAULT;
                d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET; d.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
                ComPtr<ID3D11Texture2D> texture;
                if (FAILED(device_->CreateTexture2D(&d, nullptr, &texture)) ||
                    FAILED(device_->CreateShaderResourceView(texture.Get(), nullptr, &cr.skins_srv)))
                    return fail("skin texture unavailable");
                texture->GetDesc(&d);
                for (unsigned s = 0; s < s_count; ++s)
                    context_->UpdateSubresource(texture.Get(), D3D11CalcSubresource(0, s, d.MipLevels), nullptr,
                        &a.texels[std::size_t(s) * kSkin * kSkin * 4], kSkin * 4, 0);
                context_->GenerateMips(cr.skins_srv.Get());
            }
            D3D11_RASTERIZER_DESC raster{}; raster.FillMode = D3D11_FILL_SOLID; raster.CullMode = D3D11_CULL_NONE; raster.DepthClipEnable = TRUE;
            D3D11_DEPTH_STENCIL_DESC depth{}; depth.DepthEnable = TRUE; depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
            depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
            D3D11_BLEND_DESC blend{}; for (auto& target : blend.RenderTarget) target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            D3D11_SAMPLER_DESC sampler{}; sampler.Filter = D3D11_FILTER_ANISOTROPIC; sampler.MaxAnisotropy = 8;
            sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_WRAP; sampler.MaxLOD = D3D11_FLOAT32_MAX;
            if (FAILED(device_->CreateRasterizerState(&raster, &cr.raster)) || FAILED(device_->CreateDepthStencilState(&depth, &cr.depth)) ||
                [&] { D3D11_DEPTH_STENCIL_DESC xray{}; xray.DepthEnable = TRUE; xray.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
                      xray.DepthFunc = D3D11_COMPARISON_ALWAYS; return FAILED(device_->CreateDepthStencilState(&xray, &cr.depth_xray)); }() ||
                FAILED(device_->CreateBlendState(&blend, &cr.blend)) || FAILED(device_->CreateSamplerState(&sampler, &cr.sampler)))
                return fail("pipeline states unavailable");
            cr.meshes = a.meshes; cr.minis = a.minis; cr.jaw_start = a.jaw_start; cr.jaw_frames = std::max(1u, a.jaw_frames);
            cr.larva_length = a.larva_length; cr.larva_center_z = a.larva_center_z; cr.larva_tail_y = a.larva_tail_y;
            cr.statue = a.statue;
            cr.unavailable = unavailable_species(a.omitted);
            for (unsigned m = 0; m < m_count; ++m) {
                if (a.omitted[m].empty()) continue;
                const std::string line = std::string(kMeshNames[m]) + ": " + a.omitted[m];
                OutputDebugStringA(("EID3D11 critters: " + line + "; that critter omitted.\n").c_str());
                bridge_append_probe_event("critters_species_omitted", line.c_str());
                if (!cr.omitted_text.empty()) cr.omitted_text += "; ";
                cr.omitted_text += line;
            }
            cr.ready = true;   // the CPU build data is released with `assets`
        }

        // One update and draw per world frame, only in a frame whose terrain was drawn.
        if (critters_drawn_frame_ == native_frame_index_) return S_OK;
        if (ambient_terrain_frame_ != native_frame_index_) return status("no terrain frame");
        critters_drawn_frame_ = native_frame_index_;
        // The CPU update below (unit walk, director tick, instance build) is
        // critters work; open the scope before it so it is not billed to the
        // preceding ground cover pass.
        transition_profile_pass("critters");
        AmbientWorldSource inputs;
        if (!bridge_ambient_world_source(inputs) || !inputs.terrain) return status("no retained terrain source");
        const WorldEnvironment environment = bridge_world_fog_snapshot().environment;
        if (!environment.valid) return status("no world clock/environment");
        if (!rebuild_ambient_particle_map(inputs, environment) || ambient_menu_ || ambient_area_.empty() || ambient_height_.empty()) return status("habitat/height map unavailable or menu");

        // A new map: fresh director (seeded by the map) and the Dead City wall ring.
        if (cr.terrain.owner_before(inputs.terrain) || inputs.terrain.owner_before(cr.terrain) || cr.terrain.expired()) {
            cr.terrain = inputs.terrain;
            cr.director = Director{};
            const std::string name = inputs.map ? inputs.map->terrain_resource : inputs.terrain->project_resource;
            cr.director.seed = splitmix(key(name.c_str()) ^ std::uint64_t(environment.world_time * 1000));
            cr.ring.clear();
            if (ambient_zone_ == ambient::zone_dead_city && inputs.map) {
                std::vector<std::pair<bool, Point>> pieces;
                for (const auto& o : inputs.map->objects) {
                    const auto& v = o.visual_reference;
                    if (v.compare(0, 4, "stwa") == 0 || v.compare(0, 4, "stga") == 0)
                        pieces.push_back({v.compare(0, 4, "stga") == 0, Point{o.position[0], o.position[1]}});
                }
                cr.ring = wall_ring(pieces);
            }
            // The map's spots: one per sector, the same on every visit.
            cr.spot_map = build_spot_map(splitmix(key(name.c_str())), ambient_area_.data(), ambient_tiles_x_, ambient_tiles_y_,
                sector_sounds(*inputs.terrain), inputs.map && inputs.map->mob ? campfires(*inputs.map->mob) : std::vector<Point>{});
            cr.world_time = environment.world_time;
            // The drawn terrain's base vertices sit at stored XY offsets from
            // the regular grid; ground() needs them to match the rasterized surface.
            const std::uint32_t vx = ambient_tiles_x_ * 2 + 1, vy = ambient_tiles_y_ * 2 + 1;
            cr.offset_x.assign(std::size_t(vx) * vy, 0.f); cr.offset_y.assign(std::size_t(vx) * vy, 0.f);
            for (const auto& sector : inputs.terrain->sectors)
                for (std::uint32_t i = 0; i < 33u * 33u && i < sector.vertices.size(); ++i) {
                    const std::uint32_t x = sector.map_x * 32u + i % 33u, y = sector.map_y * 32u + i / 33u;
                    if (x >= vx || y >= vy || !std::isfinite(sector.vertices[i].x) || !std::isfinite(sector.vertices[i].y)) continue;
                    cr.offset_x[std::size_t(y) * vx + x] = std::clamp(sector.vertices[i].x - float(x), -1.f, 1.f);
                    cr.offset_y[std::size_t(y) * vx + x] = std::clamp(sector.vertices[i].y - float(y), -1.f, 1.f);
                }
        }
        // Game time: frozen while paused, faster at fast speed. A jump (load,
        // time reset) restarts the director's clock-relative state.
        float dt = float((environment.world_time - cr.world_time) * kSecondsPerSegment);
        cr.world_time = environment.world_time;
        if (!(dt >= 0) || dt > 30) { const auto seed = cr.director.seed; cr.director = Director{}; cr.director.seed = seed; dt = 0; }
        const double elapsed = dt; // timers retain game time; only motion is bounded
        dt = std::min(dt, .25f);

        // Camera: eye, the ground point at the screen centre and the view radius.
        float inverse[16]{};
        if (!ambient_occlusion_invert(world_to_clip.data(), inverse) || std::abs(inverse[14]) < 1e-12f) return status("world camera unavailable");
        WorldView w;
        w.zone = ambient::ZoneClass(ambient_zone_);
        w.day = environment.hour >= 6 && environment.hour < 20;
        w.area = ambient_area_.data(); w.tiles_x = ambient_tiles_x_; w.tiles_y = ambient_tiles_y_;
        w.height = ambient_height_.data(); w.verts_x = ambient_tiles_x_ * 2 + 1; w.verts_y = ambient_tiles_y_ * 2 + 1;
        if (cr.offset_x.size() == ambient_height_.size()) { w.offset_x = cr.offset_x.data(); w.offset_y = cr.offset_y.data(); }
        w.objects = inputs.map ? &inputs.map->objects : nullptr;
        w.ring = &cr.ring;
        w.spot_map = &cr.spot_map;
        w.clip = world_to_clip.data(); w.width = float(width_); w.height_px = float(height_);
        w.eye = {inverse[2] / inverse[14], inverse[6] / inverse[14], inverse[10] / inverse[14]};
        w.level = level;
        const auto ground_hit = [&](float nx, float ny, V3& hit) {
            V3 p[2];
            for (int k = 0; k < 2; ++k) {
                const float c[4]{nx, ny, float(k), 1};
                float h[4]{};
                for (int r = 0; r < 4; ++r) h[r] = inverse[r * 4] * c[0] + inverse[r * 4 + 1] * c[1] + inverse[r * 4 + 2] * c[2] + inverse[r * 4 + 3] * c[3];
                if (std::abs(h[3]) < 1e-12f) return false;
                p[k] = {h[0] / h[3], h[1] / h[3], h[2] / h[3]};
            }
            const V3 d = p[1] - p[0];
            float z = w.ground(w.eye.x, w.eye.y);
            for (int pass = 0; pass < 3; ++pass) {
                if (std::abs(d.z) < 1e-6f) return false;
                const float t = (z - p[0].z) / d.z;
                if (t < 0 || t > 1) return false;
                hit = p[0] + d * t;
                z = w.ground(hit.x, hit.y);
            }
            return true;
        };
        // Critters live where the party is and show only in its sight, never by
        // the camera (the showcase diagnostic alone lays out in front of it).
        // Read every retained row (corpses included); each threat tests its own reach.
        if (!ground_hit(0, 0, w.focus)) {
            if (showcase) return status("camera focus misses ground");
            w.focus = {};
        }
        if (!bridge_ground_cover_units(w.focus.x, w.focus.y, std::numeric_limits<float>::infinity(), cr.units, &cr.details)) return status("active world unit rows unavailable");
        if (cr.details.size() != cr.units.size()) return fail("unit detail row mismatch");
        w.units = &cr.units; w.details = &cr.details;
        std::vector<std::size_t> members;   // live party members with a known vision range
        for (std::size_t i = 0; i < cr.units.size(); ++i)
            if (!cr.details[i].dead && cr.details[i].controlled && cr.details[i].vision > 0) members.push_back(i);
        if (!showcase) {
            if (members.empty()) {
                unsigned controlled = 0;
                for (const auto& u : cr.details) controlled += !u.dead && u.controlled ? 1u : 0u;
                char reason[160];
                std::snprintf(reason, sizeof(reason), "party vision unavailable: %u live controlled rows, %zu total rows; no spawns", controlled, cr.units.size());
                return status(reason);
            }
            float cx = 0, cy = 0;
            for (const auto i : members) { cx += cr.units[i][0]; cy += cr.units[i][1]; }
            const float n = float(members.size()); w.focus = {cx / n, cy / n, 0}; w.focus.z = w.ground(w.focus.x, w.focus.y);
            w.view_radius = 10;
            for (const auto i : members)
                w.view_radius = std::max(w.view_radius, std::hypot(cr.units[i][0] - w.focus.x, cr.units[i][1] - w.focus.y) + cr.details[i].vision);
        } else w.view_radius = 30;
        cr.obstacle_age += dt;
        if (cr.obstacle_age > 2 || std::hypot(w.focus.x - cr.obstacles.cx, w.focus.y - cr.obstacles.cy) > 20 || !cr.obstacles.n) {
            cr.obstacle_age = 0;
            cr.obstacles.rebuild(w.focus.x, w.focus.y, environment.world_time, w.objects);
        }
        w.obstacles = &cr.obstacles;
        w.unavailable = cr.unavailable;
        for (std::size_t i = 0; i < cr.units.size() && !w.party_moving; ++i)
            w.party_moving = !cr.details[i].dead && cr.details[i].moving && w.near_activity(cr.units[i]);
        // Spawn around party members, including the approach band outside sight.
        // Rendering still requires party sight; threats keep births away from feet.
        cr.spots.clear();
        for (unsigned i = 0; i < 12 && !members.empty(); ++i) {
            const std::uint32_t a = cr.director.attempt + 1;
            const std::size_t m = members[std::min(members.size() - 1, std::size_t(rnd(cr.director, a, i, key("m")) * members.size()))];
            const float radius = cr.details[m].vision * (i < 6 ? .8f : kSpotBand) * std::sqrt(rnd(cr.director, a, i, key("r")));
            const float angle = rrange(cr.director, 0, TAU, a, i, key("t"));
            const float x = cr.units[m][0] + radius * std::cos(angle), y = cr.units[m][1] + radius * std::sin(angle);
            cr.spots.push_back({x, y, w.ground(x, y)});
        }
        if (!showcase) { cr.showcase_mode = 0; director_tick(cr.director, dt, w, cr.spots, cr.larva_tail_y, elapsed); }
        else {
            // Showcase: rows on the ground in front of the camera focus, axes
            // from the screen (x right, y up the screen), each animal at the
            // display scale a spawn there would take (x2 in mode 2).
            const float size = showcase == 2 ? 2.f : 1.f;
            // re-anchored only when the camera focus travels far over the ground;
            // zoom and rotation never move the lineup (spacing stays in metres)
            const bool moved = std::hypot(w.focus.x - cr.showcase_anchor.x, w.focus.y - cr.showcase_anchor.y) > 28.f;
            if (cr.showcase_mode != showcase || moved) {
                V3 right_hit, up_hit;
                if (!ground_hit(.5f, 0, right_hit) || !ground_hit(0, .5f, up_hit)) return status("showcase: camera axes miss the ground");
                V3 r{right_hit.x - w.focus.x, right_hit.y - w.focus.y, 0}, f{up_hit.x - w.focus.x, up_hit.y - w.focus.y, 0};
                const float rl = len3(r), fl = len3(f);
                if (!(rl > 1e-3f) || !(fl > 1e-3f)) return status("showcase: degenerate camera axes");
                r = r * (1 / rl); f = f * (1 / fl);
                // The ground row (10 slots) fits the screen width (about 4 rl).
                const float spacing = std::min(2.f * (showcase == 2 ? 1.6f : 1.f), 4 * rl / 11.f);
                cr.showcase_mode = showcase; cr.showcase_right = r; cr.showcase_forward = f;
                cr.showcase_anchor = w.focus - f * (1.5f * spacing);
                cr.showcase_critters.clear(); cr.showcase_flocks.clear();
                cr.showcase_director = Director{};
                cr.showcase_director.seed = key("critter_showcase");
                const float facing = std::atan2(r.x, r.y);   // side view: heads to the screen's right
                std::uint64_t index = 0;
                for (const auto& s : showcase_layout(spacing)) {
                    ++index;
                    if ((cr.unavailable >> s.species) & 1u) continue;   // omitted at load; named in the status line
                    const float x = cr.showcase_anchor.x + r.x * s.x + f.x * s.y, y = cr.showcase_anchor.y + r.y * s.x + f.y * s.y;
                    float nx, ny, ppm = 0;
                    if (!w.project({x, y, w.ground(x, y)}, nx, ny, ppm)) ppm = 0;
                    if (s.bird == bird_none) {
                        Agent c;
                        c.species = s.species; c.scale = species_scale(s.species, ppm) * size; c.key = splitmix(index);
                        c.x = x; c.y = y; c.yaw = facing; c.state = st_idle; c.alpha = 1;
                        cr.showcase_critters.push_back({s, c});
                        continue;
                    }
                    Flock fk;
                    const float scale = bird_scale(s.bird, ppm) * size;
                    flock_init(fk, s.bird, s.count, x, y, cr.showcase_director, splitmix(index), nullptr, 1e9f, 1e9f, 0, scale);
                    if (s.bird == bird_bat) { fk.exit[0] = fk.exit[1] = 0; fk.center[0] = x; fk.center[1] = y; }   // the showcase swarm stays put
                    // A short line across the screen instead of the random cluster.
                    const float gap = bird_length(s.bird) * scale * (s.bird == bird_vulture ? 3.2f : 2.5f);
                    const BirdFlight flight = bird_flight(s.bird, scale);
                    for (unsigned j = 0; j < fk.count; ++j) {
                        Agent& b = fk.birds[j];
                        const float o = (float(j) - (fk.count - 1) * .5f) * gap;
                        b.slot_x = r.x * o; b.slot_y = r.y * o;
                        b.x = x + b.slot_x; b.y = y + b.slot_y; b.yaw = facing;
                        if (s.flying) {   // on the thermal at the normal height and radius
                            const float a = TAU * j / fk.count;
                            b.flying = true;
                            b.x = x + std::cos(a) * flight.radius; b.y = y + std::sin(a) * flight.radius; b.z = flight.altitude;
                            b.vx = -std::sin(a) * flight.speed; b.vy = std::cos(a) * flight.speed;
                        }
                    }
                    if (s.flying) fk.state = fl_circle;
                    cr.showcase_flocks.push_back({s, fk});
                }
            }
            // Animate in place on game time (frozen while paused).
            for (auto& entry : cr.showcase_critters) showcase_idle(entry.second, dt);
            for (auto& entry : cr.showcase_flocks) {
                Flock& fk = entry.second;
                fk.t += dt;
                for (unsigned j = 0; j < fk.count; ++j) {
                    Agent& b = fk.birds[j];
                    if (entry.first.flying) {
                        if (fk.variant == bird_bat) bat_update(fk, b, dt, w); else bird_update(fk, b, dt, w, cr.showcase_director);
                        continue;
                    }
                    const float bx = b.x, by = b.y;   // pecks, caws and hop height, no wandering
                    b.alpha = std::min(1.f, b.alpha + dt / kCarrionFadeIn);   // the fade bird_update would run
                    bird_ground_idle(fk, b, dt, cr.showcase_director);
                    b.x = bx; b.y = by;
                }
            }
        }

        // Instances: roots, clip frames and procedural part matrices.
        cr.bone_scratch.clear(); cr.instances_scratch.clear();
        const auto root = [&](CrInstance& inst, V3 p, float yaw, float pitch, float scale, float roll = 0) {
            const M3 r = rot_z(-yaw) * rot_x(pitch) * rot_y(roll);
            for (int i = 0; i < 3; ++i) { for (int j = 0; j < 3; ++j) inst.r[i][j] = r.m[i][j] * scale; }
            inst.r[0][3] = p.x; inst.r[1][3] = p.y; inst.r[2][3] = p.z;
        };
        const auto procedural = [&](Mesh m, const PoseP& pose, CrInstance& inst) {
            Rig rig; rig.bones = &cr.bone_scratch; rig.base = std::uint32_t(cr.bone_scratch.size()); rig.statue = cr.statue.get();
            inst.a[0] = rig.base; inst.a[1] = 0;
            procedural_rig(m)(rig, pose);
        };
        // Body length on screen (px) of an instance at p; 0 behind the camera.
        // A submitted instance counts as on screen only when its body overlaps
        // the viewport: residents wait just outside the view edge and flying
        // birds can be far above it, and those are clipped, never seen.
        float drawn_min_px = std::numeric_limits<float>::infinity(), drawn_max_px = 0;   // on-screen instances
        std::uint32_t drawn_species = 0, visible_instances = 0;
        CritterSummary summary{};
        std::string showcase_line;
        const auto measure = [&](V3 p, float length, Species s) {
            float nx, ny, ppm;
            if (!w.project(p, nx, ny, ppm)) return 0.f;
            const float px = length * ppm;
            if (px < kMinPixels) return px;
            if (summary.probe_count < CritterSummary::kProbes) {
                const auto pct = [](float v) { return std::int16_t(std::clamp(v * 100.f, -30000.f, 30000.f)); };
                summary.probes[summary.probe_count++] = {std::uint8_t(s), pct(nx), pct(ny),
                    std::uint16_t(std::min(px, 65535.f)), std::int16_t(std::clamp((p.z - w.ground(p.x, p.y)) * 10.f, -30000.f, 30000.f))};
            }
            const float mx = px / std::max(w.width, 1.f), my = px / std::max(w.height_px, 1.f);   // half a body in NDC
            if (std::abs(nx) <= 1 + mx && std::abs(ny) <= 1 + my) {
                ++visible_instances; drawn_min_px = std::min(drawn_min_px, px); drawn_max_px = std::max(drawn_max_px, px); drawn_species |= 1u << s;
            }
            return px;
        };
        const auto flock_species = [](const Flock& f) {
            return f.variant == bird_raven || f.variant == bird_crow ? sp_raven : f.variant == bird_bat ? sp_bat :
                f.variant == bird_vulture ? sp_vulture : f.variant == bird_snowbird ? sp_snowbird : sp_bird;
        };
        // Party sight decides what is drawn (showcase animals are diagnostics).
        const auto sighted = [&](const Agent& a) { return showcase ? 1.f : w.party_extent(a.x, a.y) <= 1.f ? a.seen : 0.f; };
        const auto emit_critter = [&](const Agent& c) {
            const auto& rule = species_rule(c.species);
            const V3 p{c.x, c.y, w.ground(c.x, c.y) + c.z};
            if (!(sighted(c) > 0)) return;
            if (measure(p, rule.length * c.scale, c.species) < kMinPixels) return;
            CrInstance inst{};
            inst.b[0] = c.alpha * sighted(c);
            root(inst, p, c.yaw, c.pitch, c.scale);
            Mesh mesh = m_rat; Slice slice = s_rat;
            PoseP pose; pose.t = c.age; pose.gait = c.gait; pose.speed = c.speed; pose.pushup = c.pushup; pose.claw = c.claw; pose.push = c.push;
            switch (c.species) {
            case sp_rat: case sp_snowmouse: case sp_spider: case sp_frog: {
                mesh = c.species == sp_rat ? m_rat : c.species == sp_snowmouse ? m_snowmouse : c.species == sp_spider ? m_spider : m_frog;
                slice = Slice(mesh);
                const auto& clips = cr.minis[mesh];
                const int clip = c.hop_clip || c.speed > .6f ? 2 : c.speed > 0 ? 1 : 0;
                const float n = float(clips.frames[clip]);
                float f = clip == 0 ? c.age * kMiniFps / std::sqrt(clips.scale * 4) : c.gait * n;   // small bodies move faster
                f = std::fmod(std::max(f, 0.f), n);
                const std::uint32_t f0 = std::uint32_t(f) % clips.frames[clip], f1 = (f0 + 1) % clips.frames[clip];
                inst.a[0] = clips.start[clip] + f0 * clips.nodes; inst.a[2] = clips.start[clip] + f1 * clips.nodes;
                inst.a[1] = 1; inst.b[1] = f - std::floor(f);
                break;
            }
            case sp_larva: {
                mesh = m_larva; slice = s_larva;
                const float jf = std::fmod(c.age * kMiniFps, float(cr.jaw_frames));
                const std::uint32_t j0 = std::uint32_t(jf) % cr.jaw_frames, j1 = (j0 + 1) % cr.jaw_frames;
                inst.a[1] = 2; inst.a[3] = cr.jaw_start + j0;
                inst.b[2] = c.gait; inst.b[3] = c.search;
                inst.c[0] = c.age; inst.c[1] = jf - std::floor(jf);
                const std::uint32_t j1_index = cr.jaw_start + j1; std::memcpy(&inst.c[2], &j1_index, 4);
                break;
            }
            case sp_lizard: mesh = m_lizard; slice = s_lizard; procedural(mesh, pose, inst); break;
            case sp_snake: mesh = m_snake; slice = s_snake; procedural(mesh, pose, inst); break;
            case sp_crab: mesh = m_crab; slice = s_crab; procedural(mesh, pose, inst); break;
            case sp_scolopendra: mesh = m_scolopendra; slice = s_scolopendra; procedural(mesh, pose, inst); break;
            case sp_dungbeetle: {
                mesh = m_dung; slice = s_dung; procedural(mesh, pose, inst);
                if (c.has_ball) {
                    CrInstance ball{}; ball.b[0] = c.alpha;
                    root(ball, {c.ball_x, c.ball_y, w.ground(c.ball_x, c.ball_y) + .015f * c.scale}, c.ball_yaw, c.ball_roll, c.scale);
                    procedural(m_ball, pose, ball);
                    ball.a[1] |= std::uint32_t(s_dung) << 8;
                    cr.instances_scratch.push_back({m_ball, ball});
                }
                break;
            }
            default: return;
            }
            inst.a[1] |= std::uint32_t(slice) << 8;
            cr.instances_scratch.push_back({mesh, inst});
        };
        if (!showcase) for (unsigned i = 0; i < cr.director.critter_count; ++i) emit_critter(cr.director.critters[i]);
        // Render only: a bird near the lens fades out over about two body
        // lengths before it can fill the screen or cross the near plane.
        const auto near_fade = [&](V3 p, float length) {
            return std::clamp((len3(p - w.eye) - (.5f + 2 * length)) / (1 + 2 * length), 0.f, 1.f);
        };
        const auto emit_flock = [&](const Flock& f) {
            if (!f.active) return;
            const auto& look = bird_look(f.variant);
            for (unsigned j = 0; j < f.count; ++j) {
                const Agent& b = f.birds[j];
                if (b.state == st_gone) continue;
                const V3 p = bird_world(b, w);
                const float px = measure(p, bird_length(f.variant) * f.scale, flock_species(f));
                if (px < kMinPixels) continue;
                const float alpha = b.alpha * sighted(b) * near_fade(p, bird_length(f.variant) * f.scale);
                if (!(alpha > 0)) continue;
                const bool distant = px < kFarPixels;
                const Mesh mesh = distant ? look.far_mesh : look.near_mesh;
                if (f.variant == bird_bat) {   // the game's bat figure: its flight loop, frames blended
                    const auto& clips = cr.minis[m_bat];
                    const float n = float(clips.frames[0]), fr = std::fmod(std::max(b.wing, 0.f) * n, n);
                    const std::uint32_t f0 = std::uint32_t(fr) % clips.frames[0], f1 = (f0 + 1) % clips.frames[0];
                    CrInstance inst{}; inst.b[0] = alpha;
                    root(inst, p, b.yaw, b.pitch, f.scale, b.roll);
                    inst.a[0] = clips.start[0] + f0 * clips.nodes; inst.a[2] = clips.start[0] + f1 * clips.nodes;
                    inst.a[1] = 1 | (std::uint32_t(s_bat) << 8); inst.b[1] = fr - std::floor(fr);
                    if (clips.morph_frames) {   // the membrane follows the same frames
                        const std::uint32_t m0 = clips.morph_start + (f0 % clips.morph_frames) * clips.morph_points;
                        const std::uint32_t m1 = clips.morph_start + (f1 % clips.morph_frames) * clips.morph_points;
                        std::memcpy(&inst.c[0], &m0, 4); std::memcpy(&inst.c[1], &m1, 4);
                    }
                    cr.instances_scratch.push_back({m_bat, inst});
                    continue;
                }
                PoseP pose; pose.t = b.age; pose.wing = b.wing + bird_wing_phase(b.key); pose.soar = b.soar; pose.peck = b.peck; pose.caw = b.caw;
                pose.fold = std::clamp(b.fold - b.flick, 0.f, 1.f); pose.crouch = b.crouch; pose.tail = b.tail;
                const float k = TAU * hash01(b.key, 7), age = float(f.t);
                pose.look = .5f * std::sin(age * .8f + k) * std::sin(age * .31f + 2 * k);
                if (f.variant == bird_vulture) {
                    // spread-wing statue in the air, perched statue on the ground;
                    // while the wings fold the two swap through complementary dither
                    const float fold = pose.fold, scale = bird_length(f.variant) * f.scale;
                    const bool both = fold > .02f && fold < .98f && alpha >= .999f;
                    const auto put = [&](Mesh m, float alpha) {
                        CrInstance inst{}; inst.b[0] = alpha;
                        root(inst, p, b.yaw, m == m_vulture && b.flying ? b.pitch : 0.f, scale, m == m_vulture && b.flying ? b.roll : 0.f);
                        procedural(m, pose, inst);
                        inst.a[1] |= std::uint32_t(look.slice) << 8;
                        cr.instances_scratch.push_back({m, inst});
                    };
                    if (both) { put(m_vulture, 1 - fold); put(m_vulture_perch, -fold); }   // negative: inverted dither
                    else put(fold < .5f ? m_vulture : m_vulture_perch, alpha);
                    continue;
                }
                CrInstance inst{}; inst.b[0] = alpha;
                root(inst, p, b.yaw, b.flying ? b.pitch : 0.f, bird_length(f.variant) * f.scale, b.flying ? b.roll : 0.f);
                procedural(mesh, pose, inst);
                inst.a[1] |= std::uint32_t(look.slice) << 8;
                cr.instances_scratch.push_back({mesh, inst});
            }
        };
        if (showcase) {
            for (const auto& entry : cr.showcase_critters) emit_critter(entry.second);
            for (const auto& entry : cr.showcase_flocks) emit_flock(entry.second);
            // One label per slot just below the animal, with its body size on screen.
            std::string line = std::string(showcase == 3 ? "Critter showcase x-ray (no depth test)" : showcase == 2 ? "Critter showcase x2" : "Critter showcase") + " (ambient critters paused):";
            const auto label = [&](const ShowcaseSlot& slot, V3 p, float length) {
                float nx, ny, ppm;
                const bool seen = w.project(p, nx, ny, ppm);
                const float px = seen ? length * ppm : 0.f;
                const char* name = slot.bird == bird_none ? kCritterSpeciesNames[slot.species] :
                    slot.bird == bird_bat ? "bat swarm" : slot.flying ? "vulture (thermal)" : slot.bird == bird_finch ? "finch" : slot.bird == bird_sparrow ? "sparrow" :
                    slot.bird == bird_bunting ? "bunting" : slot.bird == bird_snowbird ? "snowbird" : slot.bird == bird_crow ? "crow" :
                    slot.bird == bird_raven ? "raven" : "vulture";
                const bool inside = seen && std::abs(nx) <= 1 && std::abs(ny) <= 1;
                char text[64];
                std::snprintf(text, sizeof(text), "%s %.0fpx%s", name, px, inside ? "" : " (off screen)");
                line += ' '; line += text; line += ';';
                if (inside) {
                    summary.labels.emplace_back();
                    auto& l = summary.labels.back();
                    l.x = (nx + 1) * .5f; l.y = (1 - ny) * .5f + 14.f / std::max(w.height_px, 1.f);
                    std::snprintf(l.text, sizeof(l.text), "%s", text);
                }
            };
            for (const auto& entry : cr.showcase_critters) {
                const Agent& c = entry.second;
                label(entry.first, {c.x, c.y, w.ground(c.x, c.y)}, species_rule(c.species).length * c.scale);
            }
            for (const auto& entry : cr.showcase_flocks)
                if (entry.second.count) label(entry.first, bird_world(entry.second.birds[0], w), bird_length(entry.second.variant) * entry.second.scale);
            showcase_line = std::move(line);
        } else {
            emit_flock(cr.director.flock);
            for (const auto& f : cr.director.carrion) emit_flock(f);
            for (const auto& r : cr.director.residents)
                if (r.active) { if (r.is_flock) emit_flock(r.birds); else emit_critter(r.critter); }
        }
        if (!showcase && configured_integer(debug_overlay_.config(), "critter_labels", 0)) {
            const auto label = [&](const Agent& a, const char* state, Species species) {
                if (a.state == st_gone) return;
                float nx, ny, ppm;
                const V3 p{a.x, a.y, w.ground(a.x, a.y) + a.z + species_rule(species).length * a.scale};
                if (!w.project(p, nx, ny, ppm) || std::abs(nx) > 1 || std::abs(ny) > 1) return;
                float distance = std::numeric_limits<float>::infinity();
                for (const auto i : members) distance = std::min(distance, std::hypot(cr.units[i][0] - a.x, cr.units[i][1] - a.y));
                summary.labels.emplace_back();
                auto& l = summary.labels.back();
                l.x = (nx + 1) * .5f; l.y = (1 - ny) * .5f - 14.f / std::max(w.height_px, 1.f);
                static const char* bird_names[]{"finch", "sparrow", "bunting", "snowbird", "crow", "raven", "vulture", "bat"};
                const char* name = a.bird == bird_none ? kCritterSpeciesNames[species] : bird_names[a.bird];
                std::snprintf(l.text, sizeof(l.text), "%s %s %.1fm", name, state, distance);
            };
            const auto ground_label = [&](const Agent& a) {
                static const char* states[]{"appearing", "idle", "fleeing", "leaving", "fading", "gone"};
                label(a, states[a.state], a.species);
            };
            const auto flock_labels = [&](const Flock& f) {
                if (!f.active) return;
                static const char* states[]{"ground", "circling", "landing", "leaving", "gone"};
                for (unsigned j = 0; j < f.count; ++j)
                    label(f.birds[j], f.birds[j].solo == 1 ? "leaving" : f.birds[j].solo == 2 ? "landing" : states[f.state], flock_species(f));
            };
            for (unsigned i = 0; i < cr.director.critter_count; ++i) ground_label(cr.director.critters[i]);
            flock_labels(cr.director.flock);
            for (const auto& f : cr.director.carrion) flock_labels(f);
            for (const auto& r : cr.director.residents) if (r.active) {
                if (r.is_flock) flock_labels(r.birds); else ground_label(r.critter);
            }
        }
        // Report simulation population even when every current instance is below
        // draw size. Spot rejection totals distinguish habitat, size and threats.
        unsigned counts[sp_count]{};
        for (unsigned i = 0; i < cr.director.critter_count; ++i) ++counts[cr.director.critters[i].species];
        const auto count_flock = [&](const Flock& f) {
            if (!f.active) return;
            counts[flock_species(f)] += f.count;
        };
        count_flock(cr.director.flock);
        for (const auto& f : cr.director.carrion) count_flock(f);
        unsigned residents = 0, ground_residents = 0;
        for (const auto& r : cr.director.residents) {
            if (!r.active) continue;
            ++residents; ground_residents += r.is_flock ? 0u : 1u;
            if (r.is_flock) count_flock(r.birds); else ++counts[r.critter.species];
        }
        const auto& d = cr.director;
        unsigned present = 0, timers = 0, due = 0, zone_blocked = 0;
        unsigned flocks = 0, arriving = 0, carrion_birds = 0;
        double oldest = 0, next = std::numeric_limits<double>::infinity();
        for (const auto& c : d.corpses) {
            if (!c.present) continue;
            ++present;
            oldest = std::max(oldest, d.clock - c.seen);
            if (carrion_bird(w.zone, false, !w.outside_walls(c.x,c.y)) == bird_none) {
                ++zone_blocked; continue;
            }
            bool flocked = false;
            for (const auto& f : d.carrion) flocked = flocked || (f.active && f.corpse == c.id);
            if (flocked) continue;
            const double remaining = std::max(c.cooldown - d.clock,
                c.seen + kCarrionDelayMin + (kCarrionDelayMax-kCarrionDelayMin)*c.u - d.clock);
            if (remaining > 0) { ++timers; next = std::min(next,remaining); }
            else ++due;
        }
        for (const auto& f : d.carrion) if (f.active) {
            ++flocks; carrion_birds += f.count; arriving += f.state == fl_circle || f.state == fl_land ? 1u : 0u;
        }
        if (!timers) next = 0;
        float vision_min = std::numeric_limits<float>::infinity(), vision_max = 0;
        for (const auto i : members) { vision_min = std::min(vision_min, cr.details[i].vision); vision_max = std::max(vision_max, cr.details[i].vision); }
        std::snprintf(critters_status_, sizeof(critters_status_),
            "Party: %zu members, vision %.1f-%.1fm; spawn band %.1fx sight\n"
            "Critters active: rat %u spider %u larva %u frog %u lizard %u snake %u centipede %u beetle %u "
            "bird %u raven/crow %u vulture %u crab %u snowbird %u mouse %u bat %u; instances %zu, on screen %u (body %.0f-%.0f px)\n"
            "Spawns %u/%llu; rejected attempts no species %llu/no spot %llu; quiet ticks %llu (%.1fs left)\n"
            "Rejected spots size %llu/habitat %llu/undead-wall %llu/threat %llu/budget %llu; zone %u, spots %zu\n"
            "World spots: residents %u (budget %u on screen %u); found %llu (faded in %llu) of %llu armed; disturbed %llu; memory %zu\n"
            "Carrion: corpses %u/%zu present/tracked; oldest %.1fs; timers %u next %.1fs due %u zone-blocked %u; "
            "flocks %u arriving %u arrivals %llu (landed %llu); slots %llu/%llu found, %llu flocks without; clock %.1fs (+%.3fs)",
            members.size(), members.empty() ? 0.f : vision_min, vision_max, kSpotBand,
            counts[0],counts[1],counts[2],counts[3],counts[4],counts[5],counts[6],counts[7],
            counts[8],counts[9],counts[10],counts[11],counts[12],counts[13],counts[14],cr.instances_scratch.size(),visible_instances,
            visible_instances ? drawn_min_px : 0.f, drawn_max_px,
            d.events,d.spawn_attempts,d.no_species,d.no_spot,d.quiet_reject,
            float(std::max(0., kCombatQuiet-(d.clock-d.last_combat))),
            d.size_reject,d.habitat_reject,d.undead_reject,d.threat_reject,d.budget_reject,unsigned(w.zone),cr.spots.size(),
            residents,critter_budget(level),on_screen(d),d.spot_found,d.spot_faded,d.spot_rolls,d.spot_disturbed,d.spot_memory.size(),
            present,d.corpses.size(),oldest,timers,next,due,zone_blocked,
            flocks,arriving,d.carrion_arrivals,d.carrion_landed,d.slots_found,d.slots_wanted,d.slots_failed,d.clock,elapsed);
        summary.active = d.critter_count + ground_residents;
        summary.flock_active = d.flock.active ? 1u : 0u; summary.flock_birds = d.flock.active ? d.flock.count : 0u;
        summary.spot_residents = residents; summary.carrion_flocks = flocks; summary.carrion_birds = carrion_birds;
        summary.drawn_instances = std::uint32_t(cr.instances_scratch.size()); summary.on_screen_instances = visible_instances;
        summary.min_px = visible_instances ? drawn_min_px : 0.f; summary.max_px = drawn_max_px; summary.drawn_species = drawn_species;
        summary.corpses_present = present; summary.corpses_tracked = std::uint32_t(d.corpses.size());
        summary.corpse_timers = timers; summary.corpse_next_s = float(next); summary.corpses_due = due;
        summary.carrion_arrivals = std::uint32_t(d.carrion_arrivals);
        { unsigned k = 0; for (const auto& f : d.carrion) if (f.active && k < 2) {
            auto& o = summary.carrion_flock[k++];
            o.state = std::uint8_t(f.state); o.land_ok = f.land_ok; o.refused = f.refused; o.margin = f.margin;
            o.quiet = f.quiet; o.scattered = f.scattered; o.wait = f.landing_wait; o.count = f.count;
            float nearest = 99; for (const auto i : members) nearest = std::min(nearest, std::hypot(cr.units[i][0] - f.spot[0], cr.units[i][1] - f.spot[1]));
            o.party = nearest;
        } }
        critters_summary_ = summary;
        if (showcase) std::snprintf(critters_status_, sizeof(critters_status_), "%s", showcase_line.c_str());
        if (!cr.omitted_text.empty()) {   // assets that failed to load stay named on screen
            const std::size_t used = std::strlen(critters_status_);
            std::snprintf(critters_status_ + used, sizeof(critters_status_) - used, "\nOmitted: %s", cr.omitted_text.c_str());
        }
        if (cr.instances_scratch.empty()) return S_OK;

        // Upload: instances grouped by mesh, buffers sized by demand.
        std::stable_sort(cr.instances_scratch.begin(), cr.instances_scratch.end(),
                         [](const auto& a, const auto& b) { return a.first < b.first; });
        cr.instance_upload.clear();
        for (const auto& value : cr.instances_scratch) cr.instance_upload.push_back(value.second);
        const UINT instance_count = UINT(cr.instance_upload.size()), bone_count = std::max<UINT>(1, UINT(cr.bone_scratch.size()));
        if (instance_count > cr.instance_capacity) {
            cr.instance_capacity = instance_count + instance_count / 2 + 8;
            cr.instances.Reset(); cr.instance_srv.Reset();
            if (FAILED(make_buffer(device_.Get(), cr.instance_capacity * sizeof(CrInstance), D3D11_BIND_SHADER_RESOURCE,
                    D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, sizeof(CrInstance), nullptr, cr.instances, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE)) ||
                FAILED(make_structured_srv(device_.Get(), cr.instances.Get(), cr.instance_capacity, cr.instance_srv)))
                return fail("instance buffer allocation failed");
        }
        if (bone_count > cr.bone_capacity) {
            cr.bone_capacity = bone_count + bone_count / 2 + 64;
            cr.bones.Reset(); cr.bone_srv.Reset();
            if (FAILED(make_buffer(device_.Get(), cr.bone_capacity * sizeof(Bone), D3D11_BIND_SHADER_RESOURCE,
                    D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, sizeof(Bone), nullptr, cr.bones, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE)) ||
                FAILED(make_structured_srv(device_.Get(), cr.bones.Get(), cr.bone_capacity, cr.bone_srv)))
                return fail("part matrix buffer allocation failed");
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context_->Map(cr.instances.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return S_OK;
        std::memcpy(mapped.pData, cr.instance_upload.data(), instance_count * sizeof(CrInstance)); context_->Unmap(cr.instances.Get(), 0);
        if (!cr.bone_scratch.empty()) {
            if (FAILED(context_->Map(cr.bones.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return S_OK;
            std::memcpy(mapped.pData, cr.bone_scratch.data(), cr.bone_scratch.size() * sizeof(Bone)); context_->Unmap(cr.bones.Get(), 0);
        }
        CrFrame c{};
        std::memcpy(c.clip, world_to_clip.data(), sizeof(c.clip));
        c.eye[0] = w.eye.x; c.eye[1] = w.eye.y; c.eye[2] = w.eye.z;
        const auto& fog = ambient_terrain_state_;
        c.fog_range[0] = fog.fog_min[0]; c.fog_range[1] = fog.fog_min[1]; c.fog_range[2] = fog.fog_max[0]; c.fog_range[3] = fog.fog_max[1];
        c.fog_control[0] = fog.fog_distance; c.fog_control[1] = fog.fog_enabled ? 1.f : 0.f;
        neighbour_fog_constants(neighbour_fog_, c.fog_seam, c.fog_curve, c.fog_span_lo, c.fog_span_hi);
        c.fog_color[0] = ((ambient_fog_color_ >> 16) & 255) / 255.f; c.fog_color[1] = ((ambient_fog_color_ >> 8) & 255) / 255.f;
        c.fog_color[2] = (ambient_fog_color_ & 255) / 255.f;
        c.switches[1] = frame.render[D3DRENDERSTATE_FOGENABLE] != 0 ? 1.f : 0.f;
        // Larva crawl (prototype LARVA): length, stride, hump, centre line |
        // lift, sway, search sway, search lift | sway period.
        c.larva0[0] = cr.larva_length; c.larva0[1] = kLarvaStride; c.larva0[2] = .55f; c.larva0[3] = cr.larva_center_z;
        c.larva1[0] = .05f; c.larva1[1] = .035f; c.larva1[2] = .10f; c.larva1[3] = .07f;
        c.larva2[0] = 1.4f;
        if (FAILED(context_->Map(cr.frame_constants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return S_OK;
        std::memcpy(mapped.pData, &c, sizeof(c)); context_->Unmap(cr.frame_constants.Get(), 0);

        transition_profile_pass("critters");
        const bool write_ambient = ao_ambient_rtv_ && rtv_.Get() == scene_composite_.target();
        ao_ambient_written_ |= write_ambient;
        ID3D11RenderTargetView* targets[3]{rtv_.Get(), nullptr, write_ambient ? ao_ambient_rtv_.Get() : nullptr};
        context_->OMSetRenderTargets(write_ambient ? 3u : 1u, targets, dsv_.Get());
        D3D11_VIEWPORT viewport{0, 0, float(target_width()), float(target_height()), 0, 1}; context_->RSSetViewports(1, &viewport);
        context_->RSSetState(cr.raster.Get()); context_->OMSetDepthStencilState(showcase == 3 ? cr.depth_xray.Get() : cr.depth.Get(), 0);
        const float factor[4]{1, 1, 1, 1}; context_->OMSetBlendState(cr.blend.Get(), factor, ~0u);
        context_->IASetInputLayout(nullptr); context_->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
        context_->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->VSSetShader(cr.vs.Get(), nullptr, 0); context_->PSSetShader(cr.ps.Get(), nullptr, 0); context_->GSSetShader(nullptr, nullptr, 0);
        bind_owned_sky_pixel_inputs();  // borderFogColor reads the sky probe
        ID3D11Buffer* frame_buffers[2]{cr.frame_constants.Get(), cr.draw_constants.Get()};
        context_->VSSetConstantBuffers(11, 2, frame_buffers); context_->PSSetConstantBuffers(11, 2, frame_buffers);
        ID3D11ShaderResourceView* vs_srvs[4]{cr.vertex_srv.Get(), cr.instance_srv.Get(), cr.bone_srv.Get(), cr.baked_srv.Get()};
        context_->VSSetShaderResources(27, 4, vs_srvs);
        context_->PSSetShaderResources(31, 1, cr.skins_srv.GetAddressOf());
        context_->PSSetSamplers(8, 1, cr.sampler.GetAddressOf());
        // Shared world light, sun cascades, point lights and atmosphere.
        context_->PSSetConstantBuffers(1, 1, native_world_lighting_constants_.GetAddressOf());
        context_->PSSetConstantBuffers(2, 1, native_shadow_constants_.GetAddressOf());
        context_->PSSetConstantBuffers(5, 1, native_point_light_constants_.GetAddressOf());
        context_->PSSetConstantBuffers(6, 1, shadow_history_constants_.GetAddressOf());
        context_->PSSetShaderResources(9, 1, native_shadow_depth_srv_.GetAddressOf());
        ID3D11ShaderResourceView* point_lights = native_point_lights_.empty() ? nullptr : native_point_light_srv_.Get();
        context_->PSSetShaderResources(18, 1, &point_lights);
        ID3D11ShaderResourceView* point_light_grid = native_point_lights_.empty() ? nullptr : native_point_light_grid_srv_.Get();
        context_->PSSetShaderResources(17, 1, &point_light_grid);
        context_->PSSetSamplers(1, 1, native_shadow_sampler_.GetAddressOf());
        context_->PSSetSamplers(4, 1, native_shadow_far_sampler_.GetAddressOf());
        context_->PSSetSamplers(10, 1, native_shadow_gather_sampler_.GetAddressOf());
        (void)bind_world_fog();
        // Every mesh has geometry (checked at load). Triangles are counted per
        // instance drawn, not one per draw call.
        UINT draws = 0;
        std::uint64_t triangles = 0;
        for (std::size_t begin = 0; begin < cr.instances_scratch.size();) {
            const Mesh mesh = cr.instances_scratch[begin].first;
            std::size_t end = begin;
            while (end < cr.instances_scratch.size() && cr.instances_scratch[end].first == mesh) ++end;
            const auto& range = cr.meshes[mesh];
            if (FAILED(context_->Map(cr.draw_constants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) break;
            // SV_VertexID ignores StartVertexLocation (no vertex buffers bound;
            // checked on hardware and WARP): the mesh offset goes in crFirst.
            const std::uint32_t values[4]{std::uint32_t(begin), range.first, 0, 0};
            std::memcpy(mapped.pData, values, sizeof(values)); context_->Unmap(cr.draw_constants.Get(), 0);
            context_->DrawInstanced(range.count, UINT(end - begin), 0, 0);
            ++draws; triangles += std::uint64_t(range.count / 3) * (end - begin);
            begin = end;
        }
        // Restore the slots this pass bound beyond the caller's state guard.
        ID3D11ShaderResourceView* empty[4]{};
        context_->VSSetShaderResources(27, 4, empty); context_->PSSetShaderResources(31, 1, empty);
        context_->PSSetShaderResources(17, 2, empty);
        ID3D11Buffer* no_buffers[2]{};
        context_->VSSetConstantBuffers(11, 2, no_buffers); context_->PSSetConstantBuffers(11, 2, no_buffers);
        context_->PSSetConstantBuffers(5, 2, no_buffers);
        ID3D11SamplerState* no_samplers[1]{};
        context_->PSSetSamplers(8, 1, no_samplers); context_->PSSetSamplers(4, 1, no_samplers); context_->PSSetSamplers(10, 1, no_samplers);
        context_->OMSetRenderTargets(1, rtv_.GetAddressOf(), dsv_.Get());
        profiler_.add_draw(triangles, draws); profiler_.add_batch(draws);
        scene_updated_ = true;
    } catch (...) {
        return fail("allocation failed");
    }
    return S_OK;
}

} // namespace owned
