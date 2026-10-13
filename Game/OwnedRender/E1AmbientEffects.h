#pragma once
#include "engine_bridge.h"

namespace OwnedRender {
struct E1GroundCoverFootprintFact {
    const void* host_marker = nullptr; // Synchronous host token; never copied to c56.
    std::int32_t remaining_segments = 0;
    std::int32_t fade_segments = 0;
    bool visible = false;
};

void E1ResetGroundCoverFootprints() noexcept;
void E1RegisterGroundCoverFootprint(const void* host_marker, const void* owner,
    std::uint64_t world, std::int32_t tick, float center_x, float center_y) noexcept;
bool E1PublishGroundCoverFootprints(const std::vector<E1GroundCoverFootprintFact>& facts,
    bool enabled) noexcept;
}
