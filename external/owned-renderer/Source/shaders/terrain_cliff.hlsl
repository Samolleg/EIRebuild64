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
static const float terrainCliffFacetSplitStart=.3; // facet/smooth split difference where the facet starts to choose the side plane @param 0 1
static const float terrainCliffFacetSplitFull=.5;  // difference where the facet chooses alone @param 0 1
static const float terrainCliffFacetEdgeNear=1;    // output pixels from a triangle edge: the smooth split only @param 0 8
static const float terrainCliffFacetEdgeFar=6;     // output pixels from a triangle edge: the facet split takes over (risers 12 px wide read one plane) @param 1 32
static const float terrainCliffFacetCentre=.25;    // smallest barycentric coordinate where the facet split is full (1/3 is the centroid) @param 0.05 0.33
// Side-plane split from the drawn facet (p derivatives, untessellated) where
// it disagrees with the smooth slope normal. Walls built as 1 m staircases in
// plan (caves: risers of 15 m, one cell wide) have risers facing one grid axis
// while the smoothed normal points between them, so both side planes read and
// the plane parallel to a riser smeared the art into bands across it. A
// split difference under terrainCliffFacetSplitStart (facets within ~20
// degrees of the smooth normal: smooth cliffs, hills) changes nothing, so
// the facet only decides along real riser corners. The top-down share stays
// with the smooth normal (a per-pixel facet there drew the triangulation).
// keep (terrainCliffFacetKeep): 0 on the triangle's edges, 1 inside. On an
// edge every triangle uses the interpolated smooth split, which both sides
// share, so a change of facet never draws a line; the facet split takes over
// within terrainCliffFacetEdgeFar of the edges.
float3 terrainCliffFacetPlanes(float3 planes,float3 pDx,float3 pDy,float keep){
 float side=planes.x+planes.y;
 if(side<=.002)return planes;
 float3 f=cross(pDx,pDy);float l=length(f);
 if(!(l>1e-12))return planes;
 f/=l;
 // Steep facets (from ~37 degrees, fully past ~60): as ae1ed347, which
 // removed the stretch on outdoor staircase cliffs too (o_zone3_steep); the
 // seams it drew came from its sub-pixel metric edge band, now in pixels.
 float steep=1-smoothstep(.5,.8,abs(f.z));
 if(steep<=0)return planes;
 float2 a=abs(f.xy);a*=a;float2 wf=a/max(a.x+a.y,1e-12);
 wf=max(wf-terrainCliffPlaneCut,0);wf/=max(wf.x+wf.y,1e-6);
 float2 wv=planes.xy/side;
 float t=keep*steep*smoothstep(terrainCliffFacetSplitStart,terrainCliffFacetSplitFull,abs(wf.x-wv.x));
 return float3(lerp(wv,wf,t)*side,planes.z);
}
// The facet split's weight from the pixel's distance, in output pixels, to
// the nearest edge of its drawn triangle (b barycentric coordinates, one
// corner each from nativeVertex; bDx/bDy their screen derivatives). Sized
// in pixels, the edge blend stays several pixels wide at every distance,
// so a change of facet never shows as a hard line.
// The weight also follows the triangle's own size (smallest barycentric
// coordinate up to terrainCliffFacetCentre): the pixel ramp alone switched
// the projection, and with it the blend's material decision and rock,
// between neighbouring triangles within ~10 px at 4K, a hard cut between
// cliff textures along triangle edges on steep faces (2026-10-30 F11s,
// zone6 and zone9). Each triangle now eases from the shared smooth split on
// its edges to its facet split toward its middle.
float terrainCliffFacetKeep(float3 b,float3 bDx,float3 bDy){
 float3 g=sqrt(bDx*bDx+bDy*bDy);
 float3 d=b/max(g,1e-6);
 float inner=min(b.x,min(b.y,b.z));
 return smoothstep(terrainCliffFacetEdgeNear,terrainCliffFacetEdgeFar,min(d.x,min(d.y,d.z)))
       *smoothstep(0,terrainCliffFacetCentre,inner);
}
static const float terrainFootContactDepth=.45;  // ambient light removed at the very foot @param 0 1
static const float terrainFootContactReach=1.2;  // m: the band's height on the wall @param 0.2 4
// Wall-foot contact darkening, 0..1, from two per-vertex values interpolated
// across the triangles: rise, the pixel's height over the local floor
// (owned_terrain_foot_rise), and nearness, how near a wall rises beside it
// (owned_terrain_wall_above, 0..1, falling off over 3 m). The band runs
// evenly along the whole foot with no corner point or per-triangle step,
// fades up the wall over terrainFootContactReach and out onto the floor
// with the wall's nearness.
float terrainFootContact(float rise,float nearness){
 float t=saturate(rise/terrainFootContactReach);
 return smoothstep(.05,.85,nearness)*(1-t)*(1-t);
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
