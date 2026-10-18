// Terrain cliffs and cliff tessellation (terrain_cliff.h).
// Embedded into the game by Source/terrain_cliff.h at build time; Prototypes/shader-lab
// loads this file directly. The __cplusplus guard lines make the file a C++
// raw string where that header includes it; to HLSL they are a skipped
// group of comments and one closed string.
// Keep each piece between guard lines under 15000 bytes (MSVC literal limit).
#ifdef __cplusplus
R"hlsl( //"
#endif
static const float terrainWalkLimitZ=.76604444;   // cos 40: the game's walk limit @param 0.5 0.95
// Flat-class edges (owned_terrain_decode.cpp kTerrainSteepClassZ, .76804444,
// less a margin for the shader's own normal decode): an edge whose two corners'
// slope normals reach this stays whole, since flat tiles draw untessellated.
static const float terrainFlatEdgeZ=.766;
static const float terrainCliffFullZ=.57357644;   // cos 55: full side projection @param 0.2 0.9
static const float terrainCliffPlaneCut=.02;      // side-plane shares under this are dropped (continuously) @param 0 0.5
static const float terrainCliffEdgeBand=.15;      // tile units over which a tile's detail cross-fades into its neighbour's @param 0 0.5
static const float terrainTessGentle=.15;         // largest offset on walkable ground (m) @param 0 0.5
static const float terrainTessShape=.75;          // Phong shape factor (0 flat .. 1 full) @param 0 1
static const float terrainTessSpacing=.3;         // target spacing of new vertices along a bent edge (m) @param 0.1 1
static const float terrainTessBorder=2;           // offset fades to zero over this distance to the map border (m) @param 0 8
static const float terrainCliffFlatStart=.72;    // facet flatness where the top-down raise starts @param 0 1
static const float terrainCliffFlatFull=.95;     // facet flatness of full top-down @param 0 1
static const float terrainCliffFlatBest=.5;      // share of the flattest facet (vs the mean) in the flatness @param 0 1
static const float terrainShadowNormalOffset=.9;  // sun-shadow lookup lifted along the smooth slope normal (m, at grazing sun) @param 0 3
static const float terrainCliffAoKeep=.3;         // share of screen-space AO kept on full side faces @param 0 1

// Side projection weights from the smooth slope normal: x the YZ plane, y the
// XZ plane, z the authored mapping; they sum to 1. Walkable ground (up to the
// 40 degree walk limit) keeps the authored mapping exactly; the side share
// grows to 1 by 55 degrees. The two planes split it by the normal's dominant
// horizontal axis, a minor plane under terrainCliffPlaneCut fading out.
float3 terrainCliffPlaneWeights(float3 n){
 float side=1-smoothstep(terrainCliffFullZ,terrainWalkLimitZ,n.z);
 // Squared (not fourth power): a wide cross-fade between the two side
 // planes, so where a face turns their reads never meet in a seam line.
 float2 a=abs(n.xy);a*=a;
 float2 w=a/max(a.x+a.y,1e-12);
 w=max(w-terrainCliffPlaneCut,0);w/=max(w.x+w.y,1e-6);
 return float3(side*w,1-side);
}
// Terrain sun-shadow receivers: the shadow map holds the coarse untessellated
// terrain, so its jagged rim triangles shadow the rounded (tessellated) faces
// below in a sawtooth. The lookup is lifted along the smooth slope normal,
// more at grazing sun (normal-offset shadows), past that coarse self-shadow;
// shadows cast from farther away stay.
float3 terrainShadowLookup(float3 p,float3 slope,float3 sun){
 float3 n=normalize(slope);float c=saturate(dot(n,sun));
 // This compensates coarse cliff rims; lifting gentle ground loses low casters.
 float cliff=1-terrainCliffPlaneWeights(n).z;
 return p+n*(cliff*terrainShadowNormalOffset*sqrt(1-c*c));
}
// Projection weights of a terrain pixel: the smooth slope normal n, with the
// top-down share raised where the drawn facets around the vertices are flat
// (facets: terrain_facet_flatness, flattest and mean facet up component,
// interpolated per vertex). A ledge or saddle between two cliffs is flat in
// its facets though n sees the cliffs on both sides; side projection there
// smeared streaks across flat ground. The value is per vertex, so it changes
// smoothly across facets. (A per-pixel facet normal was rejected: it jumps at
// every mesh fold and cut hard texture edges along folds.)
float3 terrainCliffProjectionPlanes(float3 n,float2 facets){
 float3 planes=terrainCliffPlaneWeights(n);
 float raise=smoothstep(terrainCliffFlatStart,terrainCliffFlatFull,lerp(facets.y,facets.x,terrainCliffFlatBest));
 float top=lerp(planes.z,1,raise),side=planes.x+planes.y;
 return side>1e-6?float3(planes.xy*(1-top)/side,top):planes;
}
// Share of the neighbouring tile's detail per axis from the position inside
// the tile (0..1 tile units): .5 on the shared edge, 0 beyond the band, so
// both tiles compute the same mix at their edge.
float2 terrainCliffEdgeShare(float2 u){
 return .5*(1-smoothstep(0,terrainCliffEdgeBand,min(u,1-u)));
}

