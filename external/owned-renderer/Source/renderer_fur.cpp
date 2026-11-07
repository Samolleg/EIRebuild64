// Fur and hair prototype (F10 fur_mode: 0 off, 1 shells, 2 strands; fur.h).
//
// An additive pass at the end of a world native flush, over the unit records
// that carry a fur profile. It reads the records' prepared world vertices
// (nativeBuildCS output, t8) through their own index lists, so pose, morph
// and shape blending come from the owned figure path and nothing is
// re-evaluated. Off (fur_mode 0) creates, binds and draws nothing.
//
// Mode 1: shells (instanced offset layers, a jittered per-strand pattern,
// root-dark tips-light colour from the part's texture) plus silhouette fins
// (edge quads that grow only where the surface turns away from the camera),
// so the outline breaks.
// Mode 2: a shell undercoat plus strands. A compute pass counts each furry
// triangle's strands (density from area, raised at grazing angles) into a
// compact list; the strands are camera-facing tapered ribbons combed
// head-to-tail and down, clumped into tufts, Kajiya-Kay lit, with pixel-
// coverage alpha. A unit's hair part grows thin strands on its own mesh,
// counted the same way, laid along the mesh's painted texture direction and
// bent with the surface's curvature, so locks and curls keep their shape,
// layered at several depths over a dark undercoat (the mesh shrunk inward;
// the mesh's own fragments are hidden by the submission), clumped, with
// shifted primary and coloured secondary highlights.
//
// Draws nothing on a contract failure (one log line each); never a vanilla path.

