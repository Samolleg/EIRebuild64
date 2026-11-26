#pragma once

#include "engine_bridge.h"
#include "owned_resource_store.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace owned {

// Values from the native screen's completed section list, in its order. Do
// not identify a section by its figure: different sections can share one FIG.
struct WorldMapPreviewSection {
  std::string zone;
  std::string terrain_resource; // selected MPR, including extension
  std::string map_resource;     // selected MOB, including extension
  std::string figure_resource;  // exact #figure FIG, including extension
  std::array<float, 3> figure_shape{};
  std::string
      unknown_texture_resource; // native allod MapG, including extension
};

enum class WorldMapPreviewState : std::uint8_t {
  disabled,
  loading,
  ready,
  failed
};
// map_local prepares the independent resource/geometry core without reading
// the section FIG or making any UI placement assumption.
enum class WorldMapPreviewSpace : std::uint8_t { map_local, section_fitted };
enum class WorldMapPreviewPass : std::uint8_t { all, opaque, ordered };

struct WorldMapPreviewStatus {
  WorldMapPreviewState state = WorldMapPreviewState::disabled;
  std::string reason;
  std::uint64_t triangles = 0, cpu_bytes = 0;
  std::uint64_t prop_bytes = 0; // retained prop streams/metadata/instances
  std::uint64_t source_peak_bytes = 0; // existing loader's source accounting
  std::uint32_t batches = 0, static_objects = 0, omitted_objects = 0;
  std::array<float, 3> minimum{}, maximum{}; // prepared-space geometry bounds
  double load_ms = 0, prepare_ms = 0;
};

struct WorldMapPreviewCacheStats {
  std::uint32_t sections = 0, ready = 0, failed = 0;
  std::uint64_t cpu_bytes = 0, source_peak_bytes = 0;
  bool reused = false;
};

// Renderer-owned, render-only catalog of map sections. Default OFF. A
// loading-screen command starts preparing the sections sequentially on a
// background thread (with one existing loader worker); the render host keeps
// serving commands meanwhile (#309: 25-40 s on a 4-thread CPU). Until the
// cache lands, bound sections read as loading. Sections share one per-allod (192MiB retail, 512MiB embedded) retained-data budget per native allod;
// a section admission failure preserves already valid entries.
// CPU output is retained across map close/reopen and for GPU
// reset/re-upload; decoded maps and assemblies die after their section has been
// condensed. Every completed section has a fresh owned-memory resource identity
// covering its cache generation, ordinal, MPR/MOB/model inputs and FIG/shape
// input. A prepared mesh never borrows the MPR's identity. New catalog
// descriptors or resource mounts retire the cache; screen deactivation only
// unbinds it.
//
// Approved presentation rule: uniformly cover the authored FIG's XY bounds
// with MPR metres, crop to its projected union and retain uniformly scaled
// source relief above each authored carrier triangle. Static assemblies share
// one height translation sampled at their grounded MOB placement. This is a
// display rule, never a claim that shipped map metres have an allod-space
// transform.
class WorldMapPreview {
public:
  WorldMapPreview();
  ~WorldMapPreview();
  WorldMapPreview(const WorldMapPreview &) = delete;
  WorldMapPreview &operator=(const WorldMapPreview &) = delete;

  void set_enabled(bool enabled) noexcept;
  bool enabled() const noexcept;
  // Render-host loading command: starts the background preparation (or
  // reuses an unchanged completed or running one, including sections that
  // failed to prepare). A nonempty source failure invalidates the old cache
  // visibly. Returns false only for a failure known at once.
  bool precache_loading(std::uint64_t generation, WorldMapPreviewSpace space,
                        ResourceStoreConfig mounts,
                        std::vector<WorldMapPreviewSection> sections,
                        std::string &failure) noexcept;
  // Adopts a finished background preparation (and binds an activation that
  // arrived before it); every frame the preview may draw, and in activate.
  void poll() noexcept;
  // Binds prepared descriptions without I/O; while the preparation still
  // runs, binds them when it lands (status: loading meanwhile).
  // generation is an owned UI binding generation, never an engine address.
  bool activate(std::uint64_t generation, WorldMapPreviewSpace space,
                std::vector<WorldMapPreviewSection> sections,
                std::string &failure) noexcept;
  // Keeps the immutable cache available for reopening the map.
  void deactivate() noexcept;
  // The option turned off: drops the loading cache and its CPU memory (the
  // renderer releases the GPU registrations first).
  void release() noexcept;
  WorldMapPreviewStatus status(std::size_t section) const;
  WorldMapPreviewCacheStats cache_stats() const noexcept;
  bool contains(WorldMapPreviewSpace,const std::vector<WorldMapPreviewSection>&) const;
  // Immutable prepared-memory incarnation; UI reopen generations do not
  // change this value. Zero means no prepared section is bound.
  std::uint64_t section_revision(std::size_t section) const noexcept;

  // Serial render-thread call, at the native section's colour or raw-ID draw.
  // Discovered sections use identical geometry/placement in both passes.
  // section_to_clip includes the current modern HUD composition for the FIG
  // (section_fitted), or the caller's map-local-to-clip (map_local).
  // The caller modulates preview_lighting() by the current native section's
  // ambient/highlight/exploration tint for this pass and restores its
  // previous light afterward. Each emitted view supplies explicit material
  // state and interface semantics (no world effects/shadows/simulation).
  // A false result must suppress the ordinary section visual and show/log
  // the returned reason; do not route it through the original visual.
  bool submit(std::size_t section, const std::array<float, 16> &section_to_clip,
              NativeGeometrySubmitFn sink, void *context, std::string &failure,
              WorldMapPreviewPass pass = WorldMapPreviewPass::all,
              bool discovered = true) noexcept;
  // Actual upper terrain cuts along the authored projected FIG boundary. The
  // caller supplies explicit untextured UI ribbon state; never used for ID.
  bool submit_borders(std::size_t section,
                      const std::array<float, 16> &section_to_clip,
                      GameRibbonSubmitFn sink, void *context,
                      std::string &failure) noexcept;
  static NativeWorldLighting preview_lighting() noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace owned
