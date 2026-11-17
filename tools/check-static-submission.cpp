// The included branch is extracted from E1VisualFrame.cpp by the check script.
// Fake executor records which store is changed; no GPU or game process is needed.
#include <cstdio>
#include <cstdint>
#define SUCCEEDED(x) ((x) >= 0)
#define FAILED(x) ((x) < 0)
using HRESULT = int;
constexpr HRESULT S_OK = 0;
struct FixedState {};
struct NativeGeometryResult { int failure_hresult = 0; const char* failure_operation = nullptr; };
struct Terrain { FixedState owned_fixed_state; };
// Only enabled is needed on a terrain state in the production branch.
struct OwnedState { bool enabled = false; };
struct TerrainView { OwnedState owned_fixed_state; };
FixedState native_owned_submission_state(FixedState state, OwnedState) { return state; }
struct NativeStaticSubmission {
    enum class Operation { generation, shadow_static, shadow_dynamic, terrain_frame, remove, flush, set };
    Operation operation = Operation::set;
    mutable unsigned generation = 0;
    mutable bool registered = false;
    bool terrain = false, caster = false, object_space = false;
    int key = 0, full_sun = 0, layer_shifts = 0;
    const int *terrain_state = nullptr, *world_to_clip = nullptr, *records = nullptr;
    const TerrainView* terrain_view = nullptr;
};
struct NativeGeometryView { const NativeStaticSubmission* static_submission; };
struct Renderer {
    enum Route { none, posed, object, shadow, ground } route = none;
    bool accept = true;
    HRESULT status = S_OK;
    unsigned native_static_generation() { return 7; }
    unsigned set_native_shadow_phase(unsigned value) { return value; }
    template<class... T> int set_native_terrain_frame(T...) { return status; }
    template<class... T> void remove_native_static_object(T...) {}
    template<class... T> int flush_native_static_terrain(T...) { return status; }
    template<class... T> int flush_native_static(T...) { return status; }
    int set_native_static_terrain(int, const TerrainView&, FixedState, void*, bool& ok, NativeGeometryResult*) {
        route = ground; ok = accept; return status;
    }
    int set_native_static_object(int, int, FixedState, bool& ok, NativeGeometryResult*) {
        route = posed; ok = accept; return status;
    }
    int set_native_static_object_space(const NativeStaticSubmission&, FixedState, bool& ok, NativeGeometryResult*) {
        route = object; ok = accept; return status;
    }
    int set_native_active_caster(const NativeStaticSubmission& request, FixedState) {
        route = shadow; request.registered = accept; return status;
    }
};
struct Sink { FixedState state; bool staticsFlushed = false; };
bool dispatch(Renderer& r, Sink& sink, const NativeGeometryView& view, NativeGeometryResult& result) {
#include "static-submission-branch.inc"
    return false;
}
int main() {
    int failures = 0, checks = 0, records = 1;
    TerrainView terrain;
    // Test route, registration acknowledgement and HRESULT propagation separately.
    // Caster requests also have object_space=true; caster routing must win.
    for (int kind = 0; kind != 4; ++kind) for (int outcome = 0; outcome != 3; ++outcome) {
        Renderer renderer; Sink sink; NativeGeometryResult result;
        renderer.accept = outcome == 0; renderer.status = outcome == 2 ? -1 : 0;
        NativeStaticSubmission request;
        request.records = &records; request.terrain_view = &terrain;
        request.caster = kind == 2; request.object_space = kind == 1 || kind == 2;
        request.terrain = kind == 3;
        const bool ok = dispatch(renderer, sink, {&request}, result);
        const Renderer::Route expected[] = {Renderer::posed, Renderer::object, Renderer::shadow, Renderer::ground};
        ++checks;
        if (renderer.route != expected[kind] || request.registered != renderer.accept || ok != (renderer.status >= 0)) {
            std::printf("FAIL kind=%d outcome=%d route=%d registered=%d ok=%d\n", kind, outcome, renderer.route, request.registered, ok);
            ++failures;
        }
    }
    std::printf("Static submission: %d cases, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
