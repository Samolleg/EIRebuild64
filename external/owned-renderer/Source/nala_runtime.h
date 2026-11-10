#pragma once
#include <array>
#include <memory>
#include <vector>
#include "skn_runtime.h"
namespace owned { struct RetainedVisualSample; }
namespace owned::nala {
struct Input {
 bool selected=false, valid=false;
 std::array<double,45> controls{};
 std::array<unsigned,4> clothing{}; // native slots 3,4,6,5
 std::array<unsigned char,6> wounds{};
 bool operator==(const Input& b)const noexcept {
  return selected==b.selected&&valid==b.valid&&controls==b.controls&&clothing==b.clothing&&wounds==b.wounds;
 }
};
struct Mesh {
 std::vector<NativePackedGroup> positions,normals;
 std::vector<NativeTextureCoordinate> uv;
 std::vector<NativeVertexComponent> components;
 std::vector<std::uint16_t> indices;
 unsigned resource=0;
};
struct Frame {
 std::vector<Mesh> meshes;
 std::shared_ptr<const skn::RuntimeAsset> materials;
 std::uint64_t serial=0;
 unsigned root_node=0;
 float radius=0;
 std::map<std::string,std::array<double,12>> attachments;
};
std::shared_ptr<const Frame> prepare(const RetainedVisualSample&);
}