#include "fur.h"
#include "renderer.h"
#include "engine_bridge.h"
#include "world_texture_sampler.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <array>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstring>
#include <future>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace owned {
namespace {

constexpr char kFurShader[] = R"(
cbuffer FurDraw:register(b12){
 uint furInstance;uint furPreparedBase;uint furIndexOffset;uint furTriangles;
 uint furWorldState;uint2 furWorldGeneration;uint furNodeOffset;
 uint furNodeBase;uint furNodeCount;uint furPoseBase;uint furHeadNode;
 uint furTailNode;uint furShells;uint furStrandsPerTriangle;uint furSegments;
 float4 furHeadSphere;               // head part bounds in head-node space (w radius; 0 none)
 float furDensity;float furWidth;float furLengthScale;uint furKind; // kind 0 fur, 1 hair
 float2 furViewport;uint furListBase;uint furArgsOffset;
 uint furHairAxis;uint3 furAxisPadding;  // hair: 0 along texture U, 1 along V
 uint2 furTextureRef;uint furFogEnable;uint furMotion;   // the record's nativePadding.xy (texture slice or bindless index, array flag); its fog; DLSS object motion
 float furBodyRadius;float furLodMin;float furLodFull;float furInterface; // LOD: creature radius; pixel radii of no / full detail; 1: an interface record
 float4 furFogColor;                 // the record's game fog colour, w its stock fog factor (default specular alpha)
 float4 furEmissive;                 // the record's material emissive: the game's highlight (visual_highlight.h)
 uint4 furStrandClasses[2];          // fur armour: the material classes that grow strands, one bit per class index
 uint furDepthHull;                  // 1: the hair depth hull (shell VS, kind 1, no colour)
 uint furCullMode;uint furSinkSlot;uint furOrderBase; // the record's D3DCULL (1 none, 2 CW, 3 CCW); the list entry of its trail sink; the order table in furNodes
};
// Per skeleton node: x length, y role, z hair border list start (-1 none);
// then the profile's hair border distances, four to an element.
StructuredBuffer<float4> furNodes:register(t96);
StructuredBuffer<uint4> furStrandList:register(t97); // furStrandCountCS output: triangle, strand
Texture2D<uint> furClasses:register(t98);            // fur armour: the atlas' material class per texel (S1)
// Fur armour (role 6): only texels of a strand class (fur, pelt) grow
// strands. No class texture bound reads 0 (no class): nothing grows.
bool furStrandTexel(float2 uv){
 uint w,h;furClasses.GetDimensions(w,h);
 if(w==0||h==0)return false;
 uint c=furClasses.Load(int3(int2(frac(uv)*float2(w,h)),0));
 return ((furStrandClasses[c>>7][(c>>5)&3]>>(c&31))&1u)!=0;
}
// A triangle wholly on fur: its corners, pulled a little toward the centre
// (a corner on an island's border reads the texel outside the layer).
bool furStrandTriangle(float2 a,float2 b,float2 c){
 float2 m=(a+b+c)/3;
 return furStrandTexel(lerp(a,m,.2))&&furStrandTexel(lerp(b,m,.2))&&furStrandTexel(lerp(c,m,.2));
}
SamplerState furSampler:register(s9);

float furHash(uint x){x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;return (x&0xffffffu)/16777216.0;}
float furHash2(float2 p){return frac(sin(dot(p,float2(127.1,311.7)))*43758.5453);}
float furSeconds(){return nativeTerrainClock.y*.0556;} // world segments: stops and scales with the game clock
float3 furNodePosition(uint node){NativePoseNode n=nativePoseNodes[node];return float3(n.row0.w,n.row1.w,n.row2.w);}
// Tail-to-head axis of the creature (zero without both nodes).
float3 furForward(){
 if(furHeadNode==0xffffffffu||furTailNode==0xffffffffu)return float3(0,0,0);
 float3 d=furNodePosition(furPoseBase+furHeadNode)-furNodePosition(furPoseBase+furTailNode);
 return dot(d,d)>1e-10?normalize(d):float3(0,0,0);
}
bool furWorldCurrent(NativeInstance instance){
 NativeWorldState s=nativeWorldStates[furWorldState];
 return all(s.geometry==furWorldGeneration)&&all(s.pose==nativePoseGeneration(instance));
}
struct FurCorner{float3 p;float3 n;float2 uv;float len;float role;float3 dp;uint index;uint node;};
// The unit's sink into snow and sand trail troughs: the colour pass lowers
// every vertex of a unit by it (nativeFigureTrailSink), so fur and hair grow
// from the drawn surface. Without it hair floated above the sunk head by the
// trough depth (Ingos snow: fur_mode 3 vs 0 at the same paused frame).
// One value per unit (its pose root's trough depth): the count pass writes
// it once per draw into the strand list (furSinkSlot) and each vertex entry
// reads it there; the count pass itself keeps 0 (a translation changes
// neither area nor winding).
static float furTrailSink=0;
// Length after the region rules (inference, fur.h), tapered, never cut: none
// on downward faces (belly) or on the front of the head (eyes, muzzle, mouth).
float furRegion(float3 n,float role,float3 forward){
 if(role>4.5)return 1;
 float k=lerp(.45,1,smoothstep(-.95,-.5,n.z));   // belly: short, never bare (dark patches)
 if(abs(role-2)<.5&&dot(forward,forward)>0)k*=1-smoothstep(0,.5,dot(n,forward));
 return k;
}
FurCorner furLoadCorner(uint tri,uint corner,float3 forward){
 uint index=nativeGeometry.Load(furIndexOffset+tri*12+corner*4);
 NativeWorldVertex w=nativeWorldVertices[furPreparedBase+index];
 FurCorner c;c.p=w.p.xyz+float3(0,0,furTrailSink);c.n=dot(w.normal.xyz,w.normal.xyz)>1e-12?normalize(w.normal.xyz):float3(0,0,1);c.uv=w.uv;
 uint node=furNodeBase+w.partNode;c.index=index;c.node=node;
 float4 f=0;if(node<furNodeCount)f=furNodes[furNodeOffset+node];
 c.role=f.y;c.len=f.x*furLengthScale*furRegion(c.n,f.y,forward);
 // DLSS: where the vertex was last frame (the previous frame's copy of the
 // world vertices, same slot, same source vertex and part; nativeMotionVS).
 c.dp=0;
 [branch]if(furMotion!=0){
  NativeWorldVertex b=nativePreviousWorldVertices[furPreparedBase+index];
  if(b.sourcePosition==w.sourcePosition&&b.partNode==w.partNode)c.dp=b.p.xyz-w.p.xyz;
 }
 return c;
}
// DLSS object motion of a fur point (furMotion: the motion targets are
// bound): its last-frame clip position (moved with its root vertex; the
// strand's own sway is not tracked) and the camera-only one.
void furMotionClips(float3 p,float3 dp,out float4 objectClip,out float4 cameraClip){
 objectClip=0;cameraClip=0;
 [branch]if(furMotion!=0){cameraClip=mul(nativeMotionPreviousClip,float4(p,1));objectClip=mul(nativeMotionPreviousClip,float4(p+dp,1));}
}
// Colour plus the object motion and the depth it was written at (the motion
// PS variants; nativeMotionPS's rule). keep 0 leaves both targets as they
// were (blend by the output alpha). Strands write depth -1: they write no
// world depth, so the camera pass takes their motion wherever it is kept.
struct FurPixel{float4 color:SV_Target0;float4 motion:SV_Target1;float4 depth:SV_Target2;};
FurPixel furMotionPixel(float4 color,float4 p,float4 objectClip,float4 cameraClip,float depth,float keep){
 FurPixel o=(FurPixel)0;o.color=color;
 float2 size=nativeMotionJitter.zw;
 float2 pixel=p.xy-nativeMotionJitter.xy;
 float2 objectPixel=float2(objectClip.x/max(objectClip.w,1e-6)*.5+.5,.5-objectClip.y/max(objectClip.w,1e-6)*.5)*size;
 float2 cameraPixel=float2(cameraClip.x/max(cameraClip.w,1e-6)*.5+.5,.5-cameraClip.y/max(cameraClip.w,1e-6)*.5)*size;
 float2 motion=length(objectPixel-cameraPixel)>.1*size.x?cameraPixel-pixel:objectPixel-pixel;
 float written=cameraClip.w>1e-6&&objectClip.w>1e-6&&keep>0?1:0;
 o.motion=float4(motion,0,written);o.depth=float4(depth,0,0,written);
 return o;
}
// The record's fog, as the native colour pass applies it to figures: the
// stock fog factor with the depth fog, border fog colour, cave fog light,
// then the atmosphere and draw-distance fade.
float3 furFog(float3 c,float4 p,float3 wp){
 [branch]if(furFogEnable==0)return c;
 float3 caveFog=nativeCaveFogLight(wp)+nativeCaveExitGlow(wp);
 float f=nativeFogFactor(furFogColor.w,p);
 c=lerp(saturate(borderFogColorAt(wp,furFogColor.rgb)+caveFog),c,f);
 nativeAtmosphereAt(c,p,saturate(furFogColor.rgb+caveFog),wp);
 return c;
}
// Coat direction on the surface: toward the tail and down the flanks.
float3 furComb(float3 n,float3 forward){
 float3 c=-forward*.7+float3(0,0,-.6);
 c-=n*dot(c,n);
 return dot(c,c)>1e-8?normalize(c):float3(0,0,0);
}
// The growth direction: the surface normal blended with the direction away
// from the creature's tail-to-head axis. That field is continuous in world
// space, so fur on two touching parts leans the same way and covers the join
// instead of following each part's own surface frame.
float3 furGrowth(float3 p,float3 n){
 if(furHeadNode==0xffffffffu||furTailNode==0xffffffffu)return n;
 float3 head=furNodePosition(furPoseBase+furHeadNode),tail=furNodePosition(furPoseBase+furTailNode);
 float3 axis=head-tail;float span=length(axis);
 if(span<1e-5)return n;axis/=span;
 float3 radial=p-(tail+axis*clamp(dot(p-tail,axis),0,span));
 if(dot(radial,radial)<1e-10)return n;
 return normalize(n*.55+normalize(radial)*.45);
}
float3 furWind(float3 p){
 float2 d=nativeWeatherWind.direction;float force=saturate(nativeWeatherWind.force);float t=furSeconds();
 float phase=dot(p,float3(1.7,1.3,.9))*2.3;
 return float3(d,0)*(.2+.6*force)*(.55+.45*sin(t*2.3+phase))+float3(-d.y,d.x,0)*.15*sin(t*3.1+phase*1.7);
}
#ifdef EID3D12
// D3D12: x is the texture's bindless index (NATIVE_SAMPLE's rule).
float4 furTexel(uint2 reference,float2 uv){
 Texture2D t=ResourceDescriptorHeap[NonUniformResourceIndex(nativeBindlessIndex(reference.x))];return t.Sample(furSampler,uv);
}
#else
float4 furTexel(uint2 reference,float2 uv){
 return reference.y!=0?nativeTextureArray.Sample(furSampler,float3(uv,reference.x)):nativeTexture.Sample(furSampler,uv);
}
#endif
// Sun, shadow, point and cave light of a fur fragment (the critter rule).
// The sun shadow is the surface's under the fur: tested at the root on the
// mesh with the mesh's tangent plane as receiver plane, so fur takes exactly
// the shadow of the skin it grows from. The fragment's own screen
// derivatives gave the plane of a camera-facing ribbon or a wind-moved
// shell, and strands at their own points dipped in and out of the hair
// mesh's own caster: flickering shadow on hair and fur.
float3 furLight(float3 albedo,float3 wp,float3 n,float facing,float2 pixel,float3 root,float3 rootNormal,out float visibility){
 visibility=1;
 [branch]if(shadowControl.x!=0||shadowBias[4].z>.5){
  float3 sn=dot(rootNormal,rootNormal)>1e-12?normalize(rootNormal):float3(0,0,1);
  float3 t=normalize(cross(sn,abs(sn.z)<.9?float3(0,0,1):float3(1,0,0))),b=cross(sn,t);
  visibility=saturate(nativeShadowVisibility(root+sn*.02,t*.01,b*.01,pixel,false));
 }
 // World hair receives the same sun/cloud visibility as the native material
 // pass. Cloud transmittance attenuates direct light only: the ambient and
 // lamps still light the coat. Use the root, like the geometric shadow, and
 // never sample the world's cloud field at interface-preview coordinates.
 [branch]if(furKind==1&&furInterface==0)visibility=min(visibility,skyCloudVisibility(root));
 float3 local=0;
 uint2 range=nativePointLightRange(wp);
 [loop]for(uint k=range.x;k<range.y;++k){
  NativePointLight light=nativePointLights[nativePointLightGrid[k]];
  float3 delta=light.centerInverseRadiusSquared.xyz-wp;float d2=dot(delta,delta);
  float reach=1-d2*light.centerInverseRadiusSquared.w;if(reach<=0)continue;
  float f=saturate(dot(n,delta*rsqrt(max(d2,1e-12)))*.5+.5);
  local+=light.color.rgb*(reach*reach*f*(light.color.w>0?.65:1));
 }
 local+=nativeCaveBounce(wp,n);
 // The record's emissive as calculateWorldLighting adds it: highlighted
 // units glow through their fur and hair like their mesh.
 return albedo*saturate(max(nativeWorldAmbient.rgb,nativeWorldSunDiffuse.rgb*facing*visibility)+local+furEmissive.rgb);
}
float3 furSun(){return dot(nativeWorldSunDirection.xyz,nativeWorldSunDirection.xyz)>1e-12?normalize(nativeWorldSunDirection.xyz):float3(0,0,1);}
// Root dark, tips a little light: the coat's own colour stays (stripes kept).
float3 furShade(float3 albedo,float h){return albedo*lerp(.82,1.08,h);}
float furHeadRadius(){
 if(furHeadNode==0xffffffffu||furHeadSphere.w<=0)return 0;
 float4x4 m=nativeNodeMatrix(furPoseBase+furHeadNode);
 return furHeadSphere.w*length(float3(m[0][0],m[1][0],m[2][0]));
}
// Level of detail from screen size: the head (hair) or the creature (coat)
// radius in pixels, 0 at furLodMin, 1 at furLodFull. Every stage of a draw
// computes the same value from the same per-draw inputs.
float furPixels(row_major float4x4 clip){
 float3 centre;float radius;
 if(furKind==1){
  float4x4 m=nativeNodeMatrix(furPoseBase+furHeadNode);
  centre=mul(m,float4(furHeadSphere.xyz,1)).xyz;radius=furHeadRadius();
 }else{
  centre=furHeadNode!=0xffffffffu&&furTailNode!=0xffffffffu?
   (furNodePosition(furPoseBase+furHeadNode)+furNodePosition(furPoseBase+furTailNode))*.5:furNodePosition(furPoseBase);
  radius=furBodyRadius;
 }
 float4 c=mul(clip,float4(centre,1));
 if(c.w<=1e-4)return 0;
 float3 view=liquidViewDirection(clip,centre);
 float3 side=cross(view,float3(0,0,1));side=dot(side,side)>1e-8?normalize(side):float3(1,0,0);
 float4 e=mul(clip,float4(centre+side*radius,1));
 return length((e.xy/e.w-c.xy/c.w)*.5*furViewport);
}
float furLod(row_major float4x4 clip){return saturate((furPixels(clip)-furLodMin)/max(furLodFull-furLodMin,1));}
// Strand count factor (fewer far away, fading to none at furLodMin) and the
// width that keeps the cover with fewer strands.
float furStrandFactor(float lod){return lerp(.2,1,lod*sqrt(lod))*smoothstep(0,.1,lod);}
// Sub-pixel strands: a strand narrower than ~0.6 px is drawn one pixel wide
// with its coverage as alpha anyway, so fewer of them (to a quarter), each
// proportionally wider, give the same cover for less overdraw. The world
// width is the strand's base width; the scale is pixels per world unit of
// the LOD's measure (head or creature radius).
float furThin(float worldWidth,float px){
 float r=furKind==1?furHeadRadius():furBodyRadius;
 return r>0?clamp(worldWidth*px/r/.6,.25,1):1;
}
uint furLodSegments(float lod){return max(2u,(uint)round(furSegments*lerp(.5,1,lod)));}
)" R"(
// A hair vertex's distance to its mesh's open border (FurProfile::
// hair_border), in its node's units; 1e9 without a list.
float furHairBorder(FurCorner c){
 if(c.node>=furNodeCount)return 1e9;
 float start=furNodes[furNodeOffset+c.node].z;
 if(start<0)return 1e9;
 uint i=(uint)start+c.index;
 float4 v=furNodes[furNodeOffset+furNodeCount+i/4];
 uint l=i&3u;
 return l==0?v.x:(l==1?v.y:(l==2?v.z:v.w));
}
// Hair grows only on triangles its mesh draws: the record's cull mode on
// the triangle's winding in this clip space. Strands and the undercoat on
// culled back faces showed hair the mesh never shows (above helmets, the
// inside of trimmed caps) and the undercoat there covered strands dark
// (r4 run, 6f300ba0: black patches = undercoat). The sign is the runtime's,
// not derived: the textbook mapping (NDC counter-clockwise = screen
// clockwise, drawn under D3DCULL_CCW) grew hair on exactly the culled faces
// (r5 run, 5771ad7f: Karansul's inner cap, 2285's cap above its helmet),
// so these corners reach the rasteriser in the opposite order.
bool furHairFaceDrawn(row_major float4x4 clip,FurCorner c0,FurCorner c1,FurCorner c2){
 if(furCullMode!=2&&furCullMode!=3)return true;
 float4 a=mul(clip,float4(c0.p,1)),b=mul(clip,float4(c1.p,1)),c=mul(clip,float4(c2.p,1));
 if(min(a.w,min(b.w,c.w))<=1e-5)return true;
 float2 e1=b.xy/b.w-a.xy/a.w,e2=c.xy/c.w-a.xy/a.w;
 float ccw=e1.x*e2.y-e1.y*e2.x;   // > 0: counter-clockwise in NDC
 return furCullMode==3?ccw<0:ccw>0;
}
// A hair triangle's strand direction: the surface tangent of its painted
// texture axis (dP/du or dP/dv; its length is world units per UV unit).
bool furHairTangent(FurCorner c0,FurCorner c1,FurCorner c2,out float3 tangent){
 float3 e1=c1.p-c0.p,e2=c2.p-c0.p;float2 d1=c1.uv-c0.uv,d2=c2.uv-c0.uv;
 float det=d1.x*d2.y-d2.x*d1.y;tangent=0;
 if(abs(det)>=1e-12)tangent=furHairAxis==0?(e1*d2.y-e2*d1.y)/det:(e2*d1.x-e1*d2.x)/det;
 if(dot(tangent,tangent)>1e-16)return true;
 // A face whose texture coordinates collapse to a line or a point (hair
 // meshes map hems and inner layers onto one texel strip) has no painted
 // direction: its strands fall down the surface (world z is up), or along
 // an edge on a level face. The tangent is made very long so a strand's
 // texture position stays put (furStrandVS steps uv by length / |tangent|).
 // They grew no strands at all, and the dark undercoat showed through
 // (save "hair flicker", Babur: strand-less face view, 2026-11-01).
 float3 n=cross(e1,e2);n=dot(n,n)>1e-20?normalize(n):float3(0,0,1);
 float3 down=float3(0,0,-1)-n*(-n.z);
 if(dot(down,down)<.01)down=e1-n*dot(e1,n);
 if(dot(down,down)<1e-16)return false;
 tangent=normalize(down)*1e4;
 return true;
}

// ------------------------------------------------------------------ shells
struct FurShellOut{float4 p:SV_Position;float2 uv:TEXCOORD0;nointerpolation uint2 reference:TEXCOORD1;float3 wp:TEXCOORD2;
 float3 n:TEXCOORD3;float h:TEXCOORD4;float len:TEXCOORD5;
 float4 objectClip:TEXCOORD6;float4 cameraClip:TEXCOORD7;float3 root:TEXCOORD8;};
FurShellOut furShellVS(uint vid:SV_VertexID,uint shell:SV_InstanceID){
 FurShellOut o=(FurShellOut)0;o.p=float4(0,0,-1,1);
 uint tri=vid/3,corner=vid%3;
 NativeInstance instance=loadNativeInstance(furInstance);
 if(tri>=furTriangles||!furWorldCurrent(instance))return o;
 furTrailSink=asfloat(furStrandList[furSinkSlot].x);
 float3 forward=furForward();
 FurCorner c0=furLoadCorner(tri,0,forward),c1=furLoadCorner(tri,1,forward),c2=furLoadCorner(tri,2,forward);
 if(max(c0.len,max(c1.len,c2.len))<=0)return o;   // the whole triangle grows nothing
 if(c0.role>5.5)return o;                          // fur armour grows strands only
 FurCorner c=c2;if(corner==0)c=c0;else if(corner==1)c=c1;
 float lod=furLod(instance.objectToClip);
 [branch]if(furKind==1){
  // Hair undercoat: the hair mesh shrunk inward, under every strand layer.
  // Far away, where the strands thin out, it takes the hair's full colour
  // (the mesh's own look) so the hair never goes dark.
  if(!furHairFaceDrawn(instance.objectToClip,c0,c1,c2))return o;
  // Interface close-ups (portraits): just under the mesh, as their strands lie
  // on it; 3% down put it under the scalp where the hair mesh sits close.
  float3 q=c.p+c.n*furHeadRadius()*(furDepthHull!=0?.004:furInterface!=0?-.003:-.03);
  o.p=mul(instance.objectToClip,float4(q,1));
  o.uv=c.uv;o.reference=instance.nativePadding.xy;o.wp=q;o.root=c.p;o.n=c.n;o.h=lerp(1,.3,smoothstep(.3,.9,lod));o.len=1;
  // Faces without a painted direction (hems, inner layers) carry only short
  // hanging strands, so their undercoat takes the hair's full colour: dark,
  // it showed as a black band under the tips (user, Babur).
  float3 tg;if(furHairTangent(c0,c1,c2,tg)&&dot(tg,tg)>1e6)o.h=1;
  furMotionClips(q,c.dp,o.objectClip,o.cameraClip);
  return o;
 }
 // Fewer shells as the creature shrinks on screen; none below half the
 // no-strand size (fur too small to see).
 uint shells=furPixels(instance.objectToClip)<furLodMin*.5?0u:(uint)ceil(furShells*lerp(.3,1,lod));
 if(shell>=shells)return o;
 float h=(shell+1.0)/shells;
 float3 bend=furComb(c.n,forward)*.55+furWind(c.p)*.5;
 float3 p=c.p+furGrowth(c.p,c.n)*c.len*h*.85+bend*c.len*h*h;
 o.p=mul(instance.objectToClip,float4(p,1));
 o.uv=c.uv;o.reference=instance.nativePadding.xy;o.wp=p;o.root=c.p;o.n=c.n;o.h=h;o.len=c.len;
 furMotionClips(p,c.dp,o.objectClip,o.cameraClip);
 return o;
}
float4 furShellColor(FurShellOut i){
 if(i.len<=1e-5)return float4(0,0,0,-1);   // discarded by the entry (FXC loses a value read after an inlined discard)
 [branch]if(furKind==1){
  // Hair undercoat: the hair's own colour, deep in shadow (the strands carry the light).
  float3 n=normalize(i.n);float visibility;
  return float4(furFog(furLight(furTexel(i.reference,i.uv).rgb*i.h,i.wp,n,saturate(dot(n,furSun())*.5+.5),i.p.xy,i.root,n,visibility),i.p,i.wp),1);
 }
 // Strands at hashed positions (the nearest of the 3x3 neighbouring cells'
 // jittered points), each with its own length and thickness: no grid.
 float2 cell=i.uv*furDensity;float2 id=floor(cell),f=frac(cell);
 float best=2;
 [unroll]for(int y=-1;y<=1;++y)[unroll]for(int x=-1;x<=1;++x){
  float2 o=float2(x,y),c=id+o;
  float2 j=float2(furHash2(c),furHash2(c+31.7));
  float tip=.4+.6*furHash2(c+7.1);
  float radius=(.45+.35*furHash2(c+3.3))*(1-i.h/tip)+.04;
  if(i.h<tip)best=min(best,length(o+j-f)/radius);
 }
 if(best>1)return float4(0,0,0,-1);
 float3 albedo=furShade(furTexel(i.reference,i.uv).rgb,i.h);
 float3 n=normalize(i.n);float3 sun=furSun();
 float facing=saturate(dot(n,sun)*.5+.5);   // soft wrap: fur scatters light
 float visibility;
 return float4(furFog(furLight(albedo,i.wp,n,facing,i.p.xy,i.root,n,visibility),i.p,i.wp),1);
}

)" R"(
// -------------------------------------------------------------------- fins
// Each triangle edge extrudes a fur quad that grows only where the triangle
// is seen edge-on, so hair stands out along the silhouette.
struct FurFinOut{float4 p:SV_Position;float2 uv:TEXCOORD0;nointerpolation uint2 reference:TEXCOORD1;float3 wp:TEXCOORD2;
 float3 n:TEXCOORD3;float h:TEXCOORD4;float stripe:TEXCOORD5;
 float4 objectClip:TEXCOORD6;float4 cameraClip:TEXCOORD7;float3 root:TEXCOORD8;};
FurFinOut furFinVS(uint vid:SV_VertexID,uint tri:SV_InstanceID){
 FurFinOut o=(FurFinOut)0;o.p=float4(0,0,-1,1);
 NativeInstance instance=loadNativeInstance(furInstance);
 if(tri>=furTriangles||!furWorldCurrent(instance))return o;
 furTrailSink=asfloat(furStrandList[furSinkSlot].x);
 float3 forward=furForward();
 FurCorner c0=furLoadCorner(tri,0,forward),c1=furLoadCorner(tri,1,forward),c2=furLoadCorner(tri,2,forward);
 if(max(c0.len,max(c1.len,c2.len))<=0||c0.role>5.5)return o;   // fur armour: strands only
 float3 navg=normalize(c0.n+c1.n+c2.n);
 float3 view=liquidViewDirection(instance.objectToClip,(c0.p+c1.p+c2.p)/3);
 float grow=smoothstep(.35,.85,1-abs(dot(navg,view)))*smoothstep(.5,1,furPixels(instance.objectToClip)/max(furLodMin,1));
 if(grow<=0)return o;
 uint edge=vid/6,k=vid%6;
 FurCorner a=c2,b=c0;
 if(edge==0){a=c0;b=c1;}else if(edge==1){a=c1;b=c2;}
 bool atB=(k==1||k==4||k==5),top=(k==2||k==3||k==5);
 FurCorner c=a;if(atB)c=b;
 float len=c.len*grow*1.5;
 float3 p=c.p;
 if(top)p+=furGrowth(c.p,c.n)*len*.8+furComb(c.n,forward)*len*.5+furWind(c.p)*len*.5;
 float spacing=max(max(a.len,b.len)*.18,1e-4);
 o.p=mul(instance.objectToClip,float4(p,1));
 o.uv=c.uv;o.reference=instance.nativePadding.xy;o.wp=p;o.root=c.p;o.n=c.n;o.h=top?1:0;
 furMotionClips(p,c.dp,o.objectClip,o.cameraClip);
 o.stripe=(atB?length(b.p-a.p):0)/spacing+furHash(tri*3u+edge)*17;
 return o;
}
float4 furFinColor(FurFinOut i){
 float id=floor(i.stripe);float f=frac(i.stripe)-.5;
 float j=(furHash2(float2(id,3.1))-.5)*.5;
 float tip=.45+.55*furHash2(float2(id,9.7));
 if(i.h>=tip)return float4(0,0,0,-1);
 float width=(.25+.2*furHash2(float2(id,5.3)))*(1-i.h/tip)+.03;
 if(abs(f-j)>width)return float4(0,0,0,-1);
 float3 albedo=furShade(furTexel(i.reference,i.uv).rgb,i.h);
 float3 n=normalize(i.n);float3 sun=furSun();
 float facing=saturate(dot(n,sun)*.5+.5);
 float visibility;
 return float4(furFog(furLight(albedo,i.wp,n,facing,i.p.xy,i.root,n,visibility),i.p,i.wp),1);
}

)" R"(
// ----------------------------------------------------------------- strands
// Counts each furry triangle's strands (furStrandCountCS, one thread per
// triangle) into the draw's count slots after its list (nativeTriangleOutput,
// u0); furStrandOrderCS then lays the list out in triangle order and stores
// the draw's instance count (nativeIndirectArgs, u1). The strands blend
// without depth writes, so their order is the composite: a list filled in
// atomic arrival order changed every frame and overlapping strands flickered
// on a still frame (opaque-strand probe on save "fur flicker": the fur's
// frame-to-frame changes vanished). Every fur draw (strands or not) runs the
// count first: thread 0 stores the unit's trail sink (furTrailSink).
uint furCountSlot(uint tri){return furListBase+furTriangles*furStrandsPerTriangle+tri;}
uint furStrandCount(uint tri,NativeInstance instance);
[numthreads(64,1,1)]void furStrandCountCS(uint3 id:SV_DispatchThreadID){
 uint tri=id.x;
 NativeInstance instance=loadNativeInstance(furInstance);
 // An interface record (portrait, panel doll, outfit screen) stands in no
 // trough: its pose is interface space, not the unit's world position.
 if(tri==0)nativeTriangleOutput[furSinkSlot]=uint4(asuint(furInterface!=0?0:nativeFigureTrailSink(instance)),0,0,0);
 if(furStrandsPerTriangle==0||tri>=furTriangles)return;
 // The draw's strip length for this LOD (every draw's thread 0).
 if(tri==0){float lod=saturate((furPixels(instance.objectToClip)-furLodMin)/max(furLodFull-furLodMin,1));
  nativeIndirectArgs.Store(furArgsOffset,2*(furLodSegments(lod)+1));}
 nativeTriangleOutput[furCountSlot(tri)]=uint4(furStrandCount(tri,instance),0,0,0);
}
// One dispatch for the frame's strand draws, a group per draw: its list
// base, triangles, slots and argument offset from the order table (uint4 in
// the node buffer at furOrderBase).
groupshared uint furScan[256];
[numthreads(256,1,1)]void furStrandOrderCS(uint3 id:SV_GroupThreadID,uint3 group:SV_GroupID){
 uint4 draw=asuint(furNodes[furOrderBase+group.x]);
 uint listBase=draw.x,triangles=draw.y,counts=draw.x+draw.y*draw.z,argsOffset=draw.w;
 uint lane=id.x,running=0;
 for(uint first=0;first<triangles;first+=256){
  uint tri=first+lane;
  uint n=tri<triangles?nativeTriangleOutput[counts+tri].x:0;
  furScan[lane]=n;GroupMemoryBarrierWithGroupSync();
  [unroll]for(uint step=1;step<256;step<<=1){
   uint add=lane>=step?furScan[lane-step]:0;GroupMemoryBarrierWithGroupSync();
   furScan[lane]+=add;GroupMemoryBarrierWithGroupSync();
  }
  uint base=running+furScan[lane]-n;   // exclusive prefix: triangle order
  for(uint k=0;k<n;++k)nativeTriangleOutput[listBase+base+k]=uint4(tri,k,0,0);
  running+=furScan[255];GroupMemoryBarrierWithGroupSync();
 }
 if(lane==0)nativeIndirectArgs.Store(argsOffset+4,running);
}
uint furStrandCount(uint tri,NativeInstance instance){
 if(!furWorldCurrent(instance))return 0;
 float3 forward=furForward();
 FurCorner c0=furLoadCorner(tri,0,forward),c1=furLoadCorner(tri,1,forward),c2=furLoadCorner(tri,2,forward);
 float area=.5*length(cross(c1.p-c0.p,c2.p-c0.p));
 float wanted;
 if(furKind==1){
  // Hair: strands about 1.6x the triangle's size, enough ribbons to cover it
  // furDensity times over.
  float3 tangent;float radius=furHeadRadius();
  if(abs(c0.role-5)>.5||radius<=0||!furHairTangent(c0,c1,c2,tangent)||
     !furHairFaceDrawn(instance.objectToClip,c0,c1,c2))return 0;
  wanted=area*furDensity/(1.6*sqrt(area)*furWidth*radius);
 }else{
  if(max(c0.len,max(c1.len,c2.len))<=0||abs(c0.role-5)<.5)return 0;
  if(c0.role>5.5&&!furStrandTriangle(c0.uv,c1.uv,c2.uv))return 0;   // fur armour: triangles wholly on fur (furStrandVS)
  float3 navg=normalize(c0.n+c1.n+c2.n);
  float3 view=liquidViewDirection(instance.objectToClip,(c0.p+c1.p+c2.p)/3);
  float grazing=smoothstep(.35,.85,1-abs(dot(navg,view)));
  wanted=area*furDensity*lerp(.5,2,grazing);
 }
 float px=furPixels(instance.objectToClip);float lod=saturate((px-furLodMin)/max(furLodFull-furLodMin,1));
 wanted*=furStrandFactor(lod)*furThin(furKind==1?furWidth*furHeadRadius():furWidth*c0.len,px);
 uint n=(uint)wanted;if(furHash(tri*7919u+furInstance*31u)<frac(wanted))n++;
 return min(n,furStrandsPerTriangle);
}
)" R"(
struct FurStrandOut{float4 p:SV_Position;float2 uv:TEXCOORD0;nointerpolation uint2 reference:TEXCOORD1;float3 wp:TEXCOORD2;
 float3 tangent:TEXCOORD3;float3 n:TEXCOORD4;float3 view:TEXCOORD5;float t:TEXCOORD6;float alpha:TEXCOORD7;float tint:TEXCOORD8;
 float4 objectClip:TEXCOORD9;float4 cameraClip:TEXCOORD10;float3 root:TEXCOORD11;
 nointerpolation float2 shadowSeed:TEXCOORD12;};   // the strand's shadow filter rotation seed (furStrandColor)
// The ribbon vertex of a strand point: camera facing, widened to one pixel
// when thinner (the coverage goes to alpha).
void furRibbon(inout FurStrandOut o,row_major float4x4 clip,float3 p,float3 dp,float3 dir,float3 n,float width,float t,float side,float fade){
 float3 tangent=dot(dir,dir)>1e-12?normalize(dir):n;
 float3 view=liquidViewDirection(clip,p);
 float3 across=cross(tangent,view);
 across=dot(across,across)>1e-12?normalize(across):float3(1,0,0);
 float4 c=mul(clip,float4(p,1)),e=mul(clip,float4(p+across*width,1));
 float coverage=1;
 if(c.w>1e-5&&e.w>1e-5){
  float pixels=length((e.xy/e.w-c.xy/c.w)*.5*furViewport);
  if(pixels<1){width/=max(pixels,1e-3);coverage=max(pixels,.05);}
 }
 float3 q=p+across*width*.5*side;
 o.p=mul(clip,float4(q,1));
 furMotionClips(q,dp,o.objectClip,o.cameraClip);
 o.wp=p;o.tangent=tangent;o.n=n;o.view=view;o.t=t;
 o.alpha=coverage*fade;
}
// Strands draw as one triangle strip per instance: vertex 2k and 2k+1 are
// point k's two ribbon sides.
void furStrandCorner(uint vid,out float t,out float side){
 side=(vid&1u)!=0?1:-1;
 t=(float)(vid>>1)/furSegments;
}
FurStrandOut furStrandVS(uint vid:SV_VertexID,uint iid:SV_InstanceID){
 FurStrandOut o=(FurStrandOut)0;o.p=float4(0,0,-1,1);
 uint4 entry=furStrandList[furListBase+iid];uint tri=entry.x,strand=entry.y;
 // The draw's own clip: persistent records (instance bit 31) take the frame's
 // world clip (loadNativeInstance); an interface record (portrait, panel
 // doll, outfit screen) its own, never the world camera's.
 row_major float4x4 clip=loadNativeInstance(furInstance).objectToClip;
 furTrailSink=asfloat(furStrandList[furSinkSlot].x);
 float3 forward=furForward();
 FurCorner c0=furLoadCorner(tri,0,forward),c1=furLoadCorner(tri,1,forward),c2=furLoadCorner(tri,2,forward);
 uint seed=tri*7919u+strand*104729u+furInstance*31u;
 float a=furHash(seed+1u),b=furHash(seed+2u);if(a+b>1){a=1-a;b=1-b;}
 float3 root=c0.p+(c1.p-c0.p)*a+(c2.p-c0.p)*b;
 float3 n=normalize(c0.n+(c1.n-c0.n)*a+(c2.n-c0.n)*b);
 float2 uv=c0.uv+(c1.uv-c0.uv)*a+(c2.uv-c0.uv)*b;
 float rootLength=c0.len+(c1.len-c0.len)*a+(c2.len-c0.len)*b;
 float3 rootDp=c0.dp+(c1.dp-c0.dp)*a+(c2.dp-c0.dp)*b;
 o.root=root;o.shadowSeed=float2(seed&1023u,(seed>>10)&1023u);
 float px=furPixels(clip);float lod=saturate((px-furLodMin)/max(furLodFull-furLodMin,1));
 float t,side;furStrandCorner(vid,t,side);
 t*=(float)furSegments/furLodSegments(lod);
 float widen=min(rsqrt(max(lerp(.2,1,lod*sqrt(lod)),1e-3)),2.5)/
             furThin(furKind==1?furWidth*furHeadRadius():furWidth*c0.len,px);
 [branch]if(furKind==1){
  // Hair: a thin strand lying on the hair mesh along its painted direction,
  // centred on the root and bent with the surface's curvature along it
  // (the normal's change across the triangle), so it follows locks and curls.
  float3 tangentUv;float radius=furHeadRadius();
  if(!furHairTangent(c0,c1,c2,tangentUv)||radius<=0)return o;
  // A face without a painted direction (collapsed texture coordinates,
  // furHairTangent's very long fallback) hangs its strands from the root:
  // down along the surface, or straight down where the face looks down (a
  // hem's underside). Centred strands along an edge direction ran sideways
  // and up there (fur_tip_probe run: every flared tip was on those faces);
  // straight down everywhere left the sloped faces bare.
  bool hang=dot(tangentUv,tangentUv)>1e6;
  float3 slope=float3(0,0,-1)-n*(-n.z);
  float3 T=hang?(dot(slope,slope)>.09?normalize(slope):float3(0,0,-1)):tangentUv-n*dot(tangentUv,n);
  if(dot(T,T)<1e-16)return o;
  float perUv=hang?1e4:length(T);T/=hang?1:perUv;
  float3 e1=c1.p-c0.p,e2=c2.p-c0.p;
  float g11=dot(e1,e1),g12=dot(e1,e2),g22=dot(e2,e2),r1=dot(e1,T),r2=dot(e2,T),det=g11*g22-g12*g12;
  float3 dN=0;
  if(abs(det)>1e-20)dN=((r1*g22-r2*g12)*(c1.n-c0.n)+(r2*g11-r1*g12)*(c2.n-c0.n))/det;
  float bend=hang?0:dot(dN,T);
  float area=.5*length(cross(e1,e2));
  // Clumps: strands of one triangle region share a direction jitter (turned
  // about the normal) and a brightness offset.
  uint clump=tri*131u+strand/8u;
  float turn=(furHash(clump*5u+21u)-.5)*.35;
  float3 Bn=cross(n,T);T=normalize(T*cos(turn)+Bn*sin(turn));
  // Loose strays, more toward the ends (downward faces): a little longer,
  // with more wave across the strand, lying on the hair like every strand.
  // Rejected (user, twice): strays lifting off the surface and 2.5x-long
  // strays; both stood out of the outline as spikes (fur_debug red, 78faeabb).
  float stray=furHash(seed+8u)<.08+.12*saturate(-n.z)?1:0;
  // Depth layer: from inside the mesh (just over the undercoat) up to its
  // surface, never beyond: the authored hair mesh is the hair's outside.
  // Strands up to 1.5% of the head radius above it pierced helmets fitted
  // close over the hr.XX.armorYY parts and lifted the hair's outline
  // (fur_debug 3 run, 97523671: the specks on helmets were that layer).
  // Deeper strands are darker (self-shadow).
  // Interface records (portrait, panel doll, outfit screen) are close-ups:
  // where the hair mesh lies within that layer of the scalp, the deeper
  // strands went under the head and the scalp showed through the hair
  // (3ppl portraits, run without cb718552's lift). Their strands lie on the
  // hair mesh itself, never above it (so never through a helmet over it);
  // this replaces that lift toward the eye, which put strands over helmets.
  float layer=furHash(seed+10u);
  float depth=furInterface!=0?0:lerp(-.025,0,layer)*radius;
  float len=1.6*sqrt(area)*(.8+.4*furHash(seed+4u))*furLengthScale*(1+.3*stray);
  // Strands end at the hair mesh's own extent: centred on its root, a strand
  // reached up to half its length past an open border (a helmet part's
  // trimmed rim, a hairline) and stood out through the helmet or off the
  // head. Its extent is clamped to the root's distance from the border.
  float4x4 hairNode=nativeNodeMatrix(furPoseBase+c0.node);
  float border=(furHairBorder(c0)*(1-a-b)+furHairBorder(c1)*a+furHairBorder(c2)*b)*
               length(float3(hairNode[0][0],hairNode[1][0],hairNode[2][0]));
  float reach=min(.5*len,border);
  float s=hang?t*reach:(t-.5)*2*reach;   // hanging strands start at the root, as long as a centred strand's half (user: 2x read too long)
  float3 B=cross(n,T);
  // Every strand, strays included, follows the scalp's curvature along its
  // length: a stray drawn straight (tangent at its root, 2.5x longer) rose
  // off a curved scalp at both ends (user: spikes; fur_debug run, 49d4e4fc).
  float3 p=root+T*s+n*(depth-.5*bend*s*s)+B*sin(t*6.2831853+furHash(seed+7u)*6.2831853)*radius*(.003+.01*stray);
  // Wind moves strands along the surface only, never off it (same reason).
  float3 wind=furWind(root);wind-=n*dot(wind,n);
  p+=wind*radius*(.03+.06*stray*t);
  float3 dir=T-n*bend*s;
  // No body collision: the strands lie on the authored hair mesh, which
  // already rests on the body. Pushing points out of the body collision shapes swung
  // strands outward at the nape and shoulders (fur_debug run, 78faeabb).
  float2 along=furHairAxis==0?float2(1,0):float2(0,1);
  // The strand's colour runs along its texture direction but stays inside
  // the root triangle's texture footprint: a strand reaches up to ~2.4
  // triangle widths past its root (strays more), and in the character's
  // atlas that read neighbouring islands (pale strays, off-colour stripes).
  float2 uvLo=min(c0.uv,min(c1.uv,c2.uv)),uvHi=max(c0.uv,max(c1.uv,c2.uv));
  // Varied strand width and tone, so individual hairs read.
  o.uv=clamp(uv+along*(s/perUv),uvLo,uvHi);o.reference=furTextureRef;
  o.tint=(.8+.4*furHash(seed+5u))*(.8+.4*furHash(clump*5u+22u))*lerp(.4,1,saturate(layer+stray));
  furRibbon(o,clip,p,rootDp,dir,n,furWidth*radius*widen*(.6+furHash(seed+9u))*(1-.3*stray),t,side,
            smoothstep(0,.2,t)*smoothstep(1,.8,t));
  o.t=.75;
  return o;
 }
 if(rootLength<=0)return o;
 // Fur armour: fur texels only, and only in triangles wholly on fur. A
 // strand leans up to its length past its root, so roots on a triangle that
 // reaches bare art (fangs, muzzle, a loincloth's edge) laid strands over it.
 if(c0.role>5.5&&(!furStrandTexel(uv)||!furStrandTriangle(c0.uv,c1.uv,c2.uv)))return o;
 // Longer where the coat is seen edge-on: the outline is what reads as fur.
 float3 viewRoot=liquidViewDirection(clip,root);
 float grazing=smoothstep(.4,.9,1-abs(dot(n,viewRoot)));
 float len=rootLength*(.75+.5*furHash(seed+4u))*lerp(1,1.5,grazing);
 // Tufts: strands of one triangle region pull their tips toward a shared point.
 uint clump=tri*131u+strand/5u;
 float3 pull=float3(furHash(clump*3u+11u),furHash(clump*3u+12u),furHash(clump*3u+13u))-.5;pull-=n*dot(pull,n);
 float3 comb=furComb(n,forward);float3 wind=furWind(root);float3 grow=furGrowth(root,n);
 float3 p=root+len*(grow*t*.8+comb*.6*t*t+pull*.6*t*t)+wind*len*.4*t*t;
 float3 dir=grow*.8+(comb*1.2+pull*1.2+wind*.8)*t;
 o.uv=uv;o.reference=furTextureRef;o.tint=.9+.2*furHash(seed+5u);
 furRibbon(o,clip,p,rootDp,dir,n,furWidth*len*widen*(1-.8*t),t,side,1-.5*smoothstep(.65,1,t));
 return o;
}
// Hair shine from one light L, energy-normalised like the gear's GGX lamps
// and the figure lobes (pi-corrected: the renderer's diffuse is albedo times
// the light, so a BRDF f is applied as pi*f). Kajiya-Kay lobes pow(sin,n)
// normalised by (n+2)/(2 pi), shaped after Marschner: R, the cuticle's
// whitish reflection (hair Fresnel 0.046, shifted toward the root, about
// 12 degrees wide); TRT, the broader internal reflection tinted twice by the
// hair colour (shifted toward the tip, about 20 degrees); TT, the light
// through a backlit strand. The tint carries the depth layer's self-shadow.
// Rejected: the artistic peaks (R 0.35, TRT 0.6 albedo at fixed exponents)
// tuned against sun diffuse ~1: point lights deliver 0.1-0.3 at hair range,
// so torches left no visible shine (2026-11-01 same-frame diagnostic).
float3 furHairShine(float3 T,float3 N,float3 V,float3 L,float3 albedo,float tint){
 float3 H=normalize(L+V);
 float3 T1=normalize(T-N*.1),T2=normalize(T+N*.15);
 float th1=dot(T1,H),th2=dot(T2,H);
 // pi*(n+2)/(2 pi)=(n+2)/2; pow(sin,n)=pow(1-cos^2,n/2).
 float r=.046*13*pow(saturate(1-th1*th1),12);          // n 24
 float trt=.15*5*pow(saturate(1-th2*th2),4);           // n 8, times albedo^2 below
 float tt=.1*2.5*pow(saturate(-dot(V,L)),4);           // n 4, (n+1)/2
 return saturate(dot(N,L)*.5+.5)*(r*tint+trt*albedo*albedo*4)+tt*albedo;
}
float4 furStrandColor(FurStrandOut i){
 float3 albedo=furShade(furTexel(i.reference,i.uv).rgb*i.tint,i.t);
 float3 T=normalize(i.tangent),V=normalize(i.view),N=normalize(i.n),L=furSun();
 float tl=dot(T,L);float sinTL=sqrt(saturate(1-tl*tl));
 float wrap=saturate(dot(N,L)*.5+.5);
 float facing=lerp(.3,1,sinTL)*wrap;
 float visibility;
 // The shadow filter turns its taps by one rotation per strand (its seed),
 // not per screen pixel: every pixel of a strand looks up the same root, so
 // the per-pixel rotation was the only thing varying along it - salt-and-
 // pepper on the hair's self-shadow (fur_debug 4 run, 97523671) that
 // crawled as the hair moved across the screen's fixed pattern.
 float3 c=furLight(albedo,i.wp,N,facing,i.shadowSeed,i.root,N,visibility);
 float3 H=normalize(L+V);
 [branch]if(furKind==1){
  // Hair: the shine of the sun (furHairShine), shadowed at the root.
  c+=nativeWorldSunDiffuse.rgb*visibility*furHairShine(T,N,V,L,albedo,i.tint);
  // The same lobes for every point light in reach (torches, lamps, fires):
  // the sun alone left hair without shine in caves and at night. Falloff and
  // strength as furLight's diffuse term; glow-only lights (w < 0) add none.
  uint2 range=nativePointLightRange(i.wp);
  [loop]for(uint k=range.x;k<range.y;++k){
   NativePointLight light=nativePointLights[nativePointLightGrid[k]];
   if(light.color.w<0)continue;
   float3 delta=light.centerInverseRadiusSquared.xyz-i.wp;float d2=dot(delta,delta);
   float reach=1-d2*light.centerInverseRadiusSquared.w;if(reach<=0)continue;
   c+=light.color.rgb*(reach*reach*(light.color.w>0?.65:1))*furHairShine(T,N,V,delta*rsqrt(max(d2,1e-12)),albedo,i.tint);
  }
  return float4(furFog(c,i.p,i.wp),i.alpha);
 }
 // Kajiya-Kay: a primary highlight shifted toward the tip and a tinted secondary one.
 float3 T1=normalize(T+N*.08),T2=normalize(T-N*.12);
 float th1=dot(T1,H),th2=dot(T2,H);
 float s1=pow(sqrt(saturate(1-th1*th1)),90)*.12,s2=pow(sqrt(saturate(1-th2*th2)),24)*.08;
 c+=nativeWorldSunDiffuse.rgb*visibility*wrap*(s1+s2*albedo);
 return float4(furFog(c,i.p,i.wp),i.alpha);
}
)" R"(
// ---------------------------------------------------- DLSS motion variants
// The same colour plus the object motion targets (D3D11 with DLSS only).
float4 furShellPS(FurShellOut i):SV_Target0{float4 c=furShellColor(i);if(c.a<0)discard;return c;}
float4 furFinPS(FurFinOut i):SV_Target0{float4 c=furFinColor(i);if(c.a<0)discard;return c;}
float4 furStrandPS(FurStrandOut i):SV_Target0{return furStrandColor(i);}
FurPixel furShellMotionPS(FurShellOut i){float4 c=furShellColor(i);if(c.a<0)discard;return furMotionPixel(c,i.p,i.objectClip,i.cameraClip,i.p.z,1);}
FurPixel furFinMotionPS(FurFinOut i){float4 c=furFinColor(i);if(c.a<0)discard;return furMotionPixel(c,i.p,i.objectClip,i.cameraClip,i.p.z,1);}
// A strand's motion where it covers at least half the pixel.
FurPixel furStrandMotionPS(FurStrandOut i){float4 c=furStrandColor(i);return furMotionPixel(c,i.p,i.objectClip,i.cameraClip,-1,c.a>=.5?1:0);}
)";

