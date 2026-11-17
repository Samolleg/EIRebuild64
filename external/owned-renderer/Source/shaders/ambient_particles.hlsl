// Ambient particles (renderer_ambient_particles.cpp, ambient_particle_frame.h).
// Embedded into the game by Source/ambient_particle_frame.h at build time;
// Prototypes/shader-lab loads this file directly. The __cplusplus guard lines
// make the file a C++ raw string where that header includes it.
// Keep each piece between guard lines under 15000 bytes (MSVC literal limit).
#ifdef __cplusplus
R"hlsl( //"
#endif
cbuffer Ambient:register(b0){
 row_major float4x4 ambClip;float4 ambRight;float4 ambUp;float4 ambEye;float4 ambFocus;float4 ambDisp;
 float4 ambBand;float4 ambColorA;float4 ambColorB;float4 ambMotion;float4 ambMisc;float4 ambVelocity;
 float4 ambAffA;float4 ambAffB;float4 ambGrid;float4 ambAmbient;float4 ambSunDir;float4 ambSunColor;
 float4 ambFogRange;float4 ambFogControl;float4 ambFogColor;float4 ambDepth;float4 ambWindow;float4 ambReach;
 float4 ambGust; // sand: map wind direction, gust strength, integrated front position (m)
 float4 ambFogSeam;float4 ambFogCurve;float4 ambFogSpanLo;float4 ambFogSpanHi; // neighbour seam fog (neighbour_fog.h)
};
Texture2D<uint> ambArea:register(t0);
Texture2D<float> ambHeight:register(t1);
Texture2D<float> ambSceneDepth:register(t2); // PS: scene depth; VS: base ground height (DXBC)
// D3D12 (DXIL): one t table serves both stages, so the base ground has its own slot.
#ifdef EID3D12
Texture2D<float> ambBase:register(t8);
#define AMB_BASE ambBase
#else
#define AMB_BASE ambSceneDepth
#endif
Texture2D<float4> ambSources:register(t3); // spray, cave drip and mushroom weights per 2 m tile
#ifdef NATIVE_FIRE_BURN
// Fire burn twins (kinds 8-10): the area's burn mask (renderer_fire_burn.cpp:
// r ground burn, g its front time, b crown front time, a crown burn; its last
// row this frame's values, native_shader.h nativeBurnRow) and the game's fire
// frames (tendrismoke.mmp, a 4x4 atlas; frames 0-7 are flame).
Texture2D<float4> ambBurnMask:register(t9);
Texture2D<float4> ambFire:register(t10);
SamplerState ambFireSampler:register(s1);
// One mask texel at p: x ground flame, y crown flame (0..1, up fast when the
// front arrives, out over the burning seconds), z seconds since the ground's
// front (-1 none), w the same for the crown.
float4 ambBurnAt(float2 p){
 uint w,h;ambBurnMask.GetDimensions(w,h);
 if(w==0||h<2)return float4(0,0,-1,-1);
 const float4 ambBurn=float4(ambBurnMask.Load(int3(0,h-1,0)).xy,ambBurnMask.Load(int3(1,h-1,0)).z,ambBurnMask.Load(int3(0,h-1,0)).z);
 const float4 m=ambBurnMask.Load(int3(clamp(int2(floor(p*ambBurn.y)),int2(0,0),int2(w-1,h-2)),0));
 const float g=m.r>.3?ambBurn.x-m.g:-1,c=m.a>.3?ambBurn.x-m.b:-1;
 const float sg=max(ambBurn.z,.5),sc=max(ambBurn.w,.5);
 return float4(g<0?0:saturate(g/.35)*(1-smoothstep(.35*sg,sg,g)),c<0?0:saturate(c/.35)*(1-smoothstep(.35*sc,sc,c)),g<0?-1:g/sg,c<0?-1:c/sc);
}
#endif
// Waterfall kinds (2, 5-7) place their particles in the map's waterfall tiles
// (ambient_fall_tiles: impact tiles, then fall tiles; packed x | y << 16)
// instead of the camera window: ambWindow.x is the list offset, ambWindow.w
// the particles per tile.
Buffer<uint> ambFallTiles:register(t4);
// Procedural falls (procedural_falls.h), two float4 each: lip xy, flow
// direction xy; width, top, bottom, lip speed. Fall foam, lip spray and mist
// (kinds 5-7) take ambWindow.x as the first fall and ambWindow.w per fall.
Buffer<float4> ambFalls:register(t5);
// puff: age (0 fresh .. 1 dissolved), depth-fade distance (m), noise seed.
struct AmbOut{float4 position:SV_Position;float4 color:COLOR0;float3 shape:TEXCOORD0;float viewW:TEXCOORD1;float border:TEXCOORD2;float3 puff:TEXCOORD3;};
uint ambHash(uint x){x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;return x;}
float ambValueNoise(float2 p){
 float2 c=floor(p),f=frac(p);f=f*f*(3-2*f);
 float4 h=frac(sin(float4(dot(c,float2(127.1,311.7)),dot(c+float2(1,0),float2(127.1,311.7)),
                 dot(c+float2(0,1),float2(127.1,311.7)),dot(c+1,float2(127.1,311.7))))*43758.5453);
 return lerp(lerp(h.x,h.y,f.x),lerp(h.z,h.w,f.x),f.y);
}
float ambRand(uint seed,uint k){return (ambHash(seed^ambHash(k*0x9e3779b9u+0x68e31da4u))&0xffffffu)*(1.0/16777216.0);}
float ambTileAffinity(int2 tile){
 if(any(tile<0)||any(tile>=int2(ambGrid.zw)))return 0;
 uint v=ambArea.Load(int3(tile,0));uint area=v&15u;float trees=min(float(v>>4)/3.0,1.0);
 float a=area<4u?ambAffA[area]:ambAffB[area-4u];
 float litter=(area==4u||area==6u||area==7u)?0:1;
 return saturate(a*(1+ambGrid.x*trees)+ambGrid.y*trees*litter);
}
float ambAffinity(float2 p){ // 2 m tiles, bilinear between tile centres
 float2 t=p*.5-.5;int2 i=int2(floor(t));float2 f=t-i;
 return lerp(lerp(ambTileAffinity(i),ambTileAffinity(i+int2(1,0)),f.x),
             lerp(ambTileAffinity(i+int2(0,1)),ambTileAffinity(i+int2(1,1)),f.x),f.y);
}
float4 ambSourcesAt(float2 p){ // bilinear between tile centres
 uint w,h;ambSources.GetDimensions(w,h);
 float2 t=clamp(p*.5-.5,0,float2(w-1,h-1));int2 i=int2(floor(t));int2 j=min(i+1,int2(w-1,h-1));float2 f=t-i;
 return lerp(lerp(ambSources.Load(int3(i,0)),ambSources.Load(int3(j.x,i.y,0)),f.x),
             lerp(ambSources.Load(int3(i.x,j.y,0)),ambSources.Load(int3(j,0)),f.x),f.y);
}
float ambMushroomAt(float2 p){
 // Do not extend a map-edge mushroom into an unbounded clamped source.
 if(any(p<0)||any(p>=ambGrid.zw*2))return 0;
 return ambSourcesAt(p).b;
}
float ambGround(float2 p,bool base=false){
 uint w,h;ambHeight.GetDimensions(w,h);
 float2 c=clamp(p,0,float2(w-1,h-1));int2 i=int2(floor(c));int2 j=min(i+1,int2(w-1,h-1));float2 f=c-i;
 float4 z=base?float4(AMB_BASE.Load(int3(i,0)),AMB_BASE.Load(int3(j.x,i.y,0)),
                     AMB_BASE.Load(int3(i.x,j.y,0)),AMB_BASE.Load(int3(j,0))):
               float4(ambHeight.Load(int3(i,0)),ambHeight.Load(int3(j.x,i.y,0)),
                      ambHeight.Load(int3(i.x,j.y,0)),ambHeight.Load(int3(j,0)));
 return lerp(lerp(z.x,z.y,f.x),lerp(z.z,z.w,f.x),f.y);
}
// Sand gusts (ambient_particles.h sand_*): fronts travel downwind; u is the
// front phase at a point, rising with game time.
float ambSandEnvelope(float u){float k=1-saturate(u);return min(1,smoothstep(0,.07,u)*k*k*sqrt(k)*1.2);}
float ambSandDensity(float gust,float streamer){
 float g=saturate(gust),sheet=smoothstep(.45,.8,g);return .06+.94*g*lerp(streamer,1,sheet);
}
#ifdef __cplusplus
//")hlsl" R"hlsl( //"
#endif
AmbOut ambientVS(uint vertex:SV_VertexID,uint instance:SV_InstanceID){
 AmbOut o=(AmbOut)0;o.position=float4(0,0,-1,1);
 // ambWindow: world origin of the tile window, tiles per row, particles per
 // tile. Every tile holds the same particles (index within the tile), so the
 // field is periodic in world space with period ambFocus.z and wrapping
 // across tile edges is seamless: a particle leaving one tile is the same
 // particle entering from the neighbour.
 uint pool=max(uint(ambWindow.w),1u),columns=max(uint(ambWindow.z),1u),tile=instance/pool;
 uint seed=ambHash(instance-tile*pool+uint(ambMisc.y));
 float r0=ambRand(seed,0),r1=ambRand(seed,1),r2=ambRand(seed,2),r3=ambRand(seed,3),r4=ambRand(seed,4);
 float r5=ambRand(seed,5),r6=ambRand(seed,6),r7=ambRand(seed,7),r8=ambRand(seed,8),r9=ambRand(seed,9);
 float side=ambFocus.z,t=ambFocus.w;
 float phase=r5*6.2831853,rate=ambMotion.y*(.6+.8*r6);
 float2 wobble=ambMotion.x*float2(sin(t*rate+phase),cos(t*rate*.77+phase*1.7));
 float2 q=float2(r0,r1)*side+ambDisp.xy+wobble;
 float2 p=ambWindow.xy+float2(tile%columns,tile/columns)*side+frac(q/side)*side;
 uint fallIndex=0;
 if(ambVelocity.w==2){
  uint packed=ambFallTiles[uint(ambWindow.x)+tile];
  p=float2(packed&0xffffu,packed>>16)*2+float2(r0,r1)*2+wobble;
 }else if(ambVelocity.w>=5
#ifdef NATIVE_FIRE_BURN
  &&ambVelocity.w<8
#endif
 ){
  fallIndex=uint(ambWindow.x)+tile;
  p=ambFalls[fallIndex*2].xy;
 }
 // Depth of field by distance from the eye (ambReach: far end, far fade
 // width, near fade, size scale): fills the screen down to its bottom edge
 // and thins out with distance, no band tied to the screen centre.
 if(length(p-ambEye.xy)>ambReach.x)return o;
 float h=frac(r2+(ambDisp.z+ambMotion.x*.25*sin(t*rate*.5+phase*2.3))/ambBand.y);
 float fade=saturate(h*8)*saturate((1-h)*5),density=1,lift=1;
 bool sand=ambVelocity.w==1,veil=false;
 // Splashes and mist: the impact source alone decides (where falls land);
 // the water-area bilinear would starve one-tile-wide streams beside banks.
 bool spray=ambVelocity.w==2,mist=ambVelocity.w==5,fallFoam=ambVelocity.w==6,lipSpray=ambVelocity.w==7;
 float splashBase=0;bool fleck=false;
 // Falling-water kinds own their world position (fixedCenter).
 bool fixedPos=false;float3 fixedCenter=0;float puffAge=-1,sizeScale=1;
 if(spray){
  // A splash leaves wet water at a fixed point (no wind, no wobble), flies a
  // short ballistic arc and drops back. Spawn and landing must both be on
  // receiving water: never on the falling face, a bank or dry ground.
  p-=wobble;
  int2 spawnTile=int2(floor(p*.5));
  if(any(spawnTile<0)||any(spawnTile>=int2(ambGrid.zw)))return o;
  if((ambArea.Load(int3(spawnTile,0))&15u)!=6u)return o;
  density=ambSources.Load(int3(spawnTile,0)).r;
  if(density*ambMisc.z<=r3)return o;
  splashBase=ambGround(p);
  if(splashBase-ambGround(p,true)<=.05)return o;
  float2 slope=float2(ambGround(p+float2(.25,0))-ambGround(p-float2(.25,0)),
                      ambGround(p+float2(0,.25))-ambGround(p-float2(0,.25)))*2;
  density*=1-smoothstep(.1,.3,length(slope));
  // Two kinds: droplets thrown in short arcs, and foam flecks that ride the
  // water surface outward from the impact, growing in and dissolving away
  // (no pop). Both leave and land on receiving water.
  fleck=r7<.4;
  float apex=fleck?.02:ambBand.y*lerp(.2,1,r2*r2),life=fleck?lerp(1.6,3,r6):sqrt(8*apex/9.81);
  float age=frac(t/life+r5);
  h=fleck?0:4*age*(1-age)*apex/ambBand.y;
  fade=fleck?smoothstep(0,.2,age)*(1-smoothstep(.45,1,age)):smoothstep(0,.08,age)*(1-smoothstep(.75,1,age));
  p+=float2(cos(phase),sin(phase))*age*(fleck?.6:.25);
  int2 landingTile=int2(floor(p*.5));
  if(any(landingTile<0)||any(landingTile>=int2(ambGrid.zw)))return o;
  if((ambArea.Load(int3(landingTile,0))&15u)!=6u)return o;
  if(ambGround(p)-ambGround(p,true)<=.05)return o;
 }
 if(fallFoam||lipSpray||mist){
  // The procedural fall this particle belongs to (procedural_falls.h, the
  // same sheet the liquid pass draws): lip centre and flow direction, width,
  // top and bottom levels. Positions follow its ballistic profile.
  float4 fa=ambFalls[fallIndex*2],fb=ambFalls[fallIndex*2+1];
  float2 lip=fa.xy,dir=fa.zw,across_dir=float2(-dir.y,dir.x);
  float width=fb.x,top=fb.y,bottom=fb.z,speed=fb.w;
  float fallTime=sqrt(2*max(top-bottom,.1)/9.81);
  float across=(r0-.5)*width*.85;
  // The fall is the map's own liquid overlay (no generated sheet), which can
  // reach further out from the lip than a free ballistic sheet: a profile
  // point under the shown surface (ambGround) would sit inside the overlay
  // and be hidden. Foam and spray ride the overlay instead, and the mist
  // rises where the overlay meets the pool.
  float2 base=lip+across_dir*across;
  if(mist){
   // Mist: from the foot of the sheet, billowing out from it and rising up
   // to the band height, a little way up the face too; drifts gently with
   // the wind (ambVelocity.xy, scaled down) for the time it has been rising.
   float rise=h*ambBand.y;
   float foot=speed*fallTime;
   [loop]for(uint step=0;step<16&&ambGround(base+dir*foot)>bottom+.15;++step)foot+=.4;
   fixedCenter=float3(lip+dir*(foot+lerp(-.2,1.2,h*r7))+across_dir*across*1.1+ambVelocity.xy*rise/max(ambVelocity.z,.05),
                      bottom+ambBand.x+rise);
   puffAge=h;sizeScale=lerp(.6,1.4,h);
  }else if(fallFoam){
   // Foam clumps riding the sheet from the lip into the boil, dissolving.
   float life=fallTime*lerp(1.1,1.5,r6),age=frac(t/life+r5),tt=age*life;
   float z=top-4.905*tt*tt,landed=saturate((bottom-z)/.6);
   float slide=speed*tt+.06;
   [loop]for(uint step=0;step<16&&ambGround(base+dir*slide)>max(z,bottom)+.02;++step)slide+=.25;
   slide+=.12; // clear of the face, so the soft-particle fade keeps it
   fixedCenter=float3(base+dir*slide,max(max(z,bottom),ambGround(base+dir*slide))+.04);
   fade=smoothstep(0,.06,age)*(1-landed);
   puffAge=saturate(age*.8+landed*.6);sizeScale=lerp(1.15,.65,age);
  }else{
   // Lip spray: fine droplets thrown forward and up off the lip, falling back.
   float life=lerp(.45,.85,r6),age=frac(t/life+r5),tt=age*life;
   float2 at=lip+(dir*lerp(.8,2,r2)+across_dir*(r8-.5)*.6)*tt+across_dir*across;
   fixedCenter=float3(at,max(top+.03+lerp(.2,.9,r7)*tt-4.9*tt*tt,ambGround(at)+.03));
   fade=smoothstep(0,.08,age)*(1-smoothstep(.55,1,age));
  }
  fixedPos=true;
 }
 bool mushroom=ambVelocity.w==4;
 if(ambVelocity.w==3)density=ambSourcesAt(p).g; // drips: wet wall pockets only
 if(sand){
  // Fronts 14 m apart along the map wind; each front has its own strength,
  // varying along its length. Between fronts only the faint haze remains.
  float2 d=ambGust.xy;float along=dot(p,d),across=dot(p,float2(-d.y,d.x));
  // Front index modulo the CPU's 1024-front wrap, so the wrap is seamless.
  float wave=(ambGust.w-along)/14.0,u=frac(wave),n=float(int(floor(wave))&1023);
  float amp=(.35+.65*ambRand(uint(n)+1048576u,11))*(.55+.45*sin(across*.19+n*2.3));
  float gust=ambGust.z*amp*ambSandEnvelope(u);
  // Streamers: narrow lanes along the wind, broken into puffs moving with it.
  float lane=.5+.5*sin(across*2.1+1.3*sin(along*.17+n));
  float streamer=lane*lane*(.6+.4*sin(wave*12.566371+across*.5));
  density=ambSandDensity(gust,streamer);
  // The gust pushes the grains downwind and lifts them; they settle behind
  // it. Strong gusts form a low sheet just above the sand.
  p+=d*gust*1.2;
  lift=lerp(.08,1,saturate(gust*1.6))*lerp(1,.45,smoothstep(.45,.8,gust));
  h=r2*r2;fade=saturate((1-h)*4);  // most grains right at the surface
  veil=r7<.08;                     // a few faint long veils give the gust a body
  if(veil)fade*=saturate(gust*2);
 }
#ifdef __cplusplus
//")hlsl" R"hlsl( //"
#endif
#ifdef NATIVE_FIRE_BURN
 // Fire burn kinds: flames and embers while the ground or a crown burns,
 // ash for half as long again after; a particle rides a crown (2-7 m up)
 // where the crown burns harder than the ground.
 bool burnKind=ambVelocity.w>=8,burnCrown=false;float burnPresence=0,burnAge=0;
 [branch]if(burnKind){
  const float4 b=ambBurnAt(p);
  burnCrown=b.y>b.x*(.5+r9);
  if(ambVelocity.w<9.5)burnPresence=max(b.x,b.y);
  else{const float a=max(b.z,b.w);burnPresence=a<.2?0:1-smoothstep(1.2,1.8,a);}
 }
 fade*=saturate((((spray||fixedPos)?1:burnKind?burnPresence:mist?pow(saturate(ambSourcesAt(p).r*1.6),2):mushroom?ambMushroomAt(p):ambAffinity(p))*ambMisc.z*density-r3)*8);
#else
 fade*=saturate((((spray||fixedPos)?1:mist?pow(saturate(ambSourcesAt(p).r*1.6),2):mushroom?ambMushroomAt(p):ambAffinity(p))*ambMisc.z*density-r3)*8);
#endif
 // Mist rises only from open water (not from liquid tiles buried under rock).
 if(fade<=.002)return o;
 // Splashes stay on their fixed water point and own spawn/landing checks.
 float ground=spray?splashBase:ambGround(p);
 // Free particles float on a smoothed ground (centre plus 8 m and 16 m
 // rings): they follow the land's large shape, not every bump under them,
 // and fade where the local ground rises through their height (below).
 float surface=ground;
 float rise=0;
 [branch]if(!spray&&!fixedPos){
  float ring=0;
  [unroll]for(int k=0;k<4;k++){float2 o=float2(cos(k*1.5707963+.39),sin(k*1.5707963+.39));
   ring+=ambGround(p+o*8)+ambGround(p+o.yx*float2(-16,16));}
  surface=(ground+ring)/9;
  // Fade from 0.3 m before the ground reaches the particle's own height.
  rise=ground-(surface+ambBand.x+h*ambBand.y*lift)+.45;}
 float3 center=float3(p,surface+ambBand.x+h*ambBand.y*lift+(sand?.02:0));
#ifdef NATIVE_FIRE_BURN
 if(burnCrown)center.z+=2+5*r8;
#endif
 if(fixedPos)center=fixedCenter;
 float eyeDistance=length(center-ambEye.xyz);
 fade*=saturate((ambReach.x-eyeDistance)/ambReach.y)*saturate((eyeDistance-ambReach.z)/ambReach.z);
 if(fade<=.002)return o;
 float4 clipCenter=mul(ambClip,float4(center,1));
 if(clipCenter.w<=.05)return o;
 // Analytic path tangent: integrated drift plus the derivative of flutter.
 // Compare one metre behind in world space, never a previous frame or a
 // wrapped tile coordinate (the same particle continues across tile edges).
 // Gentle uphill ground still carries the field; sharp rises dissolve it.
 float2 travel=ambVelocity.xy+ambMotion.x*rate*float2(cos(t*rate+phase),-.77*sin(t*rate*.77+phase*1.7));
 float travel2=dot(travel,travel),terrainFade=1;
 [branch] if(!spray&&travel2>1e-6){
  // Fade where the ground rises into the particle's height (it hit the slope).
  terrainFade=1-smoothstep(.15,.6,rise);
 }
 fade*=terrainFade;
 if(fade<=.002)return o;
 // Pollen: half its (halved) pool is green grass chaff, so the chaff keeps
 // its old count while the yellow fluff drops to a third. The yellow fluff
 // and the fireflies are sparse and vary in size and strength, never a
 // uniform field.
 bool chaff=ambMisc.w==0&&r7<.5;
 bool sparse=(ambMisc.w==0&&!chaff)||ambMisc.w==9;
 float size=lerp(ambBand.z,ambBand.w,r4)*ambReach.w*terrainFade;
 if(sparse){size*=lerp(.7,1.35,ambRand(seed,10));fade*=lerp(.4,1,ambRand(seed,11));}
 if(veil){size*=9;fade*=.1;}
 size*=sizeScale;
#ifdef NATIVE_FIRE_BURN
 if(ambVelocity.w==8){size*=pow(.85,h*8);puffAge=h;}
#endif
 if(fleck)size*=2.4;
 // Keep sub-pixel particles at 1.5 px with matching coverage: no shimmer.
 float grow=max(1,1.5/max(size*ambRight.w/clipCenter.w,1e-4));
 size*=grow;fade/=grow*grow;
 float2 uv=float2((vertex&1u)?1:-1,(vertex&2u)?1:-1);
 float2 corner=uv;
 if(chaff)corner*=float2(.45,1.6);
 float spin=ambMotion.w+(chaff?2.5:0);
 float3 axisX=ambRight.xyz,axisY=ambUp.xyz;
 if(spin>0){
  float angle=t*spin*(.5+r8)+r9*6.2831853,s=sin(angle),c=cos(angle);
  corner=float2(corner.x*c-corner.y*s,corner.x*s+corner.y*c);
  corner.x*=.3+.7*abs(cos(angle*.61+r5*3));  // tumbling edge-on
 }
 if(ambMotion.z>1){ // streaks along the screen-space motion
  float2 v=float2(dot(ambVelocity.xyz,ambRight.xyz),dot(ambVelocity.xyz,ambUp.xyz));
  if(dot(v,v)>1e-6){v=normalize(v);axisY=v.x*ambRight.xyz+v.y*ambUp.xyz;axisX=-v.y*ambRight.xyz+v.x*ambUp.xyz;}
  corner.y*=ambMotion.z*(veil?2.2:1);
 }
 float3 world=center+(axisX*corner.x+axisY*corner.y)*size*.5;
 o.position=mul(ambClip,float4(world,1));o.viewW=o.position.w;
 float3 albedo=chaff?float3(.55,.68,.3)*lerp(.8,1.1,r9):lerp(ambColorA.rgb,ambColorB.rgb,r8);
 float emissive=ambColorB.a;
 // The shared world light vector points towards the sun; world surfaces use
 // max(ambient, sun * facing). Thin particles take a soft facing and glow
 // when seen against the sun.
 float3 view=normalize(center-ambEye.xyz),toSun=normalize(ambSunDir.xyz);
 float scatter=pow(saturate(dot(view,toSun)),4);
 float3 ambientLight=ambAmbient.rgb,sunLight=ambSunColor.rgb;
 if(ambVelocity.w==2||ambVelocity.w>=5
#ifdef NATIVE_FIRE_BURN
  &&ambVelocity.w<8
#endif
 ){
  // Water spray and foam: a desaturated light, so warm sunlight does not
  // tint it cream (liquid_surface.hlsl liquidFoamLight).
  ambientLight=lerp(dot(ambientLight,float3(.2126,.7152,.0722)).xxx,ambientLight,.35);
  sunLight=lerp(dot(sunLight,float3(.2126,.7152,.0722)).xxx,sunLight,.35);
 }
 float3 lit=albedo*(max(ambientLight,sunLight*(.6+.4*saturate(toSun.z)))+sunLight*.6*scatter);
 float pulse=.5+.5*sin(t*(1.5+3*r6)+r9*6.2831853);
 float glow=ambMisc.x>=1?pow(pulse,5)*1.6:lerp(1,.4+.9*pulse,ambMisc.x);
 // Map-border fog factor as the terrain vertices compute it; the pixel
 // shader applies it with the shared world atmosphere.
 float fog=1;
 if(ambFogControl.y!=0&&ambFogControl.x>0){
  fog=1-borderFogDensity(p,ambFogRange,ambFogControl.x,ambFogSeam,ambFogCurve,ambFogSpanLo,ambFogSpanHi);
 }
 o.border=fog;
 float alpha=ambColorA.a*fade;
 o.color=float4(lit*alpha*(1-emissive)+albedo*glow*1.5*alpha*emissive,alpha*(1-emissive));
 o.shape=float3(uv,
#ifdef NATIVE_FIRE_BURN
  ambVelocity.w==8?5:
#endif
  emissive>0?2:(spin>0?1:(mist?4:(puffAge>=0?3:0))));
 // Soft particles: big mist fades into the scene over half its size.
 // Fall foam/lip spray ride just off the fall: a short soft range.
 o.puff=float3(saturate(puffAge),(fallFoam||lipSpray)?.04:max(ambDepth.z,mist?size*.5:0),r9*61.7);
 return o;
}
float4 ambientPS(AmbOut i):SV_Target{
 float2 q=i.shape.xy;float r2=dot(q,q),a;
#ifdef NATIVE_FIRE_BURN
 if(i.shape.z>4.5){
  // Burn flame: the game's fire frame of this moment of its life (frames 0-7
  // of the 4x4 atlas, the game's UV rule: v from the top row down), additive.
  const float frame=min(floor(i.puff.x*8),7);
  const float2 cell=float2(fmod(frame,4),floor(frame/4));
  const float2 f=float2(q.x*.5+.5,.5-q.y*.5);
  const float4 tx=ambFire.Sample(ambFireSampler,(cell+lerp(.5/256,1-.5/256,f))*.25);
  i.color.rgb*=tx.rgb;a=tx.a;
 }
 else
#endif
 if(i.shape.z>1.5)a=(exp(-r2*3)+.35*exp(-r2))*saturate(1-r2);         // glowing core and halo
 else if(i.shape.z>.5)a=saturate((1-(q.x*q.x*(1.5+.7*q.y)+q.y*q.y))*3); // leaf
 else if(i.shape.z>3.5){
  // Mist veil: a very soft falloff with zero slope at the rim (no circular
  // edge), broken by two octaves of noise that thin it as it ages.
  float body=saturate(1-r2);body*=body;
  float2 w=q*1.3+i.puff.z;
  float n=ambValueNoise(w+ambValueNoise(w*.7+3.1)*1.5)*.65+ambValueNoise(q*3.1+i.puff.z*1.7+5.3)*.35;
  a=body*saturate(n*1.4-.1-i.puff.x*.45);
 }
 else if(i.shape.z>2.5){
  // Foam clump: a lumpy, noise-warped outline (not a disc) that erodes into
  // lace as it ages, lighter on its upper side.
  float2 w=q*1.4+i.puff.z;
  float2 warp=float2(ambValueNoise(w),ambValueNoise(w+4.7))-.5;
  float rr=dot(q+warp*.7,q+warp*.7);
  float body=saturate(1-rr);body=body*body*(3-2*body);
  float n=ambValueNoise(q*2.6+i.puff.z*1.3+7.3)*.6+ambValueNoise(q*5.3+i.puff.z*.7+1.9)*.4;
  float erode=i.puff.x*.8;
  a=body*smoothstep(erode+.2,erode+.5,n*.7+body*.4);
  i.color.rgb*=lerp(.7,1.08,saturate(q.y*.6+.55));
 }
 else{a=saturate(1-r2);a=a*a*(3-2*a);}                                // soft mote, full centre
 float d=ambSceneDepth.Load(int3(i.position.xy,0));
 float denom=d-ambDepth.x,sceneW=abs(denom)>1e-7?ambDepth.y/denom:1e9;
 if(sceneW<0)sceneW=1e9;
 float4 c=i.color*(a*saturate((sceneW-i.viewW)/i.puff.y));
 // Premultiplied: coverage c.a takes the fog colour/in-scatter like a world
 // surface; additive glow (no coverage) is only attenuated.
 // ambFogControl.y only enables the map border in ambientVS. The owned
 // atmosphere is independent of that flag and uses this particle's depth,
 // not the depth of the world surface behind it.
 float f=nativeFogFactor(i.border,i.position);
 c.rgb=c.rgb*f+borderFogColor(i.position,ambFogColor.rgb)*(c.a*(1-f));
 float3 added=0;float t=nativeAtmosphere(added,i.position,ambFogColor.rgb);
 c.rgb=c.rgb*t+added*c.a;
 return c;
}
#ifdef __cplusplus
//")hlsl" R"hlsl( //"
#endif
// Waterfall shells (procedural_falls.h build_fall_shell): the fall's foam as
// its own curtain over the map's liquid overlay, which stays the clear water
// body. Three float4 per vertex: {x, y, z, across (m)}, {fall time (s), arc
// from the lip (m), drop below the lip (m), alpha}, {outward normal, seed}.
// The shell is drawn with the scene depth test; its depth is pulled towards
// the eye along the view ray (same pixel, nearer depth), so the overlay's
// tessellated bulge never hides it while rocks and figures in front still do.
// Patterns run in fall time: a clump keeps its size in seconds, so it
// stretches and spreads as the water accelerates. Streams a column wide take
// their own offset and speed, a slow breakup field opens and closes gaps:
// white water clumps with soft edges, no fixed tiling.
Buffer<float4> fallShell:register(t6);
Texture2D<float4> fallDetail:register(t7); // liquid detail map: G smooth turbulence, B fine turbulence
SamplerState fallSampler:register(s0);
static const float fallShellPull=.6;        // m the depth is pulled towards the eye
static const float fallShellColumn=.55;     // m: width of a foam stream
static const float fallShellSpread=.2;      // share by which streams run faster or slower
static const float2 fallShellCoarse=float2(1.1,.38); // clump pattern repeat: across (m), fall time (s)
static const float2 fallShellFine=float2(.5,.2);    // breakup pattern repeat
static const float2 fallShellStreak=float2(.22,.6); // thin streaks
static const float fallShellAeration=2.5;   // m of drop by which the water is mostly white
struct FallOut{float4 position:SV_Position;float4 fall:TEXCOORD0;float4 normal:TEXCOORD1;float border:TEXCOORD2;};
FallOut fallShellVS(uint vertex:SV_VertexID){
 FallOut o=(FallOut)0;
 float4 a=fallShell[vertex*3],b=fallShell[vertex*3+1],c=fallShell[vertex*3+2];
 float3 toEye=ambEye.xyz-a.xyz;float distance=length(toEye);
 float3 pulled=a.xyz+toEye/max(distance,1e-3)*min(fallShellPull,distance*.5);
 o.position=mul(ambClip,float4(pulled,1));
 o.fall=float4(a.w,b.x,b.z,b.w);o.normal=c;
 float fog=1;
 if(ambFogControl.y!=0&&ambFogControl.x>0)
  fog=1-borderFogDensity(a.xy,ambFogRange,ambFogControl.x,ambFogSeam,ambFogCurve,ambFogSpanLo,ambFogSpanHi);
 o.border=fog;
 return o;
}
float fallShellHash(float n){return frac(sin(n*127.1+311.7)*43758.5453);}
float4 fallShellColumnSample(float across,float tau,float t,float k,float seed,float2 tile,float4 gradient){
 float h1=fallShellHash(k+seed),h2=fallShellHash(k*1.7+seed+3.1),h3=fallShellHash(k*2.3+seed+7.7);
 float speed=lerp(1-fallShellSpread,1+fallShellSpread,h2);
 float2 uv=float2(across/tile.x+h1*7.31,(tau-t*speed)/tile.y+h3*5.17);
 return fallDetail.SampleGrad(fallSampler,uv,gradient.xy/tile,gradient.zw/tile);
}
// The pattern of the two streams around a point, blended over wavering borders.
float4 fallShellStreams(float across,float tau,float t,float seed,float2 tile,float4 gradient,float warp){
 float x=across/fallShellColumn+warp,k=floor(x),w=smoothstep(.2,.8,x-k);
 return lerp(fallShellColumnSample(across,tau,t,k,seed,tile,gradient),
             fallShellColumnSample(across,tau,t,k+1,seed,tile,gradient),w);
}
float4 fallShellPS(FallOut i):SV_Target{
 float across=i.fall.x,tau=i.fall.y,drop=i.fall.z,alpha=i.fall.w,seed=i.normal.w,t=ambFocus.w;
 // Continuous screen gradients of (across, fall time): stream borders jump
 // the texture coordinate, never its footprint.
 float4 gradient=float4(ddx(across),ddx(tau),ddy(across),ddy(tau));
 float4 slow=fallDetail.SampleGrad(fallSampler,float2(across/2.7+seed,(tau*.5-t*.12)/.9+seed*.3),gradient.xy/2.7,gradient.zw*float2(1,.5/.9)/2.7);
 float warp=(slow.g-.5)*1.1,gap=slow.b;
 float aerated=smoothstep(.1,fallShellAeration,drop);
 // Finer detail fades to its mean before it gets smaller than a few pixels
 // (thresholded sub-pixel noise turned into diagonal hatching).
 float footprint=max(length(gradient.xz),1e-4);
 float fineKeep=1-smoothstep(.06,.14,footprint/fallShellFine.x);
 float streakKeep=1-smoothstep(.06,.14,footprint/fallShellStreak.x);
 float coarse=fallShellStreams(across,tau,t,seed,fallShellCoarse,gradient,warp).g;
 float fine=lerp(.5,fallShellStreams(across,tau,t,seed+5,fallShellFine,gradient,warp*.7).b,fineKeep);
 float streak=lerp(.5,fallShellStreams(across,tau,t*1.1,seed+11,fallShellStreak,gradient,warp*.4).b,streakKeep);
 float pattern=coarse*.7+fine*.3;
 // Clumps: sparse at the glassy lip, more as the water aerates, always with
 // wide gaps where the clear falling water underneath shows; the slow field
 // opens and closes them.
 float threshold=lerp(.72,.6,aerated)+(gap-.5)*.3;
 float clump=smoothstep(threshold-.1,threshold+.12,pattern);
 float core=smoothstep(threshold+.06,threshold+.24,pattern);
 // Thin white streaks between the clumps, more of them lower down.
 float veil=smoothstep(.62,.82,streak)*.35*aerated;
 float cover=saturate(max(clump,veil))*smoothstep(-.05,.35,drop);
 // Frayed sides and ends: the shell's alpha eats into the pattern.
 cover*=smoothstep(0,.45,alpha*1.25-(1-fine)*.35);
 float3 n=normalize(i.normal.xyz+float3(0,0,.15)),l=normalize(ambSunDir.xyz);
 float3 ambientLight=lerp(dot(ambAmbient.rgb,float3(.2126,.7152,.0722)).xxx,ambAmbient.rgb,.35);
 float3 sunLight=lerp(dot(ambSunColor.rgb,float3(.2126,.7152,.0722)).xxx,ambSunColor.rgb,.35);
 float3 light=ambientLight*1.2+sunLight*(.4+.6*saturate(dot(n,l)))*.7;
 float3 white=float3(.92,.96,1)*lerp(.8,1.04,core)*lerp(.9,1.05,fine);
 float4 c=float4(white*light*cover,cover*.9);
 float f=nativeFogFactor(i.border,i.position);
 c.rgb=c.rgb*f+borderFogColor(i.position,ambFogColor.rgb)*(c.a*(1-f));
 float3 added=0;float transmit=nativeAtmosphere(added,i.position,ambFogColor.rgb);
 c.rgb=c.rgb*transmit+added*c.a;
 return c;
}
#ifdef __cplusplus
//")hlsl"
#endif
