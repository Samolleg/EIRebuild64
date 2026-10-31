#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace owned {

// Slope normal for terrain projection weights. The stored MPR vertex normals
// are smoothed for lighting and still point well up on cliffs, so a triplanar
// blend weighted by them keeps half of the stretched top projection there.
// This one-ring central difference over actual node positions follows the
// faces around the vertex instead. Lighting keeps the stored normals.
// `position(x,y)` returns the zone-grid node position; callers clamp at the
// zone edge. A heightfield normal always faces up, so only x and y are packed
// (signed 16-bit each); the shader rebuilds z.
template<class Position>
std::uint32_t terrain_slope_normal(const Position& position, int x, int y) {
    const std::array<float,3> l=position(x-1,y),r=position(x+1,y),d=position(x,y-1),u=position(x,y+1);
    const float ax=r[0]-l[0],ay=r[1]-l[1],az=r[2]-l[2];
    const float bx=u[0]-d[0],by=u[1]-d[1],bz=u[2]-d[2];
    float nx=ay*bz-az*by,ny=az*bx-ax*bz,nz=ax*by-ay*bx;
    const float length=std::sqrt(nx*nx+ny*ny+nz*nz);
    if(!(length>1e-12f)||!(nz>0))return 0;
    nx/=length;ny/=length;
    const auto pack=[](float value){
        return static_cast<std::uint32_t>(static_cast<std::uint16_t>(static_cast<std::int16_t>(
            std::lround((std::clamp)(value,-1.f,1.f)*32767.f))));
    };
    return pack(nx)|(pack(ny)<<16);
}

// Facet flatness of a vertex: the drawn triangles around node (x,y) (LOD0
// cells split 00-10-11 / 00-11-01), as the up component of the flattest
// one's normal (low byte) and of their mean (high byte), 0..255. A ledge or
// saddle between two cliffs is flat in its facets although the one-ring slope
// normal above sees the cliffs on both sides. Stored per vertex, it
// interpolates smoothly across facets, unlike a per-pixel facet normal (which
// jumps at every mesh fold and cut hard texture edges there).
template<class Position>
std::uint32_t terrain_facet_flatness(const Position& position, int x, int y) {
    float best=0,sum=0;int count=0;
    for(int cy=y-1;cy<=y;++cy)for(int cx=x-1;cx<=x;++cx){
        const std::array<float,3> a=position(cx,cy),b=position(cx+1,cy),c=position(cx+1,cy+1),d=position(cx,cy+1);
        const std::array<float,3>* tris[2][3]={{&a,&b,&c},{&a,&c,&d}};
        for(int t=0;t<2;++t){
            // Triangle t of cell (cx,cy) touches the node unless the node is
            // the corner the diagonal leaves out.
            const bool uses_b=t==0,uses_d=t==1;
            const bool node_a=cx==x&&cy==y,node_b=cx+1==x&&cy==y,node_c=cx+1==x&&cy+1==y,node_d=cx==x&&cy+1==y;
            if(!(node_a||node_c||(uses_b&&node_b)||(uses_d&&node_d)))continue;
            const auto& p0=*tris[t][0];const auto& p1=*tris[t][1];const auto& p2=*tris[t][2];
            const float ux=p1[0]-p0[0],uy=p1[1]-p0[1],uz=p1[2]-p0[2],vx=p2[0]-p0[0],vy=p2[1]-p0[1],vz=p2[2]-p0[2];
            const float nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx;
            const float l=std::sqrt(nx*nx+ny*ny+nz*nz);
            if(!(l>1e-12f))continue;
            const float up=std::fabs(nz)/l;best=(std::max)(best,up);sum+=up;++count;
        }
    }
    if(!count)return 255u|(255u<<8);
    const auto q=[](float v){return static_cast<std::uint32_t>(std::lround((std::clamp)(v,0.f,1.f)*255.f));};
    return q(best)|(q(sum/count)<<8);
}