struct FurDrawConstants {
    UINT instance, prepared_base, index_offset, triangles;
    UINT world_state, world_generation[2], node_offset;
    UINT node_base, node_count, pose_base, head_node;
    UINT tail_node, shells, strands_per_triangle, segments;
    float head_sphere[4];
    float density, width, length_scale; UINT kind;
    float viewport[2]; UINT list_base, args_offset;
    UINT hair_axis, axis_padding[3];
    UINT texture_ref[2], fog_enable, motion;
    float body_radius, lod_min, lod_full, interface_record;
    float fog_color[4];
    float emissive[4];
    UINT strand_classes[8];
    UINT depth_hull, cull_mode, sink_slot, order_base;
};
static_assert(sizeof(FurDrawConstants) % 16 == 0, "FurDraw constant layout");

enum FurProgram : unsigned { fur_shell_vs, fur_shell_ps, fur_fin_vs, fur_fin_ps, fur_count_cs, fur_strand_vs,
    fur_strand_ps, fur_shell_motion_ps, fur_fin_motion_ps, fur_strand_motion_ps, fur_order_cs, fur_program_count };
constexpr const char* kFurEntries[fur_program_count][2] = {
    {"furShellVS", "vs_5_0"}, {"furShellPS", "ps_5_0"}, {"furFinVS", "vs_5_0"}, {"furFinPS", "ps_5_0"},
    {"furStrandCountCS", "cs_5_0"}, {"furStrandVS", "vs_5_0"}, {"furStrandPS", "ps_5_0"},
    {"furShellMotionPS", "ps_5_0"}, {"furFinMotionPS", "ps_5_0"}, {"furStrandMotionPS", "ps_5_0"},
    {"furStrandOrderCS", "cs_5_0"}};

