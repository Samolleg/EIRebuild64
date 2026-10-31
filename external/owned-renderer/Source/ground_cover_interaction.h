#pragma once

namespace owned::ground_cover {
// Shared production deformation math, also evaluated by the focused WARP check.
inline constexpr char kInteractionShader[] = R"(
float gcDensityKeep(float nearDistance,float farDistance,float farKeep,float quality,float distance){
 return max(farKeep,1-smoothstep(nearDistance*quality,farDistance*quality,distance));
}
// Screen pixels per tuft scale at a root: the steepest screen-vertical change
// of the projected point times the viewport height.
float gcTuftPixels(float4x4 clip,float3 root,float size,float screenHeight){
 float w=dot(clip[3],float4(root,1));
 if(w<=0)return 0;
 float3 gradient=(clip[1].xyz-dot(clip[1],float4(root,1))/w*clip[3].xyz)/w;
 return size*length(gradient)*.5*screenHeight;
}
// Thinned tufts, and tufts drawn so small that their leaves are about a pixel
// wide at the root (leafPixels: drawn scale times the mesh's root leaf width),
// use their far mesh of fewer, wider leaves: sub-pixel slivers shade whole
// 2x2 pixel quads and shimmer. Returns the far mesh's share: 0 near mesh only,
// 1 far mesh only, between: both drawn with complementary dither (issue #196:
// the former instant swap at keep .45 changed every tuft's shape on one ring
// as the camera moved). The per-instance variation (0-1) spreads each tuft's
// switch point (keep .35-.55, leaf width 1.2-1.92 px) so no ring shows.
float gcTuftFarWeight(float keep,float leafPixels,float variation){
 const float atKeep=.35+.2*frac(variation*7.31),atPixels=lerp(1.2,1.92,variation);
 return max(1-smoothstep(atKeep-.08,atKeep+.08,keep),1-smoothstep(atPixels,atPixels*1.5,leafPixels));
}
float gcWindSway(float2 p,float2 direction,float seconds,float travel,float gust,float phase){
 // Eight-metre fronts travel downwind; a longer wave gives them a gradual
 // envelope. Small phase jitter preserves the front while separating tufts.
 // Gusts above the calm .1 deepen the steady lean and the slow broad wave
 // (a lean travelling with the fronts) far more than the front oscillation.
 float along=dot(p,direction);
 float front=along*.78539816-seconds*travel;
 float broad=along*.36959914-seconds*travel*.47;
 float storm=saturate((gust-.1)/.9);
 float sway=.32+.06*storm+(.18+.12*min(gust,.1)+.03*storm)*sin(front+.18*sin(phase))+(.10+.06*storm)*sin(broad);
 // Per-tuft flutter, periods ~2.9 s and ~1.1 s.
 sway+=.07*sin(phase+seconds*2.2+along*.26);
 return sway+.025*sin(phase*1.7+seconds*5.5+along*1.8);
}
// Clustered cover (flower patches, snow-grass clumps) shares one world-space
// patch lattice. A lattice cell holds at most one patch, kept wholly inside
// the cell; cells divide the 4 m placement cell, so a candidate moved into
// its cell's patch never leaves its placement cell's culling bounds.
// Kind 1 flower patches, 2 snow-grass clumps: x cell metres, yz patch radius
// range, w base presence (fraction of cells with a patch).
float4 gcClusterShape(uint kind){return kind==1u?float4(4,.9,1.5,.25):float4(2,.16,.3,.25);}
// Shore kelp clumps (ground_cover.h kelp_shore_clump): candidates are kept,
// not moved, inside the patch of their lattice cell.
float4 gcKelpShape(){return float4(4,.6,1.6,.7);}
static const float kGcPatchLump=.25; // outline variation, fraction of the radius
struct GcPatch{float2 centre;float radius;float chance;uint id;};
uint gcPatchHash(uint x){x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;return x;}
GcPatch gcPatchAt(float2 p,float cell,float2 radius,uint seed){
 int2 c=int2(floor(p/cell));
 uint h=gcPatchHash(uint(c.x)*73856093u^uint(c.y)*19349663u^seed);
 GcPatch o;o.id=gcPatchHash(h^0x68e31da4u);
 o.radius=lerp(radius.x,radius.y,(h&255u)/255.0);
 float margin=o.radius*(1+kGcPatchLump);
 o.centre=float2(c)*cell+margin+float2((h>>8)&255u,(h>>16)&255u)/255.0*max(cell-2*margin,0);
 o.chance=(h>>24)/256.0; // the patch exists where chance < its presence
 return o;
}
// A uniform candidate (u in [0,1)^2) moved into the patch: denser toward the
// centre, inside a lumpy outline of at most radius * (1 + kGcPatchLump).
float2 gcPatchPoint(GcPatch pc,float2 u){
 float a=u.y*6.2831853;
 float lump=1+kGcPatchLump*(.6*sin(2*a+(pc.id&255u)*.0246)+.4*sin(3*a+((pc.id>>8)&255u)*.0246));
 return pc.centre+float2(cos(a),sin(a))*pc.radius*lump*pow(abs(u.x),.7);
}
// A visible marker owns recovery within its body stamp. Residual history
// must not hold grass/snow above the marker's continuously fading strength.
float gcTrailRecovery(float before,float target,float marked,float release){
 return marked>0?target:max(max(before-release,0),target);
}
float gcPressTarget(float2 local,float2 halfSize,float strength,float body){
 float2 q=local/halfSize;
 return strength*(body!=0? smoothstep(0,1,saturate(1-length(q))) : 1-smoothstep(.64,1,dot(q,q)));
}
float gcPressTexel(float2 local,float2 halfSize,float4 effect,float2 texel){
 // Integrate coverage instead of requiring a whole texel inside the print.
 // Small visible feet must survive the 12.5 cm mask grid, at any rotation.
 float target=0;
 [unroll]for(uint y=0;y<4;++y)[unroll]for(uint x=0;x<4;++x){
  float2 d=(float2(x,y)*.25-.375)*texel;
  float2 rotated=float2(d.x*effect.x+d.y*effect.y,-d.x*effect.y+d.y*effect.x);
  target+=gcPressTarget(local+rotated,halfSize,effect.z,effect.w);
 }
 return target/16;
}
)";
} // namespace owned::ground_cover