// Plain fill tiles for the anti-tiling edge rule. A tile is plain fill when
// it and its eight neighbours carry the same tile id (rotation ignored):
// shipped maps place such tiles next to themselves in every relative
// rotation, so a world-space read of that one tile has no authored seam to
// break. Bit cy*2+cx is set when every tile touching grid corner
// (tx+cx,ty+cy) is plain fill; the shader interpolates these corners, so
// borrowed detail yields to the authored detail along any edge shared with a
// tile that keeps its authored image.
// `id(x,y)` returns the base tile id on the zone grid, or -1 outside it.
template<class TileId>
std::uint32_t terrain_fill_corners(const TileId& id, int tx, int ty) {
    bool pure[3][3]{};
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x) {
            const int cx = tx + x - 1, cy = ty + y - 1, value = id(cx, cy);
            bool same = value >= 0;
            for (int dy = -1; same && dy <= 1; ++dy)
                for (int dx = -1; same && dx <= 1; ++dx)
                    same = id(cx + dx, cy + dy) == value;
            pure[y][x] = same;
        }
    std::uint32_t corners = 0;
    for (int cy = 0; cy < 2; ++cy)
        for (int cx = 0; cx < 2; ++cx)
            if (pure[cy][cx] && pure[cy][cx + 1] && pure[cy + 1][cx] && pure[cy + 1][cx + 1])
                corners |= 1u << (cy * 2 + cx);
    return corners;
}

// terrain_quality: 1 (high, default) honours each terrain material switch;
// 0 (low) drops anti-repetition and transition sharpening and draws the
// authored tile mapping. (The material's own side projection,
// terrain_triplanar, is gone: Terrain cliffs replaced it on every tile while
// on, and without them it smeared faces. The large-scale variation,
// terrain_macro_variation, is gone too: no visible tiling change in play.)
struct TerrainMaterialSwitches { bool antitile, height_blend; };
inline TerrainMaterialSwitches terrain_material_switches(int quality, bool antitile, bool height_blend) {
    return {quality >= 1 && antitile, quality >= 1 && height_blend};
}

// Shared verbatim by the production native shader and its GPU regression.
// The includer defines, before this text:
//   float4 terrainTapGrad(uint2 ref,float2 uv,float2 dx,float2 dy);
//   float4 terrainTapLevel(uint2 ref,float2 uv,float lod);
//   float2 terrainTapSize(uint2 ref,out float levels);
// The material works on the authored MPR tile art only. It keeps the tile's
// own low-frequency image (transitions, paths, colour) exactly and replaces
// only detail above ~1/5 tile with detail read through randomly turned hex
// cells. Every feature fades to the unmodified tile with distance. Steep
// faces belong to Terrain cliffs (terrain_cliff.h).
inline constexpr char kTerrainMaterialShader[] = R"(
static const float terrainTileUnits = 2;         // A base MPR tile covers 2x2 world units.
static const float terrainTileMargin = .125;     // Atlas padding a tile (decoder: 8 texels of the 64-texel MPR tile; HD pages scale it).
static const float terrainLowpassTexels = 5.66;  // Coarse image kept exactly: ~5.7 texels a tile.
static const float terrainHexLattice = .28867513; // Lattice units a tile: neighbouring cells one tile apart.
static const float terrainMatchNear = .035;      // Coarse-colour distance that still borrows detail.
static const float terrainMatchFar = .11;        // ...and where borrowing stops (other material).
static const float terrainDetailGainLimit = 1.6; // Variance-preserving blend never boosts beyond this.
static const float terrainBandRadius = .22;      // Transition probe radius in tile units.
static const float terrainBandStrength = 6;      // Height-bias strength against the transition span.
static const float terrainMacroAmount = .12;     // Peak-to-peak world-space brightness variation.
static const float terrainEdgeBand = .12;        // Tile units over which hex detail yields to the tile's own at an edge.

float3 terrainSlopeNormal(uint packed){
 float2 xy=float2(int(packed<<16)>>16,int(packed)>>16)/32767.0;
 return float3(xy,sqrt(saturate(1-dot(xy,xy))));
}

