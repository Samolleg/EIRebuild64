#pragma once
#include "WorldBackgroundStreaming.h"

namespace ModWorldBackground {
// Native renderer capability, independent of installed DLL identities. Matches
// the source-bound current provider's payload/mesh limits, not its old API
// fallback. Logical bytes charge pixels per mesh even when DX11 shares textures.
struct Capacity {
    static constexpr uint64_t PayloadBytes=WorldBackground::MaxPayloadBudget;
    static constexpr uint32_t Meshes=256;
    // CPU: previous/ready, renderer's pending upload and a flattened decoration
    // source can overlap. Three retained snapshots provide backpressure; once
    // GPU commit releases its pending borrow at most two persistent owners remain.
    // This bounds logical retained payload, not vector capacity/driver/RSS bytes.
    static constexpr size_t Snapshots=3;
    static constexpr uint64_t RetainedPayloadBytes=Snapshots*PayloadBytes;
    static bool Accepts(uint64_t bytes,uint32_t meshes){return bytes<=PayloadBytes&&meshes<=Meshes;}
    static bool CanPrepare(size_t retained){return retained<Snapshots;}
};
}
