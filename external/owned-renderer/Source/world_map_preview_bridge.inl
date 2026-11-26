// Included after the bridge's bounded native readers. Base game.exe only.
// These slots and fields are independently verified against the supported
// shipped image; native pointers below are synchronous borrowed values.
using MapPreviewLifecycleFn = void(__thiscall *)(void *);
using MapPreviewDrawFn = BOOL(__thiscall *)(void *, void *);
bool write_relative_call(std::uint8_t *from, const void *target);
std::array<void *, 3> g_map_preview_original{};
std::array<bool, 3> g_map_preview_slot_owned{};
bool g_map_preview_inputs_failed = false;
bool g_map_preview_hooks_ready = false;
const char *g_map_preview_hook_failure = "not_installed";
constexpr std::array<std::uint32_t, 3> kMapPreviewSlots = {0x340ba0, 0x340ba4,
                                                           0x340b8c};
constexpr std::array<std::uint32_t, 3> kMapPreviewEntries = {0x22d350, 0x22edd0,
                                                             0x22efc0};
// Supported base image: zone setup, then Step(0), for a new zone and a save.
// Hook callers only; original entries retain installed addon dispatchers.
constexpr std::array<std::uint32_t, 4> kMapPreviewLoadCalls = {
    0x1954ab, 0x1954b2, 0x197053, 0x19705a};
constexpr std::array<std::uint32_t, 4> kMapPreviewLoadEntries = {
    0x195080, 0x1da400, 0x195080, 0x1da400};
constexpr std::uint32_t kMapCatalogZonesRva = 0x39bf18;
constexpr std::uint32_t kMapCatalogZoneCountRva = 0x39bf1c;
std::array<std::array<std::uint8_t, 5>, 4> g_map_preview_load_original{};
std::array<bool, 4> g_map_preview_load_owned{};
struct MapPreviewPendingLoad {
  bool armed = false;
  std::vector<WorldMapPreviewSection> sections;
  std::string failure;
};
thread_local MapPreviewPendingLoad g_map_preview_pending_load;

bool capture_map_preview_resources(const void *zone,
                                   WorldMapPreviewSection &section) {
  if (!zone || !visual_string(zone, 0x10, section.figure_resource, true))
    throw std::runtime_error("World map preview: catalog FIG is unavailable");
  if (section.figure_resource.empty())
    return false;
  std::string allod;
  if (!visual_string(zone, 0x0c, section.terrain_resource) ||
      !visual_string(zone, 8, section.map_resource) ||
      !visual_string(zone, 4, allod))
    throw std::runtime_error(
        "World map preview: section resources are unavailable");
  section.terrain_resource += ".mpr";
  section.map_resource += ".mob";
  section.figure_resource += ".fig";
  section.unknown_texture_resource = allod + "MapG.mmp";
  section.figure_shape = {0, 0, 0};
  return true;
}

void __fastcall hook_map_preview_zone_setup(void *self, void *,
                                            const char *zone_name) {
  g_map_preview_pending_load = {};
  g_map_preview_pending_load.armed = true;
  using Setup = void(__thiscall *)(void *, const char *);
  reinterpret_cast<Setup>(g_map_preview_image + 0x195080)(self, zone_name);
}