// Hexagonal lattice (Mikkelsen 2022, "Practical Real-Time Hex-Tiling"):
// barycentric weights and the three cells around `st` (lattice units).
void terrainHexGrid(float2 st,out float3 w,out int2 v1,out int2 v2,out int2 v3){
 st*=3.46410162;
 float2 skewed=float2(st.x-.57735027*st.y,1.15470054*st.y);
 int2 base=int2(floor(skewed));
 float3 t=float3(frac(skewed),0);t.z=1-t.x-t.y;
 float s=step(0,-t.z),s2=2*s-1;
 w=float3(-t.z*s2,s-t.y*s2,s-t.x*s2);
 int i=int(s);
 v1=base+int2(i,i);v2=base+int2(i,1-i);v3=base+int2(1-i,i);
}
float2 terrainHexCentre(int2 v){return float2(v.x+.5*v.y,.8660254*v.y)/3.46410162;}
uint terrainHash(int2 v){
 uint h=uint(v.x)*0x8da6b343u^uint(v.y)*0xd8163841u;
 h^=h>>16;h*=0x7feb352du;h^=h>>15;h*=0x846ca68bu;return h^(h>>16);
}
float2 terrainHash2(int2 v){uint h=terrainHash(v);return float2(h&65535u,h>>16)/65535.0;}
// A cell turns its content around its own centre and shifts it by a random
// fraction of a tile, so the three cells never show the same repeat.
float2x2 terrainHexRotation(int2 v){
 float angle=6.28318531*terrainHash2(v+int2(7919,104729)).x;
 float c=cos(angle),s=sin(angle);
 return float2x2(c,-s,s,c);
}
float2 terrainHexSampleCoord(float2 p,int2 v){
 float2 c=terrainHexCentre(v)/terrainHexLattice;
 return mul(terrainHexRotation(v),p-c)+c+terrainHash2(v);
}
float3 terrainHexWeights(float3 w){
 float3 k=pow(max(w,0),7);return k/(k.x+k.y+k.z);
}

// Transition sharpening: p' = p + b p(1 - p), b = clamp(strength*top*h, +-.5).
// `p` is the fraction of material A, `h` the pixel's height excess of A over B.
// Zero inside a pure material and on cliffs (top = 0).
float terrainHeightShift(float p,float h,float strength,float top){
 float b=clamp(strength*top*h,-.5,.5);
 return b*p*(1-p);
}

// Value noise from a texture (terrain_noise_texture): uniform random lattice
// values in a tileable 1024 x 1024 R8 texture, read with one gather of the
// four corners and the same smoothstep interpolation the hashed lattice used
// (gather keeps full-precision weights; filtered bilinear weights are 8-bit).
// Each use is a layer with its own turn and offset, so no two layers repeat
// in alignment; a layer of lattice spacing L metres repeats every 1024 L.
Texture2D<float> terrainNoiseTexture:register(t122);
SamplerState terrainNoiseSampler:register(s12); // wrap
// Layer: cos, sin of the turn (Pythagorean triples), offset (lattice units).
// The rock streak layers (5, 6) read stretched coordinates: quarter turns
// only, so their lattice stays aligned with the stretch as before.
static const float4 terrainNoiseLayers[29]={
 float4(.6,.8,113.7,529.3),float4(.923077,.384615,871.1,77.9),float4(.470588,.882353,344.5,911.2),float4(.96,.28,702.3,268.8),
 float4(-.689655,.724138,51.9,640.4),float4(0,1,455.6,333.1),float4(-1,0,989.4,187.6),float4(-.849057,-.528302,238.2,802.7),
 float4(.983607,.180328,617.8,458.5),float4(.507692,-.861538,26.3,981.9),float4(-.969231,.246154,780.6,610.2),float4(.657534,.753425,398.9,13.4),
 float4(.988235,-.152941,164.1,717.7),float4(-.423529,.905882,934.7,401.5),float4(.438202,-.898876,507.3,145.8),float4(.742268,.670103,289.4,866.6),
 float4(-.6,-.8,666.6,590.1),float4(.384615,-.923077,82.8,344.9),float4(-.882353,.470588,845.2,236.4),float4(.28,-.96,431.7,954.3),
 float4(.724138,.689655,201.5,49.8),float4(-.324324,-.945946,573.9,772.6),float4(-.97561,.219512,12.4,507.1),float4(.528302,-.849057,759.8,690.3),
 float4(-.180328,.983607,362.6,121.7),float4(.861538,.507692,905.5,863.2),float4(-.246154,-.969231,128.9,398.4),float4(-.753425,.657534,644.3,935.9),
 float4(.6,.8,311.3,577.9)};
