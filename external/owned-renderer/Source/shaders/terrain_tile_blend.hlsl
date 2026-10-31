// Terrain material blend (terrain_tile_blend.h): transition tiles redrawn from their parent materials.
// Embedded into the game by Source/terrain_tile_blend.h at build time; Prototypes/shader-lab
// loads this file directly. The __cplusplus guard lines make the file a C++
// raw string where that header includes it; to HLSL they are a skipped
// group of comments and one closed string.
// Keep each piece between guard lines under 15000 bytes (MSVC literal limit).
#ifdef __cplusplus
R"hlsl( //"
#endif
// Field: a material's weight at a point is a smooth kernel over the 3x3 grid
// vertices around its warped position (quadratic B-spline mixed with bilinear
// so single authored vertices keep their spot), less a small tail. Score: weight plus, faded in with the
// weight, the material's texel brightness about its mean, world noise and,
// for a looser material meeting a harder one, the harder one's dark texels
// (cracks and joints); the top score wins and the others fade over the soft
// range of the looser of the top two.
static const float tileBlendWarpLarge=.9;    // Tile units (1.8 m) of the ~6 m warp. @param 0 2.5
static const float tileBlendWarpSmall=.35;   // Tile units (0.7 m) of the ~1.4 m warp. @param 0 1.5
static const float tileBlendBilinear=.6;     // Share of the bilinear kernel. @param 0 1
static const float tileBlendTail=.1;         // Field weight a material needs before it counts. @param 0 0.4
static const float tileBlendRockPresence=.4;
static const float tileBlendSteepShift=.6;   // Rock edge shift from full steepness down to the steepness start. @param 0 2
static const float tileBlendRockBand=.4;     // Width of the rock/cover height blend (noise units, plus the edge softness option). @param 0 1
static const float tileBlendRockHeight=2.2; // Weight of the textures' brightness in the rock/cover blend (times the rock detail option). @param 0 4
static const float tileBlendSnowSoft=2.;     // Extra width of the rock/snow blend (rock fades out of snow, never a cut-out blotch). @param 0 6
static const float tileBlendSnowShift=.35;   // Rock edge shift on snow and ice at snow-rock 0..1. @param 0 1 // Rock noise-edge shift where only part of the surrounding tiles have that rock. @param 0 2
static const float tileBlendMacroScale=.11; // Frequency of the large-scale variation (of the material's 2 m tile). @param 0.02 0.5
static const float tileBlendMacro=1.8;      // Strength of the large-scale variation from the material's own coarse art. @param 0 3
static const float tileBlendFarScale=.4;    // Frequency of the larger-scale read. @param 0.1 1
static const float tileBlendMeanRange=3.;    // Material cache bake: footprint LODs past the padding over which a top-down read becomes the material mean. @param 1 6
static const float tileBlendFarRange=3.;    // Texture LODs over which the larger-scale read takes over fully. @param 0.5 6
static const float tileBlendLoneCore=.1;     // Single-vertex paint: radius it fully owns (tile units, 0.2 m). @param 0 1
static const float tileBlendLoneEdge=.9;     // Single-vertex paint: radius where its blob ends (tile units). @param 0 1.5
static const float tileBlendLoneWarp=.18;    // Single-vertex paint: most the warp may shift its edge (tile units). @param 0 0.5
static const float tileBlendLoneRagged=.4;   // Single-vertex paint: outline wobble (tile units). @param 0 0.6
static const float tileBlendPlaneKeep=.1;    // The weaker side plane is read from this share of the side up (wide cross-fade, no seam where a face turns). @param 0 0.45
static const float tileBlendFootBand=.8;     // m: how far the floor material climbs a face (ragged by noise) @param 0 3
static const float tileBlendEdgeBand=.25;    // Tile units where the blend meets a tile that keeps its art. @param 0 0.5
static const float tileBlendBorderBand=1.;   // Tile units over which the field narrows to the map edge's own vertices. @param 0 3
// Decisions (scores, exposed-rock edge) read the materials' texel brightness
// at one world resolution, the same at every distance and camera angle:
// texels of about this size (m), never finer than the atlas padding allows.
static const float tileBlendDecisionTexel=.04; // @param 0.01 0.25
static const float tileBlendBedBand=.15;     // Liquid depth (m) over which a liquid-bed material fades in below the waterline. @param 0.02 1
// Live Cliff blend options (renderer constants, F10 "Cliff blend"):
// nativeTerrainTess.z rock detail (how much the rock's own coarse texture
// shapes the patch outlines); nativeCliffBlend[0] = rock threshold
// (0.5 - amount), rock
// edge softness, rock patch scale, streak elongation; nativeCliffBlend[1] =
// steepness start and full (slope normal z), snow rock strength, face
// field warp (tile units).
// Edge character by material class (terrain_tile_material_class): ground,
// hard (stone/rock/paving), grass, snow, ice, sand, mud. Looseness: the looser
// material lies on top. Noise: frequency multiplier (snow and sand in broad
// wind-shaped fronts, grass in clumps, dirt in speckles) and peak-to-peak
// score. Detail: own texel brightness -> score. Cracks: weight of a harder
// partner's darkness (snow, grass, sand settle in joints). Soft: score range.
static const uint  tileBlendLoose[7] ={2,0,3,6,1,5,4}; // @param 0 6
static const float tileBlendFreq[7]  ={3.0,1.5,2.2,.7,.9,.6,1.0}; // @param 0 6
static const float tileBlendAmp[7]   ={1.0,.6,1.1,.9,.6,.8,.7}; // @param 0 3
static const float tileBlendDetail[7]={2.5,2.5,2.5,1.0,1.0,1.0,1.5}; // @param 0 6
static const float tileBlendCrack[7] ={.6,0,1.5,1.8,0,1.2,1.0}; // @param 0 4
static const float tileBlendDetailScale=1.4; // Scales the texel-brightness terms with the widened band (texture decides, no veil). @param 0.5 4
static const float tileBlendSoftScale=2.5;  // Widens every material's soft range (height-blend band). @param 0.5 5
static const float tileBlendSoft[7]  ={.12,.2,.15,.4,.25,.35,.3}; // @param 0 1
// The plain cell of a material (slot position bits 0-5, page bits 6-11) in
// the contact atlas; false while that page has not been copied.
bool terrainTileBlendCell(uint v,out TerrainCliffCell k){
 uint slice=(v>>6)&63u,pos=v&63u;
 k.own=false;k.slice=slice;k.width=0;k.lowLod=0;k.marginLod=0;
 float margin=nativeTerrainCliff.w*.125;
 k.lo=float2((pos%8u)*.125+margin,1-(pos/8u+1)*.125+margin);k.size=.125-2*margin;
 float aw,ah,ae,levels;contactAtlas.GetDimensions(0,aw,ah,ae,levels);
 terrainCliffCellLevels(k,aw);
 k.marginLod=log2(max(margin*aw,1));
 return (((slice<32u?nativeContactMapMaterials.x:nativeContactMapMaterials.y)>>(slice&31u))&1u)!=0;
}
// Edge character class of a cell (0..6): flat-only ground (class 7:
// paving, slabs, still water) blends like hard ground.
uint terrainTileBlendEdgeClass(uint v){uint k=(v>>12)&7u;return k==7u?1u:k;}
// World noise per material (+-.5) at ~1.5 m and ~0.65 m divided by the class
// frequency; full quality adds a ~0.27 m octave.
float terrainTileBlendNoise(float2 s,uint id,float frequency,bool full){
 float2 seed=float2(id%61u,(id/61u)%67u)*float2(17.31,29.17);s*=frequency;
 float n=.5*terrainNoise(s*1.3+seed,0)+.3*terrainNoise(s*3.1-seed.yx,1),fine=.5;
 [branch]if(full)fine=terrainNoise(s*7.3+seed*1.7,2);
 return full?n+.2*fine-.5:n*1.25-.5;
}
// The material noise on the surface: the top-down plane on walkable ground,
// the side planes on faces (a top-down coordinate does not change down a
// face, which drew every edge as vertical stripes).
float terrainTileBlendSurfaceNoise(float2 s,float3 p,float3 planes,float2 fw,uint id,float frequency,bool full){
 float n=0;
 [branch]if(planes.z>.002)n+=planes.z*terrainTileBlendNoise(s,id,frequency,full);
 [branch]if(fw.x>.002)n+=fw.x*terrainTileBlendNoise(p.yz*.5,id,frequency,full);
 [branch]if(fw.y>.002)n+=fw.y*terrainTileBlendNoise(p.xz*.5,id,frequency,full);
 return n;
}
// Fast plain read: the plain tile repeated in world space (one tap).
float3 terrainTileBlendTap(TerrainCliffCell k,float2 s,float2 sDx,float2 sDy){
 return terrainCliffTap(k,uint2(0,0),frac(s),sDx,sDy);
}
float3 terrainTileBlendMean(TerrainCliffCell k){
 return terrainCliffTapLevel(k,uint2(0,0),.5,max(log2(max(k.size.x*k.width,1))-1,0));
}
// Share of a material shown as the tile's own art: where the field is nearly
// all that material, inside a plain tile of it and in the band of a tile
// toward such a plain neighbour (one image on the shared edge).
// (own: the tile's grid row; f: the position inside the tile, 0..1.)
float terrainTileBlendOwnArt(uint4 own,float2 f,uint material,float weight,bool plain){
 uint2 ends[4]={uint2(0,2),uint2(1,3),uint2(0,1),uint2(2,3)};
 uint ownCell[4]={own.x&65535u,own.x>>16,own.y&65535u,own.y>>16};
 float dist[4]={f.x,1-f.x,f.y,1-f.y};
 float g=plain&&ownCell[0]==material?1:0;
 [unroll]for(int e=0;e<4;++e)
  if(((own.z>>(8+e))&1u)!=0&&ownCell[ends[e].x]==material&&ownCell[ends[e].y]==material)g=max(g,1-smoothstep(0,tileBlendEdgeBand,dist[e]));
 return g*smoothstep(.8,1,weight);
}
#ifdef __cplusplus
//")hlsl" R"hlsl( //"
#endif