void capture_map_preview_loading_catalog(MapPreviewPendingLoad &pending) {
  if (!pending.failure.empty())
    return;
  try {
    const std::uint8_t *zones = nullptr;
    std::int32_t zone_count = 0;
    // Prepare every UI-reachable allod while loading, using the same real-zone
    // and nonempty-FIG admission as native OnActivate. The displayed map need
    // not belong to the Setup zone. UI activation still binds its own ordinals.
    if (!visual_read(g_map_preview_image, kMapCatalogZonesRva, zones) ||
        !zones ||
        !visual_read(g_map_preview_image, kMapCatalogZoneCountRva,
                     zone_count) ||
        zone_count <= 0 || zone_count > 65535)
      throw std::runtime_error(
          "World map preview: loading catalog list is unavailable");
    for (std::int32_t i = 0; i < zone_count; ++i) {
      std::int32_t type = -1;
      const auto *zone = zones + std::size_t(i) * 0x5c;
      if (!visual_read(zone, 0x28, type))
        throw std::runtime_error(
            "World map preview: catalog zone type is unavailable");
      if (type != 0)
        continue; // Native GM_REALZONE predicate.
      WorldMapPreviewSection section;
      if (!visual_string(zone, 0, section.zone))
        throw std::runtime_error(
            "World map preview: catalog zone name is unavailable");
      if (capture_map_preview_resources(zone, section))
        pending.sections.push_back(std::move(section));
    }
    if (pending.sections.empty())
      throw std::runtime_error(
          "World map preview: invalid loading section count");
  } catch (const std::exception &error) {
    pending.sections.clear();
    pending.failure = error.what();
  }
}

void __fastcall hook_map_preview_first_load_frame(int wanted, void *) {
  using Step = void(__fastcall *)(int);
  reinterpret_cast<Step>(g_map_preview_image + 0x1da400)(wanted);
  auto pending = std::move(g_map_preview_pending_load);
  g_map_preview_pending_load = {};
  if (!pending.armed || !g_map_preview_enabled || !g_map_preview_renderer)
    return;
  // The catalog is initialized before the main-menu NewZone/Setup pair.
  // Its null zone name does not constrain this global, loading-owned cache.
  // The original first picture above also registers the renderer and mounts.
  capture_map_preview_loading_catalog(pending);
  // Loading changes the owned cache binding even if the native UI object is
  // reused. Its next ordinary draw must publish the current admitted list.
  g_map_preview_published = false;
  g_map_preview_sections = 0;
  g_map_preview_inputs_failed = false;
  // The first loading picture precedes this host command. It starts the
  // cache's preparation on a background thread (#309); a map opened before
  // it lands shows its sections as loading.
  if (++g_map_preview_generation == 0)
    ++g_map_preview_generation;
  try {
    g_map_preview_renderer->precache_world_map_preview(
        g_map_preview_generation, std::move(pending.sections),
        std::move(pending.failure));
  } catch (...) {
    bridge_report_event("world_map_preview_cache_failed",
                        "loading cache command could not be queued");
  }
}

std::array<void *, 4> map_preview_load_callbacks() noexcept {
  return {reinterpret_cast<void *>(&hook_map_preview_zone_setup),
          reinterpret_cast<void *>(&hook_map_preview_first_load_frame),
          reinterpret_cast<void *>(&hook_map_preview_zone_setup),
          reinterpret_cast<void *>(&hook_map_preview_first_load_frame)};
}

void retire_map_preview() {
  if (g_map_preview_renderer && g_map_preview_published)
    g_map_preview_renderer->retire_world_map_preview();
  g_map_preview_published = false;
  g_map_preview_sections = 0;
  g_map_preview_inputs_failed = false;
}

