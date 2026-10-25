// The terrain material cache's per-map cell tables (terrain_bake.h): pure CPU
// over a height grid, shared by the renderer DLL and the x64 render host
// (the neighbour stream builds them for each neighbour).
#include "terrain_bake.h"

#include <algorithm>
#include <cmath>

namespace owned {

TerrainBakeCells terrain_bake_cells(const std::vector<float>& height,std::uint32_t tiles_x,std::uint32_t tiles_y){
 TerrainBakeCells cells;
 const int verts_x=int(tiles_x*2u+1u),verts_y=int(tiles_y*2u+1u);
 if(height.size()!=std::size_t(verts_x)*verts_y)return cells;
 const auto vertex=[&](int x,int y){
  x=std::clamp(x,0,verts_x-1);y=std::clamp(y,0,verts_y-1);
  return height[std::size_t(y)*verts_x+x];
 };
 cells.cells_x=int((tiles_x+terrain_bake_cell_tiles-1)/terrain_bake_cell_tiles);
 cells.cells_y=int((tiles_y+terrain_bake_cell_tiles-1)/terrain_bake_cell_tiles);
 const std::size_t count=std::size_t(cells.cells_x)*cells.cells_y;
 cells.low.assign(count,0);cells.high.assign(count,0);cells.rise_x.assign(count,0);cells.rise_y.assign(count,0);
 for(int cy=0;cy<cells.cells_y;++cy)for(int cx=0;cx<cells.cells_x;++cx){
  float low=1e30f,high=-1e30f,rx=0,ry=0;
  for(int vy=cy*8-1;vy<=cy*8+9;++vy)for(int vx=cx*8-1;vx<=cx*8+9;++vx){
   const float h=vertex(vx,vy);low=std::min(low,h);high=std::max(high,h);
   rx=std::max(rx,std::abs(vertex(vx+1,vy)-h));ry=std::max(ry,std::abs(vertex(vx,vy+1)-h));
  }
  const std::size_t i=std::size_t(cy)*cells.cells_x+cx;cells.low[i]=low;cells.high[i]=high;cells.rise_x[i]=rx;cells.rise_y[i]=ry;
 }
 return cells;
}

} // namespace owned
