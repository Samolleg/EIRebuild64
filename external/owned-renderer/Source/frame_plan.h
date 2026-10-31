#pragma once
// Frame plan skeleton (DX12 plan M0 / section 4 item 1), first file group:
// the UI, frame and surfaces passes record one plan pass each as they run,
// with their declared targets and reads as owned ids, and bind their
// targets from that record instead of from "whatever is bound". Plan order
// is arrival order (the game's order); nothing is buffered or reordered.
// The D3D11 executor resolves an id to today's view (Renderer::plan_rtv /
// plan_dsv); the D3D12 executor will resolve it to its own resources and
// derive barriers from the declared reads and writes.
//
// "Each pass establishes its own state": a recorded pass binds every
// pipeline state it reads and restores nothing it found.
#include <cstdint>
#include <vector>

struct ID3D11RenderTargetView;
struct ID3D11DepthStencilView;

namespace owned {

enum class PlanTarget : std::uint8_t {
    none,
    world_color,       // the world scene target (scene_composite_.target())
    world_depth,       // world_dsv_
    backbuffer,        // backbuffer_rtv_
    interface_depth,   // interface_dsv_ when it is not the world depth
    camp_popup_color,  // camp_popup_rtv_
    camp_popup_depth,  // camp_popup_dsv_
    unowned,           // a view no plan id names yet (other file groups); the pass carries it
};

constexpr std::uint32_t plan_read(PlanTarget target) noexcept { return 1u << static_cast<unsigned>(target); }

struct PlanPass {
    const char* name = "";
    PlanTarget color = PlanTarget::none, depth = PlanTarget::none;
    std::uint32_t reads = 0;  // plan_read bits
    // Only for PlanTarget::unowned: the view the pass was given.
    ID3D11RenderTargetView* unowned_color = nullptr;
    ID3D11DepthStencilView* unowned_depth = nullptr;
};

// The current frame's passes in arrival order; cleared after each present.
// Capacity is kept, so a steady frame allocates nothing.
class FramePlan {
public:
    void begin_frame() noexcept { passes_.clear(); }
    const PlanPass& add(const PlanPass& pass) noexcept {
        try {
            passes_.push_back(pass);
            return passes_.back();
        } catch (...) {
            overflow_ = pass;  // allocation failure: the pass still runs from its own record
            return overflow_;
        }
    }
    const std::vector<PlanPass>& passes() const noexcept { return passes_; }

private:
    std::vector<PlanPass> passes_;
    PlanPass overflow_;
};

} // namespace owned
