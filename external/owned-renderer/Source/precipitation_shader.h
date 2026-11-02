#pragma once
// Owned precipitation shader (renderer_precipitation.cpp). Stateless and
// GPU-expanded: every drop is a hash of its world tile, its index and its
// fall cycle, so nothing persists between frames. The field is one
// world-periodic tile repeated over the window that covers the view (the
// ambient particle layout), so drops stay anchored to the world while the
// camera moves. Each drop falls through a fixed world period wrapped into the window above pcFall.z and
// disappears at the ground under it; game seconds drive the fall (pause
// freezes, fast speed speeds it up). Beyond the drops, a depth-aware veil
// with falling sheets carries the distant rain (precipitationVeilPS).
namespace owned::precipitation {
constexpr char kPrecipitationShader[] = R"(
cbuffer Precipitation:register(b0){
 row_major float4x4 pcClip;
 float4 pcEye;      // xyz eye, w pixels per unit at distance 1
 float4 pcWindow;   // xy window origin, z tile columns, w tile period
 float4 pcFall;     // x drops per tile, y fall speed, z window bottom height, w world fall period (constant)
 float4 pcMotion;   // xy horizontal drift (m/s), z streak exposure (s), w game seconds
 float4 pcShape;    // x world width, y minimum pixels, z kind (1 rain, 2 snow), w opacity
 float4 pcColor;    // rgb lit colour, a strength
 float4 pcGlint;    // rgb forward-scatter light, a sway (snow, metres)
 float4 pcSunDir;   // xyz unit direction to the light, w far fade distance
 float4 pcThin;     // x distance where drops start thinning, y where they reach z (kept share)
 float4 pcVeil;     // x veil opacity at distance, y veil distance scale (m), z sheet contrast, w sheet speed
 float4 pcVeilColor;// rgb veil colour, a sheet horizontal offset (camera heading)
 float4 pcScreen;   // xy 1/target size, z aspect (w/h), w sheet stretch
 // Shelter map (straight-down depth of the static objects): map x and y are
 // pcShelter0/1.x*x+.y*y+.z; a surface at depth d lies at pcShelter2.y -
 // d*pcShelter2.x (slab top minus depth times span); pcShelter0.w 1 while a
 // baked map is bound.
 float4 pcShelter0;float4 pcShelter1;float4 pcShelter2;
 float4 pcSplash;   // x splash slots per tile, y splash life (s), z splash height (m), w opacity
 float4 pcSnow;     // blowing snow/sand: x share of flakes, y speed over the drift, z height (m), w life (s)
 float4 pcSheet;    // x sideways sheet scroll (sand), y share of sand sheets (puffs), z sheet size (m), w sheet opacity
 float4 pcDrift;    // xy wind drift of the falling field (metres, integrated on game time by the CPU)
 float4 pcSand;     // x share of the sand sheets lying on the ground, y share of grains that billow at walls and cliffs, z billow rise (m), w lee march steps
 float4 pcBand;     // x upright band weight, y ground sheet weight (0..1, the view-angle budget), zw band distance fade start/end (m)
 float4 pcSand2;    // x ground streak strength (veil), y canyon protection (1 on), z drop index offset (the near sand grain draw)
 float4 pcFogRange;float4 pcFogControl;float4 pcFogColor;float4 pcDepth;
 float4 pcFogSeam;float4 pcFogCurve;float4 pcFogSpanLo;float4 pcFogSpanHi; // neighbour seam fog (neighbour_fog.h)
 float4 pcSnowLight;// snow material: rgb ambient (tinted), w 1 while the scene's lamps light the flakes
 float4 pcSnowSun;  // rgb sun or moon light (tinted), w sparkle strength
};
// The world's lamps (the native shader's records, b5/t17/t18, bound by the
// precipitation draw while pcSnowLight.w is set): falling snow takes the
// surfaces' lamp law per flake.
cbuffer PcLamps:register(b5){uint pcLampCount;uint2 pcLampGridSize;uint pcLampGlowCount;float2 pcLampGridOrigin;float pcLampGridInverseCell;float pcLampGridTail;};
struct PcLamp{float4 centerInverseRadiusSquared;float4 color;float4 shadowCenter;float4 shadowSlots;};
StructuredBuffer<PcLamp> pcLamps:register(t18);
StructuredBuffer<uint> pcLampGrid:register(t17);
Texture2D<float> pcHeight:register(t0);
Texture2D<float> pcSceneDepth:register(t2);
Texture2D<float> pcShelter:register(t3);
Texture2D<float> pcBase:register(t1);   // ground under any liquid (water where pcHeight is above it)
// The game's own weather atlas (rainsnow.mmp, CWeather's texture): its third
// row holds the four snow crystals DrawSnow used (v .5-.75, one per column).
Texture2D<float4> pcFlakes:register(t4);
SamplerState pcFlakeSampler:register(s0);
struct PcOut{float4 position:SV_Position;float4 color:COLOR0;float2 uv:TEXCOORD0;float viewW:TEXCOORD1;float border:TEXCOORD2;float softness:TEXCOORD3;
 float4 flake:TEXCOORD4;}; // snow crystal: cos, sin of its turn, atlas column, mip bias (w < 0: not a crystal)
uint pcHash(uint x){x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;return x;}
float pcRandom(uint x){return (pcHash(x)&0xffffffu)/16777216.0;}
float pcGround(float2 p){
 uint w,h;pcHeight.GetDimensions(w,h);
 float2 c=clamp(p,0,float2(w-1,h-1));int2 i=int2(floor(c));int2 j=min(i+1,int2(w-1,h-1));float2 f=c-i;
 return lerp(lerp(pcHeight.Load(int3(i,0)),pcHeight.Load(int3(j.x,i.y,0)),f.x),
             lerp(pcHeight.Load(int3(i.x,j.y,0)),pcHeight.Load(int3(j,0)),f.x),f.y);
}
// Height of the highest static surface above p (roof, bridge, canopy), or
// -1e9 where the shelter map holds nothing or does not reach.
float pcShelterTop(float2 p){
 [branch]if(pcShelter0.w<.5)return -1e9;
 float2 n=float2(dot(pcShelter0.xy,p)+pcShelter0.z,dot(pcShelter1.xy,p)+pcShelter1.z);
 if(any(abs(n)>=1))return -1e9;
 uint w,h;pcShelter.GetDimensions(w,h);
 int2 t=min(int2(float2(n.x*.5+.5,.5-.5*n.y)*float2(w,h)),int2(w-1,h-1));
 float d=pcShelter.Load(int3(t,0));
 return d>=1?-1e9:pcShelter2.y-d*pcShelter2.x;
}
float pcSceneW(float d){
 float denom=d-pcDepth.x,w=abs(denom)>1e-7?pcDepth.y/denom:1e9;
 return w<0?1e9:w;
}
// Border fog, haze and draw fade folded into a colour for premultiplied
// output (pcFinish applied to rgb*a, divided by a); position as SV_Position.
float3 pcFogged(float3 rgb,float border,float4 position){
 float f=nativeFogFactor(border,position);
 rgb=rgb*f+borderFogColor(position,pcFogColor.rgb)*(1-f);
 float3 added=0;float t=nativeAtmosphere(added,position,pcFogColor.rgb);
 return rgb*t+added;
}
// A snow crystal lit as a material, once per flake (all four corners
// alike): a thin two-sided ice plate that falls roughly level and tumbles.
// The world ambient, the sun or moon through the cloud shadow at its
// tilted normal, the scene's lamps by the surfaces' law (1 - d^2/r^2,
// squared, eased over the last fifth of the radius, game lamps at .65), a
// GGX glint off the plate (F0 .02, roughness .35: an occasional sparkle,
// not a highlight on every flake) and the forward scatter toward the light.
float3 pcSnowLit(float3 p,float3 v,uint seed,float toward){
 float3 n=normalize(float3(float2(pcRandom(seed^0x0b4f3a71u),pcRandom(seed^0x5bd1e995u))*1.6-.8,1));
 n=dot(n,v)>0?-n:n; // the face toward the eye (v runs from the eye)
 float3 l=pcSunDir.xyz,e=-v;
 float sunVisible=skyCloudVisibility(p);
 float nl=abs(dot(n,l));
 float3 h=normalize(l+e);float nh=saturate(dot(n,h)),a2=.35*.35*.35*.35;
 float d=nh*nh*(a2-1)+1;
 float glint=a2/(3.14159*d*d)*(.02+.98*pow(1-saturate(dot(e,h)),5))*saturate(dot(n,l))*.25;
 float3 light=pcSnowLight.rgb+pcSnowSun.rgb*sunVisible*nl;
 [branch]if(pcSnowLight.w>.5){
  float2 cell=floor((p.xy-pcLampGridOrigin)*pcLampGridInverseCell);
  [branch]if(all(cell>=0)&&all(cell<(float2)pcLampGridSize)){
   uint index=(uint)cell.y*pcLampGridSize.x+(uint)cell.x;
   uint first=pcLampGrid[index],last=pcLampGrid[index+1];
   [loop]for(uint k=first;k<last;++k){
    PcLamp lamp=pcLamps[pcLampGrid[k]];
    if(lamp.color.w<0)continue; // emissive surfaces light nothing here
    float3 delta=lamp.centerInverseRadiusSquared.xyz-p;
    float reach=1-dot(delta,delta)*lamp.centerInverseRadiusSquared.w;
    if(reach<=0)continue;
    float facing=.35+.65*abs(dot(n,normalize(delta)));
    light+=lamp.color.rgb*(reach*reach*smoothstep(0,.36,reach)*facing*(lamp.color.w>0?.65:1));
   }
  }
 }
 float albedo=.85+.15*pcRandom(seed^0x165667b1u);
 return light*albedo+pcSnowSun.rgb*sunVisible*(glint*pcSnowSun.w+.4*pow(toward,6));
}
)" R"(
PcOut precipitationVS(uint vertex:SV_VertexID,uint instance:SV_InstanceID){
 PcOut o=(PcOut)0;
 uint perTile=(uint)pcFall.x,columns=(uint)pcWindow.z;
 uint tile=instance/perTile,drop=instance-tile*perTile;
 float period=pcWindow.w;
 float2 tileOrigin=pcWindow.xy+float2(tile%columns,tile/columns)*period;
 int2 cell=int2(floor(tileOrigin/period+.5));
 // Early outs, coherent per tile: consecutive instances share a tile, so a
 // wave skips together. Divergent random culls kept almost every wave on the
 // full path (03430630: 2.5x the grains cost about 1.85 ms). The thinning
 // keeps each tile's first share of drops (the drop's rank, constant over
 // time like its seed; positions inside a tile are random, so it shows no
 // pattern), and a sand tile wholly outside the view frustum stops at once.
 float rank=(drop+.5)/perTile;
 {
  float tileNear=max(length(tileOrigin+period*.5-pcEye.xy)-period,0);
  float keepNear=lerp(1,pcThin.z,smoothstep(pcThin.x,pcThin.y,tileNear));
  bool outside=rank>keepNear+.09;
  [branch]if(!outside&&pcShape.z>2.5){
   // The sand column: ground to about 6 m above it, bands reaching 9 m.
   float4 q=float4(tileOrigin+period*.5,pcGround(tileOrigin+period*.5)+3,1),rx=pcClip[0],ry=pcClip[1],rw=pcClip[3];
   float reach=period*.71+12;
   outside=dot(rw+rx,q)<-reach*length((rw+rx).xyz)||dot(rw-rx,q)<-reach*length((rw-rx).xyz)||
           dot(rw+ry,q)<-reach*length((rw+ry).xyz)||dot(rw-ry,q)<-reach*length((rw-ry).xyz);
  }
  if(outside){o.position=float4(0,0,-1,1);o.color=0;return o;}
 }
 uint seed=pcHash(pcHash(uint(cell.x)*73856093u^uint(cell.y)*19349663u)^(drop+(uint)pcSand2.z)*0x9e3779b9u);
 bool snow=pcShape.z>1.5;
 float speed=pcFall.y*(snow?.75+.5*pcRandom(seed^0x51ed27u):.85+.3*pcRandom(seed^0x51ed27u));
 // World-anchored fall: every drop descends at its own constant speed
 // through a fixed world period (pcFall.w), wrapped into the window above
 // pcFall.z. A camera or focus change moves only the wrap, never the drops
 // (run 7: a window-relative span rescaled every drop's phase as the view
 // moved, and drops seemed to slow, reverse and oscillate). Wind moves the
 // whole field by the CPU-integrated drift, so gusts never jerk it.
 float lift=-(pcRandom(seed)+pcMotion.w*speed/pcFall.w)-pcFall.z/pcFall.w;
 float cycle=floor(lift),f=lift-cycle;
 uint cycleSeed=pcHash(seed^uint(int(cycle))*0x85ebca6bu);
 float3 p=float3(tileOrigin+float2(pcRandom(cycleSeed),pcRandom(cycleSeed^0x68bc21ebu))*period+pcDrift.xy,
                 pcFall.z+f*pcFall.w);
 float3 velocity=float3(pcMotion.xy,-speed);
 float drifterFade=1,runA=0;
 // Blowing snow (blizzard): a share of the flakes skims the ground with the
 // wind instead of falling, fading in and out over its short run.
 bool drifter=snow&&pcRandom(seed^0x3d4d51c3u)<pcSnow.x;
 if(drifter){
  float run=pcRandom(seed^0x1b56c4e9u)+pcMotion.w/pcSnow.w;
  float runCycle=floor(run),a=run-runCycle;
  cycleSeed=pcHash(seed^uint(int(runCycle))*0x27d4eb2du);
  // Each run has its own pace and a slight sideways heading, so the stream
  // never moves as one sheet.
  float pace=.7+.6*pcRandom(cycleSeed^0x2545f491u);
  float2 heading=pcMotion.xy+float2(-pcMotion.y,pcMotion.x)*(pcRandom(cycleSeed^0x5851f42du)-.5)*.25;
  float2 xy=tileOrigin+pcDrift.xy+float2(pcRandom(cycleSeed),pcRandom(cycleSeed^0x68bc21ebu))*period+heading*pcSnow.y*pace*a*pcSnow.w;
  float lift=pcRandom(cycleSeed^0x165667b1u);
  float groundHere=pcGround(xy);
  p=float3(xy,groundHere+.05+pcSnow.z*lift*lift*(.6+.4*sin(a*3.14159)));
  velocity=float3(pcMotion.xy*pcSnow.y,-.3);drifterFade=sin(a*3.14159);runA=a;
  // Blown snow skims open, gentle ground only: it fades out on slopes from
  // about 17 to 31 degrees, so a drift never climbs a cliff face (it rode the
  // height field straight up walls, 1102 user report). Sand keeps its own
  // band slope test and billows at walls.
  [branch]if(pcShape.z<2.5){
   float2 rise=float2(pcGround(xy+float2(.6,0))-pcGround(xy-float2(.6,0)),pcGround(xy+float2(0,.6))-pcGround(xy-float2(0,.6)))/1.2;
   drifterFade*=1-smoothstep(.3,.6,length(rise));
  }
 }
 // Billows: sand that the wind drives against a building wall (the shelter
 // map's top just downwind) or up a steep slope (the ground rising downwind)
 // curls upward, grows and fades over its run.
 bool billow=false;
 if(drifter&&pcShape.z>2.5&&pcRandom(seed^0x7a3c5e91u)<pcSand.y){
  float2 w=length(pcMotion.xy)>1e-3?normalize(pcMotion.xy):float2(1,0);
  float here=pcGround(p.xy);
  float wall=max(max(pcShelterTop(p.xy+w*.8),pcShelterTop(p.xy+w*1.6))-here-1.,pcGround(p.xy+w*1.2)-here-.6);
  float hit=saturate(wall/1.5);
  if(hit>0){
   billow=true;
   // A plume: held at the wall, it climbs fast and curves over, spreading
   // sideways and curling back upwind at the top.
   float rise=1-(1-runA)*(1-runA);
   float2 side=float2(-w.y,w.x)*(pcRandom(cycleSeed^0x3c6ef372u)*2-1);
   p.z+=pcSand.z*hit*rise;
   p.xy-=pcMotion.xy*pcSnow.y*pcSnow.w*runA*.75*hit;
   p.xy+=(side*1.5-w*.7)*runA*runA*hit*pcSand.z*.45;
   velocity=float3(pcMotion.xy*.15,3);
  }
 }
 if(snow&&!drifter){
  // Flakes tumble: two slow sways per flake around the wind drift.
  float a=pcRandom(cycleSeed^0x2545f491u)*6.2832,b=pcRandom(cycleSeed^0x9e37u)*6.2832;
  float t=pcMotion.w;
  p.xy+=pcGlint.a*float2(sin(t*1.3+a)+.5*sin(t*2.9+b),cos(t*1.1+b)+.5*cos(t*2.3+a));
 }
 // Sand sheets: a share of the sand is wide soft dust bands blowing along
 // the wind just above the ground, the grains streaking through them.
 // The share of candidates is constant per preset; the view-angle budget
 // and the distance range fade a band's opacity, so nothing pops while the
 // camera pans or tilts (run p2: a hard distance cut and a moving share).
 bool puff=drifter&&!billow&&pcRandom(seed^0x9e3779b1u)<pcSheet.y;
 // Some sheets lie flat on the ground, so the storm also reads from above
 // (a steep camera sees upright bands edge-on).
 bool groundSheet=puff&&pcRandom(seed^0x4b1d2f6fu)<pcSand.x;
 if(puff)p.z=pcGround(p.xy)+(groundSheet?.12+.2*pcRandom(cycleSeed^0x2c1b3c6du):.15+.6*pcRandom(cycleSeed^0x2c1b3c6du)*pcSnow.z/3);
 // The view angle picks how many band candidates show (a soft per-band
 // threshold, so a tilt fades bands in and out instead of popping), and
 // anything outside the view frustum (the sand window is a box around the
 // view) stops here, before the lee and shelter taps.
 float bandWeight=groundSheet?pcBand.y:pcBand.x,bandPick=pcRandom(seed^0x6a09e667u);
 {
  float4 q=float4(p,1),rx=pcClip[0],ry=pcClip[1],rw=pcClip[3];
  float reach=puff?9:1.5;
  bool outside=(puff&&(bandPick>bandWeight+.08||length(p.xy-pcEye.xy)>pcBand.w))||
   dot(rw+rx,q)<-reach*length((rw+rx).xyz)||dot(rw-rx,q)<-reach*length((rw-rx).xyz)||
   dot(rw+ry,q)<-reach*length((rw+ry).xyz)||dot(rw-ry,q)<-reach*length((rw-ry).xyz);
  if(outside){o.position=float4(0,0,-1,1);o.color=0;return o;}
 }
 // Wind shadow: walls, buildings and cliffs upwind keep a lee behind them,
 // like a shadow cast along the wind. A short march upwind over the shelter
 // map and the ground finds an occluder taller than the line rising 1 m per
 // 6 m from this point; the sand thins there and recovers softly farther
 // downwind (about six times the obstacle's height). Billows (windward) stay.
 float lee=1;
 [branch]if(drifter&&pcShape.z>2.5&&!billow&&pcSand.w>0){
  float2 w=length(pcMotion.xy)>1e-3?normalize(pcMotion.xy):float2(1,0);
  float base=pcGround(p.xy),occluded=0;
  [loop]for(uint march=1;march<=(uint)pcSand.w;++march){
   float d=1.5*pow(float(march),1.6);float2 q=p.xy-w*d;
   float h=max(pcShelterTop(q),pcGround(q))-base;
   occluded=max(occluded,saturate((h-d/9)/.6));
  }
  // A street protects whatever the wind's direction: walls on both sides
  // across the wind (3, 6 or 9 m out) taller than 2.5 m enclose it. Inside,
  // the bands and billows stop and a share of grains still flies through
  // (user: an almost protected street with only airborne grains; a 12 m+
  // main street escaped the 3/6 m taps on 9b32ff4a).
  [branch]if(pcSand2.y>0){
   float2 side=float2(-w.y,w.x);
   float left=max(pcShelterTop(p.xy+side*3),pcShelterTop(p.xy+side*6))-base;
   float right=max(pcShelterTop(p.xy-side*3),pcShelterTop(p.xy-side*6))-base;
   left=max(left,pcShelterTop(p.xy+side*9)-base);right=max(right,pcShelterTop(p.xy-side*9)-base);
   occluded=max(occluded,saturate((min(left,right)-2.5)/2)*(puff?1:.65));
  }
  lee=1-.95*occluded;
 }
 float ground=pcGround(p.xy);
 float3 view=p-pcEye.xyz;float dist=length(view);
 float fade=saturate((dist-.8)/1.5)*saturate((pcSunDir.w-dist)/(.3*pcSunDir.w));
 if(p.z<ground||p.z<pcShelterTop(p.xy)-.05)fade=0;
 fade*=drifterFade*lee;
 // Far drops thin out where the veil carries the rain.
 float keep=lerp(1,pcThin.z,smoothstep(pcThin.x,pcThin.y,dist));
 float keepPick=rank;
 fade*=smoothstep(keepPick-.08,keepPick+.08,keep); // a soft per-particle threshold: no popping
 if(puff)fade*=(1-smoothstep(pcBand.z,pcBand.w,length(p.xy-pcEye.xy)))*smoothstep(bandPick-.08,bandPick+.08,bandWeight);
 float3 axis,across;float halfLength;
 if(puff){
  // A dust band lies along the wind, its width across facing the camera.
  float2 w=length(pcMotion.xy)>1e-3?normalize(pcMotion.xy):float2(1,0);
  halfLength=pcSheet.z*(groundSheet?1+1.2*pcRandom(cycleSeed^0x7f4a7c15u):.6+.8*pcRandom(cycleSeed^0x7f4a7c15u));
  // Bands follow the ground's slope along and across the wind, and thin out
  // on steep ground: on a dune face a level band read as a soft streak
  // painted down the slope (03430630/3fd330a6 sand2_mid).
  float2 sw=float2(-w.y,w.x);
  float rise=(pcGround(p.xy+w*halfLength)-pcGround(p.xy-w*halfLength))/(2*halfLength);
  float riseSide=(pcGround(p.xy+sw*1.5)-pcGround(p.xy-sw*1.5))/3;
  fade*=1-smoothstep(.12,.3,length(float2(rise,riseSide)));
  axis=normalize(float3(w,rise));across=cross(axis,view);float l=length(across);
  across=l>1e-6?across/l:float3(0,0,1);
  if(groundSheet)across=normalize(float3(sw,riseSide));
  // A band reaching under a roof or into a building is not drawn.
  if(pcShelterTop(p.xy+axis.xy*halfLength)>p.z||pcShelterTop(p.xy-axis.xy*halfLength)>p.z)fade=0;
 } else if(snow){
  // Flakes always face the camera (the quad lies across the view ray); in
  // strong wind they lean along their motion as seen on screen (the motion's
  // part across the view ray), so a flake falling toward or away from the eye
  // keeps its full crystal. The lean axis was the 3D motion itself: looking
  // along the snowfall the quad turned edge-on into a thin strip (1102 user
  // note). A view straight down takes the screen's x as its reference.
  float3 forward=view/max(dist,1e-4);
  float3 right=cross(forward,float3(0,0,1));
  right=dot(right,right)>1e-6?normalize(right):float3(1,0,0);
  axis=cross(right,forward);across=right;
  float stretch=saturate((length(velocity.xy)-3)/8);
  // Sand grains keep their 3D motion trail (fine flecks, not crystals).
  float3 drift=pcShape.z>2.5?velocity:velocity-forward*dot(velocity,forward);float driftLength=length(drift);
  float3 side=cross(drift,forward);float l=length(side);
  if(stretch>.05&&driftLength>1e-3&&l>1e-6){axis=drift/driftLength;across=side/l;}
  // Sand grains: fine flecks with a short motion trail (9b32ff4a user note:
  // "very coarse and blurry"), flakes a longer lean.
  halfLength=stretch*(pcShape.z>2.5?.004:.012)*driftLength;
 } else {
  axis=normalize(velocity);across=cross(axis,view);float l=length(across);
  across=l>1e-6?across/l:float3(1,0,0);
  halfLength=.5*length(velocity)*pcMotion.z*(.6+.8*pcRandom(cycleSeed^0x27d4eb2fu));
 }
 float4 centre=mul(pcClip,float4(p,1));
 float size=pcShape.x*(.6+.8*pcRandom(cycleSeed^0x7f4a7c15u));
 if(puff)size=halfLength*(groundSheet?.6:.45);
 if(billow)size=.15+.45*runA;
 // Blown snow: fine powder, half a falling flake, its lean at most
 // twice its own size (a world size, so it shrinks with distance like the
 // flakes and never draws as a long line up close).
 bool powder=drifter&&pcShape.z<2.5;
 if(powder){size*=.5;halfLength=min(halfLength,2*size);}
 // Drops right in front of the eye are out of focus: wider and softer.
 // (Sand grains and blown snow stay crisp: no near blur.)
 float nearBlur=pcShape.z>2.5||powder?1:1+2.5*saturate((6-dist)/5);
 size*=nearBlur;
 float shown=max(size,pcShape.y*max(centre.w,.05)/max(pcEye.w,1e-3));
 fade*=size/shown/sqrt(nearBlur);
 if(centre.w<=.05)fade=0;
 float u=(vertex>>1)&1u,side=(vertex&1u)?1:-1;
 float along=puff?(u*2-1)*halfLength:snow?(u*2-1)*(shown*.5+halfLength):(u*2-1)*halfLength;
 float3 corner=p+axis*along+across*side*shown*.5;
 if(fade<=0)corner=p; // a hidden drop collapses to a point
 o.position=mul(pcClip,float4(corner,1));o.viewW=o.position.w;
 float fog=1;
 if(pcFogControl.y!=0&&pcFogControl.x>0)
  fog=1-borderFogDensity(p.xy,pcFogRange,pcFogControl.x,pcFogSeam,pcFogCurve,pcFogSpanLo,pcFogSpanHi);
 o.border=fog;
 // Drops between the eye and the light glow (forward scattering).
 float toward=saturate(dot(view/max(dist,1e-3),pcSunDir.xyz));
 // Dust (sand) is lit like the scene: its colour already holds the world
 // light and only a weak forward glow (ce6849f4: the lifted, sun-glinted
 // bands read as white surf at night).
 float3 lit=pcColor.rgb*(.8+.4*pcRandom(cycleSeed^0x165667b1u))+pcGlint.rgb*(pcShape.z>2.5?.15:snow?.25:1)*pow(toward,6);
 [branch]if(snow&&pcShape.z<2.5&&fade>0)lit=pcSnowLit(p,view/max(dist,1e-3),cycleSeed,toward);
 float opacity=billow?pcSheet.w*.5*(1-runA)*saturate(runA*6):puff?pcSheet.w:pcShape.w*(powder?.6:1);
 // The atmosphere per corner (border fog, haze and draw fade folded into the
 // colour): per pixel it sampled the fog volume under every band's
 // overdraw. Exact for premultiplied output: the PS only scales by alpha.
 [branch]if(fade>0&&o.position.w>.05){
  float4 at=float4((o.position.xy/o.position.w*float2(.5,-.5)+.5)/fogScreen.xy,o.position.z/o.position.w,o.position.w);
  lit=pcFogged(lit,fog,at);
 }
 o.color=float4(lit,opacity*fade*(snow?1:.7+.6*pcRandom(cycleSeed^0x3c6ef372u)));
 o.uv=float2(u,side);
 // Negative softness marks a dust band; its magnitude seeds its breakup.
 // Billows draw as fine dust wisps (the band breakup), not soft grains.
 o.softness=puff||billow?-(1+7*pcRandom(cycleSeed^0x85ebca77u)):nearBlur;
 // Snow crystals (not blowing snow or sand): one of the game's four flake
 // shapes, turning slowly and tumbling (its width breathes as it rolls);
 // out-of-focus near flakes sample a blurrier mip and fade.
 o.flake=float4(1,0,0,groundSheet?-2:-1); // w -2: a ground sheet (close soft depth)
 if(pcShape.z>1.5&&pcShape.z<2.5&&!drifter){
  float t=pcMotion.w,spin=(pcRandom(cycleSeed^0x6c8e9cf5u)-.5)*3,turn=pcRandom(cycleSeed^0x5f356495u)*6.2832+t*spin;
  float roll=.6+.4*abs(cos(t*(.8+1.6*pcRandom(cycleSeed^0x2f8b2c1du))+turn));
  // z: atlas column plus the roll (.6-1, a slight tumble: the crystal never
  // squashes toward a line) in its fraction.
  o.flake=float4(cos(turn),sin(turn),floor(pcRandom(cycleSeed^0x4cf5ad43u)*3.999)+roll*.999,log2(nearBlur)*1.6);
 }
 return o;
}
)" R"(
float4 pcFinish(float4 c,float border,float4 position){
 float f=nativeFogFactor(border,position);
 c.rgb=c.rgb*f+borderFogColor(position,pcFogColor.rgb)*(c.a*(1-f));
 float3 added=0;float t=nativeAtmosphere(added,position,pcFogColor.rgb);
 c.rgb=c.rgb*t+added*c.a;
 return c;
}
float pcValue(float2 p){
 float2 i=floor(p),f=p-i;f=f*f*(3-2*f);
 uint2 c=uint2(int2(i));
 float a=pcRandom(c.x*0x8da6b343u^c.y*0xd8163841u),b=pcRandom((c.x+1)*0x8da6b343u^c.y*0xd8163841u);
 float d=pcRandom(c.x*0x8da6b343u^(c.y+1)*0xd8163841u),e=pcRandom((c.x+1)*0x8da6b343u^(c.y+1)*0xd8163841u);
 return lerp(lerp(a,b,f.x),lerp(d,e,f.x),f.y);
}
float4 precipitationPS(PcOut i):SV_Target{
 float a=i.color.a;
 [branch]if(i.softness<0){
  // Dust band: a soft lens broken into wisps that stream along it.
  float2 d=float2(i.uv.x*2-1,i.uv.y);float lens=saturate(1-dot(d,d));
  // The faint rim and corners (about a third of the quad) skip the noise.
  if(lens*lens*a<.004)discard;
  float s=-i.softness,t=pcMotion.w*.8;
  // Domain-warped wisps in three decorrelated octaves, each band with its
  // own stretch and drift: no two bands share a pattern, and the fine octave
  // keeps the edges crisp up close.
  float2 q=float2(i.uv.x*(4+2*frac(s*.37))-t*(.8+.4*frac(s*.71))+s*13.1,i.uv.y*(1.6+.8*frac(s*.53))+s*7.3);
  float2 warp=float2(pcValue(q*.5+s),pcValue(q*.5+s+5.2))-.5;
  float wisps=pcValue(q+warp*1.6)*.6+pcValue(q*2.7+warp*2.2+float2(s*3.1-t*.7,s))*.28+pcValue(q*7.9+s*1.7)*.12;
  a*=lens*lens*smoothstep(.32,.72,wisps);
  a*=saturate((pcSceneW(pcSceneDepth.Load(int3(i.position.xy,0)))-i.viewW)/(i.flake.w<-1.5?.3:1.2));
  if(a<=1e-4)discard;
  return float4(i.color.rgb*a,a); // fogged per corner (precipitationVS)
 }
 [branch]if(i.flake.w>=0){
  // The quad's square, turned and squashed into the flake's atlas cell.
  float2 d=float2(i.uv.x*2-1,i.uv.y);
  float column=floor(i.flake.z),roll=max(frac(i.flake.z),.3);
  float2 q=float2(dot(d,float2(i.flake.x,-i.flake.y))/roll,dot(d,float2(i.flake.y,i.flake.x)));
  if(any(abs(q)>1))discard;
  float2 cell=float2((column+q.x*.5+.5)*.25,.5+(q.y*.5+.5)*.25);
  a*=pcFlakes.SampleLevel(pcFlakeSampler,cell,i.flake.w).a/(1+i.flake.w);
 } else if(pcShape.z>1.5){
  // Snow: a soft round flake; sand: a crisp fleck with a hard edge.
  float2 d=float2(i.uv.x*2-1,i.uv.y);float r=saturate(1-dot(d,d));a*=pcShape.z>2.5?smoothstep(0,.35,r):r*sqrt(r);
 } else {
  // A streak: bright core, soft edge (softer when out of focus), brightest
  // near the head and tapering to the tail.
  float edge=saturate(1-abs(i.uv.y));float core=pow(edge,lerp(1.2,2.5,saturate(i.softness-1)));
  a*=core*smoothstep(0,.55,i.uv.x)*(1-.4*smoothstep(.8,1,i.uv.x));
 }
 a*=saturate((pcSceneW(pcSceneDepth.Load(int3(i.position.xy,0)))-i.viewW)/pcDepth.z);
 if(a<=1e-4)discard;
 return float4(i.color.rgb*a,a);
}
)" R"(
// Ground splashes: hashed impact slots per world tile, each replaying a short
// crown on game seconds at the ground, or on the roof or canopy top the
// shelter map holds above it. None on water (the liquid shader's rain rings
// take those impacts) and none beyond the focus band.
float pcLoad(Texture2D<float> t,float2 p){
 uint w,h;t.GetDimensions(w,h);
 float2 c=clamp(p,0,float2(w-1,h-1));int2 i=int2(floor(c));int2 j=min(i+1,int2(w-1,h-1));float2 f=c-i;
 return lerp(lerp(t.Load(int3(i,0)),t.Load(int3(j.x,i.y,0)),f.x),lerp(t.Load(int3(i.x,j.y,0)),t.Load(int3(j,0)),f.x),f.y);
}
PcOut precipitationSplashVS(uint vertex:SV_VertexID,uint instance:SV_InstanceID){
 PcOut o=(PcOut)0;
 uint perTile=(uint)pcSplash.x,columns=(uint)pcWindow.z;
 uint tile=instance/perTile,slot=instance-tile*perTile;
 float period=pcWindow.w;
 float2 tileOrigin=pcWindow.xy+float2(tile%columns,tile/columns)*period;
 int2 cell=int2(floor(tileOrigin/period+.5));
 uint seed=pcHash(pcHash(uint(cell.x)*83492791u^uint(cell.y)*2971215073u)^slot*0x5bd1e995u);
 float life=pcSplash.y*(.8+.4*pcRandom(seed^0x68e31da4u));
 float progress=pcRandom(seed)+pcMotion.w/life;
 float cycle=floor(progress),age=progress-cycle;
 uint cycleSeed=pcHash(seed^uint(int(cycle))*0xc2b2ae35u);
 float2 xy=tileOrigin+pcDrift.xy+float2(pcRandom(cycleSeed),pcRandom(cycleSeed^0x68bc21ebu))*period;
 float surface=pcGround(xy),ground=pcLoad(pcBase,xy);
 float roof=pcShelterTop(xy);
 float3 p=float3(xy,max(surface,roof));
 float3 view=p-pcEye.xyz;float dist=length(view);
 float fade=saturate((dist-1.5)/2)*(1-smoothstep(pcThin.x*.9,pcThin.y*.8,dist));
 if(roof<surface+.1&&surface>ground+.05)fade=0; // open water: rings instead
 float3 right=cross(view,float3(0,0,1));float l=length(right);right=l>1e-5?right/l:float3(1,0,0);
 float size=pcSplash.z*(.7+.6*pcRandom(cycleSeed^0x7f4a7c15u));
 float4 centre=mul(pcClip,float4(p,1));
 float shown=max(size,2.5*max(centre.w,.05)/max(pcEye.w,1e-3));
 fade*=size/shown;
 if(centre.w<=.05)fade=0;
 float u=(vertex>>1)&1u,side=(vertex&1u)?1:-1;
 float3 corner=p+right*side*shown*.6+float3(0,0,u*shown);
 if(fade<=0)corner=p;
 o.position=mul(pcClip,float4(corner,1));o.viewW=o.position.w;
 float fog=1;
 if(pcFogControl.y!=0&&pcFogControl.x>0)
  fog=1-borderFogDensity(p.xy,pcFogRange,pcFogControl.x,pcFogSeam,pcFogCurve,pcFogSpanLo,pcFogSpanHi);
 o.border=fog;
 o.color=float4(pcColor.rgb*1.25,pcSplash.w*fade);
 o.uv=float2(side,u);o.softness=age;
 return o;
}
float4 precipitationSplashPS(PcOut i):SV_Target{
 float age=i.softness,x=abs(i.uv.x),y=i.uv.y;
 // Crown: a thin wall rising and spreading, its rim breaking into droplets
 // that arc out and fall; a bright foot where the drop struck.
 float r=.25+.5*age,h=.6*(1-age);
 float wall=exp(-pow((x-r)/.09,2))*saturate(1-y/max(h,1e-3))*step(y,h);
 float dx=.3+.6*age,dy=2.2*age*(1-age)+.04;
 float drop=saturate(1-length(float2(x-dx,y-dy))/.07);
 float foot=saturate(1-y/.06)*saturate(1-x/(r+.1));
 float a=i.color.a*saturate(wall+drop*drop*1.5+.6*foot)*smoothstep(0,.06,age)*pow(1-age,1.4);
 a*=saturate((pcSceneW(pcSceneDepth.Load(int3(i.position.xy,0)))-i.viewW+.15)/pcDepth.z);
 if(a<=1e-4)discard;
 return pcFinish(float4(i.color.rgb*a,a),i.border,i.position);
}
// Distant rain: a veil over the scene by its depth, carrying slow falling
// sheets (value noise stretched along the fall, three depth layers that
// scroll at different speeds and follow the camera heading). The sky (depth 1)
// takes the full veil.
float4 precipitationVeilVS(uint id:SV_VertexID):SV_Position{
 float2 p=id==0?float2(-1,-1):(id==1?float2(-1,3):float2(3,-1));
 return float4(p,0,1);
}
float pcNoise(float2 p){
 float2 i=floor(p),f=p-i;f=f*f*(3-2*f);
 uint2 c=uint2(int2(i));
 float a=pcRandom(c.x*0x8da6b343u^c.y*0xd8163841u),b=pcRandom((c.x+1)*0x8da6b343u^c.y*0xd8163841u);
 float d=pcRandom(c.x*0x8da6b343u^(c.y+1)*0xd8163841u),e=pcRandom((c.x+1)*0x8da6b343u^(c.y+1)*0xd8163841u);
 return lerp(lerp(a,b,f.x),lerp(d,e,f.x),f.y);
}
// Sand dust (world space, game seconds). No rigid scroll: each pattern
// drifts downwind for pcDustLife seconds while it fades in and out, and a
// freshly seeded one takes over half a cycle later (two phases, variance
// kept), so structures form, travel and dissolve (4f227551 user note: the
// far overlay panned like one texture). A domain warp bends every pattern,
// the fine layer travels faster than the coarse one, and broad gust pulses
// sweep through the field with the wind. Far away only the coarse layer and
// the gusts remain: slow, soft density change. The rain and snow veil keep
// their screen-space sheets.
static const float pcDustLife=8.;
float pcDustSeed(float cycle){return frac(cycle*.6180339)*97.;}
float pcDustPhase(float2 p,float2 shift,float seed){
 float2 q=(p-shift)/32.+seed*float2(1.713,.971);
 q+=(pcNoise(q*.43+seed*.37)-.5)*1.3;
 return pcNoise(q);
}
float pcDustBlend(float n0,float n1,float w0,float w1){
 return ((n0-.5)*w0+(n1-.5)*w1)*rsqrt(max(w0*w0+w1*w1,1e-4))+.5;
}
float4 precipitationVeilPS(float4 position:SV_Position):SV_Target{
 float d=pcSceneDepth.Load(int3(position.xy,0));
 float w=pcSceneW(d);
 float amount=pcVeil.x*(1-exp(-max(w-4,0)/pcVeil.y));
 [branch]if(amount<=1e-3)discard;
 float3 colour=pcVeilColor.rgb;
 [branch]if(pcSand2.x<=0){
  float2 uv=position.xy*pcScreen.xy;uv.x=uv.x*pcScreen.z+pcVeilColor.a;
  float t=pcMotion.w*pcVeil.w,sheet=0;
  [unroll]for(uint k=0;k<2;++k){
   // Nearer layers are larger and fall faster; the slant follows the wind.
   float scale=4.+5.*k,fall=t*(1.4-.3*k);
   float2 q=float2((uv.x*scale+pcSheet.x*t*(1.4-.3*k))*pcScreen.w+uv.y*pcMotion.x*.08,uv.y*scale+fall);
   // A slow domain warp breaks the value-noise lattice so the sheets never
   // show a repeating cell pattern.
   q+=(float2(pcNoise(q*.31+k*5.1+t*.05),pcNoise(q*.31+k*9.7-t*.04))-.5)*1.4;
   sheet+=pcNoise(q+k*17.3)*(.6-.1*k);
  }
  float a=amount*lerp(1,saturate(sheet*1.6-.25),pcVeil.z);
  return float4(colour*a,a);
 }
 // Sand. The scene point (the sky and anything farther: 300 m along the ray).
 float2 suv=position.xy*fogScreen.xy;
 float3 world=fogUnproject(suv,d);
 float3 ray=fogUnproject(suv,.5)-fogEye.xyz;ray*=rsqrt(max(dot(ray,ray),1e-12));
 float dist=d<1?min(length(world-fogEye.xyz),300.):300.;
 float3 dustPoint=fogEye.xyz+ray*dist;
 // Streaks lie on near-level ground only: on a wall or cliff the world-XY
 // pattern stretched into moving vertical stripes (9b32ff4a user note).
 // The surface normal comes from the reconstructed position's derivatives
 // (outside the depth branch, where they are defined).
 float3 facing=cross(ddx(world),ddy(world));
 float flatness=dot(facing,facing)>1e-12?abs(facing.z)*rsqrt(dot(facing,facing)):0;
 float tt=pcMotion.w,ph=tt/pcDustLife,f0=frac(ph),f1=frac(ph+.5);
 float s0=pcDustSeed(floor(ph)),s1=pcDustSeed(floor(ph+.5)+.5);
 float w0=1-abs(2*f0-1),w1=1-w0;
 float2 wind=pcMotion.xy;
 float gust=pcNoise((dustPoint.xy-wind*tt*.9)/160.+31.7);
 float a=amount*(.7+.6*gust);
 // Coarse layer at under half the wind's speed, fine layer (near only) at
 // its full speed; none on Low (no sheets).
 [branch]if(pcVeil.z>0){
  float field=pcDustBlend(pcDustPhase(dustPoint.xy,wind*.45*f0*pcDustLife,s0),
                          pcDustPhase(dustPoint.xy,wind*.45*f1*pcDustLife,s1),w0,w1);
  float fineWeight=saturate(1-dist/140);
  [branch]if(fineWeight>0){
   float fine=pcDustBlend(pcNoise((dustPoint.xy-wind*f0*pcDustLife)/9.+s0*float2(.51,.33)),
                          pcNoise((dustPoint.xy-wind*f1*pcDustLife)/9.+s1*float2(.51,.33)+11.7),w0,w1);
   field=lerp(field,field*.6+fine*.4,fineWeight);
  }
  a*=lerp(1,saturate(field*1.6-.25),pcVeil.z);
 }
 // World-anchored streaks of blowing sand on the ground (read from above
 // and on to the horizon), the same two-phase drift along the wind, and the
 // near haze and streaks cut inside wind shadows and narrow streets (the
 // scene point's own lee test).
 [branch]if(d<1){
  float2 wd=length(wind)>1e-3?normalize(wind):float2(1,0),wside=float2(-wd.y,wd.x);
  float along=dot(world.xy,wd),across=dot(world.xy,wside),run=length(wind)*.6*pcDustLife;
  float n=pcDustBlend(pcNoise(float2((along-run*f0)*.16,across*1.5)+s0*float2(.29,.83)),
                      pcNoise(float2((along-run*f1)*.16,across*1.5)+s1*float2(.29,.83)+7.3),w0,w1);
  float streak=smoothstep(.48,.8,n)*pcSand2.x*saturate((60-max(w-140,0))/60)*smoothstep(.94,.99,flatness)*(.7+.6*gust);
  float base=world.z,occluded=0;
  [unroll]for(uint m=1;m<=3;++m){
   float dd=2.5*m*m;float h=pcShelterTop(world.xy-wd*dd)-base;
   occluded=max(occluded,saturate((h-dd/9)/.6));
  }
  float left=max(pcShelterTop(world.xy+wside*3),pcShelterTop(world.xy+wside*6))-base;
  float right=max(pcShelterTop(world.xy-wside*3),pcShelterTop(world.xy-wside*6))-base;
  left=max(left,pcShelterTop(world.xy+wside*9)-base);right=max(right,pcShelterTop(world.xy-wside*9)-base);
  occluded=max(occluded,saturate((min(left,right)-2.5)/2));
  float protect=1-.9*occluded*saturate(1-w/60);
  // Streaks are the same lit dust as the haze, never brighter than it
  // (ce6849f4: lifted streaks read as white smears from above).
  a=a*lerp(1,protect,saturate(1-w/80))+streak*protect*(1-a);
 }
 return float4(colour*a,a);
}
)";

