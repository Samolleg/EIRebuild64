#define NOMINMAX
#include "../Game/RuntimeFrame.h"
#include "../Game/CorePerf.h"
#include "../Game/OwnedRender/E1PerfDiagnostics.h"
#include <d3d.h>
#define D3DCOLORVALUE_DEFINED
#include "../Game/OwnedRender/E2FrameTiming.h"
#include "../Navigation/PathWorkBudget.h"
#include "../Navigation/Query.h"
#include "model_preview_clock.h"
#include <cstdio>
#include <stdexcept>

// This console check never opens the game or writes a gameplay journal.
namespace GameJournal {
void Event(const char*, const char*, const char*, ...) noexcept {}
std::string Quote(const char* value) noexcept { return value ? value : ""; }
}
static unsigned checks = 0;
static void Require(bool ok, const char* name) {
    ++checks;
    if (!ok) throw std::runtime_error(name);
}
static std::unique_ptr<Navigation::Mesh> Corridor() {
    auto geometry = std::make_shared<Navigation::Geometry>();
    Navigation::Triangle a, b;
    a.a = {0,0,0}; a.b = {256,0,0}; a.c = {256,4,0}; a.terrain = true;
    b.a = {0,0,0}; b.b = {256,4,0}; b.c = {0,4,0}; b.terrain = true;
    geometry->ground = {a,b}; geometry->Bounds();
    Navigation::Profile profile; profile.cellSize = .2f; profile.tileCells = 32;
    Navigation::IndexSources(*geometry, profile.cellSize * profile.tileCells);
    auto mesh = Navigation::Build(*geometry, profile, "diagnostics-independent-corridor");
    Require(bool(mesh), "synthetic corridor builds");
    mesh->sources = geometry;
    Require(mesh && mesh->tiles > 16, "long synthetic corridor has enough tiles for sliced work");
    return mesh;
}
static Navigation::Request Request(Navigation::SlicedSearch& search) {
    Navigation::Request request;
    request.source = {1,2,0}; request.goal = {255,2,0};
    request.height = 1.6f; request.maxLength = 1000; request.goalRadius = .25f;
    request.goalPolicy = Navigation::GoalPolicy::Exact; request.sliced = &search;
    return request;
}
int main() try {
    const bool diagnostics = RuntimeDiagnostics::Enabled();
    CorePerf::Initialize();
    Require((CorePerf::Main() != nullptr) == diagnostics, "profiling obeys launch policy");
    auto mesh = Corridor();
    if (!diagnostics) {
        // Reproduce the installed defect using its real, disabled CorePerf clock.
        Navigation::SlicedSearch search; auto request = Request(search);
        Navigation::PathProgress progress;
        double exhausted = 0;
        request.frameBudgetMs = &exhausted;
        for (int i = 0; i < 8; ++i) {
            CorePerf::Frame frame;
            request.ensureProgress = progress.FirstBatch(CorePerf::Get().frameNumber);
            Require(Navigation::Find(*mesh, request).status == Navigation::Status::Updating,
                "legacy profiling clock reproduces stalled sliced path");
        }
        Require(CorePerf::Get().frameNumber == 0, "disabled profiling clock does not advance");
    }
    Navigation::SlicedSearch search; auto request = Request(search);
    Navigation::PathWorkBudget budget;
    Navigation::PathProgress progress, otherUnit;
    Navigation::Route route;
    unsigned iterations = 0;
    for (; iterations < 128; ++iterations) {
        RuntimeFrame::Begin();
        CorePerf::Frame profileFrame;
        const auto frame = RuntimeFrame::Current();
        Require(frame > 0, "runtime progresses without diagnostics");
        request.frameBudgetMs = budget.ForFrame(frame);
        Require(*request.frameBudgetMs == 2., "new runtime iteration replenishes path allowance");
        *request.frameBudgetMs = 0; // earlier NPCs have consumed the entire allowance
        Require(*budget.ForFrame(frame) == 0, "same iteration cannot replenish budget twice");
        request.ensureProgress = progress.FirstBatch(frame);
        Require(request.ensureProgress && !progress.FirstBatch(frame), "one guaranteed batch per unit/frame");
        Require(otherUnit.FirstBatch(frame), "later units also receive their bounded batch");
        route = Navigation::Find(*mesh, request);
        if (route.status == Navigation::Status::Complete) break;
        Require(route.status == Navigation::Status::Updating, "sliced search remains resumable");
    }
    Require(iterations > 0 && iterations < 128 && route.status == Navigation::Status::Complete,
        "actual Detour path completes across multiple diagnostic-independent frames");
    Require(route.points.size() >= 2 && route.length > 250, "complete route reaches distant destination");

    // Actual preview anchor code: hiding a model for several presents must
    // re-anchor its presentation instead of mistaking it for a same-frame draw.
    RuntimeFrame::Sequence presents;
    owned::ModelPreviewView view;
    owned::RetainedVisualSample sample;
    sample.resource.signed_id = 1; sample.resource.serial = 1;
    sample.selection.neutral = true;
    Require(owned::prepare_model_preview_phase(view, sample, 1, presents.Current(), 10, 0, 1), "first preview");
    presents.Advance();
    Require(owned::prepare_model_preview_phase(view, sample, 1, presents.Current(), 11, 0, 1) && view.anchor_time == 10,
        "consecutive present retains anchor");
    presents.Advance(); presents.Advance(); presents.Advance();
    Require(owned::prepare_model_preview_phase(view, sample, 1, presents.Current(), 20, 0, 1) && view.anchor_time == 20,
        "hidden preview re-anchors after missing presents");

    OwnedRender::E1PerfCounter counter{"off-check"};
    { OwnedRender::E1PerfScope optional(counter); }
    Require(counter.calls == (diagnostics ? 1u : 0u), "optional host timing stays optional");
    OwnedRender::E2FrameTiming gpuTiming;
    const auto attached = gpuTiming.Attach(nullptr, nullptr);
    Require(attached == (diagnostics ? E_INVALIDARG : S_FALSE), "disabled GPU measurements need no device");
    gpuTiming.Begin(); gpuTiming.End(true);
    Require(gpuTiming.Read().completed == (diagnostics ? 1u : 0u), "optional GPU recorder stays inactive when disabled");
    Require(diagnostics || (CorePerf::Get().liveFrameCount == 0 && CorePerf::Get().scopes[CorePerf::IdleFrame].calls == 0),
        "fix does not turn profiling back on");
    std::printf("PASS diagnostics=%u checks=%u path_iterations=%u path_points=%zu length=%.3f preview_resume=ok\n",
        unsigned(diagnostics), checks, iterations + 1, route.points.size(), route.length);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL checks=%u: %s\n", checks, error.what());
    return 1;
}
