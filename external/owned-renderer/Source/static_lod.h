#pragma once

// Static object LOD: reduced index levels of a rigid figure, built once per
// figure with the meshoptimizer simplifier (renderer_static_lod.cpp). Every
// level indexes the figure's own components, so it serves every shape of the
// figure. The renderer stores the levels after the mesh's full index list and
// nativeBuildCS picks one per record by its projected error.

#include <array>
#include <cstdint>
#include <vector>

namespace owned {

struct FigureCpuAsset;

// Meshes smaller than this keep only their full level: their records cost
// less than a level's bookkeeping saves.
inline constexpr std::uint32_t kStaticLodMinTriangles = 96;
inline constexpr std::uint32_t kStaticLodMaxLevels = 3;
// The crossfade band beyond the pixel limit, as a fraction of it: a level's
// successor fades in while its error projects between limit and
// limit * (1 + band) (no pop: stateless, continuous with distance).
inline constexpr float kStaticLodBand = 1.0f;

// NativeInstanceGpu::world_geometry (HLSL worldGeometry) bits. Every reader
// tests the field against zero (world geometry or not), so the bits above
// bit 0 refine a world record without changing that test:
//   bit 0  world geometry (always set with the others)
//   bit 1  object space: the vertex stage evaluates the shared mesh with the
//          record's transform; the record owns no world vertices
//   bit 2  static LOD: nativeBuildCS picks a reduced index level; a retained
//          world record keeps its prepared vertices and puts their base in
//          secondMorphOffset (world records have no legacy morph stream).
inline constexpr std::uint32_t native_world_geometry = 1u;
inline constexpr std::uint32_t native_world_object_space = 2u;
inline constexpr std::uint32_t native_world_lod = 4u;
//   bit 3  crown impostor: a cut-out record drawing its baked impostor quad
//          far away (NativeInstance::secondMorphOffset holds its atlas slice;
//          object-space records never read it, they carry no morph)
inline constexpr std::uint32_t native_world_impostor = 8u;
//   bit 4  immutable vertex opacity in normal.w; world normal XYZ and
//          material lighting are unchanged, without terrain animation data.
inline constexpr std::uint32_t native_world_vertex_opacity = 16u;

// Crown impostors: each cut-out decoration figure is baked once into one
// slice of an atlas array: kImpostorFrames x kImpostorFrames orthographic
// views of kImpostorFrameTexels squared over the upper hemisphere
// (hemi-octahedral), centred on the mean shape's bounding sphere, holding
// albedo with coverage and object-space normals. The bake and the vertex
// stage share these frame directions and image axes.
inline constexpr std::uint32_t kImpostorFrames = 4;
inline constexpr std::uint32_t kImpostorFrameTexels = 64;
inline constexpr std::uint32_t kImpostorMips = 4;
// The crossfade band above the impostor size, as a fraction of it.
inline constexpr float kImpostorBand = 0.5f;
// Figures below this many triangles keep drawing their own triangles.
inline constexpr std::uint32_t kImpostorMinTriangles = 32;
inline constexpr char kImpostorFrameHlsl[] = R"(
static const uint kImpostorFrames=4;
static const float kImpostorFrameTexels=64;
// Share of the frame the bounding sphere spans (a border for filtering).
static const float kImpostorMargin=.94;
// Frame (i,j) of the grid looks from this object-space direction.
float3 nativeImpostorFrameDirection(uint2 frame){
 float2 p=float2(frame)/(kImpostorFrames-1)*2-1;
 float3 d=float3((p.x+p.y)*.5,(p.x-p.y)*.5,0);d.z=1-abs(d.x)-abs(d.y);
 return normalize(d);
}
// Grid position (0-1) of an upper-hemisphere direction (z clamped to 0).
float2 nativeImpostorGrid(float3 d){
 d.z=max(d.z,0);float s=abs(d.x)+abs(d.y)+d.z;
 float2 p=s>1e-6?d.xy/s:float2(0,0);
 return float2(p.x+p.y,p.x-p.y)*.5+.5;
}
// Image axes of a view along -d (right, up): z up unless looking straight down.
void nativeImpostorBasis(float3 d,out float3 r,out float3 u){
 float3 up=abs(d.z)>.999?float3(0,1,0):float3(0,0,1);
 r=normalize(cross(up,d));u=cross(d,r);
}
)";

// A figure's impostor bake input, prepared on the neighbour worker: per
// component the mean shape's position, the normal and the UV (8 floats), the
// triangle list, and the mean shape's bounding sphere.
struct NativeImpostorSource {
    std::vector<float> vertices;
    std::vector<std::uint32_t> indices;
    std::array<float, 4> sphere{};
};
bool build_impostor_source(const FigureCpuAsset& figure, NativeImpostorSource& output) noexcept;

struct NativeMeshLods {
    std::uint32_t levels = 0; // reduced levels, coarser in turn
    std::array<std::vector<std::uint32_t>, kStaticLodMaxLevels> indices;
    // Largest deviation of each level, object units, over every stored shape;
    // never smaller than the previous level's.
    std::array<float, kStaticLodMaxLevels> error{};
};

// False (and no levels) for a figure too small or not reducible.
bool build_static_lods(const FigureCpuAsset& figure, NativeMeshLods& output) noexcept;

} // namespace owned
