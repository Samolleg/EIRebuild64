// Terrain 2.0 ground (terrain2.h, option terrain2). Embedded into the native
// pixel shader by Source/terrain2.h after the terrain material blend, and
// into the ground cover pass (terrain2_cover.h, TERRAIN2_COVER).
// The __cplusplus guard lines make the file a C++ raw string where that
// header includes it. Keep each piece between guard lines under 15000 bytes.
#ifdef __cplusplus
R"hlsl( //"
#endif
// Each map reads its own material cells (terrain_tile_blend.h); protected
// families keep authored top art, with modern projection on the sides.
// The look is terrain 1.0's blend (terrain_tile_blend.hlsl;
// user verdict 2026-10-31: its edges, cliffs and rock read more natural than
// the corner masks), on 2.0's own reads:
//  - field: a warped kernel (quadratic B-spline mixed with bilinear) over the
//    3x3 grid vertices around the pixel, the corners' materials summed by
//    cell, less a small tail; on faces the lookup also moves with height,
//    so a material boundary wanders across a face. Lone painted vertices
//    keep a ragged blob.
//  - edges: score = weight plus, where materials are contested, each
//    material's texel height (its baked terrain2Height at a fixed world
//    resolution, so edges do not move with distance), world noise by
//    material class, and a looser material settling in a harder partner's
//    low texels; the top score wins and the others fade over the looser
//    class's soft range.
//  - faces: side projections (terrain_cliff.hlsl planes, facet split
//    included, chosen by the caller) through turned hex cells; materials
//    outside the surrounding tiles' cliff sets and flat-only ground fade
//    out on faces.
//  - rock: each surrounding tile's exposed rock (the map load's choice)
//    through face-plane noise (streaks, patches, ragged edge), steepness and
//    presence moving its edge, height-blended against the cover; the floor
//    climbs the wall foot over a ragged band.
// Every term is a continuous weight: no tile seams, no switch.
// nativeTerrain2: x 1 while on, w the entries of the map table.
// Map table (set_native_terrain_frame): entry 0 the active map, then the
// neighbour maps; per entry four records in nativeTerrain2Maps: the world
// rectangle; the world origin and 1 once its rows are in (else the marker
// colour); the layout origin and the copied atlas slices (two uints); where
// its grid, atlas and material heights are (terrain2Index). Every entry is
// read through the same indexed code: one terrain2Ground, no per-entry copy.
// Map borders (terrain2Color): every map's ground is its own result from its
// own grid, atlas and heights (never another map's materials); within
// t2BorderBand of an edge another map continues, the finished results mix
// by mirrored weights (half each on the edge, products at corners).
// terrain_quality Medium and Low (nativeTerrainMaterial.z 0): the large
// warp only and two noise octaves. Low (nativeTerrainMaterial.w 1, T2_LOW;
// constant in the D3D11 Low specialisation, a uniform test elsewhere)
// also cuts, each continuously (no new seam or step): hex detail from the
// two strongest cells (the weakest weight subtracted), the two strongest
// materials (the third share subtracted), no texel-height decisions (edges
// from the weights and class noise) and no wall-foot band.
#define T2_LOW (nativeTerrainMaterial.w>0)
SamplerState terrain2Sampler:register(s9);            // the contact sampler
float4 terrain2Record(uint entry,uint r){return nativeTerrain2Maps[4*entry+r];}
// Record 3 (update_terrain2_table): w the atlas width (asfloat) on both
// backends; D3D11: x the first atlas and height slice | pool << 16, y the
// grid's width | height << 16, z its first row in the stacked grid; D3D12:
// x, y, z the grid's, atlas's and heights' bindless indices (z 0 while the
// heights are not readable: the flat height .5).
uint4 terrain2Index(uint m){return asuint(terrain2Record(m,3));}
// The map terrain2Ground is resolving: its record 3 and copied slices, read
// once per map (terrain2Ground) instead of from the frame buffer at every
// texture read.
static uint4 t2Map=0;static uint2 t2MapReady=0;
float terrain2AtlasWidth(uint m){return asfloat(t2Map.w);}
#ifdef EID3D12
uint4 terrain2Tile(uint m,int3 c){Texture2D<uint4> t=ResourceDescriptorHeap[NonUniformResourceIndex(nativeBindlessIndex(t2Map.x))];return t.Load(c);}
uint2 terrain2TileSize(uint m){Texture2D<uint4> t=ResourceDescriptorHeap[NonUniformResourceIndex(nativeBindlessIndex(t2Map.x))];uint w,h;t.GetDimensions(w,h);return uint2(w,h);}
#define T2_ATLAS(m) Texture2DArray<float4> t=ResourceDescriptorHeap[NonUniformResourceIndex(nativeBindlessIndex(t2Map.y))]
float3 terrain2AtlasGrad(uint m,float3 uvw,float2 dx,float2 dy){T2_ATLAS(m);return t.SampleGrad(terrain2Sampler,uvw,dx,dy).rgb;}
float3 terrain2AtlasLevel(uint m,float3 uvw,float lod){T2_ATLAS(m);return t.SampleLevel(terrain2Sampler,uvw,lod).rgb;}
#define T2_HEIGHT(m,R) uint i=t2Map.z;float r=.5;[branch]if(i!=0u){ \
 Texture2DArray<float> t=ResourceDescriptorHeap[NonUniformResourceIndex(nativeBindlessIndex(i))];r=R;}return r;
float terrain2HeightLevel(uint m,float3 uvw,float lod){T2_HEIGHT(m,t.SampleLevel(terrain2Sampler,uvw,lod))}
float terrain2HeightGrad(uint m,float3 uvw,float2 dx,float2 dy){T2_HEIGHT(m,t.SampleGrad(terrain2Sampler,uvw,dx,dy))}
#define T2_SLICE_BASE 0
#else
// D3D11 (SM 5.0 indexes no resources at run time): every map's grid in one
// stacked texture, each atlas a slice range of the pool of its format, size
// and levels, its heights the same range of the parallel R8 pool
// (renderer_terrain.cpp terrain2_pool_range); a pool branch, no map switch.
Texture2D<uint4> t2Grids:register(t85);
Texture2DArray<float4> t2Atlas0:register(t86);Texture2DArray<float4> t2Atlas1:register(t87);
Texture2DArray<float4> t2Atlas2:register(t88);Texture2DArray<float4> t2Atlas3:register(t89);
Texture2DArray<float> t2Height0:register(t90);Texture2DArray<float> t2Height1:register(t91);
Texture2DArray<float> t2Height2:register(t92);Texture2DArray<float> t2Height3:register(t93);
// (c.z, the mip, is 0 from m: a literal 0 made fxc hoist one zeroed address
// register per load site out of the band-partner loop.)
uint4 terrain2Tile(uint m,int3 c){return t2Grids.Load(int3(c.x,c.y+int(t2Map.z),int(m>>8)));}
uint2 terrain2TileSize(uint m){uint y=t2Map.y;return uint2(y&65535u,y>>16);}
#define T2_SLICE_BASE float(t2Map.x&65535u)
// The statement R(k) on map m's pool k (uvw.z already the pool slice:
// terrain2Cell). The general terrain pixel programs read pool 0 only: the
// branch around every texture read cost ~0.2 ms on the save1027 frame, and
// one pool is the usual case. A map in another pool draws the marker there
// (terrain2Ground) until the NATIVE_TERRAIN2_POOLS specialisation, chosen
// while a second pool exists (renderer_device.cpp), is ready. The cache
// bake, cover pass and snow piles always take the branch.
#if defined(NATIVE_TERRAIN2_POOLS)||!defined(NATIVE_TERRAIN_PS)
#define T2_POOL(m,R) uint pool=t2Map.x>>16; \
 [branch]if(pool==0u){R(0)}else if(pool==1u){R(1)}else if(pool==2u){R(2)}else{R(3)}
