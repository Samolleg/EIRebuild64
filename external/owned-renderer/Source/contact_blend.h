#pragma once

#include <cstdint>
#include <array>
#include <algorithm>
#include <cmath>

namespace owned {

// Instance flag: a rigid, non-unit world object (building, rock, fence, trunk,
// crate). Its pixels just above the drawn terrain take the terrain material.
// Units, terrain, liquids and UI never carry it.
constexpr std::uint32_t native_ground_contact_flag = 0x80u;
// contactBlendTop below: above this many contact bands (and at least
// 0.15 m) over the drawn ground the blend leaves a pixel unchanged.
constexpr float contact_blend_top_bands = 2.5f;

// Vertical world bounds of an authored box after the complete instance transform.
// Shared by the active object producer, neighbour upload and focused regression.
inline std::array<float,2> contact_world_vertical_bounds(const std::array<float,3>& center,
    const std::array<float,3>& minimum,const std::array<float,3>& maximum,
    const std::array<float,16>& world) noexcept {
    float lo=world[11],hi=world[11];
    for(unsigned axis=0;axis<3;++axis){
        const float a=world[8+axis]*(center[axis]+minimum[axis]);
        const float b=world[8+axis]*(center[axis]+maximum[axis]);
        lo+=(std::min)(a,b);hi+=(std::max)(a,b);
    }
    return {lo,hi};
}

// Terrain materials whose atlas can be read by the blend (availability bits).
constexpr std::uint32_t contact_blend_max_materials = 64u;
// Contact atlas side cap. A map's terrain pages need not share one size
// (Dejavu zone1 ships 1024 and 2048 HD pages together); every page is copied
// from its mip at most this wide, so one array holds them all.
constexpr std::uint32_t contact_blend_atlas_side = 1024u;
// The contact ground height and colour maps (native_shader.h contactGround,
// contactColor): t13 and t14 in DXBC; on D3D12 one t table serves every stage
// and the world vertex programs read t13/t14 (persistent instances and
// instance updates), so the DXIL reads t114 and t115, which no program uses.
constexpr unsigned kContactGroundSlotD3D12 = 114, kContactColorSlotD3D12 = 115;

// Shared by the production object pixel shader and the focused WARP regression.
constexpr char kContactBlendShader[] = R"(
// F10 height remains the maximum; small objects get a proportionate band.
float contactBandHeight(float worldHeight,float maximum){return min(maximum,max(.01,worldHeight*.125));}
float3 contactLocalPosition(float3 world,float3 origin){return world-origin;}
uint contactHash(uint x){x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;return x;}
float contactLattice(int2 p){return (contactHash(uint(p.x)*73856093u^uint(p.y)*19349663u)&0xffffu)*(1.0/65535.0);}
float contactValueNoise(float2 p){
 int2 i=int2(floor(p));float2 f=p-i;f=f*f*(3-2*f);
 return lerp(lerp(contactLattice(i),contactLattice(i+int2(1,0)),f.x),
             lerp(contactLattice(i+int2(0,1)),contactLattice(i+int2(1,1)),f.x),f.y);
}
// Ragged edge mask 0..1 on the (folded) ground plane, metres: three octaves
// (~0.8 m, ~0.27 m, ~0.11 m) through a mild ~0.35 m domain warp, contrast
// stretched so the edge actually reaches both extremes.
float contactNoise(float2 s){
 float2 warp=float2(contactValueNoise(s*.7+float2(3.1,-1.7)),contactValueNoise(s*.7+float2(-7.7,5.3)))-.5;
 s+=warp*.7;
 float n=contactValueNoise(s*1.25)*.55+contactValueNoise(s*3.7+17.3)*.3+contactValueNoise(s*9.1-5.9)*.15;
 return saturate((n-.5)*1.8+.5);
}
// Facing factor: upward faces collect up to 5/3 of a wall, faces turned down less.
float contactFacing(float up){return lerp(.75,1.25,saturate(up))*(1-.4*saturate(-up));}
// Upper bound of contactBlendWeight's reach for any ground texel (grass 1,
// detail at its clamp), so the shader skips the terrain read above it.
float contactReachBound(float band,float up,float noise){return band*contactFacing(up)*(.5+.8*noise)*1.5;}
// Absolute top: nothing is touched above this many bands (largest bound).
static const float contactBlendTop=2.5;
// x: weight of the terrain material, y: albedo multiplier (contact darkening).
// rise: metres above the drawn ground; band: configured reach (m, walls);
// up: world normal z; noise: contactNoise; grass: 0 bare ground .. 1 grass
// (from the ground's local mean colour); detail: luminance of the sampled
// ground texel minus its local mean (blade tips, pebble tops are positive).
// Grass creeps higher, in tufts following its own bright texels; bare dirt,
// sand and stone stay lower and smoother.
float2 contactBlendWeight(float rise,float band,float up,float noise,float grass,float detail){
 if(band<=0)return float2(0,1);
 rise=max(rise,0);grass=saturate(grass);
 float ground=lerp(.85,1.2+1.2*clamp(detail,-.25,.25),grass);
 float reach=band*contactFacing(up)*(.5+.8*noise)*ground;
 float edge=min(reach,max(reach*.25,.015));
 float weight=1-smoothstep(reach-edge,reach,rise);
 // Contact shading: a tight ~15 cm crease plus a soft lower-band term.
 float dark=1-.2*(1-smoothstep(0,.15,rise))-.1*(1-smoothstep(0,band*.6,rise));
 return float2(weight,dark);
}
// Compose both total light and direct light with the same geometric weight.
// Material modulation and AO must never attenuate the ground-material weight.
float3 contactBlendLitColor(float3 mesh,float3 ground,float weight,float dark){
 return lerp(mesh,ground,weight)*dark;
}
// Folded ground point: walls show the ground strip in front of them turned up,
// so at the contact line the wall texel equals the adjacent ground texel.
float2 contactFoldDirection(float3 n){
 float h=length(n.xy);
 return h>1e-3?n.xy/h*smoothstep(.2,.7,h):0;
}
// Grass share of a ground colour (green clearly above red and blue).
float contactGrass(float3 c){return saturate((c.g-max(c.r,c.b))*8/max(c.g,.05));}
)";

} // namespace owned
