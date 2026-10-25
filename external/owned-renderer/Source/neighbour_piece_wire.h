#pragma once
// A neighbour piece on the remote link (neighbour_piece_wire.cpp): the x64
// render host serializes the pieces its stream builds; the renderer DLL reads
// them back. GPU objects travel as an object table (kind, description, view
// parent); the host binds each to a handle the DLL allocated, and the DLL
// wraps the same handles as its remote proxies. Every value is fixed width;
// each plain record type's size is checked on read, so an x86/x64 layout
// difference fails the read with the type's name instead of misreading it.
#include "neighbour_piece.h"

#include <d3d11.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace owned {

enum class NeighbourWireObjectKind : std::uint32_t { buffer = 1, texture2d = 2, srv = 3 };

struct NeighbourWireObject {
    NeighbourWireObjectKind kind = NeighbourWireObjectKind::buffer;
    std::uint32_t parent = UINT32_MAX;   // a view's resource (object table index)
    D3D11_BUFFER_DESC buffer{};
    D3D11_TEXTURE2D_DESC texture{};
    D3D11_SHADER_RESOURCE_VIEW_DESC view{};
};

// Host: the piece's bytes and its GPU objects in table order (each AddRef'd
// for the caller, which hands the references on when it binds them).
bool write_neighbour_piece(const NeighbourPiece& piece, std::vector<std::uint8_t>& bytes,
    std::vector<IUnknown*>& objects, std::string& failure);

// DLL: the piece from its bytes. `wrap` turns table entry `index` into an
// object holding one reference for the piece (its view parent already
// wrapped); null fails the read.
using NeighbourWireWrap = std::function<IUnknown*(std::uint32_t index, const NeighbourWireObject& object,
    IUnknown* parent)>;
bool read_neighbour_piece(const std::uint8_t* bytes, std::size_t size, NeighbourPiece& piece,
    const NeighbourWireWrap& wrap, std::string& failure);
} // namespace owned
