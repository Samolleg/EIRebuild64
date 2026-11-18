#include "StdAfx.h"
#include "GlobalMapPreview.h"
#include "E1RendererFacade.h"
#include "../I3DScreenGlobalMap.h"
#include "../I3DFigure.h"
#include "../I3DMain.h"
#include "../GlobalMap.h"
#include "../ScenarioTracker.h"
#include "../WorldInterface.h"
#include "../Renderer.h"
#include "../dx11/RendererDX11.h"
#include "../GameJournal.h"
#include <chrono>
#include <cmath>
#include <stdexcept>

// Access the exact live screen arrays; no engine addresses or retained carrier keys.
struct GlobalMapPreviewAccess {
    static int Count(const CI3DScreenGlobalMap& s) { return s.nZones; }
    static CI3DFigure* Carrier(CI3DScreenGlobalMap& s, int i) {
        return i >= 0 && i < s.figures.GetSize() ? s.figures[i] : nullptr;
    }
    static int State(const CI3DScreenGlobalMap& s, int i) { return s.zones[i].state; }
    static void InvalidateMask(CI3DScreenGlobalMap& s) { s.bGenerateHitTestDDS = TRUE; }
    static std::vector<owned::WorldMapPreviewSection> Sections(CI3DScreenGlobalMap& s);
};
namespace OwnedRender::GlobalMapPreview {
namespace {
CI3DScreenGlobalMap* active = nullptr;
thread_local CI3DScreenGlobalMap* drawing = nullptr;
thread_local bool drawingEnabled = false;
thread_local unsigned hitDepth = 0;
std::uint64_t sequence = 0, binding = 0;
bool published = false, listValid = false, optionActive = false, maskChanged = false;
std::string lastFailure;
std::uint64_t NextGeneration() noexcept { if (!++sequence) ++sequence; return sequence; }
void Failure(const char* reason) noexcept {
    if (lastFailure == reason) return;
    GameJournal::Event("RENDER", "world_map_preview_host_failed", "reason=%s", reason);
    try { lastFailure = reason; } catch (...) {}
}
void Restore(CDX11* dx) noexcept {
    try { dx->RestoreHostBindings(); }
    catch (...) { Failure("World map preview: host bindings could not be restored"); }
}
owned::WorldMapPreviewSection Section(const SScenarioZone& zone) {
    if (zone.szName.IsEmpty() || zone.szAllodName.IsEmpty() || zone.szTerrainName.IsEmpty() ||
        zone.szMobName.IsEmpty() || zone.szFigureName.IsEmpty())
        throw std::runtime_error("World map preview: incomplete scenario resource descriptor");
    owned::WorldMapPreviewSection result;
    result.zone = LPCSTR(zone.szName);
    result.terrain_resource = std::string(LPCSTR(zone.szTerrainName)) + ".mpr";
    result.map_resource = std::string(LPCSTR(zone.szMobName)) + ".mob";
    result.figure_resource = std::string(LPCSTR(zone.szFigureName)) + ".fig";
    result.unknown_texture_resource = std::string(LPCSTR(zone.szAllodName)) + "MapG.mmp";
    result.figure_shape = {0, 0, 0}; // CreateI3DFigure's three shape arguments.
    return result;
}
bool Bind(CI3DScreenGlobalMap* screen, owned::Renderer& renderer) {
    const bool enabled = renderer.world_map_previews_enabled();
    if (enabled != optionActive) {
        optionActive = enabled; published = false;
        if (!hitDepth) { maskChanged = true; GlobalMapPreviewAccess::InvalidateMask(*screen); }
    }
    if (!enabled) return true;
    if (published && binding == renderer.world_map_preview_binding_generation()) return listValid;
    if (!hitDepth) { maskChanged = true; GlobalMapPreviewAccess::InvalidateMask(*screen); }
    published = true; listValid = false; binding = NextGeneration();
    std::vector<owned::WorldMapPreviewSection> sections;
    std::string failure;
    try { sections = GlobalMapPreviewAccess::Sections(*screen); listValid = true; }
    catch (const std::exception& error) { failure = error.what(); }
    renderer.activate_world_map_preview(binding, std::move(sections), failure);
    if (!failure.empty()) Failure(failure.c_str());
    return listValid;
}
}
void Precache(CRenderer* renderer) noexcept {
    if (!renderer || !renderer->GetD3D()) { Failure("World map preview: loading renderer unavailable"); return; }
    const auto* zone = theMap.GetZoneByName(IWorldClient.GetCurrentZoneName());
    if (!zone || zone->szAllodName.IsEmpty()) return;
    const auto* allod = theMap.GetAllodByName(zone->szAllodName);
    if (!allod) return;
    auto* dx = renderer->GetD3D();
    const auto lease = dx->GetOwnedRenderer().LockFrame();
    published = false; binding = 0;
    bool enabled = false;
    owned::WorldMapPreviewCacheStats stats;
    double elapsed = 0;
    const auto generation = NextGeneration();
    const auto hr = dx->GetRendererFacade().Visit([&](owned::Renderer& backend) {
        backend.sync_embedded_world_map_preview_option();
        enabled = backend.world_map_previews_enabled();
        if (!enabled) return S_OK;
        const auto start = std::chrono::steady_clock::now();
        std::vector<owned::WorldMapPreviewSection> sections;
        std::string failure;
        try {
            // Match the native global-map screen: only this allod can be opened here.
            for (int i = 0; i < allod->zones.GetSize(); ++i) {
                const auto& zone = *theMap.GetZone(allod->zones[i]);
                if (zone.eType == GM_REALZONE && !zone.szFigureName.IsEmpty())
                    sections.push_back(Section(zone));
            }
            if (sections.empty()) throw std::runtime_error("World map preview: empty scenario catalog");
        } catch (const std::exception& error) { sections.clear(); failure = error.what(); }
        backend.precache_world_map_preview(generation, std::move(sections), failure);
        stats = backend.world_map_preview_cache_stats();
        elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        return S_OK;
    });
    GameJournal::Event("RENDER", "world_map_preview_precache",
        "generation=%llu allod=%s enabled=%u sections=%u ready=%u failed=%u reused=%u retained_mb=%.3f source_peak_mb=%.3f precache_ms=%.3f hr=%08lx",
        static_cast<unsigned long long>(generation), zone ? LPCSTR(zone->szAllodName) : "",
        enabled ? 1u : 0u, stats.sections, stats.ready, stats.failed, stats.reused ? 1u : 0u,
        double(stats.cpu_bytes) / (1024.0 * 1024.0), double(stats.source_peak_bytes) / (1024.0 * 1024.0), elapsed,
        static_cast<unsigned long>(hr));
    if (FAILED(hr)) Failure("World map preview: loading command failed");
}
void Activate(CI3DScreenGlobalMap* screen, CRenderer* renderer) noexcept {
    active = screen; published = false; listValid = false; optionActive = false; maskChanged = false; lastFailure.clear();
    (void)Refresh(screen, renderer);
}
bool Refresh(CI3DScreenGlobalMap* screen, CRenderer* renderer) noexcept {
    if (screen != active) return false;
    if (!renderer || !renderer->GetD3D()) { Failure("World map preview: activation renderer unavailable"); return false; }
    const auto lease = renderer->GetD3D()->GetOwnedRenderer().LockFrame();
    const auto hr = renderer->GetD3D()->GetRendererFacade().Visit([&](owned::Renderer& backend) {
        backend.sync_embedded_world_map_preview_option();
        Bind(screen, backend); return S_OK;
    });
    if (FAILED(hr)) Failure("World map preview: binding refresh failed");
    return maskChanged;
}
void Deactivate(CI3DScreenGlobalMap* screen, CRenderer* renderer) noexcept {
    if (active != screen) return;
    active = nullptr; published = false; listValid = false; maskChanged = false; binding = 0;
    if (!renderer || !renderer->GetD3D()) return;
    const auto lease = renderer->GetD3D()->GetOwnedRenderer().LockFrame();
    const auto hr = renderer->GetD3D()->GetRendererFacade().Visit([](owned::Renderer& backend) {
        backend.retire_world_map_preview(); return S_OK;
    });
    if (FAILED(hr)) Failure("World map preview: retire command failed");
}
DrawScope::DrawScope(CI3DScreenGlobalMap* screen, CRenderer* value) noexcept
    : previous(drawing), previousEnabled(drawingEnabled), renderer(value) {
    drawing = screen; drawingEnabled = false;
    if (screen != active || !renderer || !renderer->GetD3D()) { ready = false; return; }
    auto* dx = renderer->GetD3D();
    const auto hr = dx->GetRendererFacade().Visit([&](owned::Renderer& backend) {
        enabled = drawingEnabled = backend.world_map_previews_enabled();
        ready = Bind(screen, backend); return S_OK;
    });
    if (FAILED(hr)) { ready = false; Failure("World map preview: draw binding failed"); }
}
DrawScope::~DrawScope() {
    if (enabled && ready && !hitDepth && renderer && renderer->GetD3D()) {
        auto* dx = renderer->GetD3D();
        const auto hr = dx->GetRendererFacade().Visit([](owned::Renderer& backend) {
            backend.draw_world_map_markers(); return S_OK;
        });
        Restore(dx);
        if (FAILED(hr)) Failure("World map preview: marker flush failed");
    }
    drawing = previous; drawingEnabled = previousEnabled;
}
MarkerScope::MarkerScope(CI3DFigure* figure, CRenderer* value) noexcept {
    if (!drawingEnabled || !drawing || hitDepth ||
        (GlobalMapPreviewAccess::Carrier(*drawing, 0) != figure &&
         GlobalMapPreviewAccess::Carrier(*drawing, 1) != figure)) return;
    renderer = value;
    if (!renderer || !renderer->GetD3D()) { renderer = nullptr; return; }
    const auto hr = renderer->GetD3D()->GetRendererFacade().Visit([](owned::Renderer& backend) {
        backend.begin_world_map_marker(); return S_OK;
    });
    if (FAILED(hr)) { renderer = nullptr; Failure("World map preview: marker begin failed"); }
}
MarkerScope::~MarkerScope() {
    if (!renderer) return;
    const auto hr = renderer->GetD3D()->GetRendererFacade().Visit([](owned::Renderer& backend) {
        backend.end_world_map_marker(); return S_OK;
    });
    if (FAILED(hr)) Failure("World map preview: marker end failed");
}
HitTestScope::HitTestScope() noexcept { ++hitDepth; maskChanged = false; }
HitTestScope::~HitTestScope() { --hitDepth; }
bool SubmitSection(CI3DFigure* figure, CRenderer* renderer) noexcept {
    if (!drawingEnabled || !drawing) return false;
    int section = -1;
    for (int i = 0; i < GlobalMapPreviewAccess::Count(*drawing); ++i)
        if (GlobalMapPreviewAccess::Carrier(*drawing, 3 + i) == figure) { section = i; break; }
    if (section < 0) return false;
    const int state = GlobalMapPreviewAccess::State(*drawing, section);
    // The author's raw unknown cover is precisely this FIG, shape and placement.
    // Keep the temporary untextured ID state out of the cached MapG material.
    if (state == SCN_ZONE_UNKNOWN && hitDepth) return false;
    if (state < SCN_ZONE_UNKNOWN || state > SCN_ZONE_EXPLORED || !renderer || !renderer->GetD3D() ||
        !renderer->GetGPU()->IsHaveMatrix(MAT_STACK_PROJSPACE)) {
        Failure("World map preview: placed section state unavailable"); return true;
    }
    std::array<float, 16> matrix;
    const auto& projection = renderer->GetGPU()->GetMatrix(MAT_STACK_PROJSPACE);
    std::memcpy(matrix.data(), &projection, sizeof(matrix));
    if (!std::all_of(matrix.begin(), matrix.end(), [](float v) { return std::isfinite(v); })) {
        Failure("World map preview: non-finite section placement"); return true;
    }
    const auto& ambient = figure->GetAmbient();
    std::array<float, 3> tint{ambient.x, ambient.y, ambient.z}, border = tint;
    if (state == SCN_ZONE_EXPLORED) {
        for (auto& v : tint) v *= 1.3f;
        for (auto& v : border) v *= 2.f;
    }
    auto* dx = renderer->GetD3D();
    const auto fixed = dx->CaptureC56MaterialState();
    const auto hr = dx->GetRendererFacade().Visit([&](owned::Renderer& backend) {
        backend.submit_world_map_preview(binding, unsigned(section), matrix, tint, border,
            state != SCN_ZONE_UNKNOWN, fixed); return S_OK;
    });
    Restore(dx);
    if (FAILED(hr)) Failure("World map preview: placed section submission failed");
    return true; // Failed previews retain ownership: never draw the previous visual.
}
} // namespace OwnedRender::GlobalMapPreview

std::vector<owned::WorldMapPreviewSection> GlobalMapPreviewAccess::Sections(CI3DScreenGlobalMap& s) {
    if (s.nZones <= 0 || s.nZones > 15 || s.zones.GetSize() != s.nZones || s.figures.GetSize() < s.nZones + 3)
        throw std::runtime_error("World map preview: native section list unavailable");
    std::vector<owned::WorldMapPreviewSection> sections;
    sections.reserve(s.nZones);
    for (int i = 0; i < s.nZones; ++i) {
        const auto* zone = theMap.GetZoneByName(s.zones[i].szName);
        if (!zone || zone->eType != GM_REALZONE || !Carrier(s, 3 + i))
            throw std::runtime_error("World map preview: section/carrier association unavailable");
        sections.push_back(OwnedRender::GlobalMapPreview::Section(*zone));
    }
    return sections;
}