constexpr UINT kFurSegments = 4, kHairSegments = 6, kFurStrandSlots = 192, kHairStrandSlots = 256;

// Pipeline state shared by both backends.
D3D11_RASTERIZER_DESC fur_raster_desc() noexcept {
    D3D11_RASTERIZER_DESC raster{}; raster.FillMode = D3D11_FILL_SOLID; raster.CullMode = D3D11_CULL_NONE; raster.DepthClipEnable = TRUE;
    return raster;
}
D3D11_DEPTH_STENCIL_DESC fur_depth_desc(bool write) noexcept {
    D3D11_DEPTH_STENCIL_DESC depth{}; depth.DepthEnable = TRUE; depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    depth.DepthWriteMask = write ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
    return depth;
}
}  // namespace

void append_fur_shader_programs(std::vector<ShaderProgram>& out) {
    static const std::string source = owned_native_shader_source() + kFurShader;
    for (const auto& entry : kFurEntries)
        out.push_back({source, "fur", nullptr, entry[0], entry[1], D3DCOMPILE_OPTIMIZATION_LEVEL3});
}

#if !defined(EID3D11_SHADER_EMBED)
struct Renderer::FurState {
    std::future<std::array<ComPtr<ID3DBlob>, fur_program_count>> compile;
    bool ready = false;
    ComPtr<ID3D11VertexShader> shell_vs, fin_vs, strand_vs;
    ComPtr<ID3D11PixelShader> shell_ps, fin_ps, strand_ps, shell_motion_ps, fin_motion_ps, strand_motion_ps;
    ComPtr<ID3D11ComputeShader> count_cs, order_cs;
    ComPtr<ID3D11RasterizerState> raster, raster_clip;   // raster_clip: scissor (an interface record's clip)
    ComPtr<ID3D11DepthStencilState> depth_write, depth_test;
    // Opaque layers and blended strands; the motion variants also write the
    // DLSS object motion and depth targets (kept where the output alpha is 1).
    ComPtr<ID3D11BlendState> opaque, blend, opaque_motion, blend_motion;
    ComPtr<ID3D11BlendState> no_colour;   // the hair depth hull: depth only
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11Buffer> nodes;
    ComPtr<ID3D11ShaderResourceView> nodes_srv;
    UINT node_capacity = 0;
    // Strand list (uint4 per strand) and indirect arguments (16 bytes per draw).
    ComPtr<ID3D11Buffer> list, args;
    ComPtr<ID3D11ShaderResourceView> list_srv;
    ComPtr<ID3D11UnorderedAccessView> list_uav, args_uav;
    UINT list_capacity = 0, args_capacity = 0;
    // D3D12 backend: the same programs (the host's DXIL table builds their
    // pipelines; the start warms every recorded one) and buffers.
    std::vector<ShaderProgram> programs12;
    D3D12BufferRef nodes12, list12, args12;
    std::vector<FurNodeGpu> node_upload;
    std::unordered_map<const FurProfile*, UINT> node_offsets;
    std::uint32_t reported = 0;   // contract failures already logged (one bit each)
};

