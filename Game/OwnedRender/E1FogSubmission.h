#pragma once
#include "engine_bridge.h"
#include "native_instance_flags.h"
#include "../dx11/Vertex.h"
#include <cmath>
#include <vector>

namespace OwnedRender {
// Completed host triangles, including already projected weather/line vertices.
// This is a data adapter; c56 owns geometry preparation and the fog pixel pass.
struct E1TrianglePacket {
    std::vector<owned::NativePackedGroup> positions, normals;
    std::vector<std::uint32_t> diffuse, specular;
    std::vector<owned::NativeTextureCoordinate> uv;
    std::vector<owned::NativeVertexComponent> components;

    bool Build(const STLVertex1* vertices, std::uint32_t count,
        const WORD* indices, std::uint32_t indexCount) {
        if (!vertices || !indices || !count || count > 65536 || !indexCount || indexCount % 3)
            return false;
        for (std::uint32_t i = 0; i < indexCount; ++i)
            if (indices[i] >= count) return false;
        positions.resize((count + 3) / 4); normals.resize(positions.size());
        diffuse.resize(positions.size() * 4, 0xffffffffu);
        specular.resize(positions.size() * 4, 0xff000000u);
        uv.resize(count); components.resize(count);
        for (std::size_t group = 0; group < positions.size(); ++group)
            for (unsigned lane = 0; lane < 4; ++lane) {
                positions[group].w[lane] = 1.f;
                normals[group].z[lane] = normals[group].w[lane] = 1.f;
            }
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto& vertex = vertices[i];
            if (!std::isfinite(vertex.sx) || !std::isfinite(vertex.sy) ||
                !std::isfinite(vertex.sz) || !std::isfinite(vertex.rhw) ||
                !std::isfinite(vertex.tu) || !std::isfinite(vertex.tv)) return false;
            auto& group = positions[i / 4]; const unsigned lane = i % 4;
            group.x[lane] = vertex.sx; group.y[lane] = vertex.sy;
            group.z[lane] = vertex.sz; group.w[lane] = vertex.rhw;
            diffuse[i] = vertex.color; specular[i] = vertex.specular;
            uv[i] = {vertex.tu, vertex.tv};
            components[i] = {WORD(i), WORD(i), WORD(i)};
        }
        return true;
    }
    owned::NativeGeometryView View(const WORD* indices, std::uint32_t indexCount) const {
        owned::NativeGeometryView view;
        view.positions = positions.data(); view.position_groups = std::uint32_t(positions.size());
        view.normals = normals.data(); view.normal_groups = std::uint32_t(normals.size());
        view.diffuse = diffuse.data(); view.specular = specular.data();
        view.texture_coordinates = uv.data(); view.texture_coordinate_count = std::uint32_t(uv.size());
        view.components = components.data(); view.component_count = std::uint32_t(components.size());
        view.indices = indices; view.index_count = indexCount;
        view.semantic_kind = owned::SemanticKind::simple_geometry;
        view.ordering = owned::NativeOrdering::preserve;
        view.flags = owned::native_embedded_homogeneous_flag;
        return view;
    }
};
}