void publish_map_preview(void *screen) noexcept {
  if (!g_map_preview_renderer || !g_map_preview_enabled ||
      g_map_preview_published)
    return;
  g_map_preview_published = true;
  if (++g_map_preview_generation == 0)
    ++g_map_preview_generation;
  std::vector<WorldMapPreviewSection> sections;
  std::string failure;
  try {
    std::int32_t count = 0, records_count = 0, figures_count = 0;
    const std::uint8_t *records = nullptr;
    if (!visual_read(screen, 0x164, count) || count <= 0 || count > 15 ||
        !visual_read(screen, 0x190, records) || !records ||
        !visual_read(screen, 0x194, records_count) || records_count < count ||
        !visual_read(screen, 0x10c, figures_count) || figures_count < count + 3)
      throw std::runtime_error(
          "World map preview: native section list unavailable");
    g_map_preview_sections = static_cast<std::uint32_t>(count);
    sections.reserve(count);
    using Lookup = void *(__thiscall *)(void *, const char *);
    const auto lookup = reinterpret_cast<Lookup>(g_map_preview_image + 0x5e850);
    for (std::int32_t i = 0; i < count; ++i) {
      WorldMapPreviewSection section;
      if (!visual_string(records + 16u * i, 8, section.zone) ||
          !hud_lane_carrier_at(screen, 3u + i))
        throw std::runtime_error(
            "World map preview: native section descriptor unavailable");
      void *const zone =
          lookup(g_map_preview_image + 0x39bf00, section.zone.c_str());
      if (!capture_map_preview_resources(zone, section))
        throw std::runtime_error(
            "World map preview: section resource association unavailable");
      // The native section factory loads this FIG with three zero shape
      // arguments. No world object's complection is consulted.
      section.figure_shape = {0, 0, 0};
      sections.push_back(std::move(section));
    }
  } catch (const std::exception &error) {
    failure = error.what();
  } catch (...) {
    failure = "World map preview: section capture failed";
  }
  // A list that failed part-way is rejected whole: the draw hook omits the
  // screen's sections rather than correlating carriers against it.
  if (!failure.empty())
    g_map_preview_sections = 0;
  try {
    g_map_preview_renderer->activate_world_map_preview(
        g_map_preview_generation, std::move(sections), std::move(failure));
  } catch (...) {
    bridge_report_event("world_map_preview_failed",
                        "activation could not be queued");
  }
}

void __fastcall hook_map_preview_activate(void *self, void *) {
  retire_map_preview();
  g_map_preview_active = false;
  reinterpret_cast<MapPreviewLifecycleFn>(g_map_preview_original[0])(self);
  g_map_preview_active = true;
  publish_map_preview(self);
}
void __fastcall hook_map_preview_deactivate(void *self, void *) {
  retire_map_preview();
  g_map_preview_active = false;
  reinterpret_cast<MapPreviewLifecycleFn>(g_map_preview_original[1])(self);
}
BOOL __fastcall hook_map_preview_draw(void *self, void *, void *renderer) {
  const auto previous = g_map_preview_draw;
  g_map_preview_draw = {};
  if (g_map_preview_active && g_map_preview_enabled) {
    publish_map_preview(self); // enabling only binds an existing loading cache
    // A rejected list cannot be correlated safely. Omit this screen's
    // colour draw visibly rather than rendering its old section visuals.
    if (!g_map_preview_sections) {
      g_map_preview_draw = previous;
      return TRUE;
    }
    g_map_preview_draw.screen = self;
    // Native island bottom/rim and owned cells must share the interface depth
    // from the beginning of this map, not split at the first replaced cell.
    if (!g_map_preview_hit_test_depth && g_map_preview_renderer)
      (void)g_map_preview_renderer->composite_world();
  }
  BOOL result = FALSE;
  __try {
    result = reinterpret_cast<MapPreviewDrawFn>(g_map_preview_original[2])(
        self, renderer);
  } __finally {
    if (g_map_preview_draw.screen && !g_map_preview_hit_test_depth &&
        g_map_preview_renderer)
      g_map_preview_renderer->draw_world_map_markers();
    g_map_preview_draw = previous;
  }
  return result;
}

std::int32_t map_preview_carrier_section(void *carrier,
                                         const void *caller) noexcept {
  if (!g_map_preview_enabled || !g_map_preview_draw.screen ||
      !g_map_preview_image || caller != g_map_preview_image + 0x1febb4)
    return -1;
  // Correlate the live array used by the exact native loop, not a retained
  // pointer key or a counter that skips invisible carriers.
  for (std::uint32_t i = 0; i < g_map_preview_sections; ++i)
    if (hud_lane_carrier_at(g_map_preview_draw.screen, 3u + i) == carrier)
      return static_cast<std::int32_t>(i);
  return -1;
}