// Large-scale variation from the material's own art: its coarse image at
// about a ninth of the frequency (a tile spans ~18 m), as a brightness
// change about its mean.
float terrainTileBlendMacro(TerrainCliffCell k,float2 s,float3 mean){
 // One tap through a mirrored repeat (continuous at every repeat line) of
 // the coarse image, turned 37 degrees off the tile axes; a three-cell hex
 // read cost three taps for a ~18 m brightness change.
 float2 m=mul(float2x2(.8,-.6,.6,.8),s*tileBlendMacroScale)+float2(37.1,-19.7);
 float2 t=frac(m*.5)*2;float2 q=t<1?t:2-t;
 float3 luma=float3(.2126,.7152,.0722);
 float lm=max(dot(mean,luma),.05);
 return clamp(1+tileBlendMacro*dot(terrainCliffTapLevel(k,uint2(0,0),q,max(k.marginLod-.5,0))-mean,luma)/lm,.6,1.4);
}
// A material's plain art at world tile coordinates s through turned and
// shifted hex cells (the terrain's fill salt), blended with variance kept
// about its mean; `mean` is the cell's average colour (terrainTileBlendMean,
// read once a material by the caller).
float3 terrainTileBlendRead(TerrainCliffCell k,float2 s,float2 sDx,float2 sDy,float3 mean){
 // Softer cell weights than the terrain's (cube, not seventh power): the
 // sharp ones drew the straight hexagon edges across strongly patterned art.
 float3 w;int2 v1,v2,v3;terrainHexGrid(s*terrainHexLattice,w,v1,v2,v3);w=max(w,0);w*=w*w;w/=w.x+w.y+w.z;
 float3 sum=0;float total=0,squares=0;
 // One copy of the cell read: cell v1 with weight w.x is the current one;
 // the cells and weights rotate (no arrays).
 [loop]for(int j=0;j<3;++j){
  [branch]if(w.x>=1.0/64){
   int2 salted=v1+int2(40503,-27427);
   float2x2 r=terrainHexRotation(salted);
   float2 c=terrainHexCentre(v1)/terrainHexLattice;
   float2 q=frac(mul(r,s-c)+c+terrainHash2(salted));
   sum+=w.x*(terrainCliffTap(k,uint2(0,0),q,mul(r,sDx),mul(r,sDy))-mean);total+=w.x;squares+=w.x*w.x;
  }
  int2 rotated=v1;v1=v2;v2=v3;v3=rotated;w=w.yzx;
 }
 return saturate(mean+(squares>0?sum*min(rsqrt(squares),terrainDetailGainLimit/max(total,1e-4)):0));
}

