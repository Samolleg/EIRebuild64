// Barrier compiler; see d3d12_barriers.h.
#include "d3d12_barriers.h"

#include <algorithm>
#include <cstdio>

namespace d3d12 {

namespace {

constexpr D3D12_RESOURCE_STATES kReadOnly =
    D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER |
    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
    D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT | D3D12_RESOURCE_STATE_COPY_SOURCE | D3D12_RESOURCE_STATE_DEPTH_READ;

bool read_only(D3D12_RESOURCE_STATES state) noexcept { return state != 0 && (state & ~kReadOnly) == 0; }

// True when a resource in current must transition to be used as wanted.
bool needs_transition(D3D12_RESOURCE_STATES current, D3D12_RESOURCE_STATES wanted) noexcept {
    if (current == wanted) return false;
    // A combined read state already covering a read-only use stays.
    return !(read_only(current) && read_only(wanted) && (current & wanted) == wanted);
}

struct Tracked {
    D3D12_RESOURCE_STATES state;
    int last_pass;  // -1: not used yet this frame
    bool uav_written;
};

// One subresource's transition, before it is merged into whole-resource barriers.
struct Change {
    D3D12_RESOURCE_STATES from, to;
    int begin_slot, end_slot;  // equal: a plain transition
    bool operator==(const Change& o) const noexcept {
        return from == o.from && to == o.to && begin_slot == o.begin_slot && end_slot == o.end_slot;
    }
};

void emit_transition(BarrierPlan& plan, ID3D12Resource* resource, std::uint32_t subresource, const Change& change) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = subresource;
    barrier.Transition.StateBefore = change.from;
    barrier.Transition.StateAfter = change.to;
    if (change.begin_slot == change.end_slot) {
        plan.before[change.end_slot].push_back(barrier);
        ++plan.transitions;
        return;
    }
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY;
    plan.before[change.begin_slot].push_back(barrier);
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_END_ONLY;
    plan.before[change.end_slot].push_back(barrier);
    ++plan.splits;
}

void emit_uav(BarrierPlan& plan, ID3D12Resource* resource, int slot) {
    for (const auto& existing : plan.before[slot])
        if (existing.Type == D3D12_RESOURCE_BARRIER_TYPE_UAV && existing.UAV.pResource == resource) return;
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = resource;
    plan.before[slot].push_back(barrier);
    ++plan.uav_barriers;
}

// Transitions of the listed subresources (all of them when whole) to `to`
// for a use in slot; one ALL_SUBRESOURCES barrier when they agree.
void change_states(BarrierPlan& plan, const PlanResource& resource, std::vector<Tracked>& tracked, std::uint32_t first,
                   std::uint32_t count, bool whole, D3D12_RESOURCE_STATES to, int slot) {
    std::vector<std::pair<std::uint32_t, Change>> changes;
    for (std::uint32_t s = first; s < first + count; ++s) {
        const Tracked& t = tracked[s];
        if (!needs_transition(t.state, to)) continue;
        // A gap of at least one pass since the last use allows a split barrier.
        const int begin = t.last_pass + 1;
        changes.push_back({s, Change{t.state, to, begin < slot ? begin : slot, slot}});
    }
    bool agree = whole && changes.size() == count;
    for (std::size_t i = 1; agree && i < changes.size(); ++i) agree = changes[i].second == changes[0].second;
    if (agree && !changes.empty()) emit_transition(plan, resource.resource, kAllSubresources, changes[0].second);
    else for (const auto& [s, change] : changes) emit_transition(plan, resource.resource, s, change);
}

} // namespace

bool compile_barriers(const std::vector<PlanResource>& resources, const std::vector<PlanPass>& passes, BarrierPlan& plan,
                      std::string& error) {
    plan = BarrierPlan{};
    plan.before.resize(passes.size() + 1);
    std::vector<std::vector<Tracked>> tracked(resources.size());
    for (std::size_t r = 0; r < resources.size(); ++r)
        tracked[r].assign((std::max)(1u, resources[r].subresources), Tracked{resources[r].home, -1, false});

    for (int p = 0; p < static_cast<int>(passes.size()); ++p) {
        const PlanPass& pass = passes[p];
        for (const PlanUse& use : pass.uses) {
            const auto fail = [&](const char* what) {
                char text[200];
                std::snprintf(text, sizeof(text), "barrier plan: pass %d (%s) resource %u subresource %u: %s", p, pass.name,
                              use.resource, use.subresource, what);
                error = text;
                return false;
            };
            if (use.resource >= resources.size()) return fail("no such resource");
            auto& states = tracked[use.resource];
            const bool whole = use.subresource == kAllSubresources;
            if (!whole && use.subresource >= states.size()) return fail("no such subresource");
            const std::uint32_t first = whole ? 0 : use.subresource;
            const std::uint32_t count = whole ? static_cast<std::uint32_t>(states.size()) : 1;
            for (std::uint32_t s = first; s < first + count; ++s)
                if (states[s].last_pass == p) return fail("declared twice in one pass");
            const bool uav = (use.state & D3D12_RESOURCE_STATE_UNORDERED_ACCESS) != 0;
            bool uav_hazard = false;
            for (std::uint32_t s = first; s < first + count; ++s)
                uav_hazard |= uav && states[s].uav_written && !needs_transition(states[s].state, use.state);
            change_states(plan, resources[use.resource], states, first, count, whole, use.state, p);
            if (uav_hazard) emit_uav(plan, resources[use.resource].resource, p);
            for (std::uint32_t s = first; s < first + count; ++s) {
                Tracked& t = states[s];
                if (needs_transition(t.state, use.state)) t.state = use.state;  // else a covering read state stays
                t.last_pass = p;
                t.uav_written = uav && use.write;
            }
        }
    }

    // Back to the home states after the last pass.
    const int end = static_cast<int>(passes.size());
    for (std::size_t r = 0; r < resources.size(); ++r) {
        auto& states = tracked[r];
        bool written = false;
        for (const Tracked& t : states) written |= t.uav_written && !needs_transition(t.state, resources[r].home);
        change_states(plan, resources[r], states, 0, static_cast<std::uint32_t>(states.size()), true, resources[r].home, end);
        if (written && (resources[r].home & D3D12_RESOURCE_STATE_UNORDERED_ACCESS)) emit_uav(plan, resources[r].resource, end);
    }
    return true;
}

} // namespace d3d12