// Cliff tessellation: Phong smoothing of the drawn heightfield from its own
// per-vertex slope normals, so crests and feet round off while flat and evenly
// sloped ground (equal normals) stays exactly where it was.
// Largest offset at a vertex (m): terrainTessGentle on walkable ground, rising
// to maxAmplitude past 55 degrees; zero on the active map's border (bounds:
// xy min, zw max; empty bounds skip the fade) so welded neighbour seams stay closed.
float terrainTessLimit(float3 p,float slopeZ,float maxAmplitude,float4 bounds){
 float steep=1-smoothstep(terrainCliffFullZ,terrainWalkLimitZ,slopeZ);
 float limit=lerp(terrainTessGentle,max(maxAmplitude,terrainTessGentle),steep);
 if(all(bounds.zw>bounds.xy)){
  float2 d=min(p.xy-bounds.xy,bounds.zw-p.xy);
  limit*=saturate(min(d.x,d.y)/terrainTessBorder);
 }
 return limit;
}
static const float terrainTessPixelError=.5;      // largest screen gap (output pixels) left between the curve and its pieces @param 0.1 2
// Tessellation factor of one patch edge from its end points, normals and
// limits: both patches sharing the edge agree (no cracks). Near the camera
// the world-space rule decides (edges whose normals agree stay whole, bent
// edges get pieces terrainTessSpacing long), so the subdivision there never
// changes as the camera moves. Far away it is capped where more pieces could
// not move the surface by terrainTessPixelError on screen: the curve's
// height over the edge (its sagitta, about shape * 0.177 * length * bend
// for an arc, at most the larger limit) left over n pieces is sagitta / n^2.
// A change of n then moves the surface by under that error, so it cannot be
// seen. The camera input is symmetric in the end points (the mean of their
// clip w), so both patches still compute the same factor.
// pixelsAtUnitW: output pixels per metre at clip w = 1 (0: no cap).
float terrainTessEdgeFactor(float3 a,float3 b,float3 na,float3 nb,float limitA,float limitB,float maxFactor,
  float wa,float wb,float pixelsAtUnitW){
 float bend=sqrt(saturate(1-dot(na,nb)));
 float limit=max(limitA,limitB);
 if(maxFactor<=1||bend<.05||limit<=.005)return 1;
 float span=length(a-b);
 float n=ceil(span*bend/terrainTessSpacing);
 float w=(wa+wb)*.5;
 [branch]if(pixelsAtUnitW>0&&w>1e-3){
  float sagitta=min(terrainTessShape*.1767767*span*bend,limit)*pixelsAtUnitW/w;
  n=min(n,ceil(sqrt(sagitta/terrainTessPixelError)));
 }
 return clamp(n,1,maxFactor);
}
// Phong offset at barycentric point b of a patch (p_i positions, n_i unit
// normals): -shape * sum b_i ((P - p_i).n_i) n_i with P the flat point,
// returned as the three scalars multiplying n_i (so a clip-space copy of the
// normals moves the clip position identically), scaled so the offset never
// exceeds the interpolated limit. Zero at the corners.
float3 terrainTessPhong(float3 b,float3 p0,float3 p1,float3 p2,float3 n0,float3 n1,float3 n2,float limit){
 float3 P=b.x*p0+b.y*p1+b.z*p2;
 float3 s=-terrainTessShape*b*float3(dot(P-p0,n0),dot(P-p1,n1),dot(P-p2,n2));
 float l=length(s.x*n0+s.y*n1+s.z*n2);
 return l>limit?s*(limit/max(l,1e-6)):s;
}
#ifdef __cplusplus
//")hlsl"
#endif