bool map_preview_carrier_marker(void *carrier, const void *caller) noexcept {
  if (!g_map_preview_enabled || !g_map_preview_draw.screen ||
      !g_map_preview_image || g_map_preview_hit_test_depth ||
      caller != g_map_preview_image + 0x1febb4)
    return false;
  // Native factory: current-location marker figures occupy ordinals0 and1.
  return hud_lane_carrier_at(g_map_preview_draw.screen, 0) == carrier ||
         hud_lane_carrier_at(g_map_preview_draw.screen, 1) == carrier;
}

bool submit_map_preview_placed(void *renderer, void *figure) noexcept {
  if (g_map_preview_inputs_failed)
    return true;
  try {
    const std::uint8_t *records = nullptr;
    std::int32_t state = -1;
    if (!visual_read(g_map_preview_draw.screen, 0x190, records) || !records ||
        !visual_read(records, 16u * std::uint32_t(g_map_preview_draw.section),
                     state) ||
        state < 0 || state > 2)
      throw std::runtime_error("World map preview: zone state unavailable");
    // The undiscovered cover is the native FIG at shape0 and this exact
    // placement. Its existing owned raw-mask draw is representation-equivalent;
    // keep it so the colour cover's cached material never takes temporary
    // GenerateHitTestDDS texture/default-colour state.
    if (state == 0 && g_map_preview_hit_test_depth)
      return false;
    // State0 keeps an owned native FIG/MapG cover, without real terrain,
    // scenery or outlines. Cache coverage and native ID ordinals stay exact.
    void *direct3d = nullptr;
    std::array<float, 16> matrix{};
    std::array<float, 3> tint{};
    std::uint32_t diffuse = 0;
    if (!g_map_preview_renderer || !visual_read(renderer, 0x20, direct3d) ||
        !extract_native_transform(direct3d, matrix, diffuse) ||
        !visual_read(figure, 0x224, tint) || !g_scene_handoff.capture_sinks ||
        !g_scene_handoff.context)
      throw std::runtime_error(
          "World map preview: current transform/tint unavailable");
    const auto capture = g_scene_handoff.capture_sinks(g_scene_handoff.context);
    if (!capture)
      throw std::runtime_error("World map preview: device state unavailable");
    const auto &sinks = *static_cast<const SceneSinks *>(capture.get());
    auto border_tint = tint;
    if (state == 2)
      for (auto &channel : border_tint)
        channel *=
            2.f; // native cleared neutral is .5; preserve its hover RGB ratio
    if (state == 2)
      for (auto &channel : tint)
        channel *= 1.3f; // native .5 becomes the requested .65 brightness
    g_map_preview_renderer->submit_world_map_preview(
        g_map_preview_generation,
        static_cast<std::uint32_t>(g_map_preview_draw.section), matrix, tint,
        border_tint, state != 0, sinks.state);
  } catch (const std::exception &error) {
    g_map_preview_inputs_failed = true;
    // Ownership stays here even on failure; the ordinary section FIG is
    // omitted, and the renderer supplies the visible status notice.
    if (g_map_preview_renderer) {
      try {
        g_map_preview_renderer->activate_world_map_preview(
            g_map_preview_generation, {}, error.what());
      } catch (...) {
      }
    }
  } catch (...) {
    g_map_preview_inputs_failed = true;
  }
  return true;
}

