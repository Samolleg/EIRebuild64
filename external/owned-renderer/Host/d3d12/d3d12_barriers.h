#pragma once
// Barrier compiler (plan 2.1): walks a frame's passes once before recording
// and emits every resource barrier the frame needs, batched per pass
// boundary. A resource's state comes from its last declared use; there is
// no per-draw tracking and no runtime hazard detection.
//
// - A state change becomes a transition before the using pass, or a split
//   barrier (BEGIN_ONLY after the previous use, END_ONLY before this one)
//   when at least one pass lies between the two uses.
// - A UAV barrier is emitted only where a pass uses as UAV a resource an
//   earlier pass wrote as UAV (and at frame end for a UAV-homed resource
//   left written).
// - Read-only uses already covered by the current combined read state need
//   nothing (a retained resource homed in ALL_SHADER_RESOURCE stays there).
// - Every resource starts the frame in its home state and is returned to it
//   after the last pass.
// - Subresources are tracked where a use names one (mips, array slices);
//   a whole-resource use whose subresources agree is one barrier.
#include <d3d12.h>

#include <cstdint>
#include <string>
#include <vector>

namespace d3d12 {

constexpr std::uint32_t kAllSubresources = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

struct PlanResource {
    ID3D12Resource* resource = nullptr;
    std::uint32_t subresources = 1;
    D3D12_RESOURCE_STATES home = D3D12_RESOURCE_STATE_COMMON;  // before the first pass and after the last
};

struct PlanUse {
    std::uint32_t resource = 0;  // index into the resource list
    std::uint32_t subresource = kAllSubresources;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    bool write = false;  // a UAV write orders the next UAV use
};

struct PlanPass {
    const char* name = "";
    std::vector<PlanUse> uses;  // each resource subresource at most once
};

struct BarrierPlan {
    // before[i] runs before pass i; before[passes] after the last pass.
    std::vector<std::vector<D3D12_RESOURCE_BARRIER>> before;
    std::uint32_t transitions = 0, splits = 0, uav_barriers = 0;
};

// false with error naming the pass and resource on a malformed plan (a
// programmer error: index out of range, a subresource declared twice).
bool compile_barriers(const std::vector<PlanResource>& resources, const std::vector<PlanPass>& passes, BarrierPlan& plan,
                      std::string& error);

inline void issue_barriers(ID3D12GraphicsCommandList* list, const BarrierPlan& plan, std::size_t slot) noexcept {
    const auto& barriers = plan.before[slot];
    if (!barriers.empty()) list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
}

} // namespace d3d12