// Layers: 0-2 blend score octaves, 3-4 rock warp, 5-6 rock streaks, 7 rock
// patches, 8 rock edge, 9-10 field warp, 11-12 fine field warp, 13-24 face
// warps (three octaves, two planes, two channels), 25 single-vertex edge,
// 26-27 terrain material macro, 28 map-edge tile handover.
float terrainNoise(float2 p,uint layer){
 float4 l=terrainNoiseLayers[layer];
 p=float2(l.x*p.x-l.y*p.y,l.y*p.x+l.x*p.y)+l.zw;
 float2 i=floor(p),f=p-i;f=f*f*(3-2*f);
 // w (i,j), z (i+1,j), x (i,j+1), y (i+1,j+1): the texel corner at i+1.
 float4 g=terrainNoiseTexture.GatherRed(terrainNoiseSampler,(float2(int2(i)&1023)+1)/1024.0);
 return lerp(lerp(g.w,g.z,f.x),lerp(g.x,g.y,f.x),f.y);
}
// Two independent channels (layers `layer` and `layer`+1).
float2 terrainNoise2(float2 p,uint layer){return float2(terrainNoise(p,layer),terrainNoise(p,layer+1));}
float terrainMacro(float2 p){
 float n=.6*terrainNoise(p/23.0,26)+.4*terrainNoise(p/7.3,27);
 return 1+terrainMacroAmount*(n-.5);
}

float terrainFillWeight(uint corners,float2 q){
 q=saturate(q);
 float4 c=float4(corners&1u,(corners>>1)&1u,(corners>>2)&1u,(corners>>3)&1u);
 return lerp(lerp(c.x,c.y,q.x),lerp(c.z,c.w,q.x),q.y);
}
struct TerrainTileFrame{float2 origin;float2 margin;float2 inner;float2 size;float lowLod;};
float2 terrainTileUv(TerrainTileFrame f,float2 q){return f.origin+f.margin+q*f.inner;}
float terrainFootprintLod(TerrainTileFrame f,float2 dx,float2 dy){
 return log2(max(max(length(dx*f.size),length(dy*f.size)),1e-6));
}
// Detail (fine minus coarse) of the tile repeated through `s` (tile units).
float3 terrainWrappedDetail(uint2 ref,TerrainTileFrame f,float2 s,float2 sDx,float2 sDy,out float3 low){
 float2 uv=terrainTileUv(f,frac(s)),dx=sDx*f.inner,dy=sDy*f.inner;
 low=terrainTapLevel(ref,uv,max(f.lowLod,terrainFootprintLod(f,dx,dy))).rgb;
 return terrainTapGrad(ref,uv,dx,dy).rgb-low;
}
float3 terrainHexDetail(uint2 ref,TerrainTileFrame f,float2 p,float2 pDx,float2 pDy,float3 lowOrig,float3 origDetail){
 float3 w;int2 v1,v2,v3;
 terrainHexGrid(p*terrainHexLattice,w,v1,v2,v3);
 w=terrainHexWeights(w);
 int2 cells[3]={v1,v2,v3};
 float3 sum=0;float total=0,squares=0;
 [unroll]for(int k=0;k<3;++k){
  // Cells under 1/1024 of the blend are not read.
  [branch]if(w[k]>=1.0/1024){
   float2x2 r=terrainHexRotation(cells[k]);
   float3 low;
   float3 detail=terrainWrappedDetail(ref,f,terrainHexSampleCoord(p,cells[k]),mul(r,pDx),mul(r,pDy),low);
   // Borrow detail only where the turned read lands on the same coarse
   // colour; transition tiles keep their own detail elsewhere.
   float a=w[k]*(1-smoothstep(terrainMatchNear,terrainMatchFar,distance(low,lowOrig)));
   sum+=a*detail;total+=a;squares+=a*a;
  }
 }
 // Zero-mean detail blended with variance preservation keeps its contrast.
 float3 hex=squares>0?sum*min(total*rsqrt(squares),terrainDetailGainLimit):0;
 return hex+(1-total)*origDetail;
}
float3 terrainBandSharpen(uint2 ref,TerrainTileFrame f,float2 q,float3 lowOrig,float3 color,float3 detail,float top){
 float3 result=color;
 float2 at=clamp(q,.02,.98);
 [branch]if(top>=.05){
  float r=terrainBandRadius;
  float3 gx=terrainTapLevel(ref,terrainTileUv(f,clamp(at+float2(r,0),0,1)),f.lowLod).rgb-lowOrig;
  float3 gy=terrainTapLevel(ref,terrainTileUv(f,clamp(at+float2(0,r),0,1)),f.lowLod).rgb-lowOrig;
  float xx=dot(gx,gx),xy=dot(gx,gy),yy=dot(gy,gy);
  [branch]if(xx+yy>=.0009){
   // Principal direction of the coarse colour change.
   float angle=.5*atan2(2*xy,xx-yy);
   float2 d=float2(cos(angle),sin(angle))*r;
   float3 a=terrainTapLevel(ref,terrainTileUv(f,clamp(at+d,0,1)),f.lowLod).rgb;
   float3 b=terrainTapLevel(ref,terrainTileUv(f,clamp(at-d,0,1)),f.lowLod).rgb;
   float3 span=a-b;float spanSquared=max(dot(span,span),1e-6);
   float p=saturate(dot(lowOrig-b,span)/spanSquared);
   float h=dot(detail,span)/spanSquared;
   result+=span*terrainHeightShift(p,h,terrainBandStrength,top)*smoothstep(.002,.02,spanSquared);
  }
 }
 return result;
}