// Soft shelter map for the snow and wet surfaces (filter_native_rain_shelter,
// eight separable passes per bake). In: the raw straight-down depth (0 slab
// top, 1 empty) and the bake's foliage mask (1 where the top caster is
// foliage). Out: r closed depth (only foliage is closed: an opening in a
// roof or a roofless building stays open), g distance inside the closed footprint
// (to its silhouette, /4 m), b crown flag (closed foliage a real share
// within 2 m, spread over 2 m) packed with the leaf density (shelterCrown),
// a the local crown radius (largest inside distance within 3 m, /4 m).
// Every pass is one row or column window: a group of 256 texels loads its
// line segment plus a 64-texel apron into groupshared memory once, and each
// texel's window reads only that (one global read and write per texel and
// pass; the brute-force version read every window texel from memory, 1-4 ms).
// sfMode: x pass, yz direction (1,0 rows, 0,1 columns), w side. sfRadius: x
// closing radius, y gap reach, z distance reach, w texels per metre.
constexpr char kShelterFilterShader[] = R"(
cbuffer ShelterFilter:register(b0){int4 sfMode;float4 sfRadius;};
Texture2D<float> sfRaw:register(t0);
Texture2D<float4> sfSource:register(t1);
Texture2D<float> sfFoliage:register(t2);
RWTexture2D<unorm float4> sfOut:register(u0);
#define SF_LINE 256
#define SF_APRON 64
groupshared float3 sfCache[SF_LINE+2*SF_APRON]; // the channels this pass reads
// The channels a pass's windows read: closing depth; distance rows: depth,
// filled gaps and closed foliage; distance columns: row run and the two row
// shares; radius: inside distance, then its row max, with the packed crown.
float3 sfWindowChannels(int mode,float4 v){
 return mode<=3?float3(v.r,0,0):mode==4?v.rgb:mode==5?v.gba:mode==6?float3(v.gb,0):float3(v.ab,0);
}
[numthreads(SF_LINE,1,1)]
void shelterFilterCS(uint3 groupId:SV_GroupID,uint3 thread:SV_GroupThreadID){
 int side=sfMode.w,mode=sfMode.x;int2 dir=sfMode.yz,across=int2(dir.y,dir.x);
 int along0=int(groupId.x)*SF_LINE,lineIndex=int(groupId.y);
 // Line segment plus apron (clamped at the map's edge).
 for(int c=int(thread.x);c<SF_LINE+2*SF_APRON;c+=SF_LINE){
  int2 q=clamp(dir*(along0+c-SF_APRON)+across*lineIndex,0,side-1);
  sfCache[c]=sfWindowChannels(mode,mode==0?float4(sfFoliage.Load(int3(q,0))>.5?sfRaw.Load(int3(q,0)):1,0,0,0):sfSource.Load(int3(q,0)));
 }
 GroupMemoryBarrierWithGroupSync();
 int i=int(thread.x)+SF_APRON;int2 p=dir*(along0+int(thread.x))+across*lineIndex;
 if(p.x>=side||p.y>=side)return;
 int r=min(int(sfRadius.x),SF_APRON),gapReach=min(int(sfRadius.y),SF_APRON),reach=min(int(sfRadius.z),SF_APRON);
 // Window passes over the 3 m distance reach step 2 texels past 32 texels
 // (2048 maps: 4.7 cm texels), keeping their tap count; the field they read
 // is a smooth distance, so the coarser step moves it by under a texel.
 int step=reach>32?2:1;
 float4 s=mode==0?float4(sfCache[i].x,0,0,0):sfSource.Load(int3(p,0)),o=s;
 if(mode<=3){
  // Closing of the foliage alone: dilate (the highest leaves spread: min
  // depth), then erode by the same radius (max depth): leaf gaps up to 2.5 m
  // close at the crown's height and crown outlines return to their size.
  // Solid casters keep their raw outline: the closing of every caster
  // filled any opening under 2.5 m at the height of the walls or roof
  // around it, so rain never reached inside a roof hole or a roofless
  // building (e4ab0f76 user note, Dead City).
  float m=mode<=1?1:0;
  for(int k=-r;k<=r;++k)m=mode<=1?min(m,sfCache[i+k].x):max(m,sfCache[i+k].x);
  o=float4(m,0,0,0);
  if(mode==3){float raw=sfRaw.Load(int3(p,0));o.r=min(m,raw);o.g=m<1&&raw>m+.008?1:0;o.b=m<1?1:0;}
 }else if(mode==4){
  // Exact distance transform, row pass: texels to the nearest empty texel
  // along the row (within reach); the crown share's row max.
  float run=0;
  if(s.r<1){
   run=float(reach);
   for(int k=1;k<=reach;++k)if(sfCache[i+k].x>=1||sfCache[i-k].x>=1){run=float(k);break;}
  }
  float gap=0,closed=0;for(int g=-gapReach;g<=gapReach;++g){gap+=sfCache[i+g].y;closed+=sfCache[i+g].z;}
  o=float4(s.r,run/float(reach),gap/float(2*gapReach+1),closed/float(2*gapReach+1));
 }else if(mode==5){
  // Column pass: the Euclidean inside distance (min of row run^2 + k^2);
  // the crown share's column max.
  float best=float(reach*reach),gap=0,closed=0;
  for(int k=-reach;k<=reach;k+=step){float run=sfCache[i+k].x*float(reach);best=min(best,run*run+float(k*k));}
  for(int g=-gapReach;g<=gapReach;++g){gap+=sfCache[i+g].y;closed+=sfCache[i+g].z;}
  // A crown: closed foliage (the bake's foliage mask) is a real share of the
  // area within 2 m. The filled-gap share stood in for it before the mask
  // and missed dense crowns, whose few gaps left them on the roof rule (full
  // cover, hard edge: v3 route, pine2 at t300); roofs are never foliage.
  float crown=smoothstep(.04,.15,closed/float(2*gapReach+1));
  // Leaf density: the share of the closed crown within 2 m that the bake
  // drew as leaves (not filled gaps), the straight-down cover a crown
  // really gives. Packed below the crown flag in the 16-bit channel (8 bits
  // each), so the spread passes' max keeps the strongest crown.
  float density=closed>0?saturate(1-gap/closed):1;
  o=float4(s.r,s.r<1?saturate(sqrt(best)/sfRadius.w/4):0,(round(crown*255)*256+round(density*255))/65535,0);
 }else{
  // Local crown radius: the largest inside distance within reach (row,
  // then column), so one crown takes one radius.
  // The crown flag spreads the same way, so a whole crown (rim included)
  // takes one rule and shelter falls off monotonically to its outline.
  // The crown flag spreads 2 m (not the 3 m radius window) and stays inside
  // the closed outline (a5650978: more crown texels than occupied ones).
  float m=0,crown=0;
  for(int k=-reach;k<=reach;k+=step)m=max(m,sfCache[i+k].x);
  for(int c=-gapReach;c<=gapReach;++c)crown=max(crown,sfCache[i+c].y);
  o=float4(s.r,s.g,s.r<1?crown:0,m);
 }
 sfOut[p]=o;
}
// Pre-filtered cover (blur_native_rain_shelter: one pass per bake, and when
// the crown fill-in moves by 1/256). In t1 the soft map; out u0: r the rain
// cover, g the snow cover (each texel's rule, shelterTexelKeep), b the caster
// depth and a the caster share, all Gaussian-blurred (sfRadius x radius, y
// sigma in texels, z the fill-in). The pixel shaders take one bilinear read
// (nativeShelterCover, gcShelterKeep): b/a is the mean caster depth there.
// A group blurs one 16 x 16 tile: the tile plus an 8-texel apron into
// groupshared memory, a row pass over the apron rows, then the columns.
#define SB_TILE 16
#define SB_APRON 8
#define SB_SPAN (SB_TILE+2*SB_APRON)
groupshared float4 sbTexel[SB_SPAN*SB_SPAN];
groupshared float4 sbRow[SB_SPAN*SB_TILE];
// The soft map's b channel (shelterFilterCS): the crown flag and the crown's
// leaf density, 8 bits each.
float2 shelterCrown(float b){
 float packed=round(b*65535);
 return float2(floor(packed/256)/255,fmod(packed,256)/255);
}
// Shelter at one texel of the soft map for build-up fill 0..1: roofs and
// other solids take a 0.25 m feather inside their outline; crowns (filled
// leaf gaps nearby) shelter from the centre out, and as snow builds up it
// fills in from the rim toward the trunk, by distance inside the closed
// crown outline, down to a core of half the crown's radius (frame_001039:
// the blurred occupancy, mixed with the roof edge by a varying crown share,
// sheltered the rim more than the inner ring).
// Crowns fill in slowly (the square of the build-up) and keep a bare core
// of half their radius (9252c204 user note: the core vanished within
// minutes); roofs and other solids never fill in.
// Crowns: a soft edge over rim of their radius from the rim, moving in by
// at most a quarter of the radius, so the inner 40% always stays sheltered
// (c6c05f03: a fill by distance in metres against 2-3 m crowns left
// nothing); roofs over 0.25 m.
// rim: the crown edge's width as a share of its radius (snow .35; rain wet
// .65, so rain reaches in toward the trunk: bfa1e170 drew a hard dry polygon).
// leak (rain 1, snow 0): crowns pass rain through their leaf gaps, so a
// crown covers at most .85 of its leaf density (user note on e4ab0f76:
// trees should not give perfect cover; the .15 stands for drip and wind).
float shelterTexelKeep(float4 m,float fill,float rim,float leak){
 float2 crown=shelterCrown(m.b);
 float inside=m.g*4,radius=max(m.a*4,.5);
 float start=fill*.25*radius;
 float crownKeep=smoothstep(start,start+rim*radius,inside)*lerp(1,.85*crown.y,leak);
 return lerp(smoothstep(0,.25,inside),crownKeep,crown.x);
}
float sbWeight(int k,float sigma){return exp(-float(k*k)/(2*sigma*sigma));}
[numthreads(SB_TILE,SB_TILE,1)]
void shelterBlurCS(uint3 groupId:SV_GroupID,uint3 thread:SV_GroupThreadID,uint index:SV_GroupIndex){
 int side=sfMode.w,r=min(int(sfRadius.x),SB_APRON);float sigma=max(sfRadius.y,.5),fill=sfRadius.z;
 int2 origin=int2(groupId.xy)*SB_TILE-SB_APRON;
 for(int c=int(index);c<SB_SPAN*SB_SPAN;c+=SB_TILE*SB_TILE){
  float4 m=sfSource.Load(int3(clamp(origin+int2(c%SB_SPAN,c/SB_SPAN),0,side-1),0));
  sbTexel[c]=m.r<1?float4(shelterTexelKeep(m,fill,.65,1),shelterTexelKeep(m,fill,.35,0),m.r,1):0;
 }
 GroupMemoryBarrierWithGroupSync();
 float total=0;for(int t=-r;t<=r;++t)total+=sbWeight(t,sigma);
 for(int e=int(index);e<SB_SPAN*SB_TILE;e+=SB_TILE*SB_TILE){
  int x=e%SB_TILE,y=e/SB_TILE;float4 a=0;
  for(int k=-r;k<=r;++k)a+=sbWeight(k,sigma)*sbTexel[y*SB_SPAN+x+SB_APRON+k];
  sbRow[e]=a/total;
 }
 GroupMemoryBarrierWithGroupSync();
 int2 p=int2(groupId.xy)*SB_TILE+int2(thread.xy);
 if(p.x>=side||p.y>=side)return;
 float4 a=0;
 for(int k=-r;k<=r;++k)a+=sbWeight(k,sigma)*sbRow[(int(thread.y)+SB_APRON+k)*SB_TILE+int(thread.x)];
 sfOut[p]=saturate(a/total);
}
)";

