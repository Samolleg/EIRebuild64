// The converter never loads terrain or records runtime diagnostics.
#include "owned_asset_loader.h"
#include <stdexcept>
namespace owned {
void bridge_append_probe_event(const char *, const char *) noexcept {}
void owned_terrain_reset_liquid_outflow(TerrainCpuAsset &) {
  throw std::logic_error("terrain is outside FIG converter");
}
void owned_terrain_prepare_astral(ResourceStore &, TerrainCpuAsset &) {
  throw std::logic_error("terrain is outside FIG converter");
}
void owned_terrain_prepare_shading(TerrainCpuAsset &) {
  throw std::logic_error("terrain is outside FIG converter");
}
std::string owned_terrain_classify_swamp_frames(TerrainCpuAsset &) {
  throw std::logic_error("terrain is outside FIG converter");
}
} // namespace owned