// Far plain fill (anti-tiling, settings.y): once the fine detail has faded
// (a pixel covers the tile's ~5.7-texel coarse image), the coarse image
// itself repeats every tile and the ground looks tiled at a distance, live
// and cached alike. Inside plain fill (same art on every side, so no
// transition or path to keep) the coarse image is rebuilt from turned and
// shifted hex reads of it, blended about the tile's mean with variance kept,
// taking over as the fine detail fades.
float3 terrainFarFill(uint2 ref,TerrainTileFrame f,float2 p,float lod,float3 orig){
 float meanLod=log2(max(f.size.x*(f.inner.x+2*f.margin.x),1));
 float3 mean=terrainTapLevel(ref,terrainTileUv(f,float2(.5,.5)),meanLod).rgb;
 float3 w;int2 v1,v2,v3;
 terrainHexGrid(p*terrainHexLattice,w,v1,v2,v3);
 // Softer than the fine detail's weights: coarse colour must not change
 // across a cell edge in a line.
 w=pow(max(w,0),3);w/=max(w.x+w.y+w.z,1e-6);
 int2 cells[3]={v1,v2,v3};
 float3 sum=0;float total=0,squares=0;
 [unroll]for(int k=0;k<3;++k){
  [branch]if(w[k]>=1.0/256){
   float3 c=terrainTapLevel(ref,terrainTileUv(f,frac(terrainHexSampleCoord(p,cells[k]))),lod).rgb;
   sum+=w[k]*(c-mean);total+=w[k];squares+=w[k]*w[k];
  }
 }
 // Variance-preserving blend of the deviations about the mean.
 return squares>0?saturate(mean+sum*min(rsqrt(squares),terrainDetailGainLimit/max(total,1e-4))):orig;
}

// Long tiles (native_terrain_long_tile: a 1 x 8 strip of a long texture on
// one tile, the authored cliff art) take no material stage, so a distant
// cliff of them shows the strip repeating every tile. Once a pixel covers
// the strip's coarse image (~5.7 texels across), the strip is read through
// three hex cells over world XY, each shifted across the strip (mirrored, so
// continuous) but never along it (the strip's top-to-bottom art follows the
// cliff), blended about the strip's column mean with variance kept.
float4 terrainLongFar(uint2 ref,float2 uv,float2 uvDx,float2 uvDy,float3 p,float tiles,float4 orig){
 float levels;float2 size=terrainTapSize(ref,levels);
 if(tiles<1)return orig;
 float tile=1/tiles,stripTexels=size.x*tile;
 float lowLod=log2(max(stripTexels/terrainLowpassTexels,1));
 float fineLod=log2(max(max(length(uvDx*size),length(uvDy*size)),1e-6));
 float far=saturate(fineLod-(lowLod-.5));
 [branch]if(far<=.001||stripTexels<32||levels<lowLod+2)return orig;
 float u0=floor(uv.x*tiles)*tile,margin=terrainTileMargin*tile,inner=tile-2*margin;
 float x=(uv.x-u0-margin)/inner;
 float lod=min(max(lowLod,fineLod),log2(stripTexels)-1);
 float3 mean=terrainTapLevel(ref,float2(u0+.5*tile,uv.y),log2(stripTexels)).rgb;
 float3 w;int2 v1,v2,v3;
 terrainHexGrid(p.xy/terrainTileUnits*terrainHexLattice,w,v1,v2,v3);
 w=pow(max(w,0),3);w/=max(w.x+w.y+w.z,1e-6);
 int2 cells[3]={v1,v2,v3};
 float3 sum=0;float total=0,squares=0;
 [unroll]for(int k=0;k<3;++k){
  [branch]if(w[k]>=1.0/256){
   float t=frac((x+terrainHash2(cells[k]).x)*.5)*2,q=t<1?t:2-t;
   float3 c=terrainTapLevel(ref,float2(u0+margin+q*inner,uv.y),lod).rgb;
   sum+=w[k]*(c-mean);total+=w[k];squares+=w[k]*w[k];
  }
 }
 float3 color=squares>0?saturate(mean+sum*min(rsqrt(squares),terrainDetailGainLimit/max(total,1e-4))):orig.rgb;
 return float4(lerp(orig.rgb,color,far),orig.a);
}