#else
#define T2_POOL(m,R) R(0)
#define T2_ONE_POOL 1
#endif
#define T2_ATLAS_GRAD(k) r=t2Atlas##k.SampleGrad(terrain2Sampler,uvw,dx,dy).rgb;
float3 terrain2AtlasGrad(uint m,float3 uvw,float2 dx,float2 dy){float3 r=0;T2_POOL(m,T2_ATLAS_GRAD)return r;}
#define T2_ATLAS_LEVEL(k) r=t2Atlas##k.SampleLevel(terrain2Sampler,uvw,lod).rgb;
float3 terrain2AtlasLevel(uint m,float3 uvw,float lod){float3 r=0;T2_POOL(m,T2_ATLAS_LEVEL)return r;}
#define T2_HEIGHT_LEVEL(k) r=t2Height##k.SampleLevel(terrain2Sampler,uvw,lod);
float terrain2HeightLevel(uint m,float3 uvw,float lod){float r=0;T2_POOL(m,T2_HEIGHT_LEVEL)return r;}
#define T2_HEIGHT_GRAD(k) r=t2Height##k.SampleGrad(terrain2Sampler,uvw,dx,dy);
float terrain2HeightGrad(uint m,float3 uvw,float2 dx,float2 dy){float r=0;T2_POOL(m,T2_HEIGHT_GRAD)return r;}
#endif
// Surface (pseudo-PBR S3): with t2Surface set (the caller: the material
// level is on, or the cache bake), each material read also takes its baked
// height at the colour's own gradients (mipped like the colour), and
// terrain2Ground hands out the blended height and the roughness of its
// cell classes (nativeMaterialTerrainRough) with the read's crevices (its
// detail darker than the cell mean) packed in: surf = (height, roughness).
static bool t2Surface=false;
static const float t2CavityGain=4; // detail luma below the mean to crevice share
#ifdef TERRAIN2_COVER
float terrain2Roughness(uint v){return 1;}
float2 terrain2SurfacePack(float3 s){return float2(s.x,1);}
#else
float terrain2Roughness(uint v){uint k=(v>>12)&7u;return nativeMaterialTerrainRough[k>>2][k&3u];}
// (height, crevice, class roughness) to (height, roughness): crevices read rough.
float2 terrain2SurfacePack(float3 s){return float2(s.x,lerp(s.z,1,saturate(s.y*nativeMaterialTerrainCavity)));}
#endif
uint2 terrain2Ready(uint m){return t2MapReady;}
// The frame values' lane (native_shader.h nativeFrameLane): 0, but varying
// per map-loop iteration, so fxc reads them inside the loop instead of
// hoisting every per-frame constant used in it into a register.
#ifdef TERRAIN2_COVER
#define T2_FRAME_LANE(m)
#else
#define T2_FRAME_LANE(m) nativeFrameLane=(m)>>16
#endif
static const float3 terrain2Marker=float3(1,0,1);
// Terrain 1.0's blend constants (terrain_tile_blend.hlsl tileBlend*, the
// values it was accepted with).
static const float t2WarpLarge=.9,t2WarpSmall=.35; // tile units of the ~6 m and ~1.4 m field warps
static const float t2Bilinear=.6;      // share of the bilinear kernel (single vertices keep their spot)
static const float t2Tail=.1;          // field weight a material needs before it counts
static const float t2LoneCore=.1,t2LoneEdge=.9,t2LoneWarp=.18,t2LoneRagged=.4; // single-vertex blob (tile units)
static const float t2PlaneKeep=.1;     // the weaker side plane reads from this share of the side up
static const float t2FootBand=.8;      // m the floor material climbs a face
static const float t2DecisionTexel=.04;// m: the texel size every decision reads at
static const float t2HeightGain=.6;    // baked height (.5 +- amp/2) to the texel term (1.0 read luma about the mean)
static const float t2BedBand=.15,t2BedDry=.6; // liquid-bed fade (m of liquid; dry weight where it is gone)
// t2SnowSoft: how much snow widens the rock edge (1.0's 2 tripled it: rock
// through snow on cave walls and cliffs read as soft dark smears, user
// report on 267b9302, gz14k cave).
static const float t2RockPresence=.4,t2SteepShift=.6,t2RockBand=.4,t2RockHeight=2.2,t2SnowSoft=.75,t2SnowShift=.35;
// Edge character by material class (cell bits 12-14): ground, hard, grass,
// snow, ice, sand, mud. Looser lies on top; noise frequency and amplitude;
// texel term; cracks (weight of a harder partner's low texels); soft range.
static const uint  t2Loose[7]   ={2,0,3,6,1,5,4};
static const float t2Freq[7]    ={3.0,1.5,2.2,.7,.9,.6,1.0};
static const float t2NoiseAmp[7]={1.0,.6,1.1,.9,.6,.8,.7};
static const float t2Detail[7]  ={2.5,2.5,2.5,1.0,1.0,1.0,1.5};
static const float t2Crack[7]   ={.6,0,1.5,1.8,0,1.2,1.0};
// Snow's soft range is half of 1.0's .4: its edges read blurry next to the
// other classes (user verdict on 267b9302: crisper than 1.0 wanted).
static const float t2Soft[7]    ={.12,.2,.15,.2,.25,.35,.3};
static const float t2DetailScale=1.4,t2SoftScale=2.5;
static const float t2BorderBand=3;     // half-width of the map border band, metres (1.5 tiles)
// Class of a cell (flat-only ground, class 7, blends like hard ground).
uint terrain2Class(uint v){uint k=(v>>12)&7u;return k==7u?1u:k;}
// Material noise (+-.5) at ~1.5 m and ~0.65 m over the class frequency;
// full adds a ~0.27 m octave. Seeded by the map's own cell.
float terrain2Noise(float2 s,uint id,float frequency,bool full){
 float2 seed=float2(id%61u,(id/61u)%67u)*float2(17.31,29.17);s*=frequency;
 float n=.5*terrainNoise(s*1.3+seed,0)+.3*terrainNoise(s*3.1-seed.yx,1),fine=.5;
 [branch]if(full)fine=terrainNoise(s*7.3+seed*1.7,2);
 return full?n+.2*fine-.5:n*1.25-.5;
}
// On the surface: the top plane on walkable ground, the side planes on
// faces (w: plane weights top, yz, xz).
float terrain2SurfaceNoise(float2 s,float3 p,float3 w,uint id,float frequency,bool full){
 float n=0;
 [branch]if(w.x>.002)n+=w.x*terrain2Noise(s,id,frequency,full);
 [branch]if(w.y>.002)n+=w.y*terrain2Noise(p.yz*.5,id,frequency,full);
 [branch]if(w.z>.002)n+=w.z*terrain2Noise(p.xz*.5,id,frequency,full);
 return n;
}
// Side-plane read weights: the weaker plane counts from t2PlaneKeep of the
// side share (a wide cross-fade where a face turns).
float2 terrain2FacePlanes(float3 planes){
 float side=planes.x+planes.y;
 float lo=min(planes.x,planes.y)/max(side,1e-6);
 float kept=saturate((lo-t2PlaneKeep)/(.5-t2PlaneKeep))*lo;
 return (planes.x<planes.y?float2(kept,1-kept):float2(1-kept,kept))*side;
}
// A plan offset (tiles) as distance along the surface (blobs stay a tile
// across on a slope).
float2 terrain2OnSurface(float2 d,float3 n){
 float h=length(n.xy);
 if(h<1e-3)return d;
 float2 g=n.xy/h;return d+g*dot(d,g)*(1/max(n.z,.05)-1);
}
// Rock mask noise on one face plane (h horizontal, v height, metres): wash
// streaks (elongated by the streak option), ~2 m patches, a ragged octave,
// on a warped and turned lattice (no straight lattice edges down a face).
float terrain2RockNoise(float h,float v){
 h/=nativeCliffBlend[0].z;v/=nativeCliffBlend[0].z;
 float2 wv=2*terrainNoise2(float2(h,v)*.35,3)-1;
 float h0=h+.9*wv.x,v0=v+.9*wv.y;h=.94*h0+.34*v0;v=-.34*h0+.94*v0;
 float e=nativeCliffBlend[0].w;
 float streak=.6*terrainNoise(float2(h*.8,v*.8/e),5)+.4*terrainNoise(float2(h*1.7,v*1.7/e),6)-.5;
 float patch=.6*terrainNoise(float2(h,v)*.45,7)-.3;
 float ragged=.12*(terrainNoise(float2(h,v)*3.3,8)-.5);
 return .9*streak+patch+ragged;
}
// The field's four candidate slots (no arrays): first true component, the
// cell of the first slot in m, and adding weight w of cell v (its slot, an
// open one, else the weakest when v is stronger; force: always).
// (Written lane by lane: the vector form made fxc keep constant lanes live
// across the map loop.)
bool4 terrain2First(bool4 m){bool a=m.x,b=a||m.y,c=b||m.z;return bool4(a,m.y&&!a,m.z&&!b,m.w&&!c);}
uint terrain2Pick(uint4 v,bool4 m){return m.x?v.x:m.y?v.y:m.z?v.z:v.w;}
void terrain2Add(inout uint4 sc,inout float4 wq,inout bool4 authored,uint v,float w,bool keep,bool force){
 bool4 hit=sc==v&&authored==keep;
 [branch]if(any(hit)){wq+=hit?w:0;return;}
 bool4 open=sc==0xffffu,slot;
 [branch]if(any(open))slot=terrain2First(open);
 else{
  float m=min(min(wq.x,wq.y),min(wq.z,wq.w));
  if(!force&&w<=m)return;
  slot=terrain2First(wq==m);
 }
 sc=slot?v:sc;wq=slot?w:wq;authored=slot?keep:authored;
}
// The exposed rock of a tile's blend row (z bits 13-15: corner 0-3, 4 top,
// 5 bottom, 7 none; terrain_tile_blend_rows).
uint terrain2CliffRock(uint4 r){
 uint i=(r.z>>13)&7u;
 return i==7u?0xffffu:i==0u?r.x&65535u:i==1u?r.x>>16:i==2u?r.y&65535u:i==3u?r.y>>16:i==4u?r.z>>16:r.w>>16;
}
//")hlsl" R"hlsl( //"
// Material cell v of table entry m: terrainTileBlendCellFrom with that map's
// grid, atlas and copied slices.
bool terrain2Cell(uint m,uint v,out TerrainCliffCell k){
 uint e=0;
 [branch]if(((v>>6)&63u)==63u){uint2 g=terrain2TileSize(m);e=(g.y&1u)!=0&&(v&63u)<g.x?terrain2Tile(m,int3(v&63u,g.y-1,0)).x:0xffffffffu;}
 uint donor;bool copied=terrainTileBlendCellFrom(v,e,terrain2AtlasWidth(m),terrain2Ready(m),k,donor);
 k.slice+=T2_SLICE_BASE; // the map's first slice in its pool, once a cell (not at every read)
 [branch]if(donor!=0xffffffffu){
  float lod=max(log2(max(k.size.x*k.width,1))-1,0);
  float3 mean=terrain2AtlasLevel(m,float3(terrainCliffCellUv(k,.75),k.slice),lod);
  terrainTileBlendDonorCell(k,donor);k.slice+=T2_SLICE_BASE;
  k.shift=mean-terrain2AtlasLevel(m,float3(terrainCliffCellUv(k,.5),k.slice),lod);
 }
 return copied;
}
float3 terrain2Tap(uint m,TerrainCliffCell k,float2 q,float2 dx,float2 dy){
 return saturate(terrain2AtlasGrad(m,float3(terrainCliffCellUv(k,q),k.slice),dx*k.size,dy*k.size)+k.shift);
}
// A footprint (tile units) held inside the atlas padding: all cells of the
// contact atlas have one size (terrainTileBlendCellFrom), so one factor serves
// every material of a map.
float terrain2Hold(float2 dx,float2 dy,float aw){
 float margin=nativeTerrainCliff.w*.125;
 float lod=log2(max(max(length(dx),length(dy))*(.125-2*margin)*aw,1e-8));
 return exp2(min(log2(max(margin*aw,1))-.5-lod,0));
}
// The turned hex cells (Mikkelsen hex tiling; terrainTileBlendRead's
// lattice and salt) at tile coordinates s of one projection plane, shared
// by every material (cell weights, turns, gradients).
struct Terrain2Hex{float3 w;float2 q[3],dx[3],dy[3];float gain;};
Terrain2Hex terrain2HexCells(float2 s,float2 sDx,float2 sDy,float aw){
 Terrain2Hex x;
 float hold=terrain2Hold(sDx,sDy,aw);sDx*=hold;sDy*=hold;
 float3 w;int2 v1,v2,v3;terrainHexGrid(s*terrainHexLattice,w,v1,v2,v3);w=max(w,0);w*=w*w;w/=w.x+w.y+w.z;
 // Cells under 1/64 drop out continuously (subtracted, renormalised); Low
 // subtracts the weakest cell's weight, so two cells are read.
 w=max(w-(T2_LOW?min(w.x,min(w.y,w.z)):1.0/64),0);w/=T2_LOW?max(w.x+w.y+w.z,1e-6):w.x+w.y+w.z; // (Low: zero at an exact three-way tie)
 float total=0,squares=0;
 [unroll]for(int j=0;j<3;++j){
  x.w[j]=w.x;
  int2 salted=v1+int2(40503,-27427);
  float2x2 r=terrainHexRotation(salted);
  float2 c=terrainHexCentre(v1)/terrainHexLattice;
  x.q[j]=frac(mul(r,s-c)+c+terrainHash2(salted));x.dx[j]=mul(r,sDx);x.dy[j]=mul(r,sDy);
  total+=w.x;squares+=w.x*w.x;
  int2 rotated=v1;v1=v2;v2=v3;v3=rotated;w=w.yzx;
 }
 x.gain=squares>0?min(rsqrt(squares),terrainDetailGainLimit/max(total,1e-4)):0;
 return x;
}
// The three projection planes' hex cells (0 top, world xy; 1 the yz plane;
// 2 the xz plane), made once a pixel (the side planes where they have
// weight) and shared by every read; the plane weights come with each read.
// (Made again by each read, they cost more ALU than the registers saved:
// +0.35-0.5 ms on the save1027 frame, D3D11.)
struct Terrain2Planes{Terrain2Hex h[3];};
// A canonical own plain source needs only its cell and map readiness;
// neither placed-art UVs nor fold/donor art belongs in this decode.
bool terrain2OwnPlainCell(uint m,uint cell,out TerrainCliffCell k){
 uint donor;bool valid=terrainTileBlendCellFrom(cell,0,terrain2AtlasWidth(m),terrain2Ready(m),k,donor);
 k.slice+=T2_SLICE_BASE;
 return cell!=0xffffu&&((cell>>6)&63u)!=63u&&valid;
}
// Protected plain art: the placed tile's affine basis carries its MPR turn
// and authored 2 m scale. Homogeneous tiles retain their exact physical slot;
// on mixed tiles only the selected family's own unmixed plain source is read.
// This is the owned sampling rule, not a reconstruction of baked layer UVs.
// The existing family field and boundary scores still mix the contributions.
bool terrain2AuthoredCell(uint m,uint cell,uint4 art,bool exact,float2 f,
                         out TerrainCliffCell k,out float2 uv,out float2 du,out float2 dv){
 float2 a=float2(art.x&65535u,art.x>>16)/65535.0;
 du=float2(art.y&65535u,art.y>>16)/65535.0-a;
 dv=float2(art.z&65535u,art.z>>16)/65535.0-a;
 uint slice=art.w&255u; // policy bits never form part of an atlas slice
 if(exact){
  uint2 c=min(uint2((a+.5*(du+dv))*8),7u);
  cell=(cell&0xf000u)|(slice<<6)|(7u-c.y)*8u+c.x;
 }
 bool valid=terrain2OwnPlainCell(m,cell,k);uv=0;
 if(slice>=63u||!valid)return false;
 if(exact)uv=a+f.x*du+f.y*dv;
 else{
  float2 lo=min(min(a,a+du),min(a+dv,a+du+dv));
  float2 span=max(abs(du)+abs(dv),1e-6);
  uv=terrainCliffCellUv(k,(a-lo+f.x*du+f.y*dv)/span);
  du=du/span*k.size;dv=dv/span*k.size;
 }
 return true;
}
float3 terrain2AuthoredRead(uint m,uint cell,uint4 art,bool exact,float2 f,float2 dx,float2 dy,out float2 hc){
 TerrainCliffCell k;float2 uv,du,dv;hc=float2(.5,0);
 if(cell==0xffffu||!terrain2AuthoredCell(m,cell,art,exact,f,k,uv,du,dv))return terrain2Marker;
 float3 mean=terrain2AtlasLevel(m,float3(terrainCliffCellUv(k,.5),k.slice),max(log2(max(k.size.x*k.width,1))-1,0));
#ifdef TERRAIN2_COVER
 return terrain2CoverValue(cell,mean);
#else
 float2 gx=dx.x*du+dx.y*dv,gy=dy.x*du+dy.y*dv;
 float3 col=terrain2AtlasGrad(m,float3(uv,k.slice),gx,gy);
 [branch]if(t2Surface)hc.x=terrain2HeightGrad(m,float3(uv,k.slice),gx,gy);
 hc.y=saturate(dot(mean-col,float3(.2126,.7152,.0722))*t2CavityGain);
 return col;
#endif
}
// A material's texel term for the decisions (scores, rock edge): its baked
// height through the planes' hex cells at the fixed decision level
// (t2DecisionTexel), so which material wins does not change with distance;
// a signed term (0 at the bake's middle, clamped like 1.0's luma term).
// luma (0..1): the share of terrain 1.0's term instead, the colour's luma
// about the cell mean at the same level and cells. Snow blends take it
// (terrain2Ground): the snow and soil bakes are smooth (sand parameters,
// high-passed, integrated slopes), so their heights alone decided snow edges
// in soft 0.3-0.5 m blobs where 1.0's per-texel luma gave a crisp, grainy
// edge (user report on 02b04e90, Ingos; A/B save931, 2026-10-31).
float terrain2Decide(uint m,uint cell,Terrain2Planes P,float3 wp,float luma,
                     bool authored,uint4 art,bool exact,float2 f){
 TerrainCliffCell k;float h=0,l=0,t=0;
 [branch]if(authored){
  // Only the top keeps the placed affine art. Side decisions use the same
  // canonical material and world projections as the modern colour below.
  float2 uv,du,dv;
  [branch]if(wp.x>0){
   if(cell==0xffffu||!terrain2AuthoredCell(m,cell,art,exact,f,k,uv,du,dv))return 0;
   float margin=log2(max(nativeTerrainCliff.w*.125*k.width,1));
   float lod=clamp(log2(max(k.size.x*k.width*t2DecisionTexel*.5,1)),0,max(margin-.5,0));
   [branch]if(luma<1)h=wp.x*terrain2HeightLevel(m,float3(uv,k.slice),lod);
   [branch]if(luma>0){
    float3 mean=terrain2AtlasLevel(m,float3(terrainCliffCellUv(k,.5),k.slice),max(log2(max(k.size.x*k.width,1))-1,0));
    l=wp.x*dot(terrain2AtlasLevel(m,float3(uv,k.slice),lod)-mean,float3(.2126,.7152,.0722));
   }
   t=wp.x;
  }
  wp.x=0;
  [branch]if(wp.y+wp.z<=0){
   if(t<=0)return 0;
   return lerp(clamp(t2HeightGain*(h/t-.5),-.25,.25),clamp(l/t,-.25,.25),luma);
  }
  if(!terrain2OwnPlainCell(m,cell,k))return 0;
 }else{
  [branch]if(cell==0xffffu||!terrain2Cell(m,cell,k))return 0;
 }
 float margin=log2(max(nativeTerrainCliff.w*.125*k.width,1));
 float lod=clamp(log2(max(k.size.x*k.width*t2DecisionTexel*.5,1)),0,max(margin-.5,0));
 float3 mean=0;
 [branch]if(luma>0)mean=terrain2AtlasLevel(m,float3(terrainCliffCellUv(k,k.fold>0?.75:.5),k.slice),max(log2(max(k.size.x*k.width,1))-1,0))+k.shift;
 [unroll]for(int p=0;p<3;++p){
  [branch]if(wp[p]>.002){
   Terrain2Hex x=P.h[p];
   [unroll]for(int j=0;j<3;++j){
    [branch]if(x.w[j]>0){
     float c=wp[p]*x.w[j];float3 uvw=float3(terrainCliffCellUv(k,x.q[j]),k.slice);
     [branch]if(luma<1)h+=c*terrain2HeightLevel(m,uvw,lod);
     [branch]if(luma>0)l+=c*dot(terrain2AtlasLevel(m,uvw,lod)+k.shift-mean,float3(.2126,.7152,.0722));
     t+=c;
    }
   }
  }
 }
 if(t<=0)return 0;
 return lerp(clamp(t2HeightGain*(h/t-.5),-.25,.25),clamp(l/t,-.25,.25),luma);
}
// One material's colour through the planes' hex cells (wp: plane weights),
// detail kept about its mean across cells and planes. A missing or not yet
// copied cell: the marker.
// hc (S3, t2Surface): the read's height (its hex cells' baked heights at
// the colour's gradients, 0.5 the bake's middle) and crevice share.
// unvaried: only the protected top's colour share, excluded from the
// caller's macro variation. The shared hex loop owns every side read.
float3 terrain2Read(uint m,uint cell,Terrain2Planes P,float3 wp,bool authored,uint4 art,bool exact,
                    float2 f,float2 dx,float2 dy,out float2 hc,out float3 unvaried){
 TerrainCliffCell k;hc=float2(.5,0);unvaried=0;
 bool3 projected=wp>.002; // retain eligibility before authored side normalization
 float top=0,side=1;float3 topColor=0;float2 topHc=float2(.5,0);
 [branch]if(authored){
  top=wp.x;side=wp.y+wp.z;
  [branch]if(top>0)topColor=terrain2AuthoredRead(m,cell,art,exact,f,dx,dy,topHc);
  unvaried=top*topColor;
  [branch]if(side<=0){hc=topHc;return topColor;}
  if(!terrain2OwnPlainCell(m,cell,k)){
   hc=top*topHc+side*hc;return unvaried+side*terrain2Marker;
  }
  // Same normalized side split as exposed rock; top/side colour and
  // surface values then mix with the original projection shares.
  wp=float3(0,wp.yz)/max(side,1e-6);
  projected.x=false;
 }else{
  [branch]if(cell==0xffffu||!terrain2Cell(m,cell,k))return terrain2Marker;
 }
 // terrainTileBlendMean: a folded cell's middle is its quadrant's corner.
 float3 mean=saturate(terrain2AtlasLevel(m,float3(terrainCliffCellUv(k,k.fold>0?.75:.5),k.slice),max(log2(max(k.size.x*k.width,1))-1,0))+k.shift);
#ifdef TERRAIN2_COVER
 float3 col=terrain2CoverValue(cell,mean); // the cover pass's per-material value (terrain2_cover.h)
#else
 float3 detail=0;float squares=0,h=0,hw=0;
 [unroll]for(int p=0;p<3;++p){
  [branch]if(projected[p]){
   Terrain2Hex x=P.h[p];float3 sum=0;
   [unroll]for(int j=0;j<3;++j){
    [branch]if(x.w[j]>0){
     sum+=x.w[j]*(terrain2Tap(m,k,x.q[j],x.dx[j],x.dy[j])-mean);
     [branch]if(t2Surface){h+=wp[p]*x.w[j]*terrain2HeightGrad(m,float3(terrainCliffCellUv(k,x.q[j]),k.slice),x.dx[j]*k.size,x.dy[j]*k.size);hw+=wp[p]*x.w[j];}
    }
   }
   detail+=wp[p]*sum*x.gain;squares+=wp[p]*wp[p];
  }
 }
 detail*=min(rsqrt(max(squares,1e-8)),terrainDetailGainLimit);
 hc=float2(hw>0?h/hw:.5,saturate(-dot(detail,float3(.2126,.7152,.0722))*t2CavityGain));
 float3 col=saturate(mean+detail);
#endif
 [branch]if(authored){hc=top*topHc+side*hc;return unvaried+side*col;}
 return col;
}
//")hlsl" R"hlsl( //"
// The ground colour of table entry mi. p: its map-local rest position, pw: its layout frame (noise,
// reads), pDx/pDy: their screen derivatives, nrm: the smooth slope normal,
// planes: the projection weights (x YZ, y XZ, z top; terrain_cliff.hlsl),
// bedDepth: liquid depth over the pixel, footRise: its height over the
// local floor. Past the map's edge its edge vertices continue (clamped).
// surf (S3): the blended (height, roughness), terrain2SurfacePack.
float3 terrain2Ground(uint mi,float3 p,float3 pw,float3 pDx,float3 pDy,float3 nrm,float3 planes,float bedDepth,float footRise,out float2 surf,out float3 unvaried){
 surf=float2(.5,1);unvaried=0;
 t2Map=terrain2Index(mi);t2MapReady=asuint(terrain2Record(mi,2).zw);
#ifdef T2_ONE_POOL
 [branch]if((t2Map.x>>16)!=0u)return terrain2Marker; // its pool's program is not ready yet (T2_POOL)
#endif
 uint2 g=terrain2TileSize(mi);uint gw=g.x,gh=contactBlendTileRows(g.y);
 [branch]if(terrain2Record(mi,1).w<=0||gw==0||gh==0)return terrain2Marker;
 bool full=nativeTerrainMaterial.z>0;
 int2 G=int2(gw,gh);
 float2 s=p.xy*.5,sw=pw.xy*.5,sDx=pDx.xy*.5,sDy=pDy.xy*.5;
 int2 c=clamp(int2(floor(s)),0,G-1);
 uint4 own=terrain2Tile(mi,int3(c.x,c.y+int(gh),0));
 uint4 art=terrain2Tile(mi,int3(c,0));
 float2 authoredF=frac(s);
 bool ownAuthored=(art.w&4096u)!=0;
 float aw=terrain2AtlasWidth(mi);
 float2 fw=terrain2FacePlanes(planes);
 float3 wp=float3(planes.z,fw);float faceShare=1-planes.z;
 Terrain2Planes P;
 P.h[0]=terrain2HexCells(sw,sDx,sDy,aw);P.h[1]=P.h[0];P.h[2]=P.h[0];
 [branch]if(wp.y>.002)P.h[1]=terrain2HexCells(pw.yz*.5,pDx.yz*.5,pDy.yz*.5,aw);
 [branch]if(wp.z>.002)P.h[2]=terrain2HexCells(pw.xz*.5,pDx.xz*.5,pDy.xz*.5,aw);
 // A known tile with one material everywhere within the field's reach (row
 // z bit 2 clear: never steep, never on the map border): that material.
 [branch]if((own.z&5u)==1u){
  float2 hc;
  float3 one=terrain2Read(mi,own.x&65535u,P,wp,(art.w&256u)!=0,art,
   ownAuthored&&(art.w>>16)==(own.x&65535u),authoredF,sDx,sDy,hc,unvaried);
  surf=terrain2SurfacePack(float3(hc,terrain2Roughness(own.x&65535u)));return one;
 }
 // Warped field lookup: two octaves of world-space displacement (Low: the
 // large one). On faces it also moves with height (face-plane noise, the
 // face warp option), so a boundary seen on a face does not run straight
 // down it.
 float2 q=s+t2WarpLarge*(2*terrainNoise2(sw/3,9)-1);
 [branch]if(full)q+=t2WarpSmall*(2*terrainNoise2(sw*1.4,11)-1);
 [branch]if(fw.x+fw.y>.002){
  float2 a=0,b=0,c3=0;
  [branch]if(fw.x>0){a=fw.x*(2*terrainNoise2(pw.yz*.18,13)-1);b=fw.x*(2*terrainNoise2(pw.yz*.7,17)-1);c3=fw.x*(2*terrainNoise2(pw.yz*1.7,21)-1);}
  [branch]if(fw.y>0){a+=fw.y*(2*terrainNoise2(pw.xz*.18,15)-1);b+=fw.y*(2*terrainNoise2(pw.xz*.7,19)-1);c3+=fw.y*(2*terrainNoise2(pw.xz*1.7,23)-1);}
  q+=nativeCliffBlend[1].w*(a+.6*b+.3*c3);
 }
 // Kernel over the 3x3 vertices around the nearest one (tiles n-1 and n of
 // each axis hold them): quadratic B-spline mixed with bilinear.
 int2 n=int2(floor(q+.5));float2 d=q-n;
 float3 kx=lerp(float3(.5*(.5-d.x)*(.5-d.x),.75-d.x*d.x,.5*(.5+d.x)*(.5+d.x)),float3(max(-d.x,0),1-abs(d.x),max(d.x,0)),t2Bilinear);
 float3 ky=lerp(float3(.5*(.5-d.y)*(.5-d.y),.75-d.y*d.y,.5*(.5+d.y)*(.5+d.y)),float3(max(-d.y,0),1-abs(d.y),max(d.y,0)),t2Bilinear);
 uint4 sc=0xffffu;float4 wq=0;bool4 authored=false;
 [unroll]for(int ty=0;ty<2;++ty)[unroll]for(int tx=0;tx<2;++tx){
  int2 want=n-1+int2(tx,ty),tc=clamp(want,0,G-1),over=want-tc;
  uint4 r=terrain2Tile(mi,int3(tc.x,tc.y+int(gh),0));
  uint policies=terrain2Tile(mi,int3(tc,0)).w>>8;
  [unroll]for(int k=0;k<4;++k){
   int vx=tx+(k&1),vy=ty+(k>>1);
   if(!(((k&1)!=0&&tx!=1)||((k>>1)!=0&&ty!=1))){
    // Past the grid the clamped tile's nearer corner (the edge vertex).
    int2 corner=clamp(int2(k&1,k>>1)+over,0,1);int ci=corner.x+2*corner.y;
    uint v=ci==0?r.x&65535u:ci==1?r.x>>16:ci==2?r.y&65535u:r.y>>16;
    if(v!=0xffffu)terrain2Add(sc,wq,authored,v,kx[vx]*ky[vy],((policies>>ci)&1u)!=0,false);
   }
  }
 }
 // A material painted on one vertex only (row w bits 8-11) keeps a ragged
 // blob there (walkable ground and slopes; a face shows its corners).
 uint loneCell=0xffffu;float loneCore=0;
 [branch]if((own.w&0xf00u)!=0){
  float2 shift=q-s;shift*=min(1,t2LoneWarp/max(length(shift),1e-4));
  float ragged=t2LoneRagged*(terrainNoise(sw*3.7,25)-.5);
  float loneGround=smoothstep(.1,.6,planes.z);
  uint4 oc=uint4(own.x&65535u,own.x>>16,own.y&65535u,own.y>>16);
  [unroll]for(int k0=0;k0<4;++k0){
   float core=((own.w>>(8+k0))&1u)!=0?loneGround*(1-smoothstep(t2LoneCore,t2LoneEdge,length(terrain2OnSurface(s+shift-(c+int2(k0&1,k0>>1)),nrm))+ragged)):0;
   [branch]if(core>0){
    uint lone=oc[k0];
    bool keep=((art.w>>(8+k0))&1u)!=0;
    terrain2Add(sc,wq,authored,lone,0,keep,true);
    bool4 hit=sc==lone&&authored==keep;
    wq=hit?max(wq,core*(1+t2Tail)):wq*(1-core*core);
    if(core>loneCore){loneCore=core;loneCell=lone;}
   }
  }
 }
 bool4 valid=sc!=0xffffu;uint4 kind=(sc>>12)&7u;
 // Faces: materials outside the cliff sets of the four tiles around the
 // lookup (bilinear) fade with the side share; flat-only ground (paving,
 // slabs) too unless nothing else is left. The same rows give each tile's
 // exposed rock and its weight.
 uint4 cliffRock=0xffffu;float4 cliffW=0;
 [branch]if(faceShare>.002){
  float2 bf=d+.5;float4 foreign=0;
  [unroll]for(int t4=0;t4<4;++t4){
   int2 o=int2(t4&1,t4>>1);
   float wt=(o.x!=0?bf.x:1-bf.x)*(o.y!=0?bf.y:1-bf.y);
   uint4 r=terrain2Tile(mi,int3(clamp(n-1+o,0,G-1)+int2(0,gh),0));
   uint3 set=uint3(r.x,r.y,(r.z>>16)|(r.w&0xffff0000u));
   bool4 member=sc==(set.x&65535u)||sc==(set.x>>16)||sc==(set.y&65535u)||sc==(set.y>>16)||sc==(set.z&65535u)||sc==(set.z>>16);
   foreign+=member?0:wt;
   cliffW[t4]=wt;cliffRock[t4]=terrain2CliffRock(r);
  }
  wq*=sc==loneCell?1-faceShare*foreign*(1-loneCore):1-faceShare*foreign;
  float natural=dot(valid&&kind!=7u?wq:0,1);
  [branch]if(natural>t2Tail)wq*=valid&&kind==7u?1-faceShare:1;
 }
 // Liquid-bed materials (cell bit 15) above the waterline fade out as the
 // dry materials' weight grows (no liquid: bedDepth exactly 0).
 float bedKeep=saturate(bedDepth/t2BedBand);
 [branch]if(bedKeep<1&&bedDepth!=0){
  bool4 bed=valid&&(sc&0x8000u)!=0u;
  float dry=dot(valid&&!bed?wq:0,1);
  wq*=bed?lerp(1,bedKeep,smoothstep(0,t2BedDry,dry)):1;
 }
 // Thin kernel tails drop out; where none clears the tail the weights stay.
 float4 wqAll=valid?wq:0;
 wq=valid?max(wq-t2Tail,0):0;
 float wsum=dot(wq,1);
 [branch]if(wsum<=0){
  if(dot(wqAll,1)<=1e-4)return terrain2Marker;
  wq=wqAll;wsum=dot(wq,1);
 }
 float wmax=max(max(wq.x,wq.y),max(wq.z,wq.w));
 bool4 topMask=terrain2First(wq==wmax);
 wmax/=wsum;
 [branch]if(wmax>=.999){wq=topMask?wq:0;wsum=dot(wq,1);}
 float contested=wmax>=.999?0:saturate(3*(1-wmax));
 wq/=wsum;
//")hlsl" R"hlsl( //"
 // Exposed rock: the four tiles' rocks (equal rocks merged), steepness and
 // the face-plane noise; a rock whose edge cannot reach the pixel even at
 // the brightest texels is not read.
 float steep=0,rockN=0;bool4 need=false;
 [branch]if(faceShare>.002&&any(cliffRock!=0xffffu)){
  [unroll]for(int r4=0;r4<4;++r4)[unroll]for(int r5=r4+1;r5<4;++r5)
   if(cliffRock[r5]==cliffRock[r4]){cliffW[r4]+=cliffW[r5];cliffW[r5]=0;}
  float side=planes.x+planes.y;
  steep=side>.05?smoothstep(nativeCliffBlend[1].x,nativeCliffBlend[1].y,nrm.z):0;
  [branch]if(steep>0){
   [branch]if(fw.x>.002)rockN+=fw.x*terrain2RockNoise(pw.y,pw.z);
   [branch]if(fw.y>.002)rockN+=fw.y*terrain2RockNoise(pw.x,pw.z);
   rockN/=max(side,1e-6);
   float reach=-.5*(nativeCliffBlend[0].y+t2RockBand)*(1+t2SnowSoft)-.5*t2RockHeight*nativeTerrainTess.z;
   float4 m0=rockN-nativeCliffBlend[0].x-t2RockPresence*(1-saturate(2*cliffW))-t2SteepShift*(1-steep);
   need=cliffRock!=0xffffu&&cliffW>.001&&m0>reach;
  }
 }
 // Decisions (texel heights): the slots where contested or on a face, then
 // the rocks (side planes only); one read in a loop (compiled once), the
 // slot vectors rotated (four turns restore their order).
 float3 wr=float3(0,fw)/max(fw.x+fw.y,1e-6);
 bool decide=!T2_LOW&&(contested>0||faceShare>.002),decideRock=!T2_LOW;
 // Snow blends decide on the colour's luma (terrain2Decide), eased in with
 // the snow's share so nothing steps where snow enters the field.
 float lumaShare=smoothstep(0,.25,dot(valid&&kind==3u?wq:0,1));
 float4 texQ=0,rockQ=0;
 [loop]for(uint t=0;t<8u;++t){
  bool rk=t>=4u;
  [branch]if(rk?need.x&&decideRock:decide&&wq.x>0){
   float v=terrain2Decide(mi,rk?cliffRock.x:sc.x,P,rk?wr:wp,rk?0:lumaShare,
    !rk&&authored.x,art,ownAuthored&&sc.x==(art.w>>16),authoredF);
   if(rk)rockQ.x=v;else texQ.x=v;
  }
  if(rk){cliffRock=cliffRock.yzwx;cliffW=cliffW.yzwx;rockQ=rockQ.yzwx;need=need.yzwx;}
  else{sc=sc.yzwx;wq=wq.yzwx;texQ=texQ.yzwx;authored=authored.yzwx;}
 }
 // Scores: weight plus, where contested, the texel term, class noise and,
 // for a looser material, its harder partner's low texels.
 uint4 order=uint4(0,1,2,3);float4 score=-9;
 [loop]for(int j=0;j<4;++j){
  [branch]if(wq.x>0){
   uint kc=terrain2Class(sc.x);
   float best=0;uint bestOrder=9u,partnerClass=0;float partnerTexel=0;
   [unroll]for(int o2=1;o2<4;++o2)
    if(wq[o2]>best||(wq[o2]==best&&best>0&&order[o2]<bestOrder)){best=wq[o2];bestOrder=order[o2];partnerClass=terrain2Class(sc[o2]);partnerTexel=texQ[o2];}
   float crack=best>0&&t2Loose[partnerClass]<t2Loose[kc]?-t2Crack[kc]*t2DetailScale*partnerTexel:0;
   float terms=0;
   [branch]if(contested>0)terms=t2Detail[kc]*t2DetailScale*texQ.x+t2NoiseAmp[kc]*terrain2SurfaceNoise(sw,pw,wp,sc.x,t2Freq[kc],full)+crack;
   score.x=wq.x+contested*saturate(3*wq.x)*terms;
  }
  sc=sc.yzwx;wq=wq.yzwx;texQ=texQ.yzwx;order=order.yzwx;score=score.yzwx;
 }
 // Soft range of the looser of the two top scores, widened so the texels
 // decide over a band; squared shares.
 float best0=max(max(score.x,score.y),max(score.z,score.w));
 bool4 firstMask=terrain2First(score==best0);
 uint c1=terrain2Class(terrain2Pick(sc,firstMask)),c2=c1;
 float4 others=firstMask?-9:score;
 float runner=max(max(others.x,others.y),max(others.z,others.w));
 [branch]if(runner>-9)c2=terrain2Class(terrain2Pick(sc,terrain2First(others==runner)));
 float depth=t2Soft[t2Loose[c1]>=t2Loose[c2]?c1:c2]*t2SoftScale;
 float4 a=max(score-best0+depth,0);a*=a;
 float total=dot(a,1);
 if(total<=0)return terrain2Marker;
 a/=total;
 // Low: the two strongest materials (the third share subtracted).
 [branch]if(T2_LOW){
  float2 lo=min(a.xy,a.zw),hi=max(a.xy,a.zw);
  float4 b=max(a-min(min(hi.x,hi.y),max(lo.x,lo.y)),0);float bs=dot(b,1);a=bs>0?b/bs:a; // (a three-way tie keeps all)
 }
 // Rock exposure: the noise edge moved by presence, steepness and snow,
 // then the rock's and the cover's texel heights across a wide band. It
 // shows through the cover that is not stone and not its own class.
 float4 er=0;
 [branch]if(any(need)){
  float coverHeight=dot(a,texQ);
  float snowy=dot(valid&&(kind==3u||kind==4u)?a:0,1);
  float width=(nativeCliffBlend[0].y+t2RockBand)*(1+t2SnowSoft*snowy);
  float amount=saturate(steep*6)*lerp(1,saturate(4*nativeCliffBlend[1].z),snowy);
  [unroll]for(int r6=0;r6<4;++r6){
   [branch]if(need[r6]){
    uint rockClass=(cliffRock[r6]>>12)&7u;
    float through=dot(valid&&kind!=1u&&kind!=rockClass?a:0,1);
    float m=rockN-nativeCliffBlend[0].x-t2RockPresence*(1-saturate(2*cliffW[r6]))-t2SteepShift*(1-steep)
           -t2SnowShift*(1-nativeCliffBlend[1].z)*snowy+t2RockHeight*nativeTerrainTess.z*(rockQ[r6]-coverHeight);
    float x=saturate(m/width+.5);
    er[r6]=through*amount*x*x*(3-2*x)*smoothstep(0,.1,cliffW[r6]);
   }
  }
 }
 float e=max(max(er.x,er.y),max(er.z,er.w));
 // Colours: the slots by their shares, then the rocks (side planes) where
 // they show; one read in a loop (compiled once).
 // S3: their (height, crevice, class roughness) by the same shares.
 float3 cover=0,rc=0,sCover=0,sRock=0,raw=0;float rw=0;
 float4 cw=er*cliffW;
 [loop]for(uint t2=0;t2<8u;++t2){
  bool rk=t2>=4u;
  float wt=rk?cw.x:a.x;
  [branch]if(wt>0){
   uint cell=rk?cliffRock.x:sc.x;float2 hc;float3 materialRaw;
   float3 col=terrain2Read(mi,cell,P,rk?wr:wp,!rk&&authored.x,art,
    ownAuthored&&cell==(art.w>>16),authoredF,sDx,sDy,hc,materialRaw);
   raw+=wt*materialRaw;
   float3 s3=float3(hc,terrain2Roughness(cell));
   if(rk){rc+=wt*col;rw+=wt;sRock+=wt*s3;}else{cover+=wt*col;sCover+=wt*s3;}
  }
  if(rk){cliffRock=cliffRock.yzwx;cw=cw.yzwx;}else{sc=sc.yzwx;a=a.yzwx;authored=authored.yzwx;}
 }
 [branch]if(rw>0){cover=lerp(cover,rc/rw,e);sCover=lerp(sCover,sRock/rw,e);raw*=1-e;}
#ifndef TERRAIN2_COVER
 // Wall foot: the floor in front of and below the face climbs it over a
 // ragged band: the floor vertices' materials at the plan point moved
 // downhill by the pixel's rise, read there folded up, less the share the
 // face already shows of each.
 [branch]if(!T2_LOW&&faceShare>.002&&footRise<1.6*t2FootBand&&length(nrm.xy)>1e-3){
  float2 down=normalize(nrm.xy);
  // The floor read moves downhill by unfold x rise, so the floor continues
  // up the face at its own scale: a face of slope angle t rising r lies
  // r / tan t uphill of its foot and r / sin t up the surface from it, so
  // the unfolded floor point is (cos t + 1) / sin t x r downhill (1 on a
  // vertical wall; capped on gentle slopes). Moving by r alone held the
  // read still along a 45 degree face: fall-line streaks at the foot of
  // moderate faces (save1164, user F11 frame_027077/028669).
  float unfold=min((1+nrm.z)/length(nrm.xy),4);
  float2 sf=clamp((p.xy+down*footRise*unfold)*.5,0,float2(G)-1e-3);int2 cf=int2(floor(sf));float2 ff=sf-cf;
  uint4 r=terrain2Tile(mi,int3(cf.x,cf.y+int(gh),0));
  uint4 floorArt=terrain2Tile(mi,int3(cf,0));uint floorPolicies=floorArt.w>>8;
  uint4 ground=uint4(r.x&65535u,r.x>>16,r.y&65535u,r.y>>16);
  float4 gw4=float4((1-ff.x)*(1-ff.y),ff.x*(1-ff.y),(1-ff.x)*ff.y,ff.x*ff.y);
  // The ragged edge's noise runs on the plan position (and a little on the
  // height). It was keyed on the position projected onto the face's
  // horizontal (along = the slope normal turned), but the layout position
  // is hundreds of metres long, so the slightest turn of the smooth normal
  // across a face moved that coordinate by whole noise cells: thin fans of
  // streaks down the band at the foot of curved cliffs (save1164 "snow cliff
  // artifact"; the band's share alone drew them, terrain_isolate 3, 1102).
  float edge=t2FootBand*(.55+.9*terrainNoise(pw.xy*1.1+pw.z*float2(.35,-.35)+float2(57.3,-19.1),8));
  float foot=faceShare*(1-smoothstep(.35*edge,edge,footRise));
  float2 fs=(pw.xy+down*footRise*unfold)*.5;
  float2 fdx=(pDx.xy+down*pDx.z*unfold)*.5,fdy=(pDy.xy+down*pDy.z*unfold)*.5;
  float hold=terrain2Hold(fdx,fdy,aw);fdx*=hold;fdy*=hold;
  float3 gc=0,gs=0;float gsum=0;
  [loop]for(int k6=0;k6<4;++k6){
   uint m=ground.x;
   float w=gw4.x*(1-dot(sc==m?a:0,1));
   TerrainCliffCell kg;
   [branch]if(foot>.002&&m!=0xffffu&&w>.002){
    bool keep=((floorPolicies>>k6)&1u)!=0;
    bool readable;
    [branch]if(keep){
     // The existing cliff-foot projection takes precedence here too;
     // protected families still require their own plain source.
     readable=terrain2OwnPlainCell(mi,m,kg);
    }else readable=terrain2Cell(mi,m,kg);
    [branch]if(readable){
     gc+=w*terrain2Tap(mi,kg,frac(fs),fdx,fdy);gsum+=w;
     float fh=.5;[branch]if(t2Surface)fh=terrain2HeightGrad(mi,float3(terrainCliffCellUv(kg,frac(fs)),kg.slice),fdx*kg.size,fdy*kg.size);
     gs+=w*float3(fh,0,terrain2Roughness(m));
    }
   }
   ground=ground.yzwx;gw4=gw4.yzwx;
  }
  [branch]if(gsum>.002){cover=lerp(cover,gc/gsum,foot*gsum);sCover=lerp(sCover,gs/gsum,foot*gsum);raw*=1-foot*gsum;}
 }
#endif
 surf=terrain2SurfacePack(sCover);
 unvaried=raw;
 return cover;
}
// Share of table entry mi at world xy: smooth in each axis over
// +-t2BorderBand around its edges (half on the edge, 0 a band outside).
// Two maps sharing an edge get mirrored shares (s(d) + s(-d) = 1), four
// at a corner products of them, so every map that draws a pixel finds the
// same mix and nothing steps where one map's geometry ends.
float terrain2MapShare(uint mi,float2 x){
 float4 r=terrain2Record(mi,0);
 float2 s=smoothstep(-1,1,min(x-r.xy,r.zw-x)/t2BorderBand);
 return s.x*s.y;
}
// The ground colour at world rest position x of a pixel drawn by table
// entry self (p, pw: its map-local and layout positions; dx, dy: their
// screen derivatives; nrm, planes, bed, rise: terrain2Ground's): its own
// result, and within the border band mixed with every other map whose
// share there is above 0 (each its own result; finished colours only are
// mixed). One terrain2Ground in one loop, its own map first (the vertex p
// and pw), then within the band the others in table order. Then the
// terrain's large-scale brightness (terrainMacro, the anti-tiling option)
// in the layout frame every map shares.
// The wall foot fades out over the band: rise is a vertex attribute each map
// derives from its own grid, and only neighbour maps fade it at sides welded
// to the active map (owned_terrain_foot_rise), so the two batches meeting on
// an edge read different rises there (a straight step on zone3obr's border,
// user report 2026-10-31). keep, from the shares every batch computes alike
// (the dominant map's normalised share, 1/2 on a shared edge), moves rise
// past the foot's reach on the edge and leaves it unchanged a band inside.
// surf (S3): the mixed (height, roughness) by the same shares.
float3 terrain2Color(uint self,float3 x,float3 p,float3 pw,float3 dx,float3 dy,float3 nrm,float3 planes,float bed,float rise,out float2 surf){
 float4 r=terrain2Record(self,0);float2 d=min(x.xy-r.xy,r.zw-x.xy);
 bool band=min(d.x,d.y)<t2BorderBand;
 [branch]if(band){
  float top=0,sum=0;
  [loop]for(uint mi=0;mi<uint(nativeTerrain2.w);++mi){float w=terrain2MapShare(mi,x.xy);top=max(top,w);sum+=w;}
  float keep=sum>0?saturate(2*top/sum-1):1;
  rise+=(1-keep)*2*t2FootBand;
 }
 float3 c=0,raw=0;float total=0;surf=0;
 uint n=band?uint(nativeTerrain2.w):1u;
 [loop]for(uint i=0;i<n;++i){
  uint mi=i==0u?self:i<=self?i-1u:i;
  float w=band?(i==0u?max(terrain2MapShare(self,x.xy),1e-3):terrain2MapShare(mi,x.xy)):1; // (also a raster edge just outside)
  [branch]if(w>0){
   T2_FRAME_LANE(mi);
   float3 q=x-terrain2Record(mi,1).xyz;float2 s;
   float3 qp=i==0u?p:q,qw=i==0u?pw:float3(q.xy+terrain2Record(mi,2).xy,q.z);
   float3 unvaried;
   c+=w*terrain2Ground(mi,qp,qw,dx,dy,nrm,planes,bed,rise,s,unvaried);raw+=w*unvaried;total+=w;surf+=w*s;
  }
 }
 T2_FRAME_LANE(0u);
 c/=total;raw/=total;surf/=total;
 return nativeTerrainMaterial.y>0?(c-raw)*terrainMacro(pw.xy)+raw:c;
}
// terrain2Color of walkable ground (top plane, no liquid bed, no wall foot,
// as the material cache bakes it) at world xy x of table entry self, every
// map read through the table: for passes without the terrain batches'
// per-map slots (the snow and sand piles, renderer_ground_cover.cpp), so
// they show the ground they stand on. One terrain2Ground copy in the loop.
// surf (S3, t2Surface): the mixed (height, roughness), as terrain2Color.
float3 terrain2FlatColor(uint self,float2 x,float3 dx,float3 dy,float3 nrm,out float2 surf){
 float4 r=terrain2Record(self,0);float2 d=min(x-r.xy,r.zw-x);
 bool band=min(d.x,d.y)<t2BorderBand;
 float3 c=0,raw=0;float total=0;surf=0;
 [loop]for(uint mi=0;mi<uint(nativeTerrain2.w);++mi){
  float w=mi==self?(band?max(terrain2MapShare(self,x),1e-3):1):band?terrain2MapShare(mi,x):0;
  [branch]if(w>0){
   float3 q=float3(x-terrain2Record(mi,1).xy,0);float2 s;
   float3 unvaried;
   c+=w*terrain2Ground(mi,q,float3(q.xy+terrain2Record(mi,2).xy,0),dx,dy,nrm,float3(0,0,1),0,1e3,s,unvaried);raw+=w*unvaried;total+=w;surf+=w*s;
  }
 }
 c/=max(total,1e-3);raw/=max(total,1e-3);surf/=max(total,1e-3);
 return nativeTerrainMaterial.y>0?(c-raw)*terrainMacro(x-terrain2Record(self,1).xy+terrain2Record(self,2).xy)+raw:c;
}
#ifdef __cplusplus
//")hlsl"
#endif