// A material on a face plane (s, derivatives in tile units): turned hex
// cells while a texel covers under about 3 pixels; farther out single taps
// (base scale fading into a larger, turned, mirrored scale with a
// large-scale brightness change). The far reads stay in this branch: a
// heavier far path (hex reads at two scales) slowed every terrain pixel by
// its registers alone, 0.4-0.8 ms at 4K.
// (A single second read turned 37 degrees and scaled 0.71 was rejected: on
// textures with line features the two orientations beat into fine stripes.)
// Fast blend: one plain tap at every distance (the 2 m repeat shows on
// faces). The hex reads were most of the steep-pixel cost: the face is
// read per material, per side plane and for the exposed rock.
// Far de-tiling (terrain_far_detile, nativeTerrainTess.w): the full blend
// keeps the hex reads at every distance (mip-filtered by the footprint,
// held inside the padding) and adds the larger-scale read only where the
// footprint passes what the padding covers, as the cliff projection does.
float3 terrainTileBlendFace(TerrainCliffCell k,float2 s,float2 sDx,float2 sDy,float3 mean,bool full){
 float lod=log2(max(max(length(sDx),length(sDy))*k.size.x*k.width,1e-8));
 // Footprint held inside the cell's padding (no bleed from the next cell).
 float hold=exp2(min(k.marginLod-.5-lod,0));sDx*=hold;sDy*=hold;
 float3 c;
 [branch]if(!full)c=terrainTileBlendTap(k,s,sDx,sDy);
 else if(nativeTerrainTess.w>.5){
  c=terrainTileBlendRead(k,s,sDx,sDy,mean);
  float far=smoothstep(0,tileBlendFarRange,lod-(k.marginLod-.5));
  [branch]if(far>.001){
   float g=tileBlendFarScale;
   float2x2 r=float2x2(.8,-.6,.6,.8);
   float2 m=mul(r,s*g)+float2(11.3,-7.9),t=frac(m*.5)*2,q=t<1?t:2-t;
   float3 farTap=terrainCliffTapLevel(k,uint2(0,0),q,max(k.marginLod-.5,0));
   c=lerp(c,farTap*terrainTileBlendMacro(k,s,mean),far);
  }
 }
 else if(lod<1.5)c=terrainTileBlendRead(k,s,sDx,sDy,mean);
 else{
  // Far (a texel under about a third of a pixel): one plain tap, then the
  // art at a larger scale turned off the tile axes through a mirrored
  // repeat, and a large-scale brightness change from the coarse image, so
  // the 2 m repeat cannot be read across a big distant face.
  float far=smoothstep(1.5,1.5+tileBlendFarRange,lod);
  float g=tileBlendFarScale;
  float2x2 r=float2x2(.8,-.6,.6,.8);
  float2 m=mul(r,s*g)+float2(11.3,-7.9),t=frac(m*.5)*2,q=t<1?t:2-t;
  float2 flip=sign(.5-frac(m*.5))*g;
  float3 farTap=terrainCliffTap(k,uint2(0,0),q,mul(r,sDx)*flip,mul(r,sDy)*flip);
  c=lerp(terrainTileBlendTap(k,s,sDx,sDy),farTap,far);
  c*=lerp(1,terrainTileBlendMacro(k,s,mean),far);
 }
 return c;
}
// Side-plane weights for the reads: the weaker plane counts from
// tileBlendPlaneKeep of the side share (continuous, equal at a half), a wide
// cross-fade so no seam line shows where a face turns.
float2 terrainTileBlendFacePlanes(float3 planes){
 float side=planes.x+planes.y;
 float lo=min(planes.x,planes.y)/max(side,1e-6);
 float kept=saturate((lo-tileBlendPlaneKeep)/(.5-tileBlendPlaneKeep))*lo;
 float2 w=planes.x<planes.y?float2(kept,1-kept):float2(1-kept,kept);
 return w*side;
}
// Both side planes in one loop (one copy of the read code).
float3 terrainTileBlendFaces(TerrainCliffCell k,float3 p,float3 pDx,float3 pDy,float3 planes,bool full,float3 mean){
 float3 c=0;float2 fw=terrainTileBlendFacePlanes(planes);
 [loop]for(int pl=0;pl<2;++pl){
  float w=pl==0?fw.x:fw.y;
  [branch]if(w>.002){
   float2 fs=pl==0?p.yz:p.xz,fdx=pl==0?pDx.yz:pDx.xz,fdy=pl==0?pDy.yz:pDy.xz;
   c+=w*terrainTileBlendFace(k,fs*.5,fdx*.5,fdy*.5,mean,full);
  }
 }
 return c;
}
#ifdef __cplusplus
//")hlsl" R"hlsl( //"
#endif
// Decision reads: the near layout of the colour reads (plain tap top-down;
// on faces the turned hex cells of the full blend, the plain tap of the
// fast one) at the fixed decision level instead of the pixel's footprint,
// so which material wins and where rock shows do not change with distance.
float terrainTileBlendDecisionLod(TerrainCliffCell k){
 return clamp(log2(max(k.size.x*k.width*tileBlendDecisionTexel*.5,1)),0,max(k.marginLod-.5,0));
}
float3 terrainTileBlendReadLevel(TerrainCliffCell k,float2 s,float3 mean,float lod){
 float3 w;int2 v1,v2,v3;terrainHexGrid(s*terrainHexLattice,w,v1,v2,v3);w=max(w,0);w*=w*w;w/=w.x+w.y+w.z;
 float3 sum=0;float total=0,squares=0;
 [loop]for(int j=0;j<3;++j){
  [branch]if(w.x>=1.0/64){
   int2 salted=v1+int2(40503,-27427);
   float2x2 r=terrainHexRotation(salted);
   float2 c=terrainHexCentre(v1)/terrainHexLattice;
   float2 q=frac(mul(r,s-c)+c+terrainHash2(salted));
   sum+=w.x*(terrainCliffTapLevel(k,uint2(0,0),q,lod)-mean);total+=w.x;squares+=w.x*w.x;
  }
  int2 rotated=v1;v1=v2;v2=v3;v3=rotated;w=w.yzx;
 }
 return saturate(mean+(squares>0?sum*min(rsqrt(squares),terrainDetailGainLimit/max(total,1e-4)):0));
}
float3 terrainTileBlendFacesDecision(TerrainCliffCell k,float3 p,float3 planes,bool full,float3 mean,float lod){
 float3 c=0;float2 fw=terrainTileBlendFacePlanes(planes);
 [loop]for(int pl=0;pl<2;++pl){
  float w=pl==0?fw.x:fw.y;
  [branch]if(w>.002){
   float2 fs=(pl==0?p.yz:p.xz)*.5;
   c+=w*(full?terrainTileBlendReadLevel(k,fs,mean,lod):terrainCliffTapLevel(k,uint2(0,0),frac(fs),lod));
  }
 }
 return c;
}
// A material's colour at this pixel: the top-down read (or the tile's own
// art where that is the same material) on walkable ground, and side-plane
// reads on steep faces (planes: x YZ, y XZ, z top; terrainCliffPlaneWeights),
// so steep cover keeps its own texture without stretching.
// Returned without the tile's own art: `share` is its coefficient, so the
// caller can add the finished terrain colour (planes.z * ownArt of it) later.
// `decision` is the read whose texel brightness drives the scores and the
// exposed-rock edge: the side-plane reads on faces, the top-down read on
// walkable ground (never the stretched top-down image on a face, whose
// texels follow height contours and drew nested rings into the masks), at
// the fixed decision level (terrainTileBlendDecisionLod).
// Far top-down read of a material (full blend): once a texel is under about
// a third of a pixel, its plain tile repeated every 2 m reads as a grid, so
// the read cross-fades (over tileBlendFarRange LODs) to the tile at a larger
// scale through a turned, mirrored repeat, times the large-scale brightness
// from its coarse art, as the far face reads do. xyz: that far colour, w:
// its weight (0 near: no taps).
float4 terrainTileBlendFarTop(TerrainCliffCell k,float2 s,float2 sDx,float2 sDy){
 float lod=log2(max(max(length(sDx),length(sDy))*k.size.x*k.width,1e-8));
 float far=smoothstep(1.5,1.5+tileBlendFarRange,lod);
 [branch]if(far<=.001)return 0;
 float g=tileBlendFarScale;
 float2x2 r=float2x2(.8,-.6,.6,.8);
 float2 m=mul(r,s*g)+float2(11.3,-7.9),t=frac(m*.5)*2,q=t<1?t:2-t;
 float2 flip=sign(.5-frac(m*.5))*g;
 // Footprint held inside the cell's padding (no bleed from the next cell).
 float hold=exp2(min(k.marginLod-.5-(lod+log2(g)),0));
 float3 farTap=terrainCliffTap(k,uint2(0,0),q,mul(r,sDx)*flip*hold,mul(r,sDy)*flip*hold);
 return float4(farTap*terrainTileBlendMacro(k,s,terrainTileBlendMean(k)),far);
}
// beyond: footprint LODs past what the atlas padding covers (material cache
// bake only; live pixels fade the blend out there instead): the top-down
// read is held inside the padding and turns into the material's mean over
// tileBlendMeanRange (a footprint as wide as the tile averages to it).
// The texel brightness read the decisions use (contested or a face).
float3 terrainTileBlendDecision(TerrainCliffCell k,float2 s,float3 p,float3 planes,bool full,float3 mean){
 const float lod=terrainTileBlendDecisionLod(k);
 [branch]if(planes.z<.998)return terrainTileBlendFacesDecision(k,p,planes,full,mean,lod)/max(1-planes.z,1e-4);
 return terrainCliffTapLevel(k,uint2(0,0),frac(s),lod);
}
float3 terrainTileBlendMaterial(TerrainCliffCell k,float2 s,float2 sDx,float2 sDy,float3 p,float3 pDx,float3 pDy,
  float3 planes,bool full,float ownArt,float3 mean,float beyond,out float share){
 float3 c=0,top=0;share=planes.z*ownArt;
 [branch]if(planes.z>.002&&ownArt<.999){
  // Far de-tiling: the larger-scale read crossfades in by the footprint
  // as near; past the padding the plain read is held, and only past what
  // the larger-scale read's own padding covers does the colour turn to the mean.
  [branch]if(beyond>0&&full&&nativeTerrainTess.w>.5){
   float hold=exp2(-beyond);top=terrainTileBlendTap(k,s,sDx*hold,sDy*hold);
   float4 farTop=terrainTileBlendFarTop(k,s,sDx,sDy);top=lerp(top,farTop.rgb,farTop.w);
   top=lerp(top,mean,saturate((beyond+log2(tileBlendFarScale))/tileBlendMeanRange));
  }
  else if(beyond>0){float hold=exp2(-beyond);top=lerp(terrainTileBlendTap(k,s,sDx*hold,sDy*hold),mean,saturate(beyond/tileBlendMeanRange));}
  else{
   top=terrainTileBlendTap(k,s,sDx,sDy);
   [branch]if(full){float4 farTop=terrainTileBlendFarTop(k,s,sDx,sDy);top=lerp(top,farTop.rgb,farTop.w);}
  }
  c+=planes.z*(1-ownArt)*top;
 }
 [branch]if(planes.z<.998)c+=terrainTileBlendFaces(k,p,pDx,pDy,planes,full,mean);
 return c;
}
// Exposed rock on steep faces: the tile's nearest ground/stone family shows
// through the cover in irregular patches and down-slope wash marks. The mask
// is smooth noise only (features of about half a metre and more): thin
// texel lines of the rock or the cover never decide it (they drew slashes,
// fragments and nested rectangles), and steepness scales the amount softly
// instead of setting a threshold (a threshold followed the per-triangle
// plane weights and drew zig-zag slivers). On snow and ice, rock shows only
// faintly and never a stone much darker than the snow. Rock cover stays
// itself. Returns the exposure (0..1) of the rock colour `rc`.
// The rock mask's noise on one face plane (h horizontal, v height, metres).
float terrainTileBlendRockNoise(float h,float v){
 // Wash marks (elongated down the face by the streak option), patches
 // about 2 m, and a 0.3 m octave so the edge is ragged; the scale option
 // sizes all of them.
 h/=nativeCliffBlend[0].z;v/=nativeCliffBlend[0].z;
 // Warped and turned lattice: value noise keeps its lattice axes, and
 // stretched down the face those drew long straight vertical edges.
 float2 wv=2*terrainNoise2(float2(h,v)*.35,3)-1;
 float h0=h+.9*wv.x,v0=v+.9*wv.y;h=.94*h0+.34*v0;v=-.34*h0+.94*v0;
 float e=nativeCliffBlend[0].w;
 float streak=.6*terrainNoise(float2(h*.8,v*.8/e),5)+.4*terrainNoise(float2(h*1.7,v*1.7/e),6)-.5;
 float patch=.6*terrainNoise(float2(h,v)*.45,7)-.3;
 float ragged=.12*(terrainNoise(float2(h,v)*3.3,8)-.5);
 return .9*streak+patch+ragged;
}
// `presence` (0..1): how much of the surrounding tiles' cliff has this rock;
// less moves the noise edge so the rock thins out in patches (it never fades
// to a see-through veil).
float terrainTileBlendRock(uint rock,float snowShare,float presence,float coverHeight,float3 p,float3 pDx,float3 pDy,float3 planes,float slopeZ,bool full,out float3 rc){
 rc=0;
 float side=planes.x+planes.y;
 uint rockClass=(rock>>12)&7u;
 // Steepness moves the noise edge (less steep: less rock, in ragged
 // texture-driven patches) instead of fading the rock's opacity, which drew
 // the slope contour at the cliff foot as a soft straight line.
 float steep=side>.05?smoothstep(nativeCliffBlend[1].x,nativeCliffBlend[1].y,slopeZ):0;
 float amount=saturate(steep*6);
 if(rock==0xffffu||amount<=0)return 0;
 // Noise per face plane, blended by the read weights. (Blending the planes'
 // coordinates instead mixed absolute world positions with weights that
 // change across the face, which squeezed the noise into thin lines where
 // the dominant plane turns.)
 float2 fw=terrainTileBlendFacePlanes(planes);
 float n=0;
 [branch]if(fw.x>.002)n+=fw.x*terrainTileBlendRockNoise(p.y,p.z);
 [branch]if(fw.y>.002)n+=fw.y*terrainTileBlendRockNoise(p.x,p.z);
 // Height blend: the noise sets where rock lies, and across a wide band
 // the two textures' own brightness decides which one shows (bright stone
 // rises out of the cover, the cover settles in the rock's dark joints).
 // The rock detail option scales the brightness term; edge softness adds
 // to the band width.
 float width=(nativeCliffBlend[0].y+tileBlendRockBand)*(1+tileBlendSnowSoft*snowShare),detailWeight=nativeTerrainTess.z;
 float m=n/side-nativeCliffBlend[0].x-tileBlendRockPresence*(1-presence)-tileBlendSteepShift*(1-steep);
 // Snow and ice: the snow-rock option moves the edge (less rock, never a
 // see-through veil); at 0 snow faces show none.
 m-=tileBlendSnowShift*(1-nativeCliffBlend[1].z)*snowShare;
 amount*=lerp(1,saturate(4*nativeCliffBlend[1].z),snowShare);
 if(amount<=0||m<=-.5*width-.5*tileBlendRockHeight*detailWeight)return 0;
 TerrainCliffCell k;
 if(!terrainTileBlendCell(rock,k))return 0;
 float3 luma=float3(.2126,.7152,.0722);
 float3 mean=terrainTileBlendMean(k);
 // The edge's brightness term at the decision level (the same at every distance).
 float rockHeight=clamp(dot(terrainTileBlendFacesDecision(k,p,planes,full,mean,terrainTileBlendDecisionLod(k))/side-mean,luma),-.25,.25);
 m+=tileBlendRockHeight*detailWeight*(rockHeight-coverHeight);
 float a=saturate(m/width+.5);
 float exposure=amount*a*a*(3-2*a);
 // The rock colour only where it shows (the caller weights it by the exposure).
 [branch]if(exposure>0)rc=terrainTileBlendFaces(k,p,pDx,pDy,planes,full,mean)/side;
 return exposure;
}
// The cliff materials of tile c of the blend grid (rows h..2h of the contact
// tiles): its four corners and, on steep tiles, the materials at the top and
// bottom of its slope; `rock` is its exposed-rock material (the lowest stone
// cell among them, else the lowest ground cell unless z bit 12 limits rock to
// stone; none: 0xffff).
// The tile's blend row (r: the row of tile clamp(c), terrainTileBlendCliffRow).
uint4 terrainTileBlendCliffRow(int2 c,uint gw,uint gh){
 return contactTiles.Load(int3(clamp(c,0,int2(gw,gh)-1)+int2(0,gh),0));
}
// Whether material `cell` belongs to a tile's cliff, from its six cells
// packed as (row x, row y, top | bottom << 16); the map load stores none
// (0xffff) as top and bottom of every tile that is not steep.
bool terrainTileBlendCliffHas(uint3 s,uint cell){
 return cell==(s.x&65535u)||cell==(s.x>>16)||cell==(s.y&65535u)||cell==(s.y>>16)||cell==(s.z&65535u)||cell==(s.z>>16);
}
// The exposed rock of tile row r: the map load chose it among the six cells
// (z bits 13-15: its index 0-3 corner, 4 top, 5 bottom; 7 none;
// terrain_tile_blend_rows).
uint terrainTileBlendCliffRock(uint4 r){
 uint i=(r.z>>13)&7u;
 return i==7u?0xffffu:i==0u?r.x&65535u:i==1u?r.x>>16:i==2u?r.y&65535u:i==3u?r.y>>16:i==4u?r.z>>16:r.w>>16;
}
#ifdef __cplusplus
//")hlsl" R"hlsl( //"
#endif