std::array<void *, 3> map_preview_callbacks() noexcept {
  return {reinterpret_cast<void *>(&hook_map_preview_activate),
          reinterpret_cast<void *>(&hook_map_preview_deactivate),
          reinterpret_cast<void *>(&hook_map_preview_draw)};
}
bool uninstall_map_preview_hooks() noexcept {
  g_map_preview_hooks_ready = false;
  if (!g_map_preview_image)
    return true;
  const auto callbacks = map_preview_callbacks();
  bool restored = true;
  const auto load_callbacks = map_preview_load_callbacks();
  for (std::size_t i = 0; i < kMapPreviewLoadCalls.size(); ++i) {
    if (!g_map_preview_load_owned[i])
      continue;
    auto *call = g_map_preview_image + kMapPreviewLoadCalls[i];
    DWORD protection = 0, ignored = 0;
    if (!relative_call_targets(
            call, static_cast<const std::uint8_t *>(load_callbacks[i])) ||
        !VirtualProtect(call, 5, PAGE_EXECUTE_READWRITE, &protection)) {
      restored = false;
      continue;
    }
    std::memcpy(call, g_map_preview_load_original[i].data(), 5);
    FlushInstructionCache(GetCurrentProcess(), call, 5);
    g_map_preview_load_owned[i] = false;
    if (!VirtualProtect(call, 5, protection, &ignored))
      restored = false;
  }
  for (std::size_t i = 0; i < 3; ++i) {
    if (!g_map_preview_slot_owned[i])
      continue;
    auto **slot =
        reinterpret_cast<void **>(g_map_preview_image + kMapPreviewSlots[i]);
    DWORD protection = 0, ignored = 0;
    if (*slot != callbacks[i] ||
        !VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &protection)) {
      restored = false;
      continue; // preserve a later foreign owner
    }
    InterlockedExchangePointer(slot, g_map_preview_original[i]);
    g_map_preview_slot_owned[i] = false;
    if (!VirtualProtect(slot, sizeof(void *), protection, &ignored))
      restored = false;
  }
  if (restored) {
    try {
      retire_map_preview();
    } catch (...) {
    }
    g_map_preview_image = nullptr;
    g_map_preview_active = false;
    g_map_preview_pending_load = {};
  }
  return restored;
}
bool install_map_preview_hooks(std::uint8_t *game) noexcept {
  g_map_preview_hook_failure =
      "base screen/carrier/hit-test prerequisite unavailable";
  if (!game || running_game_sites().report.engine != GameEngine::base ||
      !g_hud_visual_hooks_installed || !g_global_map_hit_test_image)
    return false;
  g_map_preview_hook_failure =
      "loading owner or catalog admission bytes changed";
  if (!signature(game + 0x5e910, {0x53, 0x55, 0x56, 0x8b, 0xf1, 0x57, 0x33,
                                  0xff, 0x8b, 0x46, 0x08}) ||
      !signature(game + 0x22d90f, {0x8b, 0x43, 0x20, 0x3b, 0xc2}) ||
      !signature(game + 0x22d926,
                 {0x8b, 0x43, 0x1c, 0x8b, 0x04, 0x88, 0x8d, 0x2c, 0x40,
                  0xc1, 0xe5, 0x03, 0x2b, 0xe8, 0xa1, 0x18, 0xbf, 0x79,
                  0x00, 0x8d, 0x2c, 0xa8, 0x89, 0x6c, 0x24, 0x58, 0x39,
                  0x55, 0x28, 0x0f, 0x85, 0x32, 0x01, 0x00, 0x00, 0x8b,
                  0x45, 0x10, 0x39, 0x50, 0xf8}))
    return false;
  for (std::size_t i = 0; i < kMapPreviewLoadCalls.size(); ++i)
    if (!relative_call_targets(game + kMapPreviewLoadCalls[i],
                               game + kMapPreviewLoadEntries[i]))
      return false;
  const std::uint8_t setup_stock[]{0x6a, 0xff, 0x68, 0xf8, 0x63, 0x72, 0x00,
                                   0x64, 0xa1, 0x00, 0x00, 0x00, 0x00};
  const std::uint8_t step_stock[]{0x55, 0x8b, 0xec, 0x6a, 0xff,
                                  0x68, 0xe1, 0x86, 0x72, 0x00};
  if (resolve_entry_owner(game + 0x195080, setup_stock, sizeof(setup_stock), 5,
                          addon_module_contains)
              .owner == EntryOwner::rejected ||
      resolve_entry_owner(game + 0x1da400, step_stock, sizeof(step_stock), 5,
                          addon_module_contains)
              .owner == EntryOwner::rejected)
    return false;
  // Constructor establishes the primary table, never the secondary self+94.
  g_map_preview_hook_failure =
      "native section factory or catalog bytes changed";
  if (!signature(game + 0x1f68d8, {0xc7, 0x06, 0x3c, 0x0b, 0x74, 0x00}) ||
      !signature(game + 0x1ac1a0, {0x64, 0xa1, 0, 0, 0, 0, 0x6a, 0xff, 0x68}) ||
      !signature(game + 0x1ac25c,
                 {0x8b, 0x07, 0x6a, 0, 0x6a, 0, 0x8d, 0x4c, 0x24, 0x18, 0x6a, 0,
                  0x51, 0x8b, 0xcf, 0xff, 0x50, 0x4c}) ||
      !signature(game + 0x5e850, {0x53, 0x55, 0x56, 0x8b, 0xf1, 0x57, 0x33,
                                  0xff, 0x8b, 0x46, 0x1c}) ||
      !relative_call_targets(game + 0x1ff3cf, game + 0x1ac1a0))
    return false;
  std::array<std::vector<std::uint8_t>, 3> stock{
      {{0x64, 0xa1, 0, 0, 0, 0, 0x6a, 0xff, 0x68, 0xb8, 0xd2, 0x72, 0},
       {0x56, 0x8b, 0xf1, 0x57, 0x8b, 0x8e, 0x14, 0x02, 0, 0},
       {0x56, 0x8b, 0xf1, 0x6a, 0x01, 0x6a, 0x1b}}};
  const auto callbacks = map_preview_callbacks();
  g_map_preview_hook_failure = "native screen vtable slot changed";
  for (std::size_t i = 0; i < 3; ++i) {
    auto **slot = reinterpret_cast<void **>(game + kMapPreviewSlots[i]);
    if (*slot != game + kMapPreviewEntries[i])
      return false;
    const auto owner =
        resolve_entry_owner(game + kMapPreviewEntries[i], stock[i].data(),
                            stock[i].size(), 5, addon_module_contains);
    if (owner.owner == EntryOwner::rejected) {
      g_map_preview_hook_failure = owner.reason;
      return false;
    }
    g_map_preview_original[i] =
        *slot; // calls retain any verified entry dispatcher
  }
  g_map_preview_image = game;
  const auto load_callbacks = map_preview_load_callbacks();
  for (std::size_t i = 0; i < kMapPreviewLoadCalls.size(); ++i) {
    auto *call = game + kMapPreviewLoadCalls[i];
    DWORD protection = 0, ignored = 0;
    if (!VirtualProtect(call, 5, PAGE_EXECUTE_READWRITE, &protection)) {
      (void)uninstall_map_preview_hooks();
      return false;
    }
    std::memcpy(g_map_preview_load_original[i].data(), call, 5);
    const bool patched = write_relative_call(call, load_callbacks[i]);
    g_map_preview_load_owned[i] = patched;
    FlushInstructionCache(GetCurrentProcess(), call, 5);
    const bool protected_again =
        VirtualProtect(call, 5, protection, &ignored) != FALSE;
    if (!patched || !protected_again) {
      (void)uninstall_map_preview_hooks();
      return false;
    }
  }
  g_map_preview_hook_failure = "native screen slot protection change failed";
  for (std::size_t i = 0; i < 3; ++i) {
    auto **slot = reinterpret_cast<void **>(game + kMapPreviewSlots[i]);
    DWORD protection = 0, ignored = 0;
    if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &protection)) {
      (void)uninstall_map_preview_hooks();
      return false;
    }
    InterlockedExchangePointer(slot, callbacks[i]);
    g_map_preview_slot_owned[i] = true;
    record_owned_patch(slot, &g_map_preview_original[i], sizeof(void *));
    if (!VirtualProtect(slot, sizeof(void *), protection, &ignored)) {
      (void)uninstall_map_preview_hooks();
      return false;
    }
  }
  g_map_preview_hook_failure =
      "verified loading callers, catalog, screen slots and raw-ID scope";
  g_map_preview_hooks_ready = true;
  return true;
}
