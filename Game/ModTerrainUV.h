#pragma once
// Half-world-unit field samples; map coordinates are independent of the camera.
namespace ModTerrainUV {
template<class Vertex> inline void Assign(Vertex &v,float mapX,float mapY,unsigned width,unsigned height) {
    v.tu=(mapX*2.0f+0.5f)/width;
    v.tv=(mapY*2.0f+0.5f)/height;
}
}