// The blend of an active-map terrain pixel (p world, t its unprocessed tile
// sample) as float4(colour without the tile art, share of the tile art): the
// caller adds share x the finished terrain colour, and skips the terrain
// material stage when the share is 0. Tiles away from transitions and tiles that keep their art return t.
// Steep tiles (z bit 3) also own their faces: `faces` tells the caller that
// the side projection is done here (terrain cliffs then leave the pixel).
// nativeTerrainCliff.z: 1 fast (one world-space tap a material, two noise
// octaves), 2 full (hex anti-tiling reads, three octaves). The material
// cache's bake is always full (it pays once per page); live pixels keep the
// setting, so fast live ground beside a cached page can place a boundary
// slightly differently (fast has no small warp or fine noise octave).
// A plan offset (tile units) as distance along the surface: stretched down
// the slope by 1/n.z (at most 4x), so a blob on a steep face stays about a
// tile across on the face instead of running its whole height.
float2 terrainTileBlendOnSurface(float2 d,float3 n){
 float h=length(n.xy);
 if(h<1e-3)return d;
 float2 g=n.xy/h;float along=dot(d,g);
 return d+g*along*(1/max(n.z,.05)-1);
}
#ifdef __cplusplus
//")hlsl" R"hlsl( //"
#endif
// A tile that is known but inactive (one material everywhere within reach):
// the field cannot change here, so the tile keeps its art, which repeats
// every 2 m at a distance (variant tiles of one material are not plain fill,
// so the terrain material's far rule does not reach them). Far, with the full
// blend, the material's de-tiled far read takes over instead; live pixels
// fade it with the top-down blend's distance fade, the bake keeps it.
float4 terrainTileBlendInactive(uint4 own,float3 p,float3 pDx,float3 pDy){
#ifdef NATIVE_TERRAIN_BAKE_PASS
 bool fullFar=true;
#else
 bool fullFar=nativeTerrainCliff.z>1.5;
#endif
 TerrainCliffCell km;
 [branch]if((own.z&1u)!=0&&fullFar&&terrainTileBlendCell(own.x&65535u,km)){
  float2 kDx=pDx.xy*.5,kDy=pDy.xy*.5;
  float4 farTop=terrainTileBlendFarTop(km,(p.xy+nativeContactLayout.xy)*.5,kDx,kDy);
#ifndef NATIVE_TERRAIN_BAKE_PASS
  farTop.w*=saturate(km.marginLod-.5-log2(max(max(length(kDx),length(kDy))*km.size.x*km.width,1e-8)));
#endif
  [branch]if(farTop.w>.001)return float4(farTop.w*farTop.rgb,1-farTop.w);
 }
 return float4(0,0,0,1);
}
// The blend's four candidate slots (uint4 cells, float4 weights; no arrays).
// The first true component (x before y before z before w).
bool4 terrainTileBlendFirst(bool4 m){return m&&!bool4(false,m.x,m.x||m.y,m.x||m.y||m.z);}
// The cell of the first slot in m.
uint terrainTileBlendPick(uint4 v,bool4 m){return m.x?v.x:m.y?v.y:m.z?v.z:v.w;}
// Adds weight w of material v: its slot, else the first open slot, else the
// weakest slot when v is stronger (force: always, for a single-vertex blob,
// which then takes over that slot); the weight that leaves the list goes to
// dropped.
void terrainTileBlendAdd(inout uint4 sc,inout float4 wq,inout float dropped,uint v,float w,bool force){
 bool4 hit=sc==v;
 [branch]if(any(hit)){wq+=hit?w:0;return;}
 bool4 open=sc==0xffffu,slot;
 [branch]if(any(open))slot=terrainTileBlendFirst(open);
 else{
  float m=min(min(wq.x,wq.y),min(wq.z,wq.w));
  if(!force&&w<=m){dropped+=w;return;}
  slot=terrainTileBlendFirst(wq==m);dropped+=m;
 }
 sc=slot?v:sc;wq=slot?w:wq;
}
// bedDepth: liquid depth over the pixel (signed, at rest plus the live shift).
// footRise: the pixel's height over the local floor (owned_terrain_foot_rise).
// facetKeep: terrainCliffFacetKeep of the pixel.
float4 terrainTileBlendColor(float3 p,float3 pDx,float3 pDy,float3 slope,float2 facets,float3 t,float bedDepth,float footRise,float facetKeep,out bool faces){
 faces=false;
 uint gw,gh;contactTiles.GetDimensions(gw,gh);gh/=2;
 float2 s=p.xy*.5;int2 c=int2(floor(s));
 if(gw==0||any(c<0)||c.x>=int(gw)||c.y>=int(gh))return float4(0,0,0,1);
 uint4 own=contactTiles.Load(int3(c.x,c.y+int(gh),0));
 [branch]if((own.z&5u)!=5u)return terrainTileBlendInactive(own,p,pDx,pDy);
 // p and s are map-local (grid lookups); warps, noise and material reads
 // use the layout frame (pw, sw: map-local plus the map's saved-layout
 // origin on whole tiles, nativeContactLayout), the same whether the map
 // is active or a neighbour, and continuous into the map it adjoins.
 float3 pw=float3(p.xy+nativeContactLayout.xy,p.z);float2 sw=pw.xy*.5;
#ifdef NATIVE_TERRAIN_BAKE_PASS
 bool full=true;
#else
 bool full=nativeTerrainCliff.z>1.5;
#endif
 // Faces follow the Terrain cliffs switch (nativeTerrainCliff.x): off keeps
 // the authored top-down mapping there, as without the blend.
 // (From the slope alone, not the tile's steep bit: a per-tile switch drew
 // the tile edge wherever a face crossed into a tile without one.)
#ifdef NATIVE_TERRAIN_FLAT
 // Flat tile class (the renderer draws steep-class tiles with the full variant):
 // every vertex normal is inside the walk limit, so the planes are top-down.
 float3 planes=float3(0,0,1);
#else
 float3 planes=nativeTerrainCliff.x>0?terrainCliffFacetPlanes(terrainCliffProjectionPlanes(slope,facets),pDx,pDy,facetKeep):float3(0,0,1);
#endif
 float2 fw=terrainTileBlendFacePlanes(planes);
 // Materials of this cliff (steep tiles): the material at the top and at
 // the bottom of the slope (terrain_tile_blend_rows); with the tile's own
 // corners they are the only materials a face may show.
 float2 sDx=pDx.xy*.5,sDy=pDy.xy*.5;
 // Beyond the footprint the atlas padding covers, the authored tile remains.
 TerrainCliffCell k0;terrainTileBlendCell(own.x&65535u,k0);
 float foot=log2(max(max(length(sDx),length(sDy))*k0.size.x*k0.width,1e-8));
 // (Top-down share only: faces read through their own planes, whose reads
 // hold their footprint inside the padding. Fading faces to the authored
 // tile put stretched art in facet-shaped slivers at grazing views.)
 // The material cache's bake keeps the blend at every distance (it bakes a
 // page once, so far pages hold the field instead of the repeating
 // transition tiles): its top-down reads are held and turn into the
 // material mean (terrainTileBlendMaterial).
// Every distance keeps the blend's layout (no fade to the authored tile art
// by footprint: that replaced the field with the tiles' own transitions at
// a distance). Past what the atlas padding covers, the top-down reads are
// held inside it and turn into the material mean, a colour change only,
// as in the material cache's bake.
 float fade=1,beyond=foot-(k0.marginLod-.5);
 float2 f=s-c;
 float dist[4]={f.x,1-f.x,f.y,1-f.y};
 uint ownCell[4]={own.x&65535u,own.x>>16,own.y&65535u,own.y>>16};
 // Toward a tile that keeps its art the whole blend yields to this tile's art.
 [unroll]for(int e=0;e<4;++e)if(((own.z>>(4+e))&1u)!=0)fade*=smoothstep(0,tileBlendEdgeBand,dist[e]);
 if(fade<=0)return float4(0,0,0,1);
 // Toward the map edge the field narrows to the edge's own vertices, which
 // adjoining maps share (the editor names one family at each): warps, the
 // kernel across the edge, single-vertex blobs and the noise and texel
 // scores (seeded per map) fade out over tileBlendBorderBand, so both maps
 // draw the same material boundaries on the line. (Fading to the authored
 // art there showed baked triangle masks along zone2/zone3obr.)
 float2 edgeT=smoothstep(0,tileBlendBorderBand,float2(min(s.x,float(gw)-s.x),min(s.y,float(gh)-s.y)));
 float edgeKeep=min(edgeT.x,edgeT.y);
 // The tile terms (cliff sets, exposed rock, tile art) hand over to values
 // both maps share over a band whose reach varies with world-space noise
 // between half a tile and one tile (layout frame, so both maps agree): a
 // fixed band drew the handover contour as a straight line along the edge.
 // It stays inside the border tiles, which are always active.
 float tileKeep=1;
 [branch]if(edgeKeep<1){
  float2 tileT=smoothstep(0,tileBlendBorderBand,float2(min(s.x,float(gw)-s.x),min(s.y,float(gh)-s.y))*(1+terrainNoise(sw*.6,28)));
  tileKeep=min(tileT.x,tileT.y);
 }
 // Warped field lookup: two octaves of world-space displacement.
 float2 q=s+edgeKeep*(tileBlendWarpLarge*(2*terrainNoise2(sw/3,9)-1)
           +(full?tileBlendWarpSmall*(2*terrainNoise2(sw*1.4,11)-1):0)); // fast: large octave only
 // On faces the field also moves with height (face-plane noise, two
 // octaves, the face warp option in tile units), so a material boundary
 // seen on a face does not run straight down it: the vertex field is flat
 // in plan, so without this every face pixel above one plan point shares
 // its material and the edges are vertical lines.
 [branch]if(fw.x+fw.y>.002){
  // A third, ~0.6 m octave: with only the broad ones a boundary still ran
  // straight down several metres of face. Each plane's noise only where
  // its weight is above 0 (the weaker plane's is exactly 0 on most faces).
  float2 a=0,b=0,c3=0;
  [branch]if(fw.x>0){a=fw.x*(2*terrainNoise2(pw.yz*.18,13)-1);b=fw.x*(2*terrainNoise2(pw.yz*.7,17)-1);c3=fw.x*(2*terrainNoise2(pw.yz*1.7,21)-1);}
  [branch]if(fw.y>0){a+=fw.y*(2*terrainNoise2(pw.xz*.18,15)-1);b+=fw.y*(2*terrainNoise2(pw.xz*.7,19)-1);c3+=fw.y*(2*terrainNoise2(pw.xz*1.7,23)-1);}
  q+=edgeKeep*nativeCliffBlend[1].w*(a+.6*b+.3*c3);
 }
 // Kernel over the 3x3 vertices around the nearest one: quadratic B-spline
 // mixed with bilinear. Tiles n-1 and n of each axis hold those vertices.
 int2 n=int2(floor(q+.5));float2 d=q-n;
 float3 kx=lerp(float3(.5*(.5-d.x)*(.5-d.x),.75-d.x*d.x,.5*(.5+d.x)*(.5+d.x)),float3(max(-d.x,0),1-abs(d.x),max(d.x,0)),tileBlendBilinear);
 float3 ky=lerp(float3(.5*(.5-d.y)*(.5-d.y),.75-d.y*d.y,.5*(.5+d.y)*(.5+d.y)),float3(max(-d.y,0),1-abs(d.y),max(d.y,0)),tileBlendBilinear);
 // Across a near map edge only the edge vertex column/row counts.
 [branch]if(edgeKeep<1){
  int2 edgeLine=int2(s.x*2<float(gw)?0:int(gw),s.y*2<float(gh)?0:int(gh));
  float3 lx=float3(n.x-1==edgeLine.x,n.x==edgeLine.x,n.x+1==edgeLine.x),ly=float3(n.y-1==edgeLine.y,n.y==edgeLine.y,n.y+1==edgeLine.y);
  kx=lerp(lx,kx,edgeT.x);ky=lerp(ly,ky,edgeT.y);
 }
 // Candidate list: the field's materials in four register slots (cell sc,
 // weight wq, in first-seen order like the old nine-entry list). A fifth
 // distinct material evicts the weakest slot when it is stronger, else it is
 // dropped; `dropped` carries the weight lost either way (terrain_blend_debug
 // 6 shows where it exceeds the tail). No arrays: every per-material value
 // is a float4/uint4 slot vector, read with constant indices or rotated.
 uint4 sc=0xffffu;float4 wq=0;float dropped=0;float art=0;
 [unroll]for(int ty=0;ty<2;++ty)[unroll]for(int tx=0;tx<2;++tx){
  int2 want=n-1+int2(tx,ty),tc=clamp(want,0,int2(gw,gh)-1),over=want-tc;
  uint4 r=contactTiles.Load(int3(tc.x,tc.y+int(gh),0));
  // Tile (tx,ty) holds kernel vertices tx..tx+1; the far one only for tx==1.
  [unroll]for(int k=0;k<4;++k){
   int vx=tx+(k&1),vy=ty+(k>>1);
   if(!(((k&1)!=0&&tx!=1)||((k>>1)!=0&&ty!=1))){
    float wk=kx[vx]*ky[vy];
    // Past the grid the clamped tile's nearer corner stands in, so the
    // map's edge vertex is read where the kernel asks for it.
    int2 corner=clamp(int2(k&1,k>>1)+over,0,1);int ci=corner.x+2*corner.y;
    uint v=ci==0?r.x&65535u:ci==1?r.x>>16:ci==2?r.y&65535u:r.y>>16;
    if(v==0xffffu)art+=wk;else terrainTileBlendAdd(sc,wq,dropped,v,wk,false);
   }
  }
 }
 // A material the map painted on one vertex only (rows w bits 8-11) keeps
 // a blob on that vertex: the warps would carry it off and the kernel and
 // tail thin it away. Within about 1.2 m it takes over the field (full to
 // 0.6 m); a short slice of the warp shapes its edge without moving it,
 // and the usual noise and texel scores roughen that edge.
 // (Any of the tile's four corners: a blob reaches past the half-tile
 // where another vertex is nearer, and a nearest-vertex test cut it square.)
 uint loneCell=0xffffu;float loneCore=0;
 [branch]if((own.w&0xf00u)!=0){
  float2 shift=q-s;shift*=min(1,tileBlendLoneWarp/max(length(shift),1e-4));
  // A ~0.35 m wobble roughens the outline (no clean disc).
  float ragged=tileBlendLoneRagged*(terrainNoise(sw*3.7,25)-.5);
  // Walkable ground and slopes only: on a cliff face the plan-view blob
  // would run the face's height (a pasted slab); the face shows its
  // corner materials through the normal blend instead.
  float loneGround=smoothstep(.1,.6,planes.z)*edgeKeep;
  [unroll]for(int k0=0;k0<4;++k0){
   float core=((own.w>>(8+k0))&1u)!=0?loneGround*(1-smoothstep(tileBlendLoneCore,tileBlendLoneEdge,length(terrainTileBlendOnSurface(s+shift-(c+int2(k0&1,k0>>1)),slope))+ragged)):0;
   [branch]if(core>0){
    uint lone=ownCell[k0];
    terrainTileBlendAdd(sc,wq,dropped,lone,0,true);
    // The others keep a little weight toward the edge, so the outline is a
    // contested blend (noise and texel scores) like any other boundary.
    bool4 hit=sc==lone;
    wq=hit?max(wq,core*(1+tileBlendTail)):wq*(1-core*core);
    if(core>loneCore){loneCore=core;loneCell=lone;}
   }
  }
 }
#ifdef __cplusplus
//")hlsl" R"hlsl( //"
#endif
 fade*=saturate(1-3*art);
 if(fade<=0)return float4(0,0,0,1);
 bool4 valid=sc!=0xffffu;
 uint4 kind=(sc>>12)&7u;
 // Thin kernel tails drop out (they only fed islands far from the
 // boundary), so most pixels see a single material.
 // On faces, materials that do not belong to this cliff fade out with the
 // side share (none on walkable ground, so the tile edge stays continuous).
 // Which materials belong to the cliff comes from the four tiles around
 // the warped lookup, blended bilinearly (a per-tile choice drew every tile
 // edge where two tiles' cliffs differ as a straight cut).
 float faceShare=1-planes.z;
 [branch]if(faceShare>.002){
  // One row a tile (its cells, top and bottom, and the rock the map load
  // chose), then membership by comparison.
  int2 b0=int2(floor(q-.5));float2 bf=q-.5-b0;
  float4 foreign=0;
  [unroll]for(int t4=0;t4<4;++t4){
   int2 o=int2(t4&1,t4>>1);
   float wt=(o.x!=0?bf.x:1-bf.x)*(o.y!=0?bf.y:1-bf.y);
   uint4 r=terrainTileBlendCliffRow(b0+o,gw,gh);
   uint3 set=uint3(r.x,r.y,(r.z>>16)|(r.w&0xffff0000u));
   bool4 member=sc==(set.x&65535u)||sc==(set.x>>16)||sc==(set.y&65535u)||sc==(set.y>>16)||sc==(set.z&65535u)||sc==(set.z>>16);
   foreign+=member?0:wt;
  }
  // Toward a near map edge the cliff sets fade out of it (each map holds
  // only its own tiles' sets, so they differ across the edge): on the line
  // no material is foreign on either map.
  foreign*=tileKeep;
  // A single-vertex paint is this tile's own corner: its blob is never
  // faded as foreign (the neighbours' sets cut it along tile lines).
  wq*=sc==loneCell?1-faceShare*foreign*(1-loneCore):1-faceShare*foreign;
  // Man-made and flat-only ground (paving, slabs, still water) belongs on
  // the flat top, never on the face: it fades out with the side share
  // unless nothing else of this cliff is left there.
  float natural=dot(valid&&kind!=7u?wq:0,1);
  [branch]if(natural>tileBlendTail)wq*=valid&&kind==7u?1-faceShare:1;
 }
 // Liquid-bed materials (cell bit 15: underwater families) show only under
 // the liquid: above the waterline they fade out unless nothing else of
 // the field is left there (a bed painted above water keeps its art).
 float bedKeep=saturate(bedDepth/tileBlendBedBand);
 [branch]if(bedKeep<1){
  bool4 bed=valid&&(sc&0x8000u)!=0u;
  float dry=dot(valid&&!bed?wq:0,1);
  [branch]if(dry>tileBlendTail)wq*=bed?bedKeep:1;
 }
 wq=valid?max(wq-tileBlendTail,0):0;
 float wsum=dot(wq,1);
 if(wsum<=0)return float4(0,0,0,1);
 float wmax=max(max(wq.x,wq.y),max(wq.z,wq.w));
 bool4 topMask=terrainTileBlendFirst(wq==wmax);
 uint topCell=terrainTileBlendPick(sc,topMask);
 wmax/=wsum;
 bool plain=(own.z&2u)!=0;
 // Where the field is (nearly) all one material, a plain tile of it and the
 // band of a tile toward such a plain neighbour show the tile's own art.
 // One material: no scores or noise (contested is 0 and only it is read);
 // a plain tile of it on walkable ground is its own art.
 [branch]if(wmax>=.999){
  if(planes.z>=.998&&nativeTerrainTess.y<.5&&terrainTileBlendOwnArt(own,f,topCell,wmax,plain)>=.999)return float4(0,0,0,1);
  wq=topMask?wq:0;
  wsum=dot(wq,1);
 }
 // Contested: noise and texel terms fade out where one material dominates.
 float contested=wmax>=.999?0:saturate(3*(1-wmax));
 float3 luma=float3(.2126,.7152,.0722);
 wq/=wsum;
 // Per slot texel brightness (texQ), from the decision read: it feeds the
 // scores and the exposed-rock edge only (its world resolution is fixed; the
 // pixel's footprint and the tile art's own mip never decide). One copy of
 // the read in a loop that rotates the slot vectors (slot x is the current
 // one; four turns restore the order). The colours are read after the
 // scores, only for the slots the soft range keeps.
 const bool decide=contested>0||planes.z<.998;
 float4 texQ=0;
 [loop]for(int i=0;i<4;++i){
  [branch]if(wq.x>0){
   TerrainCliffCell k;
   if(!terrainTileBlendCell(sc.x,k))return float4(0,0,0,1);
   [branch]if(decide){
    float3 mean=terrainTileBlendMean(k);
    texQ.x=clamp(dot(terrainTileBlendDecision(k,sw,pw,planes,full,mean)-mean,luma),-.25,.25);
   }
  }
  sc=sc.yzwx;wq=wq.yzwx;texQ=texQ.yzwx;
 }
#ifdef __cplusplus
//")hlsl" R"hlsl( //"
#endif
 // Scores (slot x current, the slot vectors rotated as above; order keeps
 // each slot's first-seen index for ties).
 uint4 order=uint4(0,1,2,3);float4 score=-9;
 [loop]for(int j=0;j<4;++j){
  [branch]if(wq.x>0){
   uint kc=terrainTileBlendEdgeClass(sc.x);
   // Partner: the strongest other material (first-seen on ties); a looser
   // material settles in its dark texels.
   float best=0;uint bestOrder=9u,partnerClass=0;float partnerTexel=0;
   [unroll]for(int o2=1;o2<4;++o2)
    if(wq[o2]>best||(wq[o2]==best&&best>0&&order[o2]<bestOrder)){best=wq[o2];bestOrder=order[o2];partnerClass=terrainTileBlendEdgeClass(sc[o2]);partnerTexel=texQ[o2];}
   float crack=0;
   if(best>0&&tileBlendLoose[partnerClass]<tileBlendLoose[kc])crack=-tileBlendCrack[kc]*tileBlendDetailScale*partnerTexel;
   float terms=0;
   [branch]if(contested>0)terms=edgeKeep*(tileBlendDetail[kc]*tileBlendDetailScale*texQ.x+tileBlendAmp[kc]*terrainTileBlendSurfaceNoise(sw,pw,planes,fw,sc.x,tileBlendFreq[kc],full)+crack);
   score.x=wq.x+contested*saturate(3*wq.x)*terms;
  }
  sc=sc.yzwx;wq=wq.yzwx;texQ=texQ.yzwx;order=order.yzwx;score=score.yzwx;
 }
 // Soft range of the looser of the two top scores (first-seen on ties).
 float best0=max(max(score.x,score.y),max(score.z,score.w));
 bool4 firstMask=terrainTileBlendFirst(score==best0);
 uint firstCell=terrainTileBlendPick(sc,firstMask);
 uint c1=terrainTileBlendEdgeClass(firstCell),c2=c1;
 float4 others=firstMask?-9:score;
 float runner=max(max(others.x,others.y),max(others.z,others.w));
 [branch]if(runner>-9)c2=terrainTileBlendEdgeClass(terrainTileBlendPick(sc,terrainTileBlendFirst(others==runner)));
 // Height blend: the soft range is widened so the textures' brightness
 // decides over a wide band (the looser material settles into the other's
 // dark gaps and thins out over ~0.5-1 m) instead of a near-binary cut.
 float depth=tileBlendSoft[tileBlendLoose[c1]>=tileBlendLoose[c2]?c1:c2]*tileBlendSoftScale;
 float4 a=max(score-best0+depth,0);a*=a;
 float total=dot(a,1);
 if(total<=0)return float4(0,0,0,1);
 // Colour and tile-art share summed over the slots by their blend share a,
 // read only where a is above 0 (a slot at 0 adds nothing; the cell read
 // succeeded in the decision loop). Summed as they are read: four
 // per-slot colour vectors held across the reads cost registers.
 float3 cover=0;float share=0,rockShown=0;
 // Cover heights for the exposed rock (the blend shares a); texel
 // brightness is not needed past this point.
 float coverHeight=dot(a,texQ)/total;
 // The slots' own-art shares before the colour reads, so the tile row, the
 // position in the tile and the field weights are not held across them.
 float4 ownQ=0;
 // Toward a near map edge the tile art (each map's own tiles) gives way to
 // the materials both maps draw from the shared edge vertices.
 [unroll]for(int i3=0;i3<4;++i3)if(a[i3]>0)ownQ[i3]=tileKeep*terrainTileBlendOwnArt(own,f,sc[i3],wq[i3],plain);
 [loop]for(int i2=0;i2<4;++i2){
  [branch]if(a.x>0){
   TerrainCliffCell k;terrainTileBlendCell(sc.x,k);
   float slotShare;float3 mean=0;
   // The mean feeds the face reads and held far reads.
   [branch]if(planes.z<.998||beyond>0)mean=terrainTileBlendMean(k);
   float3 color=terrainTileBlendMaterial(k,sw,pDx.xy*.5,pDy.xy*.5,pw,pDx,pDy,planes,full,ownQ.x,mean,beyond,slotShare);
   cover+=a.x*color;share+=a.x*slotShare;
  }
  sc=sc.yzwx;ownQ=ownQ.yzwx;a=a.yzwx;
 }
 cover/=total;share/=total;
 [branch]if(planes.z<.998){
  // Exposed rock: each of the four surrounding tiles' rock (a stone
  // material of its cliff, else a ground one), weighted like the cliff
  // materials above, so the rock and its amount change smoothly between
  // tiles; a tile without one adds no rock. Equal rocks are read once.
  float e=0,rcWeight=0;float3 rcSum=0;
  // The four tiles' rocks and weights (rows read again here, not held in
  // registers across the material reads; none where the face share is 0).
  uint4 cliffRock=0xffffu;float4 cliffW=0;
  // Toward a near map edge the tiles' rocks (each map's own tiles) give way
  // to the stone of the edge's own vertices, which the adjoining map shares
  // (same family, same class), along the edge between its two nearest
  // vertices, so both maps expose the same rock on the line.
  uint4 edgeRock=0xffffu;float4 edgeW=0;
  [branch]if(faceShare>.002){
   int2 b0=int2(floor(q-.5));float2 bf=q-.5-b0;
   [unroll]for(int t4=0;t4<4;++t4){
    int2 o=int2(t4&1,t4>>1);
    cliffW[t4]=tileKeep*(o.x!=0?bf.x:1-bf.x)*(o.y!=0?bf.y:1-bf.y);
    cliffRock[t4]=terrainTileBlendCliffRock(terrainTileBlendCliffRow(b0+o,gw,gh));
   }
   [branch]if(tileKeep<1){
    // The nearer edge: a row (y) or a column (x) of edge vertices.
    bool row=edgeT.y<=edgeT.x;
    float along=row?s.x:s.y;uint count=row?gw:gh;
    int k=clamp(int(floor(along)),0,int(count)-1);float g=saturate(along-k);
    bool low=row?s.y*2<float(gh):s.x*2<float(gw);
    int2 tc=row?int2(k,low?0:int(gh)-1):int2(low?0:int(gw)-1,k);
    uint4 r=terrainTileBlendCliffRow(tc,gw,gh);
    // Its two edge vertices: (0,0)(1,0) / (0,1)(1,1) of a row tile,
    // (0,0)(0,1) / (1,0)(1,1) of a column tile.
    uint2 v=row?(low?uint2(r.x&65535u,r.x>>16):uint2(r.y&65535u,r.y>>16))
               :(low?uint2(r.x&65535u,r.y&65535u):uint2(r.x>>16,r.y>>16));
    [unroll]for(int v2=0;v2<2;++v2){
     uint cell=v[v2];
     // Stone, or ground unless the row limits rock to stone (bit 12, the
     // map's rock rule; maps with editor profiles agree on it); never a bed.
     uint kindV=(cell>>12)&7u;
     if(cell!=0xffffu&&(kindV==1u||(kindV==0u&&(r.z&4096u)==0u))&&(cell&0x8000u)==0u){
      edgeRock[v2]=cell;edgeW[v2]=(1-tileKeep)*(v2==0?1-g:g);
     }
    }
    if(edgeRock.y==edgeRock.x){edgeW.x+=edgeW.y;edgeW.y=0;edgeRock.y=0xffffu;}
   }
  }
  [unroll]for(int r4=0;r4<4;++r4)[unroll]for(int r5=r4+1;r5<4;++r5)
   if(cliffRock[r5]==cliffRock[r4]){cliffW[r4]+=cliffW[r5];cliffW[r5]=0;}
  // An edge rock one of the tiles also has adds to that tile's weight.
  [unroll]for(int r7=0;r7<2;++r7)[unroll]for(int r8=0;r8<4;++r8)
   if(edgeW[r7]>0&&edgeRock[r7]==cliffRock[r8]){cliffW[r8]+=edgeW[r7];edgeW[r7]=0;}
  // The cover's classes (from the slot cells, not held across the reads).
  bool4 valid=sc!=0xffffu;uint4 kind=(sc>>12)&7u;
  // One copy of the rock read: tile x is the current one (rotated).
  // The tiles' four rocks, then the edge's two (rotated into slot x).
  [loop]for(int r6=0;r6<6;++r6){
   if(r6==4){cliffRock=edgeRock;cliffW=edgeW;}
   [branch]if(cliffW.x>.001&&cliffRock.x!=0xffffu){
    // Rock shows through the share of the cover that is not stone and not
    // its own class (rock cover stays itself), so it thins out smoothly
    // where the cover blends into such a material.
    uint rockClass=(cliffRock.x>>12)&7u;
    float through=dot(valid&&kind!=1u&&kind!=rockClass?a:0,1),snowy=dot(valid&&(kind==3u||kind==4u)?a:0,1);
    // No cover the rock can show through: its exposure is 0 (no read).
    [branch]if(through>0){
     float3 rc;float er=through/total*terrainTileBlendRock(cliffRock.x,snowy/total,saturate(2*cliffW.x),coverHeight,pw,pDx,pDy,planes,slope.z,full,rc);
     e=max(e,er);rcSum+=er*cliffW.x*rc;rcWeight+=er*cliffW.x;
    }
   }
   cliffRock=cliffRock.yzwx;cliffW=cliffW.yzwx;
  }
  cover=lerp(cover,rcSum/max(rcWeight,1e-5),e);share*=1-e;faces=fade>.5;
  rockShown=e;
  // Wall foot: the ground in front of and below the face climbs it over a
  // ragged band of tileBlendFootBand. The ground is the field's vertex
  // material nearest to the plan point moved downhill (smooth slope normal)
  // by the pixel's rise over the local floor, read there folded up, so the
  // face continues the floor's texels at the fold and fades into the wall
  // above. A ground of the face's own winning material changes nothing.
  // Every input is interpolated per vertex (slope, rise) or a function of
  // world position, so the band never follows a triangle edge.
  // Toward a map edge it fades with the field's other terms (edgeKeep), so
  // the active map and its neighbour meet without it on the shared line;
  // the folded point is clamped into the grid (where edgeKeep is 0 anyway).
  [branch]if(faceShare*edgeKeep>.002&&footRise<1.6*tileBlendFootBand&&length(slope.xy)>1e-3){
   float2 down=normalize(slope.xy);
   float2 sf=clamp((p.xy+down*footRise)*.5,0,float2(gw,gh)-1e-3);int2 cf=int2(floor(sf));float2 ff=sf-cf;
   {
    uint4 r=contactTiles.Load(int3(cf.x,cf.y+int(gh),0));
    uint4 ground=uint4(r.x&65535u,r.x>>16,r.y&65535u,r.y>>16);
    float4 gw4=float4((1-ff.x)*(1-ff.y),ff.x*(1-ff.y),(1-ff.x)*ff.y,ff.x*ff.y);
    float2 along=float2(-down.y,down.x);
    float edge=tileBlendFootBand*(.55+.9*terrainNoise(float2(dot(pw.xy,along)*1.1,pw.z*.6)+float2(57.3,-19.1),8));
    float foot=faceShare*edgeKeep*(1-smoothstep(.35*edge,edge,footRise));
    float2 fs=(pw.xy+down*footRise)*.5;
    float2 fdx=(pDx.xy+down*pDx.z)*.5,fdy=(pDy.xy+down*pDy.z)*.5;
    // Each corner of the ground cell by its bilinear weight, less the share
    // the face already shows of that material (continuous: no step where
    // the ground or the face's winner changes).
    float3 g=0;float gsum=0;
    [unroll]for(int k6=0;k6<4;++k6){
     uint m=ground[k6];
     float w=gw4[k6]*(1-dot(sc==m?a:0,1)/total);
     TerrainCliffCell kg;
     [branch]if(foot>.002&&m!=0xffffu&&w>.002&&terrainTileBlendCell(m,kg)){g+=w*terrainTileBlendTap(kg,fs,fdx,fdy);gsum+=w;}
    }
    [branch]if(gsum>.002){foot*=gsum;cover=lerp(cover,g/gsum,foot);share*=1-foot;}
   }
  }
 }
 // Temporary diagnostic (terrain_blend_debug, nativeTerrainTess.y), drawn
 // unlit (alpha -1 tells nativePS): 1 field: R top material weight,
 // G contested, B tile-art share; 2 planes: R YZ, G XZ, B top; 3 masks:
 // R exposed rock, G cover, B fade; 4 the winning material as a hashed
 // colour (white where exposed rock wins); 6 candidate overflow: red where
 // a material beyond the four slots carried weight above the tail (its
 // weight in red), the field's top weight in green elsewhere.
 [branch]if(nativeTerrainTess.y>.5){
  uint h=terrainHash(int2(firstCell,77));
  float3 v=nativeTerrainTess.y<1.5?float3(wmax,contested,1-fade+fade*share):
           nativeTerrainTess.y<2.5?planes:nativeTerrainTess.y<3.5?float3(rockShown,1-rockShown,fade):
           nativeTerrainTess.y<4.5?(rockShown>.5?float3(1,1,1):float3(h&255u,(h>>8)&255u,(h>>16)&255u)/255.0):
           (dropped>tileBlendTail?float3(.5+.5*saturate(dropped),0,0):float3(0,.25*wmax,0));
  return float4(v,-1);
 }
 // The pixel is fade*cover + (1-fade + fade*share) * (finished terrain colour).
 return float4(fade*cover,1-fade+fade*share);
}
#ifdef __cplusplus
//")hlsl"
#endif
