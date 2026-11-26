#include "owned_resource_runtime.h"
#include "renderer.h"
#include "scene_core_types.h"
#include <cmath>
#include <cstdio>

namespace owned {

void scene_core::MapPreviewPrecache::run(Renderer &renderer) {
  renderer.precache_world_map_preview(generation, std::move(sections),
                                      std::move(failure));
}
void scene_core::MapPreviewActivate::run(Renderer &renderer) {
  renderer.activate_world_map_preview(generation, std::move(sections),
                                      std::move(failure));
}
void scene_core::MapPreviewRetire::run(Renderer &renderer) {
  renderer.retire_world_map_preview(release);
}
void scene_core::MapPreviewDraw::run(Renderer &renderer) {
  renderer.draw_world_map_preview(generation, section, matrix, tint,
                                  border_tint, discovered, state);
}
void scene_core::MapMarkerBegin::run(Renderer &renderer) {
  renderer.begin_world_map_marker();
}
void scene_core::MapMarkerEnd::run(Renderer &renderer) {
  renderer.end_world_map_marker();
}
void scene_core::MapMarkersDraw::run(Renderer &renderer) {
  renderer.draw_world_map_markers();
}

void Renderer::begin_world_map_marker() {
  if (scene_queue()) {
    scene_post_command(scene_core::MapMarkerBegin{});
    return;
  }
  map_preview_marker_collecting_ = true;
}
void Renderer::end_world_map_marker() {
  if (scene_queue()) {
    scene_post_command(scene_core::MapMarkerEnd{});
    return;
  }
  map_preview_marker_collecting_ = false;
}
void Renderer::discard_world_map_markers() noexcept {
  for (const auto &marker : map_preview_markers_)
    if (marker.selected.record != UINT_MAX)
      release_native_queued_draw(marker.selected.record);
  map_preview_markers_.clear();
  map_preview_marker_collecting_ = false;
}
void Renderer::draw_world_map_markers() {
  if (scene_queue()) {
    scene_post_command(scene_core::MapMarkersDraw{});
    return;
  }
  map_preview_marker_collecting_ = false;
  struct Release {
    Renderer &renderer;
    ~Release() { renderer.discard_world_map_markers(); }
  } release{*this};
  if (map_preview_markers_.empty())
    return;
  bool ready = SUCCEEDED(flush_ui()) && SUCCEEDED(composite_world()) &&
               SUCCEEDED(flush_native());
  if (ready) {
    D3DVIEWPORT7 viewport{};
    viewport.dwWidth = target_width();
    viewport.dwHeight = target_height();
    viewport.dvMaxZ = 1;
    RECT bounds{};
    bool visible = false;
    for (const auto &marker : map_preview_markers_) {
      const auto &draw =
          native_instance_records_[marker.selected.record].command;
      std::array<float, 16> clip;
      std::copy_n(&draw.instance.object_to_clip._11, 16, clip.begin());
      RECT part{};
      // The queue's owned bounds already include every morph displacement.
      const auto hr = calculate_native_aabb_screen_extents(
          draw.bounds_min, draw.bounds_max, clip, viewport, part);
      if (FAILED(hr)) {
        ready = false;
        break;
      }
      if (hr == S_FALSE)
        continue;
      if (!visible)
        bounds = part;
      else {
        bounds.left = (std::min)(bounds.left, part.left);
        bounds.top = (std::min)(bounds.top, part.top);
        bounds.right = (std::max)(bounds.right, part.right);
        bounds.bottom = (std::max)(bounds.bottom, part.bottom);
      }
      visible = true;
    }
    if (ready && !visible)
      return;
    if (ready) {
      // Projected bounds are output pixels. The existing portrait helper
      // takes native-canvas coordinates and scales them exactly once.
      const float x = float(canvas_width()) / viewport.dwWidth;
      const float y = float(canvas_height()) / viewport.dwHeight;
      clear_interface_depth(bounds.left * x, bounds.top * y, bounds.right * x,
                            bounds.bottom * y);
    }
  }
  try {
    for (auto &marker : map_preview_markers_) {
      if (!ready)
        break;
      PortraitLightingScope lighting(
          *this, marker.lighting_valid ? &marker.lighting : nullptr);
      if (!lighting.ready()) {
        ready = false;
        break;
      }
      // Placed arrows have their own complete clip matrix (worldGeometry=0).
      // Morph bytes and prepared light, unlike that matrix, would otherwise
      // be lost when the intervening section flushes discard the queue.
      native_pending_upload_ = std::move(marker.morphs);
      auto &record = native_instance_records_[marker.selected.record];
      record.command.instance.morph_offset = 0;
      record.uploaded = false;
      if (!record.scheduled) {
        native_instance_dirty_.push_back(marker.selected.record);
        record.scheduled = true;
      }
      native_pending_draws_.push_back(
          marker.selected); // transfer its existing enqueue reference
      marker.selected.record = UINT_MAX;
      ready = lighting.finish();
      // Every marker's morph bytes start at offset 0 of the pending upload:
      // flush this one before the next marker replaces them. A lit scope
      // flushed already; one without prepared light does not.
      if (ready && !marker.lighting_valid)
        ready = SUCCEEDED(flush_native());
    }
  } catch (const std::bad_alloc &) {
    ready = false;
  }
  if (!ready) {
    discard_native();
    debug_overlay_.set_world_map_notice("World map arrow overlay failed");
    bridge_report_event("world_map_preview_marker_failed",
                        "Arrow overlay submission failed");
  }
}

Renderer::NativeStaticSet *
Renderer::native_static_set_for_layout(unsigned slot) noexcept {
  if (slot == 2)
    return &native_static_set_;
  if (slot == 3)
    return &native_terrain_set_;
  if (slot == 4)
    return &neighbours_.opaque[0];
  if (slot >= kNeighbourChunkLayout && slot < kNeighbourChunkLayout + kNeighbourChunks - 1)
    return &neighbours_.opaque[1 + slot - kNeighbourChunkLayout];
  if (slot >= kMapOpaqueLayout && slot < kMapOrderedLayout)
    return &map_preview_opaque_[slot - kMapOpaqueLayout];
  if (slot >= kMapOrderedLayout && slot < kMapOrderedLayout + kMapPreviewSections)
    return &map_preview_ordered_[slot - kMapOrderedLayout];
  return nullptr;
}

void Renderer::clear_world_map_preview_gpu() noexcept {
  for (unsigned i = 0; i < kMapPreviewSections; ++i) {
    for (auto *set : {&map_preview_opaque_[i], &map_preview_ordered_[i]}) {
      for (const auto &[key, draws] : set->objects)
        for (const auto &draw : draws)
          release_native_queued_draw(draw.record, false);
      *set = {};
    }
    map_preview_opaque_[i].layout_slot = kMapOpaqueLayout + i;
    map_preview_ordered_[i].layout_slot = kMapOrderedLayout + i;
    native_submission_layouts_[kMapOpaqueLayout + i] = {};
    native_submission_layouts_[kMapOrderedLayout + i] = {};
  }
  map_preview_resource_revision_.fill(0);
  map_preview_discovered_.fill(false);
  map_preview_native_generation_ = 0;
}

bool Renderer::register_world_map_preview_gpu(
    std::uint32_t section, const std::array<float, 16> &matrix, bool discovered,
    const FixedState &state, std::string &failure) {
  const auto revision = world_map_preview_.section_revision(section);
  if (section >= kMapPreviewSections || !revision) {
    failure = "World map preview: prepared section is unavailable";
    return false;
  }
  if (map_preview_native_generation_ != native_static_generation_ ||
      (map_preview_resource_revision_[section] &&
       (map_preview_resource_revision_[section] != revision ||
        map_preview_discovered_[section] != discovered)))
    clear_world_map_preview_gpu();
  map_preview_native_generation_ = native_static_generation_;
  if (map_preview_resource_revision_[section] == revision)
    return true;

  // CPU resources were prepared during loading. Register them once at the
  // first real interface draw, with its authoritative fixed state and viewport.
  // Reopening the same cached allod keeps these owned groups and GPU layouts.
  struct Sink {
    Renderer *renderer;
    const FixedState *state;
    bool discovered;
  } sink{this, &state, discovered};
  for (const auto pass :
       {WorldMapPreviewPass::opaque, WorldMapPreviewPass::ordered}) {
    const auto first = native_pending_draws_.size();
    const bool submitted = world_map_preview_.submit(
        section, matrix,
        [](void *context, const NativeGeometryView &view,
           NativeGeometryResult &result) {
          auto &sink = *static_cast<Sink *>(context);
          // The placed boundary already owns the cover's complete figure
          // state, including alpha, depth and winding. Only real terrain and
          // scenery replace it with their independently owned materials.
          const auto state = sink.discovered
                                 ? native_owned_submission_state(
                                       *sink.state, view.owned_fixed_state)
                                 : *sink.state;
          return SUCCEEDED(
              sink.renderer->queue_native_owned(view, state, &result));
        },
        &sink, failure, pass, discovered);
    if (!submitted) {
      for (std::size_t i = first; i < native_pending_draws_.size(); ++i)
        release_native_queued_draw(native_pending_draws_[i].record);
      native_pending_draws_.resize(first);
      clear_world_map_preview_gpu();
      return false;
    }
    auto &set = pass == WorldMapPreviewPass::opaque
                    ? map_preview_opaque_[section]
                    : map_preview_ordered_[section];
    // A section may have no ordered material layers.
    if (native_pending_draws_.size() == first)
      continue;
    bool registered = false;
    (void)adopt_native_static_draws(set, revision, first, registered,
                                    pass == WorldMapPreviewPass::ordered);
    if (!registered) {
      failure = "World map preview: retained group registration failed";
      clear_world_map_preview_gpu();
      return false;
    }
  }
  map_preview_resource_revision_[section] = revision;
  map_preview_discovered_[section] = discovered;
  return true;
}

#ifdef EID3D11_EMBEDDED
void Renderer::sync_embedded_world_map_preview_option() {
  const bool enabled = world_map_previews_enabled();
  if (embedded_world_map_preview_option_ == enabled) return;
  retire_world_map_preview(!enabled);
  embedded_world_map_preview_option_ = enabled;
}
#endif

void Renderer::precache_world_map_preview(
    std::uint64_t generation, std::vector<WorldMapPreviewSection> sections,
    std::string failure) {
  if (scene_queue()) {
    scene_core::MapPreviewPrecache command;
    command.generation = generation;
    command.sections = std::move(sections);
    command.failure = std::move(failure);
    scene_post_command(std::move(command));
    return;
  }
  // Release selected-record references before replacing their CPU resource
  // owners.
  clear_world_map_preview_gpu();
  world_map_preview_generation_ = 0;
  world_map_preview_reasons_.clear();
  ResourceStoreConfig mounts;
  std::uint64_t sources = 0;
  if (failure.empty() && !owned_resource_runtime_mounts(mounts, &sources))
    failure = "World map preview: loading resource mounts are unavailable";
#ifdef EID3D11_EMBEDDED
  if (embedded_world_map_preview_sources_ != sources)
    world_map_preview_.release();
  embedded_world_map_preview_sources_ = sources;
#endif
  // Starts the preparation on its own thread (#309); the cache_ready /
  // cache_failed event follows when it lands.
  if (!world_map_preview_.precache_loading(
          generation, WorldMapPreviewSpace::section_fitted, std::move(mounts),
          std::move(sections), failure))
    bridge_report_event("world_map_preview_cache_failed", failure.c_str());
}

void Renderer::activate_world_map_preview(
    std::uint64_t generation, std::vector<WorldMapPreviewSection> sections,
    std::string failure) {
  if (scene_queue()) {
    scene_core::MapPreviewActivate command;
    command.generation = generation;
    command.sections = std::move(sections);
    command.failure = std::move(failure);
    scene_post_command(std::move(command));
    return;
  }
#ifdef EID3D11_EMBEDDED
  if (generation && failure.empty() && !sections.empty() &&
      !world_map_preview_.contains(WorldMapPreviewSpace::section_fitted,sections))
    precache_world_map_preview(generation,sections,{});
#endif
  world_map_preview_.deactivate();
  world_map_preview_generation_ = generation;
  world_map_preview_reasons_.assign(sections.size(), {});
  world_map_preview_.set_enabled(true);
  (void)world_map_preview_.activate(generation,
                                    WorldMapPreviewSpace::section_fitted,
                                    std::move(sections), failure);
  // Generation 0 is the option refused at enabling (hooks unavailable): the
  // option is OFF again at once and no preview state follows to clear the
  // notice, so it shows for 10 s instead of drawing the overlay for good.
  debug_overlay_.set_world_map_notice(
      failure, generation || failure.empty() ? 0 : GetTickCount64() + 10000);
  if (!failure.empty())
    bridge_report_event("world_map_preview_failed", failure.c_str());
}

void Renderer::retire_world_map_preview(bool release) {
  if (scene_queue()) {
    scene_core::MapPreviewRetire command;
    command.release = release;
    scene_post_command(std::move(command));
    return;
  }
  discard_world_map_markers();
  if (release) {
    // Selected-record references go before their CPU resource owners.
    clear_world_map_preview_gpu();
    world_map_preview_.release();
  }
  world_map_preview_.set_enabled(false);
  world_map_preview_generation_ = 0;
  world_map_preview_reasons_.clear();
  debug_overlay_.set_world_map_notice({});
}

void Renderer::submit_world_map_preview(std::uint64_t generation,
                                        std::uint32_t section,
                                        const std::array<float, 16> &matrix,
                                        const std::array<float, 3> &tint,
                                        const std::array<float, 3> &border_tint,
                                        bool discovered,
                                        const FixedState &state) {
  auto complete = matrix;
  // Prepared world records deliberately skip queue_native_figure's legacy
  // HUD composition. Complete the same interface transform here, once, while
  // the authoritative native HUD placement is current on the game thread.
  if (widescreen::interface_owned)
    widescreen::compose_hud_clip(complete.data(), widescreen::hud_blit,
                                 float(canvas_width()), float(canvas_height()));
  if (scene_queue()) {
    scene_core::MapPreviewDraw command;
    command.generation = generation;
    command.section = section;
    command.matrix = complete;
    command.tint = tint;
    command.border_tint = border_tint;
    command.discovered = discovered;
    command.state = state;
    scene_post_command(std::move(command));
    return;
  }
  draw_world_map_preview(generation, section, complete, tint, border_tint,
                         discovered, state);
}

void Renderer::draw_world_map_preview(std::uint64_t generation,
                                      std::uint32_t section,
                                      const std::array<float, 16> &matrix,
                                      const std::array<float, 3> &tint,
                                      const std::array<float, 3> &border_tint,
                                      bool discovered,
                                      const FixedState &state) {
  if (generation != world_map_preview_generation_ ||
      section >= world_map_preview_reasons_.size())
    return;
  world_map_preview_.poll();
  const auto status = world_map_preview_.status(section);
  std::string failure;
  if (status.state == WorldMapPreviewState::loading)
    failure = "Preparing world map previews...";
  else if (status.state != WorldMapPreviewState::ready)
    failure = status.reason.empty()
                  ? "World map preview unavailable; section omitted"
                  : status.reason;
  else {
    // Interface geometry must follow queued background quads. The existing
    // portrait scope composites the world once (subsequent calls are no-ops),
    // applies exact prepared UI light and flushes before restoring it.
    auto light = WorldMapPreview::preview_lighting();
    for (unsigned a = 0; a < 3; ++a) {
      light.ambient[a] *= tint[a];
      light.sunlight_diffuse[a] *= tint[a];
    }
    if (FAILED(flush_ui()))
      failure = "World map preview: interface flush failed";
    else {
      PortraitLightingScope lighting(*this, &light);
      if (!lighting.ready())
        failure = "World map preview: light setup failed";
      else {
        if (!set_native_world_clip(matrix))
          failure = "World map preview: clip setup failed";
        else if (register_world_map_preview_gpu(section, matrix, discovered,
                                                state, failure)) {
          // The portrait scope has composited the world. Cached layouts
          // therefore keep interface targets and shading even with an empty
          // ordering queue.
          if (FAILED(flush_native_static_set(map_preview_opaque_[section],
                                             state)) ||
              FAILED(flush_native_static_set(map_preview_ordered_[section],
                                             state)))
            failure = "World map preview: retained group draw failed";
        }
        if (!lighting.finish() && failure.empty())
          failure = "World map preview: draw flush failed";
      }
    }
  }
  if (failure.empty() && discovered && !hit_test_active_) {
    struct BorderSink {
      Renderer *renderer;
      const FixedState *state;
      const std::array<float, 3> *tint;
    } border{this, &state, &border_tint};
    (void)world_map_preview_.submit_borders(
        section, matrix,
        [](void *context, const GameRibbonView &source) {
          auto &sink = *static_cast<BorderSink *>(context);
          FixedState state = *sink.state;
          state.render[D3DRENDERSTATE_ZENABLE] = D3DZB_TRUE;
          state.render[D3DRENDERSTATE_ZFUNC] = D3DCMP_LESSEQUAL;
          state.render[D3DRENDERSTATE_ZWRITEENABLE] = FALSE;
          state.render[D3DRENDERSTATE_STENCILENABLE] = FALSE;
          state.render[D3DRENDERSTATE_CULLMODE] = D3DCULL_NONE;
          state.render[D3DRENDERSTATE_FILLMODE] = D3DFILL_SOLID;
          state.render[D3DRENDERSTATE_FOGENABLE] = FALSE;
          state.render[D3DRENDERSTATE_ALPHABLENDENABLE] = TRUE;
          state.render[D3DRENDERSTATE_SRCBLEND] = D3DBLEND_ONE;
          state.render[D3DRENDERSTATE_DESTBLEND] = D3DBLEND_INVSRCALPHA;
          state.render[D3DRENDERSTATE_ALPHATESTENABLE] = FALSE;
          state.render[D3DRENDERSTATE_SPECULARENABLE] = FALSE;
          // The bridge divides out the exact native neutral brightness.
          // SetZonesColors changes RGB only for the current hovered section.
          // Presentation policy is gold/green consistently across all allods.
          const bool hovered =
              std::any_of(sink.tint->begin(), sink.tint->end(),
                          [](float channel) { return channel != 1.f; });
          state.render[D3DRENDERSTATE_TEXTUREFACTOR] =
              hovered ? 0xff40e050u : 0xffffd050u;
          state.stage[0][D3DTSS_COLOROP] = D3DTOP_SELECTARG1;
          state.stage[0][D3DTSS_COLORARG1] = D3DTA_TFACTOR;
          state.stage[0][D3DTSS_ALPHAOP] = D3DTOP_SELECTARG1;
          state.stage[0][D3DTSS_ALPHAARG1] = D3DTA_TFACTOR;
          auto &renderer = *sink.renderer;
          // The complete matrix is in output clip space, as for the prepared
          // section. Use the output pixel grid for both ribbon projection and
          // extrusion, independently of the legacy viewport's canvas units.
          state.viewport.dwX = state.viewport.dwY = 0;
          state.viewport.dwWidth = (std::max)(UINT(1), renderer.output_width_);
          state.viewport.dwHeight =
              (std::max)(UINT(1), renderer.output_height_);
          state.viewport.dvMinZ = 0;
          state.viewport.dvMaxZ = 1;
          const float clip_x =
              widescreen::interface_owned
                  ? widescreen::make_layout(float(renderer.output_width_),
                                            float(renderer.output_height_))
                        .clip_x
                  : 1.f;
          if (FAILED(renderer.ensure_game_particle_pipeline()))
            return false;
          auto strip = source.strip;
          strip.v0 = -1.f;
          strip.v1 = 1.f;
          constexpr float stroke_half_width = 3.5f, aa_fringe = 1.f;
          renderer.game_effect_elements_.resize(source.count);
          for (std::uint32_t j = 0; j < source.count; ++j) {
            const auto &p = source.points[j];
            const auto &m = source.object_to_clip;
            const float w = m[12] * p.x + m[13] * p.y + m[14] * p.z + m[15];
            // gameRibbonVS divides its offset by W; cancelling that factor
            // preserves a 3.5-pixel solid half-width plus one outer pixel for
            // analytic coverage, through perspective and HUD composition.
            game_ribbon_point_gpu(source.points, source.count, j, strip,
                                  (stroke_half_width + aa_fringe) *
                                      std::abs(w) / clip_x,
                                  renderer.game_effect_elements_[j]);
          }
          GameEffectDraw draw;
          draw.object_to_clip = source.object_to_clip.data();
          draw.clip_half_width = float(state.viewport.dwWidth) * .5f;
          draw.clip_half_height = float(state.viewport.dwHeight) * .5f;
          draw.element_count = draw.ribbon_points = source.count;
          draw.vertex_count = (source.count - 1) * 6;
          draw.vertex_shader = renderer.game_ribbon_vs_.Get();
          draw.vertex_program = WorldSupportShader::game_ribbon_vs;
          draw.fog = draw.reflection_depth = false;
          draw.ribbon_antialias = true;
          draw.profile_pass = "world_map_borders";
          return SUCCEEDED(renderer.draw_game_effect(draw, state, nullptr));
        },
        &border, failure);
  }
  auto &previous = world_map_preview_reasons_[section];
  if (previous != failure) {
    previous = failure;
    if (!failure.empty())
      bridge_report_event("world_map_preview_section", failure.c_str());
    const auto pending = std::find_if(
        world_map_preview_reasons_.begin(), world_map_preview_reasons_.end(),
        [](const auto &reason) { return !reason.empty(); });
    debug_overlay_.set_world_map_notice(
        pending == world_map_preview_reasons_.end() ? std::string{} : *pending);
  }
}


// #268: what the world map shows, once per opening of the map screen (its
// first zone hit-test frame without the world camera): the option, and
// per section whether its preview was baked during the load.
void Renderer::report_world_map_previews() noexcept {
  try {
    std::string detail;
    if (!configured_boolean(debug_overlay_.config(), "world_map_previews", false))
      detail = "option off: the game's own map, no previews";
    else if (!world_map_preview_generation_)
      detail = "option on, but no previews were prepared for this load (the option was off at the load, or its load hooks did not run)";
    else {
      unsigned ready = 0, loading = 0, failed = 0, off = 0;
      std::string missing;
      for (std::size_t i = 0; i < world_map_preview_reasons_.size(); ++i) {
        const auto status = world_map_preview_.status(i);
        const char *state = "off";
        switch (status.state) {
        case WorldMapPreviewState::ready: ++ready; continue;
        case WorldMapPreviewState::loading: ++loading; state = "loading"; break;
        case WorldMapPreviewState::failed: ++failed; state = "failed"; break;
        case WorldMapPreviewState::disabled: ++off; break;
        }
        missing += " section " + std::to_string(i) + " " + state;
        if (!status.reason.empty()) missing += " (" + status.reason + ")";
        missing += ";";
      }
      char head[192];
      std::snprintf(head, sizeof(head),
                    "option on, load %llu: %u of %zu sections baked, %u loading, %u failed, %u off",
                    static_cast<unsigned long long>(world_map_preview_generation_), ready,
                    world_map_preview_reasons_.size(), loading, failed, off);
      detail = head;
      if (!missing.empty()) detail += "; missing:" + missing;
    }
    bridge_report_event("world_map_previews_at_open", detail.c_str());
  } catch (...) {
  }
}
} // namespace owned