// One log line per failure kind; the affected fur is omitted.
static void fur_report(std::uint32_t& reported, unsigned bit, const char* reason, D3D12Frame* d3d12) {
    if (d3d12) d3d12->omit(reason, false);   // named in host.log and F11 (once a name)
    if (reported & (1u << bit)) return;
    reported |= 1u << bit;
    OutputDebugStringA((std::string("EID3D11 ") + reason + "; that fur omitted.\n").c_str());
    bridge_append_probe_event("fur_omitted", reason);   // readable without a debugger (bridge.jsonl)
}

void Renderer::release_fur() noexcept { fur_.reset(); fur_failed_ = false; }

// The fur of this flush's unit records (fur_mode 1 shells + fins, 2 undercoat
// + strands). Called inside flush_native (D3D11) or flush_native_d3d12 after
// its colour batches, while the world vertices (t8), pose nodes (t12),
// instances (t13), lighting, fog, shadow and point-light bindings of the
// flush are still bound. Binds what it adds and restores the pipeline state
// it changes.
void Renderer::draw_fur_records() noexcept {
    const int mode = configured_integer(debug_overlay_.config(), "fur_mode", 0);
    if (mode <= 0) { if (fur_ || fur_failed_) release_fur(); return; }
    if (fur_failed_) return;
    // Interface figures after composition (character pane, equipment screen)
    // are owned unit records with the same profiles and bindings: both
    // backends draw their fur into the interface target their colour pass
    // used (the hair colour record is hidden in the strand modes, so without
    // this pass the outfit screen showed no hair at all on D3D12).
    const bool interface12 = d3d12_ && scene_composited_;
    if (d3d12_ ? !interface12 && (!world_d3d12_bound_ || !world_d3d12_) : !device_ || !context_ || !rtv_ || !dsv_) return;
    try {
        if (!fur_) {
            auto state = std::make_shared<FurState>();
            std::vector<ShaderProgram> programs;
            append_fur_shader_programs(programs);
            if (d3d12_) state->programs12 = std::move(programs);
            else state->compile = std::async(std::launch::async, [programs = std::move(programs)]() {
                std::array<ComPtr<ID3DBlob>, fur_program_count> blobs;
                for (unsigned k = 0; k < fur_program_count; ++k) {
                    ComPtr<ID3DBlob> errors;
                    if (FAILED(compile_shader(programs[k], &blobs[k], &errors))) {
                        if (errors) OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
                        blobs = {};
                        break;
                    }
                }
                return blobs;
            });
            fur_ = std::move(state);
            if (!d3d12_) return;
        }
        auto& fur = *fur_;
#if !EID3D11_FUR_ARMOUR
        fur_report(fur.reported, 10, "fur: fur armour needs the S1 material classes (not in this build)", nullptr);
#endif
        const auto fail = [&](const char* reason) {
            fur_failed_ = true;
            OutputDebugStringA((std::string("EID3D11 fur: ") + reason + "; fur omitted.\n").c_str());
            bridge_append_probe_event("fur_omitted", reason);
            if (d3d12_) d3d12_->omit(reason, false);
        };
        if (!fur.ready) {
            if (d3d12_) {
                if (fur.programs12.size() != fur_program_count) return fail("fur programs unavailable (D3D12)");
                fur.ready = true;
            } else {
                if (fur.compile.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
                const auto blobs = fur.compile.get();
                for (const auto& blob : blobs) if (!blob) return fail("fur shaders unavailable");
                const auto vs = [&](unsigned k, ComPtr<ID3D11VertexShader>& out) {
                    return create_shader(device_.Get(), blobs[k]->GetBufferPointer(), blobs[k]->GetBufferSize(), nullptr, &out); };
                const auto ps = [&](unsigned k, ComPtr<ID3D11PixelShader>& out) {
                    return create_shader(device_.Get(), blobs[k]->GetBufferPointer(), blobs[k]->GetBufferSize(), nullptr, &out); };
                const D3D11_RASTERIZER_DESC raster = fur_raster_desc();
                D3D11_RASTERIZER_DESC raster_clip = raster; raster_clip.ScissorEnable = TRUE;
                const D3D11_DEPTH_STENCIL_DESC write = fur_depth_desc(true), test = fur_depth_desc(false);
                D3D11_BLEND_DESC opaque{}; opaque.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
                D3D11_BLEND_DESC blend = opaque; auto& target = blend.RenderTarget[0];
                target.BlendEnable = TRUE; target.SrcBlend = D3D11_BLEND_SRC_ALPHA; target.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
                target.BlendOp = D3D11_BLEND_OP_ADD; target.SrcBlendAlpha = D3D11_BLEND_ZERO; target.DestBlendAlpha = D3D11_BLEND_ONE;
                target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
                // Motion variants: the motion (RG) and depth (R) targets take the
                // output where its alpha is 1 and keep their value where it is 0.
                D3D11_BLEND_DESC opaque_motion = opaque, blend_motion = blend;
                D3D11_BLEND_DESC no_colour{}; no_colour.IndependentBlendEnable = TRUE;   // every target's write mask 0
                for (D3D11_BLEND_DESC* desc : {&opaque_motion, &blend_motion}) {
                    desc->IndependentBlendEnable = TRUE;
                    for (UINT k = 1; k < 3; ++k) {
                        auto& rt = desc->RenderTarget[k];
                        rt.BlendEnable = TRUE; rt.SrcBlend = rt.SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
                        rt.DestBlend = rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA; rt.BlendOp = rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
                        rt.RenderTargetWriteMask = k == 1 ? D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN
                                                          : D3D11_COLOR_WRITE_ENABLE_RED;
                    }
                }
                D3D11_BUFFER_DESC constants{}; constants.ByteWidth = sizeof(FurDrawConstants); constants.Usage = D3D11_USAGE_DYNAMIC;
                constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER; constants.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                if (FAILED(vs(fur_shell_vs, fur.shell_vs)) || FAILED(ps(fur_shell_ps, fur.shell_ps)) ||
                    FAILED(vs(fur_fin_vs, fur.fin_vs)) || FAILED(ps(fur_fin_ps, fur.fin_ps)) ||
                    FAILED(vs(fur_strand_vs, fur.strand_vs)) || FAILED(ps(fur_strand_ps, fur.strand_ps)) ||
                    FAILED(ps(fur_shell_motion_ps, fur.shell_motion_ps)) || FAILED(ps(fur_fin_motion_ps, fur.fin_motion_ps)) ||
                    FAILED(ps(fur_strand_motion_ps, fur.strand_motion_ps)) ||
                    FAILED(create_shader(device_.Get(), blobs[fur_count_cs]->GetBufferPointer(), blobs[fur_count_cs]->GetBufferSize(), nullptr, &fur.count_cs)) ||
                    FAILED(create_shader(device_.Get(), blobs[fur_order_cs]->GetBufferPointer(), blobs[fur_order_cs]->GetBufferSize(), nullptr, &fur.order_cs)) ||
                    FAILED(device_->CreateRasterizerState(&raster, &fur.raster)) || FAILED(device_->CreateRasterizerState(&raster_clip, &fur.raster_clip)) ||
                    FAILED(device_->CreateDepthStencilState(&write, &fur.depth_write)) ||
                    FAILED(device_->CreateDepthStencilState(&test, &fur.depth_test)) ||
                    FAILED(device_->CreateBlendState(&opaque, &fur.opaque)) || FAILED(device_->CreateBlendState(&blend, &fur.blend)) ||
                    FAILED(device_->CreateBlendState(&opaque_motion, &fur.opaque_motion)) ||
                    FAILED(device_->CreateBlendState(&blend_motion, &fur.blend_motion)) ||
                    FAILED(device_->CreateBlendState(&no_colour, &fur.no_colour)) ||
                    FAILED(device_->CreateBuffer(&constants, nullptr, &fur.constants)))
                    return fail("fur pipeline objects unavailable");
                fur.ready = true;
            }
        }

        // This flush's colour records with a fur profile.
        // strand: this item takes strands (fur_coat_strands / fur_hair_strands),
        // else shells and fins.
        // instances: the shell draw's instance count, fins: the fin draw runs
        // (the CPU LOD selection below).
        struct Item { UINT record; UINT kind; bool strand; bool armour; ID3D11ShaderResourceView* classes; const D3D12Texture* classes12;
            FurDrawConstants c;
            UINT instances = 0; bool fins = true;
            // The record's interface clip (a portrait's frame, a panel's
            // aperture), as its colour batch takes it (ui_clip_rect).
            bool clipped = false; RECT scissor{}; };
        std::vector<Item> items;
        fur.node_upload.clear(); fur.node_offsets.clear();
        for (const auto& selected : native_pending_draws_) {
            if (!(selected.passes & 1u) || selected.record >= native_instance_records_.size()) continue;
            const auto& record = native_instance_records_[selected.record];
            if (!record.fur) continue;
            const auto& command = record.command;
            if (command.hud_composition.opacity < 1.f) continue;
            if (!command.mesh || !command.instance.world_geometry || command.instance.pose_node == UINT_MAX ||
                command.world_allocation == UINT_MAX || record.descriptor.world_state != command.world_allocation) {
                fur_report(fur.reported, 0, "fur: unit record without prepared world vertices", d3d12_);
                continue;
            }
            const auto& profile = *record.fur;
            // Creature fur only (mode 1): a character's hair part keeps its own mesh, no shells.
            if (mode == 1 && fur_strand_hair_record(&profile, record.fur_node_base, command.instance.flags, 3)) continue;
            const UINT kind = fur_strand_hair_record(&profile, record.fur_node_base, command.instance.flags, mode) ? 1u : 0u;
            if (kind == 1 && (!profile.head_sphere_valid || profile.head_node == UINT32_MAX)) {
                fur_report(fur.reported, 1, "fur: hair record without a head part for its scale", d3d12_);
                continue;
            }
            if (fur.node_offsets.emplace(&profile, static_cast<UINT>(fur.node_upload.size())).second) {
                fur.node_upload.insert(fur.node_upload.end(), profile.nodes.begin(), profile.nodes.end());
                // The hair border distances follow the nodes, four floats an element.
                const auto& border = profile.hair_border;
                for (std::size_t k = 0; k < border.size(); k += 4) {
                    float q[4]{1e9f, 1e9f, 1e9f, 1e9f};
                    std::copy(border.begin() + k, border.begin() + (std::min)(k + 4, border.size()), q);
                    FurNodeGpu packed; std::memcpy(&packed, q, sizeof(packed));
                    fur.node_upload.push_back(packed);
                }
            }
            // Fur armour (a profile without a creature coat; its body records):
            // strands in the strand modes, on the atlas' strand-class texels.
            ID3D11ShaderResourceView* classes = nullptr;
            const D3D12Texture* classes12 = nullptr;
            const bool armour = kind == 0 && profile.armour && mode >= 1;   // fur armour strands at both fur levels
            if (armour) {
#if EID3D11_FUR_ARMOUR
                if (material_level() <= 0) { fur_report(fur.reported, 9, "fur: fur armour needs material_quality 1 or more (material classes)", d3d12_); continue; }
                // The atlas' class texture: D3D11 by the bucket's texture, D3D12
                // by the record's own texture value (batches span textures there).
                if (d3d12_) {
                    classes12 = material_class_d3d12(command.material_backing);
                    if (!classes12) continue;   // no fur on this atlas: nothing to grow
                } else {
                    bool strand_texels = false;
                    classes = material_class_srv(native_state_buckets_[command.state_bucket].material_key.texture_backing, strand_texels);
                    if (!classes || !strand_texels) continue;   // no fur on this atlas: nothing to grow
                }
#else
                continue;
#endif
            }
            items.push_back({selected.record, kind, kind == 1 || fur_coat_strands(mode) || armour, armour, classes, classes12, {}, 0u, true});
            items.back().clipped = ui_clip_rect(command.hud_composition, command.camp_clip, items.back().scissor);
        }
        if (items.empty() || fur.node_upload.empty()) return;

        // Temporal upscaler object motion: with the motion targets and last
        // frame's world vertices (D3D12: the world pass's, object_motion_ready_d3d12).
        const bool motion = d3d12_ ? !interface12 && object_motion_ready_d3d12() :
            scene_composite_.dlss() && rtv_.Get() == scene_composite_.target() &&
            dlss_previous_world_srv_ && scene_composite_.object_motion_target() && dlss_motion_constants_current();

        // Per-record constants (shared by every stage of that record).
        const auto& config = debug_overlay_.config();
        // fur_quality 0 (low): half the strands and shells, fewer strand segments,
        // no hair-on-body collision; 1 (high) the full set.
        const bool low = configured_integer(config, "fur_quality", 1) <= 0;
        const UINT shells = static_cast<UINT>(std::clamp(configured_integer(config, "fur_shells", 16), 2, 48)) / (low ? 2u : 1u);
        const float strand_scale = low ? .5f : 1.f;
        const UINT fur_segments = low ? 3u : kFurSegments, hair_segments = low ? 4u : kHairSegments;
        const float length_scale = std::clamp(configured_integer(config, "fur_length", 100), 10, 400) / 100.f;
        const float density_scale = std::clamp(configured_integer(config, "fur_density", 100), 10, 400) / 100.f;
        const D3D11_VIEWPORT viewport = target_viewport();
        bool strands = false, shells_and_fins = false;   // any item of each path
        for (const auto& item : items) (item.strand ? strands : shells_and_fins) = true;
        UINT list_entries = 0, fur_strand_draws = 0;
        for (auto& item : items) {
            const auto& record = native_instance_records_[item.record];
            const auto& command = record.command;
            const auto& profile = *record.fur;
            float coat = 0;
            for (const auto& node : profile.nodes) if (node.role != float(fur_role_hair)) coat = (std::max)(coat, node.length);
            auto& c = item.c;
            c.instance = record.descriptor.instance_index; c.prepared_base = command.world_base;
            c.index_offset = command.mesh->index_offset; c.triangles = command.index_count / 3u;
            c.world_state = record.descriptor.world_state;
            c.world_generation[0] = record.descriptor.world_generation_low; c.world_generation[1] = record.descriptor.world_generation_high;
            c.node_offset = fur.node_offsets[&profile]; c.node_base = record.fur_node_base;
            c.node_count = static_cast<UINT>(profile.nodes.size());
            c.pose_base = command.instance.pose_node - record.fur_node_base;
            c.head_node = profile.head_node; c.tail_node = profile.tail_node;
            c.shells = item.armour ? 0u : item.strand ? (item.kind == 1 ? 1u : (std::max)(low ? 2u : 4u, shells / 3u)) : (std::max)(2u, shells);   // strands: undercoats; armour none
            c.length_scale = length_scale; c.kind = item.kind;   // mode 1: hair takes shells (kind 0)
            c.viewport[0] = viewport.Width; c.viewport[1] = viewport.Height;
            c.density = 300.f * density_scale;   // shells and fins: strand cells per UV unit
            std::copy(profile.head_sphere.begin(), profile.head_sphere.end(), c.head_sphere);
            if (!profile.head_sphere_valid) c.head_sphere[3] = 0;
            c.hair_axis = profile.hair_axis;
            c.texture_ref[0] = command.instance.padding[0]; c.texture_ref[1] = command.instance.padding[1];
            // The record's fog, as its colour batch takes it (native_batch_constants):
            // the bucket's fog state and the stock fog factor in its default
            // specular alpha (prepareNativeVertex).
            {
                const FixedState& fog_state = native_static_frame_ ? *native_static_frame_ : native_state_buckets_[command.state_bucket].state;
                const DWORD fog = fog_state.render[D3DRENDERSTATE_FOGCOLOR];
                c.fog_enable = !scene_composited_ && fog_state.render[D3DRENDERSTATE_FOGENABLE] != 0 ? 1u : 0u;  // never in the interface
                c.fog_color[0] = ((fog >> 16) & 255) / 255.f; c.fog_color[1] = ((fog >> 8) & 255) / 255.f; c.fog_color[2] = (fog & 255) / 255.f;
                c.fog_color[3] = ((command.instance.default_specular >> 24) & 255) / 255.f;
            }
            // The record's cull mode, as its colour batch takes it (apply_state):
            // hair grows only on the faces its mesh draws.
            c.cull_mode = native_state_buckets_[command.state_bucket].state.render[D3DRENDERSTATE_CULLMODE];
            c.motion = motion ? 1u : 0u;
            std::copy(std::begin(command.instance.emissive), std::end(command.instance.emissive), c.emissive);
#if EID3D11_FUR_ARMOUR
            if (item.armour) { const auto bits = material_strand_classes(); std::copy(bits.begin(), bits.end(), c.strand_classes); }
#endif
            // LOD pixel radii at 4K-relative scale. User anchors (2026-10-29): the menu
            // view is a close-up (full detail); the max gameplay zoom-out is the lowest
            // strand level, never mesh only. Route10 put the menu tiger near 150 px and
            // the menu heads below ~30 px, the zoom-32 gameplay camera at ~2.5x smaller
            // (inference from those runs; re-check with the measured cost route).
            // Hair (head radius) full at 22 px, none below 3; coat (creature radius)
            // full at 120 px, none below 15.
            const float scale = viewport.Height / 2160.f;
            c.body_radius = profile.radius;
            c.lod_min = (item.kind == 1 ? 3.f : 15.f) * scale;
            c.lod_full = (item.kind == 1 ? 22.f : 120.f) * scale;
            c.interface_record = scene_composited_ ? 1.f : 0.f;
            // CPU LOD selection for creature coats (the per-unit cost floor: the
            // shell and fin vertex stages run for every triangle and layer and
            // only then drop by the GPU's LOD). The creature's screen radius
            // from its pose root (raised by half the radius toward the body)
            // and the frame's world clip, raised by a margin so it never falls
            // below the GPU's own measure: below no-detail size nothing is
            // drawn, else the shell draw takes at most the layers the GPU can
            // keep; the GPU still picks the exact count, so the look stays.
            // World flushes only (an interface figure is a close-up).
            float pixels = FLT_MAX;
            if (item.kind == 0 && !item.armour && !scene_composited_ && command.pose_slot < native_pose_slots_.size()) {
                const auto& root = native_pose_slots_[command.pose_slot].value.root_to_world;
                const auto& m = native_world_clip_;
                const float p[3]{root[3], root[7], root[11] + profile.radius * .5f};
                const float w = m[12] * p[0] + m[13] * p[1] + m[14] * p[2] + m[15];
                pixels = w > 1e-4f ? 1.25f * profile.radius * std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]) / w * .5f * viewport.Width : 0.f;
            }
            c.sink_slot = list_entries++;   // the unit's trail sink (furStrandCountCS)
            const float lod = std::clamp((pixels - c.lod_min) / (std::max)(c.lod_full - c.lod_min, 1.f), 0.f, 1.f);
            item.instances = pixels < c.lod_min * .5f ? 0u :
                (std::min)(c.shells, static_cast<UINT>(std::ceil(c.shells * (.3f + .7f * lod))));
            item.fins = pixels >= c.lod_min * .5f;
            if (item.strand && item.kind == 0 && pixels <= c.lod_min) continue;   // no coat strands below no-detail size
            if (item.strand && item.kind == 1) {
                // Hair: ribbons 0.35% of the head radius wide, covering the mesh four times over.
                c.density = 4.f * density_scale * strand_scale;
                c.width = .0035f; c.segments = hair_segments; c.strands_per_triangle = kHairStrandSlots;
                c.list_base = list_entries; c.args_offset = 16u * fur_strand_draws++;
                list_entries += c.triangles * (kHairStrandSlots + 1u);   // the list, then one count slot per triangle
            } else if (item.strand) {
                // A strand every seventh of the coat length (more at grazing angles).
                const float spacing = .17f * (std::max)(coat * length_scale, 1e-4f);
                c.density = density_scale * strand_scale / (spacing * spacing);
                c.width = .1f; c.segments = fur_segments; c.strands_per_triangle = kFurStrandSlots;
                c.list_base = list_entries; c.args_offset = 16u * fur_strand_draws++;
                list_entries += c.triangles * (kFurStrandSlots + 1u);
            }
        }
        // Initial indirect arguments: one strip per strand, no strands yet
        // (furStrandCountCS writes the LOD's strip length and adds the strands).
        // (At least one argument slot: every draw runs the count pass, which binds it.)
        const UINT arg_slots = (std::max)(fur_strand_draws, 1u);
        std::vector<UINT> args(std::size_t(arg_slots) * 4u, 0u);
        for (const auto& item : items)
            if (item.c.strands_per_triangle) args[item.c.args_offset / 4u] = 2u * (item.c.segments + 1u);
        // The order pass's table (furStrandOrderCS, one group per strand draw):
        // each draw's list base, triangles, slots and argument offset, after
        // the nodes in the node buffer; every draw's constants name its start.
        const UINT order_base = static_cast<UINT>(fur.node_upload.size());
        for (auto& item : items) {
            item.c.order_base = order_base;
            if (!item.c.strands_per_triangle) continue;
            const UINT entry[4]{item.c.list_base, item.c.triangles, item.c.strands_per_triangle, item.c.args_offset};
            FurNodeGpu packed; std::memcpy(&packed, entry, sizeof(packed));
            fur.node_upload.push_back(packed);
        }
        const UINT order_groups = static_cast<UINT>(fur.node_upload.size()) - order_base;
        const UINT node_count = static_cast<UINT>(fur.node_upload.size());

        if (d3d12_) {
            // ---- D3D12: the same passes through the executor (its slot
            // model, barriers and hazard checks), drawn into the world target.
            const auto report12 = [&](HRESULT hr, const char* what) {
                if (FAILED(hr)) d3d12_->omit(what, true);
                else if (hr == S_FALSE) d3d12_->omit("fur (pipeline not ready)", true);
                return hr == S_OK;
            };
            const D3D12Buffer* no_buffer = nullptr;
            if (FAILED(native_d3d12_buffer(fur.nodes12, UINT64(node_count) * sizeof(FurNodeGpu), sizeof(FurNodeGpu), 0)) ||
                FAILED(d3d12_->write_buffer(*fur.nodes12, 0, fur.node_upload.data(), UINT64(node_count) * sizeof(FurNodeGpu)))) {
                fur_report(fur.reported, 2, "fur node buffer unavailable (D3D12)", d3d12_); return;
            }
            profiler_.add_upload(node_count * sizeof(FurNodeGpu));
            if (FAILED(native_d3d12_buffer(fur.list12, UINT64(list_entries) * 16u, 16u, kD3D12BufferUnordered)) ||
                FAILED(native_d3d12_buffer(fur.args12, UINT64(arg_slots) * 16u, 0, kD3D12BufferUnordered | kD3D12BufferIndirect)) ||
                FAILED(d3d12_->write_buffer(*fur.args12, 0, args.data(), UINT64(args.size()) * sizeof(UINT)))) {
                fur_report(fur.reported, 5, "fur strand list or arguments unavailable (D3D12)", d3d12_); return;
            }
            const auto page12 = [&](const Item& item) -> const D3D12Buffer* {
                const auto& command = native_instance_records_[item.record].command;
                const D3D12Buffer* page = command.geometry_page < native_geometry_pages_.size() ?
                    native_geometry_pages_[command.geometry_page].d3d12.get() : nullptr;
                if (!page) fur_report(fur.reported, 3, "fur: record geometry page unavailable (D3D12)", d3d12_);
                return page;
            };
            const auto& programs = fur.programs12;
            d3d12_->set_resource(15, native12_.world_states.get());
            d3d12_->set_resource(96, fur.nodes12.get());
            {
                // Every draw: its trail sink; strand draws: their strand counts.
                transition_profile_pass("fur_strand_count");
                d3d12_->set_unordered(0, fur.list12.get()); d3d12_->set_unordered(1, fur.args12.get());
                for (const auto& item : items) {
                    const D3D12Buffer* page = page12(item);
                    if (!page) continue;
                    d3d12_->set_resource(1, page);
                    d3d12_->set_resource(98, item.classes12);
                    d3d12_->set_constants(12, &item.c, sizeof(item.c));
                    const UINT groups = item.c.strands_per_triangle ? (item.c.triangles + 63u) / 64u : 1u;
                    if (!report12(d3d12_->dispatch(programs[fur_count_cs], groups, 1, 1), "fur strand counts (dispatch failed)"))
                        continue;
                }
                // Every strand draw's list in triangle order: a stable blend order (furStrandOrderCS).
                if (order_groups)
                    report12(d3d12_->dispatch(programs[fur_order_cs], order_groups, 1, 1), "fur strand order (dispatch failed)");
                d3d12_->set_unordered(0, no_buffer); d3d12_->set_unordered(1, no_buffer);
                d3d12_->set_resource(97, fur.list12.get());
            }
            // The world colour target alone (the colour pass may have bound
            // the shadow history and AO shares beside it); an interface flush
            // keeps the target its colour batches drew into. With a temporal
            // upscaler the object motion targets beside it, as on D3D11: the
            // motion variants, b11 and last frame's world vertices (t46).
            if (motion) {
                auto& up = upscale12_;
                D3D12Texture* const targets[3]{world_d3d12_.get(), up.object_motion.get(), up.object_depth.get()};
                d3d12_->set_targets(targets, 3, world_d3d12_.get());
                d3d12_->set_constants(11, up.motion_constants.data(), UINT(sizeof(up.motion_constants)));
                d3d12_->set_resource(46, up.previous_world.get());
            } else if (!interface12) d3d12_->set_target(world_d3d12_.get(), !hit_test_active_);   // the hit-test target: no world role
            const ShaderProgram& shell_ps = programs[motion ? fur_shell_motion_ps : fur_shell_ps];
            const ShaderProgram& fin_ps = programs[motion ? fur_fin_motion_ps : fur_fin_ps];
            const ShaderProgram& strand_ps = programs[motion ? fur_strand_motion_ps : fur_strand_ps];
            d3d12_->set_sampler(9, world_texture_sampler(D3DTFN_LINEAR, D3DTFG_LINEAR, D3DTFP_LINEAR,
                D3DTADDRESS_WRAP, D3DTADDRESS_WRAP, true, 1u << configured_integer(debug_overlay_.config(), "anisotropy", 3)).description);
            D3D12NativeState opaque;
            opaque.raster = fur_raster_desc(); opaque.depth = fur_depth_desc(true);
            if (motion) opaque.custom_blend.custom = D3D12NativeState::kCustomBlendMotionTargets;
            D3D12NativeState blended = opaque;
            blended.depth = fur_depth_desc(false);
            blended.custom_blend.custom = D3D12NativeState::kCustomBlendOver | (motion ? D3D12NativeState::kCustomBlendMotionTargets : 0u);
            blended.custom_blend.source = D3D11_BLEND_SRC_ALPHA; blended.custom_blend.destination = D3D11_BLEND_INV_SRC_ALPHA;
            blended.custom_blend.source_alpha = D3D11_BLEND_ZERO; blended.custom_blend.destination_alpha = D3D11_BLEND_ONE;
            transition_profile_pass(shells_and_fins ? "fur_shells" : "fur_undercoat");
            for (const auto& item : items) {
                if (!item.instances) continue;
                const D3D12Buffer* page = page12(item);
                if (!page) continue;
                d3d12_->set_resource(1, page);
                d3d12_->set_constants(12, &item.c, sizeof(item.c));
                if (!report12(d3d12_->draw_instanced(programs[fur_shell_vs], shell_ps, opaque, item.clipped ? &item.scissor : nullptr,
                        item.c.triangles * 3u, item.instances, false), "fur shells (draw failed)")) continue;
                profiler_.add_draw(std::uint64_t(item.c.triangles) * item.instances, 1); profiler_.add_batch();
            }
            if (shells_and_fins) {
                transition_profile_pass("fur_fins");
                for (const auto& item : items) {
                    if (item.strand || !item.fins) continue;
                    const D3D12Buffer* page = page12(item);
                    if (!page) continue;
                    d3d12_->set_resource(1, page);
                    d3d12_->set_constants(12, &item.c, sizeof(item.c));
                    if (!report12(d3d12_->draw_instanced(programs[fur_fin_vs], fin_ps, opaque, item.clipped ? &item.scissor : nullptr, 18u, item.c.triangles, false),
                            "fur fins (draw failed)")) continue;
                    profiler_.add_draw(std::uint64_t(item.c.triangles) * 6u, 1); profiler_.add_batch();
                }
            }
            if (strands) {
                transition_profile_pass("fur_strands");
                for (UINT kind = 0; kind < 2; ++kind)
                    for (const auto& item : items) {
                        if (item.kind != kind || !item.c.strands_per_triangle) continue;
                        const D3D12Buffer* page = page12(item);
                        if (!page) continue;
                        d3d12_->set_resource(1, page);
                        d3d12_->set_resource(98, item.classes12);
                        d3d12_->set_constants(12, &item.c, sizeof(item.c));
                        if (!report12(d3d12_->draw_indirect(programs[fur_strand_vs], strand_ps, blended, item.clipped ? &item.scissor : nullptr, *fur.args12,
                                item.c.args_offset, nullptr, nullptr, true), "fur strands (draw failed)")) continue;
                        profiler_.add_draw(std::uint64_t(item.c.triangles), 1);
                        profiler_.add_batch();
                    }
                // As on D3D11: blended hair strands do not write depth.
                // Publish their outer hull for later depth consumers.
                transition_profile_pass("fur_depth_hull");
                if (!interface12) d3d12_->set_target(world_d3d12_.get(), !hit_test_active_);
                D3D12NativeState hull_state = opaque;
                hull_state.custom_blend.custom = D3D12NativeState::kCustomBlendOver;
                hull_state.custom_blend.write_mask = 0;
                for (const auto& item : items) {
                    if (item.kind != 1 || !item.c.strands_per_triangle) continue;
                    const D3D12Buffer* page = page12(item);
                    if (!page) continue;
                    FurDrawConstants hull = item.c; hull.depth_hull = 1;
                    d3d12_->set_resource(1, page);
                    d3d12_->set_constants(12, &hull, sizeof(hull));
                    if (!report12(d3d12_->draw_instanced(programs[fur_shell_vs], ShaderProgram{}, hull_state,
                            item.clipped ? &item.scissor : nullptr, hull.triangles * 3u, 1, false), "fur hair depth hull (draw failed)")) continue;
                    profiler_.add_draw(hull.triangles, 1); profiler_.add_batch();
                }
            }
            d3d12_->set_resource(1, no_buffer); d3d12_->set_resource(15, no_buffer);
            d3d12_->set_resource(96, no_buffer); d3d12_->set_resource(97, no_buffer);
            d3d12_->set_resource(98, static_cast<const D3D12Texture*>(nullptr));
            if (motion) {
                // b11 is also the ground cover and critter frame slot: zero after (draw_object_motion_d3d12).
                const std::array<float, 20> zero{};
                d3d12_->set_constants(11, zero.data(), UINT(sizeof(zero)));
                d3d12_->set_resource(46, no_buffer);
            }
            if (!interface12) bind_world_targets_d3d12();
            return;
        }

        // ---- D3D11.
        const auto sampling = world_texture_sampler(D3DTFN_LINEAR, D3DTFG_LINEAR, D3DTFP_LINEAR,
            D3DTADDRESS_WRAP, D3DTADDRESS_WRAP, true, 1u << configured_integer(debug_overlay_.config(), "anisotropy", 3));
        auto& sampler = sampler_states_[sampling.key];
        if (!sampler && FAILED(device_->CreateSamplerState(&sampling.description, &sampler))) return fail("fur material sampler unavailable");
        fur.sampler = sampler;
        const auto structured = [&](UINT count, UINT stride, UINT bind, UINT cpu, D3D11_USAGE usage, ComPtr<ID3D11Buffer>& buffer,
                                    ComPtr<ID3D11ShaderResourceView>& srv, ComPtr<ID3D11UnorderedAccessView>* uav) {
            D3D11_BUFFER_DESC description{}; description.ByteWidth = count * stride; description.Usage = usage;
            description.BindFlags = bind; description.CPUAccessFlags = cpu;
            description.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; description.StructureByteStride = stride;
            buffer.Reset(); srv.Reset(); if (uav) uav->Reset();
            D3D11_SHADER_RESOURCE_VIEW_DESC view{}; view.Format = DXGI_FORMAT_UNKNOWN;
            view.ViewDimension = D3D11_SRV_DIMENSION_BUFFER; view.Buffer.NumElements = count;
            HRESULT hr = device_->CreateBuffer(&description, nullptr, &buffer);
            if (SUCCEEDED(hr)) hr = device_->CreateShaderResourceView(buffer.Get(), &view, &srv);
            if (SUCCEEDED(hr) && uav) {
                D3D11_UNORDERED_ACCESS_VIEW_DESC access{}; access.Format = DXGI_FORMAT_UNKNOWN;
                access.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; access.Buffer.NumElements = count;
                hr = device_->CreateUnorderedAccessView(buffer.Get(), &access, uav->GetAddressOf());
            }
            return hr;
        };
        if (node_count > fur.node_capacity) {
            const UINT capacity = (std::max)(node_count, fur.node_capacity * 2u);
            if (FAILED(structured(capacity, sizeof(FurNodeGpu), D3D11_BIND_SHADER_RESOURCE, D3D11_CPU_ACCESS_WRITE,
                    D3D11_USAGE_DYNAMIC, fur.nodes, fur.nodes_srv, nullptr))) {
                fur.node_capacity = 0; fur_report(fur.reported, 2, "fur node buffer unavailable", d3d12_); return;
            }
            fur.node_capacity = capacity;
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context_->Map(fur.nodes.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
        std::memcpy(mapped.pData, fur.node_upload.data(), node_count * sizeof(FurNodeGpu));
        context_->Unmap(fur.nodes.Get(), 0);
        profiler_.add_upload(node_count * sizeof(FurNodeGpu));

        // Strand list (every draw's trail sink, then the strands) and arguments, sized by demand.
        {
            if (list_entries > fur.list_capacity) {
                const UINT capacity = (std::max)(list_entries, fur.list_capacity + fur.list_capacity / 2u);
                if (FAILED(structured(capacity, 16, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, 0, D3D11_USAGE_DEFAULT,
                        fur.list, fur.list_srv, &fur.list_uav))) {
                    fur.list_capacity = 0; fur_report(fur.reported, 5, "fur strand list unavailable", d3d12_); return;
                }
                fur.list_capacity = capacity;
            }
            if (arg_slots > fur.args_capacity) {
                const UINT capacity = (std::max)(arg_slots, 8u);
                D3D11_BUFFER_DESC description{}; description.ByteWidth = capacity * 16u; description.Usage = D3D11_USAGE_DEFAULT;
                description.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
                description.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
                D3D11_UNORDERED_ACCESS_VIEW_DESC access{}; access.Format = DXGI_FORMAT_R32_TYPELESS;
                access.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; access.Buffer.NumElements = capacity * 4u;
                access.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
                fur.args.Reset(); fur.args_uav.Reset(); fur.args_capacity = 0;
                if (FAILED(device_->CreateBuffer(&description, nullptr, &fur.args)) ||
                    FAILED(device_->CreateUnorderedAccessView(fur.args.Get(), &access, &fur.args_uav))) {
                    fur_report(fur.reported, 6, "fur strand arguments unavailable", d3d12_); return;
                }
                fur.args_capacity = capacity;
            }
            const D3D11_BOX box{0, 0, 0, arg_slots * 16u, 1, 1};
            context_->UpdateSubresource(fur.args.Get(), 0, &box, args.data(), 0, 0);
        }

        // Previous pipeline state, restored at the end.
        ComPtr<ID3D11BlendState> old_blend; float old_factor[4]{}; UINT old_mask = 0;
        ComPtr<ID3D11DepthStencilState> old_depth; UINT old_stencil = 0;
        ComPtr<ID3D11RasterizerState> old_raster;
        ComPtr<ID3D11VertexShader> old_vs; ComPtr<ID3D11PixelShader> old_ps;
        context_->OMGetBlendState(&old_blend, old_factor, &old_mask);
        context_->OMGetDepthStencilState(&old_depth, &old_stencil);
        context_->RSGetState(&old_raster);
        UINT old_scissor_count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE; D3D11_RECT old_scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
        context_->RSGetScissorRects(&old_scissor_count, old_scissors);
        const auto clip = [&](const Item& item) {
            if (!item.clipped) { context_->RSSetState(fur.raster.Get()); return; }
            context_->RSSetState(fur.raster_clip.Get()); context_->RSSetScissorRects(1, &item.scissor);
        };
        context_->VSGetShader(&old_vs, nullptr, nullptr); context_->PSGetShader(&old_ps, nullptr, nullptr);

        const auto set_constants = [&](const FurDrawConstants& c) {
            if (FAILED(context_->Map(fur.constants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
            std::memcpy(mapped.pData, &c, sizeof(c)); context_->Unmap(fur.constants.Get(), 0); return true;
        };
        const auto record_views = [&](const Item& item, ID3D11ShaderResourceView*& page, ID3D11ShaderResourceView*& texture,
                                      ID3D11ShaderResourceView*& texture_array) {
            const auto& command = native_instance_records_[item.record].command;
            page = native_geometry_page_srv(command.geometry_page);
            const auto& bucket = native_state_buckets_[command.state_bucket];
            texture = bucket.texture_array ? nullptr : bucket.texture_srv.Get();
            texture_array = bucket.texture_array ? bucket.texture_srv.Get() : nullptr;
            if (!page || !bucket.texture_srv) { fur_report(fur.reported, 3, "fur: record geometry page or texture unavailable", d3d12_); return false; }
            return true;
        };

        // The world states were the build pass's UAV (u6); they are read as t15.
        ID3D11UnorderedAccessView* no_uavs[2]{};
        context_->CSSetUnorderedAccessViews(6, 1, no_uavs, nullptr);
        ID3D11ShaderResourceView* none = nullptr;

        // Every draw's trail sink, and the strand counts of the strand draws
        // (compute), before any draw reads the list.
        {
            transition_profile_pass("fur_strand_count");
            context_->CSSetShader(fur.count_cs.Get(), nullptr, 0);
            context_->CSSetConstantBuffers(12, 1, fur.constants.GetAddressOf());
            context_->CSSetShaderResources(8, 1, native_world_vertex_srv_.GetAddressOf());
            context_->CSSetShaderResources(15, 1, native_world_state_srv_.GetAddressOf());
            context_->CSSetShaderResources(96, 1, fur.nodes_srv.GetAddressOf());
            // The trail sink's inputs: the frame values (t127) and the trail
            // field (t36; none after composition, as the flush binds it).
            ID3D11ShaderResourceView* frame = native_terrain_frame_srv_.Get();
            ID3D11ShaderResourceView* snow = scene_composited_ ? nullptr : snow_terrain_srv_.Get();
            context_->CSSetShaderResources(PointShadowTextureLayout::terrain_frame_srv, 1, &frame);
            context_->CSSetShaderResources(36, 1, &snow);
            ID3D11UnorderedAccessView* uavs[2]{fur.list_uav.Get(), fur.args_uav.Get()};
            context_->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
            for (const auto& item : items) {
                ID3D11ShaderResourceView *page, *texture, *texture_array;
                if (!record_views(item, page, texture, texture_array) || !set_constants(item.c)) continue;
                context_->CSSetShaderResources(1, 1, &page);
                context_->CSSetShaderResources(98, 1, &item.classes);
                context_->CSSetShader(fur.count_cs.Get(), nullptr, 0);
                context_->Dispatch(item.c.strands_per_triangle ? (item.c.triangles + 63u) / 64u : 1u, 1, 1);
            }
            // Every strand draw's list in triangle order: a stable blend order (furStrandOrderCS).
            if (order_groups) {
                context_->CSSetShader(fur.order_cs.Get(), nullptr, 0);
                context_->Dispatch(order_groups, 1, 1);
            }
            context_->CSSetUnorderedAccessViews(0, 2, no_uavs, nullptr);
            context_->CSSetShaderResources(1, 1, &none); context_->CSSetShaderResources(8, 1, &none);
            context_->CSSetShaderResources(15, 1, &none); context_->CSSetShaderResources(96, 1, &none);
            context_->CSSetShaderResources(98, 1, &none); context_->CSSetShaderResources(36, 1, &none);
            context_->CSSetShaderResources(PointShadowTextureLayout::terrain_frame_srv, 1, &none);
            ID3D11Buffer* no_buffer = nullptr; context_->CSSetConstantBuffers(12, 1, &no_buffer);
            context_->CSSetShader(nullptr, nullptr, 0);
        }

        // With DLSS the opaque layers and the denser strand pixels also write
        // the object motion targets (scene_composite.h cameraMotionPS).
        if (motion) {
            ID3D11RenderTargetView* targets[3]{rtv_.Get(), scene_composite_.object_motion_target(), scene_composite_.object_depth_target()};
            context_->OMSetRenderTargets(3, targets, dsv_.Get());
            context_->VSSetShaderResources(46, 1, dlss_previous_world_srv_.GetAddressOf());
            context_->VSSetConstantBuffers(11, 1, dlss_motion_constants_.GetAddressOf());
            context_->PSSetConstantBuffers(11, 1, dlss_motion_constants_.GetAddressOf());
        } else context_->OMSetRenderTargets(1, rtv_.GetAddressOf(), dsv_.Get());
        context_->IASetInputLayout(nullptr); context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->RSSetState(fur.raster.Get());
        context_->VSSetShaderResources(15, 1, native_world_state_srv_.GetAddressOf());
        context_->VSSetShaderResources(96, 1, fur.nodes_srv.GetAddressOf());
        context_->VSSetShaderResources(97, 1, fur.list_srv.GetAddressOf());
        context_->VSSetConstantBuffers(12, 1, fur.constants.GetAddressOf());
        context_->PSSetConstantBuffers(12, 1, fur.constants.GetAddressOf());
        context_->PSSetSamplers(9, 1, fur.sampler.GetAddressOf());

        // Opaque layers first: shells (fur records in both modes, hair in mode
        // 1), then mode 1's silhouette fins.
        context_->OMSetBlendState((motion ? fur.opaque_motion : fur.opaque).Get(), nullptr, 0xffffffffu);
        context_->OMSetDepthStencilState(fur.depth_write.Get(), 0);
        transition_profile_pass(shells_and_fins ? "fur_shells" : "fur_undercoat");
        context_->VSSetShader(fur.shell_vs.Get(), nullptr, 0); context_->PSSetShader((motion ? fur.shell_motion_ps : fur.shell_ps).Get(), nullptr, 0);
        for (const auto& item : items) {
            if (!item.instances) continue;
            ID3D11ShaderResourceView *page, *texture, *texture_array;
            if (!record_views(item, page, texture, texture_array) || !set_constants(item.c)) continue;
            context_->VSSetShaderResources(1, 1, &page);
            context_->PSSetShaderResources(0, 1, &texture); context_->PSSetShaderResources(6, 1, &texture_array);
            profiler_.sub_scope(item.kind ? "hair_undercoat" : "coat_shells");
            clip(item); context_->DrawInstanced(item.c.triangles * 3u, item.instances, 0, 0);
            profiler_.add_draw(std::uint64_t(item.c.triangles) * item.instances, 1); profiler_.add_batch();
            profiler_.add_sub_draw(1, std::uint64_t(item.c.triangles) * item.instances);
        }
        if (shells_and_fins) {
            transition_profile_pass("fur_fins");
            context_->VSSetShader(fur.fin_vs.Get(), nullptr, 0); context_->PSSetShader((motion ? fur.fin_motion_ps : fur.fin_ps).Get(), nullptr, 0);
            for (const auto& item : items) {
                if (item.strand || !item.fins) continue;
                ID3D11ShaderResourceView *page, *texture, *texture_array;
                if (!record_views(item, page, texture, texture_array) || !set_constants(item.c)) continue;
                context_->VSSetShaderResources(1, 1, &page);
                context_->PSSetShaderResources(0, 1, &texture); context_->PSSetShaderResources(6, 1, &texture_array);
                clip(item); context_->DrawInstanced(18u, item.c.triangles, 0, 0);
                profiler_.add_draw(std::uint64_t(item.c.triangles) * 6u, 1); profiler_.add_batch();
            }
        }
        if (strands) {
            // Blended strands over the coat and the hair base layer.
            transition_profile_pass("fur_strands");
            context_->OMSetBlendState((motion ? fur.blend_motion : fur.blend).Get(), nullptr, 0xffffffffu);
            context_->OMSetDepthStencilState(fur.depth_test.Get(), 0);
            context_->PSSetShader((motion ? fur.strand_motion_ps : fur.strand_ps).Get(), nullptr, 0);
            context_->VSSetShader(fur.strand_vs.Get(), nullptr, 0);
            context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);   // one strip per strand
            // Coat strands, then hair strands, each in its own GPU sub-scope.
            for (UINT kind = 0; kind < 2; ++kind)
                for (const auto& item : items) {
                    if (item.kind != kind || !item.c.strands_per_triangle) continue;
                    ID3D11ShaderResourceView *page, *texture, *texture_array;
                    if (!record_views(item, page, texture, texture_array) || !set_constants(item.c)) continue;
                    profiler_.sub_scope(kind ? "hair" : item.armour ? "armour" : "coat");
                    context_->VSSetShaderResources(1, 1, &page);
                    context_->VSSetShaderResources(98, 1, &item.classes);
                    context_->PSSetShaderResources(0, 1, &texture); context_->PSSetShaderResources(6, 1, &texture_array);
                    clip(item); context_->DrawInstancedIndirect(fur.args.Get(), item.c.args_offset);
                    profiler_.add_draw(std::uint64_t(item.c.triangles), 1);
                    profiler_.add_sub_draw(1, item.c.triangles);
                    profiler_.add_batch();
                }
            context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            // Hair depth hull: the hair mesh just outside the outer strand layer
            // writes depth only, after the strands are blended. Strands write no
            // depth, so later passes that tell foreground by depth read the bed
            // behind a hair fringe: water refraction pulled the strands' colour
            // into the water (ghost hair behind the character; user, b5d8769e).
            transition_profile_pass("fur_depth_hull");
            context_->OMSetBlendState(fur.no_colour.Get(), nullptr, 0xffffffffu);
            context_->OMSetDepthStencilState(fur.depth_write.Get(), 0);
            context_->VSSetShader(fur.shell_vs.Get(), nullptr, 0); context_->PSSetShader(nullptr, nullptr, 0);
            for (const auto& item : items) {
                if (item.kind != 1 || !item.c.strands_per_triangle) continue;
                ID3D11ShaderResourceView *page, *texture, *texture_array;
                FurDrawConstants hull = item.c; hull.depth_hull = 1;
                if (!record_views(item, page, texture, texture_array) || !set_constants(hull)) continue;
                context_->VSSetShaderResources(1, 1, &page);
                clip(item); context_->DrawInstanced(item.c.triangles * 3u, 1, 0, 0);
                profiler_.add_draw(item.c.triangles, 1); profiler_.add_batch();
            }
        }
        context_->VSSetShaderResources(1, 1, &none); context_->VSSetShaderResources(15, 1, &none);
        context_->VSSetShaderResources(96, 1, &none); context_->VSSetShaderResources(97, 1, &none);
        context_->VSSetShaderResources(98, 1, &none);
        context_->PSSetShaderResources(0, 1, &none); context_->PSSetShaderResources(6, 1, &none);
        ID3D11Buffer* no_buffer = nullptr;
        context_->VSSetConstantBuffers(12, 1, &no_buffer); context_->PSSetConstantBuffers(12, 1, &no_buffer);
        if (motion) {
            context_->VSSetShaderResources(46, 1, &none);
            context_->VSSetConstantBuffers(11, 1, &no_buffer); context_->PSSetConstantBuffers(11, 1, &no_buffer);
            context_->OMSetRenderTargets(1, rtv_.GetAddressOf(), dsv_.Get());
        }
        ID3D11SamplerState* no_sampler = nullptr; context_->PSSetSamplers(9, 1, &no_sampler);
        context_->OMSetBlendState(old_blend.Get(), old_factor, old_mask);
        context_->OMSetDepthStencilState(old_depth.Get(), old_stencil);
        context_->RSSetState(old_raster.Get()); context_->RSSetScissorRects(old_scissor_count, old_scissors);
        context_->VSSetShader(old_vs.Get(), nullptr, 0); context_->PSSetShader(old_ps.Get(), nullptr, 0);
    } catch (const std::bad_alloc&) {
        if (fur_) fur_report(fur_->reported, 4, "fur: out of memory", d3d12_);
    }
}

#endif // !EID3D11_SHADER_EMBED
}  // namespace owned