float4 terrainDetailColor(uint2 ref,float2 uv,float2 uvDx,float2 uvDy,float3 p,float3 pDx,float3 pDy,
  float tiles,uint fillCorners,float4 settings,float4 orig){
 TerrainTileFrame f;float levels;
 f.size=terrainTapSize(ref,levels);
 float tileTexels=f.size.x/max(tiles,1);
 f.lowLod=log2(tileTexels/terrainLowpassTexels);
 float fineLod=terrainFootprintLod(f,uvDx,uvDy);
 float fade=saturate(f.lowLod-.5-fineLod);
 if(tiles<1||tileTexels<32||levels<f.lowLod+1)fade=0;
 float4 result=orig;
 const float coarseLod=f.lowLod;
 float2 cell=floor(uv*tiles);
 f.origin=cell/tiles;f.margin=terrainTileMargin/tiles;f.inner=1/tiles-2*f.margin;
 float2 q=(uv-f.origin-f.margin)/f.inner;
 // Far plain fill weight (0 near, where the fine detail runs fully).
 float far=settings.y>0&&tiles>=1&&tileTexels>=32&&levels>=coarseLod+1?(1-fade)*terrainFillWeight(fillCorners,q):0;
 [branch]if(fade>0){
  f.lowLod=max(f.lowLod,fineLod);
  float3 lowOrig=terrainTapLevel(ref,uv,f.lowLod).rgb;
  float3 origDetail=orig.rgb-lowOrig;
  float2 e=smoothstep(0,terrainEdgeBand,min(q,1-q));
  float edge=max(e.x*e.y,terrainFillWeight(fillCorners,q));
  float3 detail=origDetail;
  [branch]if(settings.y>0){
   float3 hex=terrainHexDetail(ref,f,p.xy/terrainTileUnits,pDx.xy/terrainTileUnits,pDy.xy/terrainTileUnits,lowOrig,origDetail);
   // A neighbour with another tile id borrows from its own art, so at a
   // shared edge both keep their authored detail; only edges between plain
   // fill tiles (same art on both sides) keep the borrowed detail.
   detail=lerp(origDetail,hex,edge);
  }
  float3 color=lowOrig+detail;
  [branch]if(settings.z>0)color=terrainBandSharpen(ref,f,q,lowOrig,color,detail,1);
  if(settings.y>0)color*=terrainMacro(p.xy);
  result.rgb=lerp(orig.rgb,saturate(color),fade);
 }
 [branch]if(far>.001){
  TerrainTileFrame g=f;g.lowLod=coarseLod;
  result.rgb=lerp(result.rgb,terrainFarFill(ref,g,p.xy/terrainTileUnits,max(coarseLod,fineLod),orig.rgb),far);
 }
 return result;
}
// settings: x unused (0; was the removed side projection), y anti-tiling (hex
// cells + faint brightness variation), z height blend, w unused (0; was the
// removed large-scale variation, terrain_macro_variation). Each switch off
// keeps the previous result exactly.
float4 terrainMaterialColor(uint2 ref,float2 uv,float2 uvDx,float2 uvDy,float3 p,float3 pDx,float3 pDy,
  float tiles,uint fillCorners,float4 settings,float4 orig){
 float4 result=orig;
 [branch]if(any(settings.yz>0))result=terrainDetailColor(ref,uv,uvDx,uvDy,p,pDx,pDy,tiles,fillCorners,settings,orig);
 return result;
}
)";

} // namespace owned
