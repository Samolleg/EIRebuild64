#pragma once
// The renderer DLL's side of the host neighbour stream (remote device mode):
// the records of x64_remote_protocol.h, and pieces read back as remote
// proxies. Every failure loses the device with a named reason; nothing here
// decodes a neighbour in the game process.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace owned { struct NeighbourPiece; }

namespace x64remote {

void neighbour_begin(std::uint64_t epoch, bool enabled, const std::vector<std::uint8_t>& inputs) noexcept;
void neighbour_stop(std::uint64_t epoch) noexcept;
void neighbour_uploaded(std::uint64_t epoch) noexcept;
// The next piece in layout order, or null (none ready, or the device was
// lost naming why).
std::unique_ptr<owned::NeighbourPiece> neighbour_take(std::uint64_t epoch) noexcept;
struct NeighbourStatus {
    std::uint64_t epoch = 0;
    bool settled = true, remaining_known = true;
    std::uint32_t remaining = 0;
    std::string error;
};
// The host's last published stream status.
NeighbourStatus neighbour_status() noexcept;

} // namespace x64remote
