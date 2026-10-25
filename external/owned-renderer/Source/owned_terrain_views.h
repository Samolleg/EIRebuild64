#pragma once
// Terrain command views over an immutable decoded terrain (owned_terrain_views.cpp).
#include "engine_bridge.h"
#include "neighbour_maps.h"
#include "owned_terrain_decode.h"
#include "owned_terrain_front_end.h"

namespace owned {

// The geometry view of one terrain command over the packet's decoded source.
// Its decode reads only the immutable source (no counters, no game memory);
// a caller that counts decodes installs its own decode function.
bool source_terrain_view(const OwnedTerrainFramePacket& packet,
                         const OwnedTerrainCommand& command,
                         const OwnedTerrainDynamicSector& dynamic,
                         OwnedTerrainSourceDecodeContext& context,
                         NativeGeometryView& view) noexcept;

} // namespace owned