// Rain rings for wet ground (Renderer::update_native_rain_rings): the height
// gradient of the water's drop rings (liquid_surface.hlsl liquidRainRipples,
// the same drop, dimple and ring terms and constants) rendered into one
// tiling 6.25 m texture, 5 x 5 drop cells of 1.25 m per layer, four offset
// layers summed. The cells wrap at the tile edge, so the texture tiles
// seamlessly; the layers' grids are axis-aligned (a rotated grid would not
// tile). Re-rendered only when the game time or the rain amount changes, so
// it freezes on pause. rrTime: x game seconds, y rain 0..1, z texels per side.
constexpr char kRainRingsShader[] = R"(
cbuffer RainRings:register(b0){float4 rrTime;};
RWTexture2D<float2> rrOut:register(u0);
static const uint rrCells=5;
static const float rrCell=1.25,rrLife=.9,rrWavelength=.11,rrRingSlope=.55,rrDimpleSize=.07,rrDimpleSlope=.5,rrDensity=.9;
static const float2 rrRadius=float2(.04,.5);
uint rrHash(uint x){x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;return x;}
float rrUnit(uint h){return (h>>8)*(1.0/16777216.0);}
[numthreads(8,8,1)]
void rainRingsCS(uint3 id:SV_DispatchThreadID){
 float side=rrTime.z,seconds=rrTime.x,rain=rrTime.y;
 if(float(id.x)>=side||float(id.y)>=side)return;
 float2 q0=(float2(id.xy)+.5)/side*rrCells,result=0;
 [unroll]for(uint layer=0;layer<4;++layer){
  float2 q=q0+float2(.37,.71)*layer;
  float2 cell=floor(q),local=q-cell;
  uint2 wrapped=uint2(cell)%rrCells;
  uint key=rrHash(wrapped.x*0x9e3779b1u^rrHash(wrapped.y+layer*0x632be5abu));
  float cycle=seconds/rrLife+rrUnit(key);
  uint drop=rrHash(key^asuint(int(floor(cycle)))*0x85ebca77u);
  if(rrUnit(drop)>=rain*rrDensity)continue;
  float age=frac(cycle);
  uint place=rrHash(drop);
  float2 center=.3+.4*float2(rrUnit(place),rrUnit(rrHash(place)));
  float2 d=(local-center)*rrCell;
  float r=length(d);
  float2 edge=min(center,1-center)*rrCell;
  float fit=saturate((min(edge.x,edge.y)-r)/rrWavelength);
  float x=(r-rrRadius.x-rrRadius.y*age)/rrWavelength;
  float u=r/rrDimpleSize;
  if(fit<=0||(abs(x)>2.6&&u>2.5))continue;
  float envelope=exp(-x*x*.7);
  float decay=smoothstep(0,.05,age)*pow(1-age,1.5);
  float ring=rrRingSlope*decay*envelope*(-sin(6.2831853*x)-cos(6.2831853*x)*x*.22);
  float dimple=rrDimpleSlope*2.33*u*exp(-u*u)*(1-smoothstep(0,.2,age));
  result+=d/max(r,1e-4)*(ring+dimple)*fit;
 }
 rrOut[id.xy]=result;
}
)";
} // namespace owned::precipitation
