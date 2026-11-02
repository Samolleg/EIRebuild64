// Terrain tile cells (terrain_tile_blend.h): where a material's plain or folded
// art lies in a map's contact atlas. Terrain 2.0 (terrain2.hlsl terrain2Cell)
// and its cover pass (terrain2_cover.h) read cells through these. The Terrain
// 1.0 blend that once lived here is retired.
// Embedded into the game by Source/terrain_tile_blend.h at build time. The
// __cplusplus guard lines make the file a C++ raw string where that header
// includes it; to HLSL they are a skipped group of comments and one closed string.
#ifdef __cplusplus
R"hlsl( //"
#endif
// The plain cell of a material (slot position bits 0-5, page bits 6-11) in
// the contact atlas; false while that page has not been copied.
// A folded cell (page 63, terrain_tile_fold_cells) names an entry of the
// contact grid's last row: a transition slot and the corner quadrant of the
// material's own art there, read mirror-folded (terrainCliffCellUv) so the
// quadrant repeats seamlessly as a whole tile.
// The cell from its fold-row entry e (folded cells; 0 otherwise), the atlas
// width aw and the map's copied slices; donor: the plain cell a folded
// family draws shifted (terrainTileBlendDonorCell), else 0xffffffff.
// Terrain 2.0 reads other maps' cells through it (terrain2Cell).
bool terrainTileBlendCellFrom(uint v,uint e,float aw,uint2 ready,out TerrainCliffCell k,out uint donor){
 uint slice=(v>>6)&63u,pos=v&63u,quadrant=0;bool folded=slice==63u;
 [branch]if(folded){slice=(e>>6)&63u;pos=e&63u;quadrant=(e>>12)&3u;}
 k.own=false;k.slice=slice;k.width=0;k.lowLod=0;k.marginLod=0;k.fold=0;k.dir=1;k.shift=0;
 float margin=nativeTerrainCliff.w*.125;
 k.lo=float2((pos%8u)*.125+margin,1-(pos/8u+1)*.125+margin);k.size=.125-2*margin;
 terrainCliffCellLevels(k,aw);
 k.marginLod=log2(max(margin*aw,1));
 [branch]if(folded){
  // Signature corners NW, NE, SE, SW; the preview top is the smaller v.
  // lo moves to the quadrant's outer corner and dir points to the slot
  // centre. The fold stops at 30% of the slot: past that the transition art
  // holds the other material, and the fold repeats its tile-edge texels
  // along every 2 m line.
  float2 corner=float2(quadrant==1u||quadrant==2u?1:0,quadrant>=2u?1:0);
  k.lo+=corner*k.size;k.dir=1-2*corner;k.fold=k.size.x*.3;
 }
 bool copied=slice<63u&&(((slice<32u?ready.x:ready.y)>>(slice&31u))&1u)!=0;
 // A folded family with a plain-art donor (entry bits 14-25, bit 26) draws
 // the donor's plain tile shifted to the folded quadrant's mean colour: the
 // quadrant itself, mirror-folded, repeated as a kaleidoscope every half
 // tile (cand-1030, save1162 rock) where the blend reads it at full weight.
 uint d=(e>>14)&4095u,ds=d>>6;
 donor=folded&&copied&&((e>>26)&1u)!=0&&ds<63u&&(((ds<32u?ready.x:ready.y)>>(ds&31u))&1u)!=0?d:0xffffffffu;
 return copied;
}
// Moves cell k to its donor's plain slot (after the folded quadrant's mean was read).
void terrainTileBlendDonorCell(inout TerrainCliffCell k,uint d){
 float margin=nativeTerrainCliff.w*.125;
 k.slice=d>>6;k.fold=0;k.dir=1;
 k.lo=float2(((d&63u)%8u)*.125+margin,1-((d&63u)/8u+1)*.125+margin);
}
#ifdef __cplusplus
//")hlsl"
#endif
