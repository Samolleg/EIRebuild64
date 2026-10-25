#pragma once
// The renderer DLL's neighbour stream, wherever it runs: this process's own
// stream in standard mode (neighbour_maps.h), the x64 render host's in remote
// device mode (x64_remote_neighbours.h). Either way the renderer adopts the
// same NeighbourPiece (neighbour_piece.h); in remote mode no decoded
// neighbour ever exists in the game process.
#include "owned_resource_store.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace owned {

struct NeighbourPiece;

void neighbour_client_begin(const std::string& mpr, std::uint64_t epoch, const ResourceStoreConfig& mounts) noexcept;
std::unique_ptr<NeighbourPiece> neighbour_client_take(std::uint64_t epoch) noexcept;
void neighbour_client_uploaded(std::uint64_t epoch) noexcept;
void neighbour_client_stop() noexcept;
void neighbour_client_stop_epoch(std::uint64_t epoch) noexcept;
bool neighbour_client_settled(std::uint64_t epoch) noexcept;
bool neighbour_client_remaining(std::uint64_t epoch, std::size_t& remaining) noexcept;
std::string neighbour_client_error(std::uint64_t epoch);

} // namespace owned
