#pragma once

#include <cstddef>
#include <cstdint>

namespace owned {

// Optional experiment: these sites must not gate the ordinary engine bridge.
// Supported-image evidence: world Render -> view builder; main-loop client
// segment and wall-frame Step callers; remaining client backlog / tick length.
constexpr std::uint32_t kCameraPanObjectRva = 0x0039b2a8;
constexpr std::uint32_t kCameraPanViewCallRva = 0x00169e77;
constexpr std::uint32_t kCameraPanViewEntryRva = 0x000f0ef0;
constexpr std::uint32_t kCameraPanBacklogRva = 0x0039bc68;
constexpr std::uint32_t kCameraPanTickLengthRva = 0x0038ff34;
constexpr std::uint32_t kCameraPanClockOperandsRva = 0x00057375;

// Identical in the verified vanilla and Erlang native camera. The owned RMB,
// timing and pan hooks alter callers outside these bodies; do not mask bytes.
constexpr std::size_t kCameraPanStepBodyBytes = 3103u;
constexpr std::uint64_t kCameraPanStepBodyHash = 0x56bb1baea7e8c92eull;
constexpr std::size_t kCameraPanViewBodyBytes = 436u;
constexpr std::uint64_t kCameraPanViewBodyHash = 0x536ae6464739f1faull;

} // namespace owned
