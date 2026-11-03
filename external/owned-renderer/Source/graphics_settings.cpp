#include "graphics_settings.h"

#include "critter_summary.h"
#include "debug_overlay_config.h"
#include "ui_language.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>

namespace owned {
namespace {

constexpr const char* kCategories[] = {
    "Display & Anti-aliasing", "Lighting & Shadows", "Terrain", "Water",
    "Vegetation & Ground Cover", "Atmosphere", "Effects & Particles", "World",
    "Interface", "Critters",   // the Graphics tab lists Critters right after World
};

// THE preset table. Values are Low, Medium, High, Ultra. High is exactly the
// registered default of every key (the look accepted on mid/high hardware).
// Low turns the optional passes off (critters stay at low) and keeps minimal
// sun shadows so weak GPUs stay near the game's CPU-bound frame time.
// Reviewed by the user 2026-10-04. Costs that drove each row
// (5090, 4K, F11 normal_frame GPU timers) are summarised in
// RENDERER_ARCHITECTURE.md, "Player graphics settings and presets".
constexpr PlayerGraphicsSetting kSettings[] = {
    // Display & Anti-aliasing
    {"display_mode", kCategories[0], "Display mode",
     "Borderless fullscreen fills the monitor. Windowed opens a movable window of the size below. "
     "Use Apply display beside Window size to apply the change immediately.",
     "Borderless fullscreen\0" "Windowed\0", false, {0, 0, 0, 0}},
    {"window_width", kCategories[0], "Window size",
     "Size of the game window in windowed mode; the interface is laid out for it. "
     "Use Apply display to change it without restarting.", nullptr, false, {0, 0, 0, 0}},
    {"upscaler", kCategories[0], "Upscaling",
     "Renders the world at a lower resolution and rebuilds a sharp, smooth image at the screen size. "
     "Replaces anti-aliasing and render scale while on. NVIDIA DLSS needs the 64-bit render host and an RTX card; "
     "AMD FSR 2 runs on most graphics cards from 2015 on, in both modes. Off renders at the render scale.",
     nullptr, false, {0, 0, 0, 0}},
    {"upscaler_quality", kCategories[0], "Upscaling quality",
     "The internal resolution of the upscaler. Quality looks best, Ultra Performance is fastest; "
     "DLAA and FSR Native AA keep the full resolution and only smooth.",
     nullptr, false, {0, 0, 0, 0}},
    {"upscale_sharpness", kCategories[0], "Sharpening",
     "Sharpens the image after the upscaler, DLSS or FSR. Keep it low to stay close to the original look; 0 turns it off.",
     nullptr, false, {.2f, .2f, .2f, .2f}},
    {"frame_generation", kCategories[0], "Frame generation",
     "Adds one generated frame between each two rendered frames: about double the frames on screen for about one rendered frame "
     "of extra delay. Needs DLSS or FSR upscaling and the Direct3D 12 renderer. DLSS Frame Generation needs a GeForce RTX 40 or 50; "
     "FSR frame generation runs on any Direct3D 12 card. It pauses on full-screen menus and below the minimum FPS.",
     "Off\0" "NVIDIA DLSS Frame Generation\0" "AMD FSR frame generation\0", false, {0, 0, 0, 0}},
    {"frame_generation_min_fps", kCategories[0], "Frame generation minimum FPS",
     "Frame generation pauses while the rendered frame rate is below this (generated frames at low rates feel sluggish), and resumes above it.",
     nullptr, false, {30, 30, 30, 30}},
    {"latency_pacing", kCategories[0], "Latency pacing",
     "Starts each game frame later, by as much as it would otherwise wait, so input is read closer to when the frame is shown. "
     "Lower input delay at the same frame rate; never a frame cap. Direct3D 12 renderer.",
     "Off\0" "On\0", false, {0, 0, 0, 0}},
    {"reflex", kCategories[0], "NVIDIA Reflex",
     "NVIDIA cards, Direct3D 12 renderer: keeps the GPU queue short for lower input delay. Boost also waits before releasing the next game frame.",
     "Off\0" "On\0" "On + Boost\0", false, {0, 0, 0, 0}},
    {"render_scale", kCategories[0], "Render scale",
     "The 3D world renders at this share of the screen resolution and is sharpened back up to it. "
     "The interface, text and cursor always stay at full resolution. Lower is faster. Only with Upscaling off.",
     nullptr, true, {67, 85, 100, 100}},
    {"smaa", kCategories[0], "Anti-aliasing (SMAA)",
     "Smooths jagged edges in the world. Cheap.", nullptr, true, {0, 1, 1, 1}},
    {"anisotropy", kCategories[0], "Texture filtering",
     "Keeps ground textures sharp when seen at a low angle. Higher is sharper.",
     "1x\0" "2x\0" "4x\0" "8x\0" "16x\0", true, {2, 3, 3, 4}},
    {"vsync", kCategories[0], "VSync",
     "Matches frames to the monitor's refresh to stop tearing. Off gives the highest frame rate.",
     nullptr, false, {0, 0, 0, 0}},
    {"frame_limit", kCategories[0], "Frame rate limit",
     "Caps the frame rate to save power and heat, or to keep it steady. Off is uncapped.",
     nullptr, false, {0, 0, 0, 0}},
    {"background_mode", kCategories[0], "When in the background",
     "What the game does while another window is in front. Keep running: the world and the picture go on "
     "(at the background frame rate below). Pause: the world stops and the last picture stays on screen.",
     "Keep running\0" "Pause\0", false, {0, 0, 0, 0}},
    {"x64_render_host", kCategories[0], "64-bit render host",
     "Runs the renderer and the graphics driver in a separate 64-bit process, leaving the game much more memory "
     "(fewer out-of-memory crashes on big maps); DLSS needs it. Applies after restarting the game. "
     "If the host does not start, the option turns itself off.",
     nullptr, false, {0, 0, 0, 0}},
    {"renderer_backend", kCategories[0], "Renderer",
     "Direct3D 12 runs in the 64-bit render host and is needed for frame generation and NVIDIA Reflex. Applies after restarting the game. A graphics card that cannot start Direct3D 12 stays on Direct3D 11 (the reason shows in F10 and host.log).",
     "Direct3D 11\0" "Direct3D 12\0", false, {0, 0, 0, 0}},
    {"background_frame_limit", kCategories[0], "Background frame rate limit",
     "Caps the frame rate while another window is in front, so a game left running idles quietly. Off is uncapped.",
     nullptr, false, {0, 0, 0, 0}},
    // Lighting & Shadows
    {"hero_light", kCategories[1], "Hero light",
     "A light follows each player-controlled hero. Off keeps nearby shadows darker.",
     nullptr, false, {0, 0, 0, 0}},
    {"shadows", kCategories[1], "Sun shadows",
     "Shadows cast by the sun and moon.", nullptr, true, {1, 1, 1, 1}},
    {"material_quality", kCategories[1], "Material quality",
     "Metal, leather, cloth and bone on characters, and stone, wood and plaster on buildings and props, catch the sun, the sky and lamps "
     "differently. Off keeps the plain image. The painted colours and the game's lighting stay.",
     "Off\0" "Low\0" "Medium\0" "High\0", true, {0, 1, 2, 3}},
    {"shadow_cascade_quality", kCategories[1], "Shadow resolution",
     "Sharpness of sun shadows. Higher uses more video memory.",
     "Low\0" "Medium\0" "High\0", true, {0, 1, 1, 2}},
    {"shadow_close_cascades", kCategories[1], "Shadow layers near the camera",
     "How many detailed shadow layers cover the area around the camera. Fewer is faster.",
     "1\0" "2\0" "3\0", true, {1, 3, 3, 3}},
    {"shadow_far", kCategories[1], "Distant shadows",
     "Shadows further away from the camera.", nullptr, true, {0, 1, 1, 1}},
    {"shadow_neighbour_casters", kCategories[1], "Neighbouring-map shadows",
     "Trees and objects on the neighbouring maps drawn around the play area cast sun shadows. "
     "Near limits them to the detailed layers around the camera; All also casts into the distant layers.",
     "Off\0" "Near\0" "All\0", true, {1, 1, 2, 2}},
    {"shadow_softness", kCategories[1], "Soft shadows",
     "Shadow edges grow softer further from the object that casts them.",
     "Off\0" "Low\0" "High\0", true, {0, 1, 1, 2}},
    {"point_shadows", kCategories[1], "Lamp and torch shadows",
     "Lamps, torches and fires cast shadows.", nullptr, true, {0, 1, 1, 1}},
    {"point_shadow_quality", kCategories[1], "Lamp shadow resolution",
     "Sharpness of lamp and torch shadows. Higher uses more video memory.",
     "Low\0" "Medium\0" "High\0", true, {0, 1, 2, 2}},
    {"ao_quality", kCategories[1], "Ambient occlusion",
     "Soft shading in corners, creases and where objects meet the ground.",
     "Off\0" "Low\0" "High\0", true, {0, 1, 2, 2}},
    {"emissive_lights", kCategories[1], "Lava glow lighting",
     "Lava and other glowing surfaces light up their surroundings.",
     "Off\0" "On\0" "Fine\0", true, {0, 1, 1, 2}},
    {"cave_brightness", kCategories[1], "Cave brightness",
     "Overall light level in caves and dungeons.", nullptr, false, {0, 0, 0, 0}},
    {"cave_ambient_floor", kCategories[1], "Cave darkness floor",
     "How dark unlit parts of caves may get. Lower is darker; lamps and lava light the rest.", nullptr, false, {0, 0, 0, 0}},
    {"cave_gi", kCategories[1], "Bounce light",
     "Torch, campfire, lamp and lava light bounces off the ground and rock and lights the surroundings; the sun bounces off the ground, and daylight comes in through cave exits.",
     "Off\0" "Low\0" "High\0", true, {0, 1, 2, 2}},
    // Terrain
    {"terrain_quality", kCategories[2], "Terrain detail",
     "Ground texture quality: less visible tiling and sharper transitions.",
     "Low\0" "Medium\0" "High\0", true, {0, 1, 2, 2}},
    {"terrain_blend", kCategories[2], "Terrain blending",
     "Natural, irregular edges between ground materials instead of repeating tile shapes.",
     "Off\0" "On\0", true, {0, 1, 1, 1}},
    {"terrain_cliffs", kCategories[2], "Cliff texturing",
     "Cliffs show the map's own ground art without stretching. Needs terrain blending.", nullptr, true, {0, 1, 1, 1}},
    {"contact_blend", kCategories[2], "Ground contact blend",
     "Buildings, rocks and fences blend into the ground they stand on.",
     nullptr, true, {0.f, .7f, .7f, .7f}},
    {"terrain_tessellation", kCategories[2], "Smooth cliff shapes",
     "Rounds off sharp cliff edges. Costs GPU time on all terrain.",
     "Off\0" "On\0", true, {0, 0, 1, 1}},
    {"terrain_lod", kCategories[2], "Terrain level of detail",
     "Distant ground uses simpler geometry. Faster; far terrain is slightly less detailed.",
     nullptr, true, {1, 1, 0, 0}},
    {"lod", kCategories[7], "Static object LOD",
     "Simpler geometry for eligible distant static objects.", nullptr, false, {}},
    {"lod_pixel_error", kCategories[7], "Static LOD pixel limit",
     "Larger values allow simpler geometry nearer to the camera.", nullptr, false, {}},
    // Water
    {"water_simple", kCategories[3], "Stock-look water",
     "Draw the original-looking animated water. Turn off to use reflections and realistic water. "
     "This choice keeps its own value when changing the overall quality preset.", nullptr, false, {}},
    {"water_reflections", kCategories[3], "Water reflections",
     "Water reflects the world and the sky.", nullptr, true, {0, 0, 1, 1}},
    {"water_reflection_quality", kCategories[3], "Water reflection quality",
     "Detail of water reflections. High gives finer reflected edges; Low is faster.",
     "Low\0" "Medium\0" "High\0", true, {0, 1, 2, 2}},
    {"water_physical", kCategories[3], "Realistic water",
     "Refraction, depth colour and sun glints on water. Needs water reflections.",
     nullptr, true, {0, 0, 1, 1}},
    {"water_surf_quality", kCategories[3], "Sea surf detail",
     "Breaking waves and foam on exposed sea coasts. Low draws fewer wave layers; faster.",
     "Low\0" "Medium\0" "High\0", true, {0, 1, 2, 2}},
    // Vegetation & Ground Cover
    {"ground_cover", kCategories[4], "Grass and ground cover",
     "Grass, flowers, leaves, stones and reeds on the ground.",
     "Off\0" "Low\0" "Normal\0" "High\0", true, {1, 1, 2, 3}},
    {"ground_cover_distance", kCategories[4], "Ground cover distance (m)",
     "How far grass and ground cover reach. Shorter is faster.", nullptr, true, {75, 100, 200, 300}},
    {"ground_cover_shadows", kCategories[4], "Ground cover shadows",
     "Sun shadows of the ground cover. Large only: fallen branches, dead bushes and cave roots. "
     "All adds grass, flowers and small debris; costs noticeably more.",
     "Off\0" "Large only\0" "All\0", false, {1, 1, 1, 1}},
    {"tree_sway", kCategories[4], "Swaying trees",
     "Tree and bush leaves move in the wind.", nullptr, false, {0, 0, 0, 0}},
    // Atmosphere
    {"sky_clouds", kCategories[5], "Sky and clouds",
     "Animated sky with clouds, sun, moon, stars and cloud shadows. Original keeps the game's sky.",
     "Original\0" "Low\0" "High\0", true, {0, 1, 2, 2}},
    {"fog_preset", kCategories[5], "Fog",
     "Faithful: the game's fog. Atmospheric: haze and ground fog by time and weather. "
     "Volumetric: adds swamp mist and light shafts.",
     "Faithful\0" "Atmospheric\0" "Volumetric\0", true, {0, 1, 2, 2}},
    {"fog_volume_quality", kCategories[5], "Volumetric fog quality",
     "Detail of volumetric fog (Volumetric fog only).", "Low\0" "High\0", true, {0, 0, 1, 1}},
    {"sun_shafts", kCategories[5], "Sun shafts",
     "Beams of sunlight through trees, fog and mist. Simple is cheap.",
     "Off\0" "Simple\0" "Volumetric\0" "Both\0", true, {0, 1, 2, 3}},
    {"fog_strength", kCategories[5], "Fog strength",
     "How thick the added haze and mist are.", nullptr, false, {0, 0, 0, 0}},
    {"weather_wind_variation", kCategories[5], "Changing wind",
     "Wind changes with time and weather instead of staying steady.",
     nullptr, false, {0, 0, 0, 0}},
    // Effects & Particles
    {"bloom", kCategories[6], "Bloom",
     "Soft glow around bright light: sun, lava, fire and magic.",
     "Off\0" "Low\0" "High\0", true, {0, 1, 2, 2}},
    {"depth_of_field", kCategories[6], "Depth of field",
     "Blurs the far background when the camera is tilted toward the horizon. "
     "The steep gameplay view stays sharp. High gives a smoother blur. Off by default; no preset turns it on.",
     "Off\0" "Low\0" "High\0", false, {0, 0, 0, 0}},
    {"depth_of_field_strength", kCategories[6], "Depth of field strength",
     "How strong the background blur is and how close behind the focus it starts. "
     "1 is subtle, 3 is strongly cinematic.", nullptr, false, {0, 0, 0, 0}},
    {"ambient_particles", kCategories[6], "Ambient particles",
     "Pollen, falling leaves, dust, embers and fireflies.",
     "Off\0" "Low\0" "High\0", true, {0, 1, 1, 2}},
    {"wind_trails", kCategories[6], "Wind streaks",
     "Faint streaks that trace strong gusts.", "Off\0" "Normal\0" "Dense\0", true, {0, 1, 1, 2}},
    {"fur_mode", kCategories[6], "Fur and hair",
     "Creatures get layered fur and characters get hair strands grown from the scalp instead of flat hair meshes. "
     "The middle level is lighter and keeps the original hair.", "Off\0" "Creature fur and fur armour\0" "Full: fur, fur armour and hair strands\0", true, {0, 1, 2, 2}},
    {"precipitation", kCategories[6], "Rain and snow",
     "Rain streaks and snowflakes drawn by the renderer. Higher levels draw denser rain; Off draws no rain or snow.",
     "Off\0" "Low\0" "Medium\0" "High\0" "Ultra\0", true, {1, 2, 3, 4}},
    {"precip_strength", kCategories[6], "Rain and snow strength",
     "Varies: each shower is light, medium or heavy. Only the picture changes; the game's weather is the same.",
     "Varies\0" "Light\0" "Medium\0" "Heavy\0", false, {0, 0, 0, 0}},
    {"effect_distance", kCategories[6], "Effect distance (m)",
     "How far campfires, torches, particle sources, lights and lightning are kept. 100 is the game's own range; "
     "never beyond the draw distance.", nullptr, true, {100, 200, 300, 500}},
    // World
    {"neighbour_maps", kCategories[7], "Neighbouring maps",
     "Shows the land beyond the map edge (scenery only).", nullptr, true, {0, 1, 1, 1}},
    {"draw_distance", kCategories[7], "Draw distance (m)",
     "How far terrain, objects and neighbouring land are drawn. The far edge fades into the sky or fog. "
     "Shorter is faster and loads less of the neighbouring maps.",
     nullptr, true, {100, 200, 300, 500}},
    {"neighbour_fog_range", kCategories[7], "Neighbour haze depth",
     "How deep the haze over neighbouring land is. Higher shows more of it, up to the draw distance.",
     nullptr, false, {0, 0, 0, 0}},
    // Critters: the level, then one row per type (kCritterTypeSettings: the
    // on/off switch; draw_player_setting adds its spawn weight beside it).
    {"critters", kCategories[9], "Critters",
     "Small animals and birds that react to your party.",
     "Off\0" "Low\0" "High\0", true, {1, 1, 2, 2}},
#define CRITTER_TYPE_ROW(i) {kCritterTypeSettings[i].key, kCategories[9], kCritterTypeSettings[i].label, \
                             kCritterTypeSettings[i].tooltip, nullptr, false, {1, 1, 1, 1}}
    CRITTER_TYPE_ROW(0), CRITTER_TYPE_ROW(1), CRITTER_TYPE_ROW(2), CRITTER_TYPE_ROW(3), CRITTER_TYPE_ROW(4),
    CRITTER_TYPE_ROW(5), CRITTER_TYPE_ROW(6), CRITTER_TYPE_ROW(7), CRITTER_TYPE_ROW(8), CRITTER_TYPE_ROW(9),
    CRITTER_TYPE_ROW(10), CRITTER_TYPE_ROW(11), CRITTER_TYPE_ROW(12), CRITTER_TYPE_ROW(13), CRITTER_TYPE_ROW(14),
#undef CRITTER_TYPE_ROW
    // Interface & Cursor (the menu language has its own section at the top)
    {"hud_fade_duration", kCategories[8], "HUD fade-out duration (ms)",
     "Shared fade-out duration for every widget after the idle delay. 0 fades instantly.", nullptr, false, {}},
    {"hud_fade_in_duration", kCategories[8], "HUD fade-in duration (ms)",
     "Shared fade-in duration on hover or meaningful updates. 0 restores visibility instantly; repeated updates keep the fade moving.", nullptr, false, {}},
    {"hud_fade_delay", kCategories[8], "HUD idle time before fade-out (ms)",
     "Time without hover or meaningful updates before fading out. A fade-in finishes first, including at 0 idle time.", nullptr, false, {}},
    {"spell_bar_auto_fade", kCategories[8], "Spell bar: auto fade",
     "Fade after the configured idle time. Hover or meaningful updates fade it in; animation alone does not count.", nullptr, false, {}},
    {"spell_bar_fade_transparency", kCategories[8], "Spell bar: faded transparency (%)",
     "Transparency reached when this widget fades. 0 stays visible; 100 hides it until hover or update.", nullptr, false, {}},
    {"ability_bar_auto_fade", kCategories[8], "Ability bar: auto fade",
     "Fade after the configured idle time. Hover or meaningful updates fade it in; animation alone does not count.", nullptr, false, {}},
    {"ability_bar_fade_transparency", kCategories[8], "Ability bar: faded transparency (%)",
     "Transparency reached when this widget fades. 0 stays visible; 100 hides it until hover or update.", nullptr, false, {}},
    {"minimap_auto_fade", kCategories[8], "Minimap: auto fade",
     "Fade after the configured idle time. Hover or meaningful updates fade it in; animation alone does not count.", nullptr, false, {}},
    {"minimap_fade_transparency", kCategories[8], "Minimap: faded transparency (%)",
     "Transparency reached when this widget fades. 0 stays visible; 100 hides it until hover or update.", nullptr, false, {}},
    {"character_info_auto_fade", kCategories[8], "Character info: auto fade",
     "Fade after the configured idle time. Hover or meaningful updates fade it in; animation alone does not count.", nullptr, false, {}},
    {"character_info_fade_transparency", kCategories[8], "Character info: faded transparency (%)",
     "Transparency reached when this widget fades. 0 stays visible; 100 hides it until hover or update.", nullptr, false, {}},
    {"portraits_auto_fade", kCategories[8], "Character portraits: auto fade",
     "Fade after the configured idle time. Hover or meaningful updates fade it in; animation alone does not count.", nullptr, false, {}},
    {"portraits_fade_transparency", kCategories[8], "Character portraits: faded transparency (%)",
     "Transparency reached when this widget fades. 0 stays visible; 100 hides it until hover or update.", nullptr, false, {}},
    {"weapon_switcher_auto_fade", kCategories[8], "Weapon switcher: auto fade",
     "Fade after the configured idle time. Hover or meaningful updates fade it in; animation alone does not count.", nullptr, false, {}},
    {"weapon_switcher_fade_transparency", kCategories[8], "Weapon switcher: faded transparency (%)",
     "Transparency reached when this widget fades. 0 stays visible; 100 hides it until hover or update.", nullptr, false, {}},
    {"quick_items_auto_fade", kCategories[8], "Quick items: auto fade",
     "Fade after the configured idle time. Hover or meaningful updates fade it in; animation alone does not count.", nullptr, false, {}},
    {"quick_items_fade_transparency", kCategories[8], "Quick items: faded transparency (%)",
     "Transparency reached when this widget fades. 0 stays visible; 100 hides it until hover or update.", nullptr, false, {}},
    {"message_panel_auto_fade", kCategories[8], "Loot and quest messages: auto fade",
     "Fade after the configured idle time. Hover or meaningful updates fade it in; animation alone does not count.", nullptr, false, {}},
    {"message_panel_fade_transparency", kCategories[8], "Loot and quest messages: faded transparency (%)",
     "Transparency reached when this widget fades. 0 stays visible; 100 hides it until hover or update.", nullptr, false, {}},
    {"stance_auto_fade", kCategories[8], "Current stance: auto fade",
     "Fade after the configured idle time. Hover or meaningful updates fade it in; animation alone does not count.", nullptr, false, {}},
    {"stance_fade_transparency", kCategories[8], "Current stance: faded transparency (%)",
     "Transparency reached when this widget fades. 0 stays visible; 100 hides it until hover or update.", nullptr, false, {}},
    {"game_speed_auto_fade", kCategories[8], "Game speed and time: auto fade",
     "Fade after the configured idle time. Hover or meaningful updates fade it in; animation alone does not count.", nullptr, false, {}},
    {"game_speed_fade_transparency", kCategories[8], "Game speed and time: faded transparency (%)",
     "Transparency reached when this widget fades. 0 stays visible; 100 hides it until hover or update.", nullptr, false, {}},
    {"stance_scale", kCategories[8], "Current stance: scale (%)",
     "Individual scale multiplied by global UI scale. Fixed in the screen corner, independent of HUD width.", nullptr, false, {}},
    {"game_speed_scale", kCategories[8], "Game speed and time: scale (%)",
     "Individual scale multiplied by global UI scale. Fixed in the screen corner, independent of HUD width.", nullptr, false, {}},
    {"ui_scale", kCategories[8], "HUD scale (%)",
     "Size of the in-mission HUD, from 50% to 120%. Changes immediately, including clickable areas. "
     "Menus, inventory and shops keep their size.", nullptr, false, {100, 100, 100, 100}},
    {"combat_text_scale", kCategories[8], "Combat text scale (%)",
     "Independent size of damage numbers and floating combat indicators, including backstab and critical hits. Applies immediately.", nullptr, false, {}},
    {"spell_bar_direction", kCategories[8], "Spell icon alignment",
     "Layout direction of the spell icons. Each icon keeps its quick-slot binding and stays upright.",
     "Bottom to top\0" "Top to bottom\0" "Left to right\0" "Right to left\0", false, {}},
    {"ability_bar_direction", kCategories[8], "Ability icon alignment",
     "Layout direction of follow, steal and the other ability icons. Each keeps its original action.",
     "Bottom to top\0" "Top to bottom\0" "Left to right\0" "Right to left\0", false, {}},
    {"portrait_direction", kCategories[8], "Character portrait alignment",
     "Original centered layout or a vertical/horizontal row. Portraits, bars and click targets move together.",
     "Centered (original)\0" "Top to bottom\0" "Bottom to top\0" "Left to right\0" "Right to left\0", false, {}},
    {"message_panel_text_scale", kCategories[8], "Info panel text scale (%)",
     "Size of loot and quest text. Panel resizing changes wrapping; global HUD scale multiplies this size.", nullptr, false, {}},
    {"portrait_head", kCategories[8], "Party portraits: real head",
     "The party portraits show each member's own head and hair, with the same hair, materials and expressions as in the world, "
     "instead of the baked portrait faces.", nullptr, false, {1, 1, 1, 1}},
    {"portrait_helmet", kCategories[8], "Party portraits: helmet",
     "Portraits wear the helmet the member wears. Off (the original look) shows the bare head and hair.", nullptr, false, {0, 0, 0, 0}},
    {"hud_width", kCategories[8], "HUD width",
     "On ultrawide screens, keeps the map, portrait, spells and weapons within a 16:9 or 21:9 area "
     "around the centre instead of at the far edges. Applies after restarting the game.",
     "Full width\0" "21:9\0" "16:9\0", false, {2, 2, 2, 2}},
    {"pillarbox_fill", kCategories[8], "Pillarbox fill",
     "Loading screens and movies made for another screen shape keep their shape; the bars beside them "
     "are black or a soft blur of the same picture.",
     "Black\0" "Blurred\0", false, {1, 1, 1, 1}},
    {"camp_ui_layout", kCategories[8], "Camp layout",
     "Switch the open camp between the original UI comparison and the wide inventory grid. "
     "Keeps the current mode and transaction; no restart is needed. The wide grid needs a 16:9 or wider display.",
     "Original UI (comparison)\0" "Wide grid\0", false, {1, 1, 1, 1}},
    {"cursor_scale", kCategories[8], "Cursor size (%)",
     "Size of the mouse cursor.", nullptr, false, {0, 0, 0, 0}},
    {"confine_cursor", kCategories[8], "Keep cursor in the window",
     "The cursor cannot leave the game window while it has focus.", nullptr, false, {0, 0, 0, 0}},
    {"highlight_briefing", kCategories[8], "Highlight briefing characters",
     "On the base screen, characters with something to say glow.", nullptr, false, {0, 0, 0, 0}},
    {"highlight_selected", kCategories[8], "Highlight selected characters",
     "Your selected characters glow.", nullptr, false, {0, 0, 0, 0}},
    {"highlight_hover_characters", kCategories[8], "Highlight characters under the cursor",
     "The character the cursor would act on glows softly.", nullptr, false, {0, 0, 0, 0}},
    {"highlight_hover_objects", kCategories[8], "Highlight objects under the cursor",
     "Chests, levers, traps and corpses under the cursor glow softly.",
     nullptr, false, {0, 0, 0, 0}},
};

static_assert(std::size(kCritterTypeSettings) == 15, "add a CRITTER_TYPE_ROW for the new critter type");

// The rows of kSettings that read differently before settings_version 1 (the
// table of accepted candidate-66), kept only for the migration: a file saved
// on an old preset gets that preset's current values. in_preset false: the
// row was not in the old table.
struct PresetRowBeforeVersion1 { const char* key; bool in_preset; float values[kGraphicsPresetCount]; };
constexpr PresetRowBeforeVersion1 kPresetRowsBeforeVersion1[] = {
    {"shadow_close_cascades", true, {2, 3, 3, 3}},
    {"ground_cover", true, {0, 1, 2, 3}},
    {"ground_cover_distance", true, {100, 100, 200, 300}},
    {"depth_of_field", true, {0, 1, 2, 2}},
    {"precipitation", false, {0, 0, 0, 0}},
};

// Rows added to the preset table by settings_version 2: an older file saved
// on a named preset never held them (shadow_neighbour_casters, candidate-72).
constexpr PresetRowBeforeVersion1 kPresetRowsBeforeVersion2[] = {
    {"shadow_neighbour_casters", false, {0, 0, 0, 0}},
};

// settings_version 4 split terrain_quality Low/High into Low/Medium/High;
// migrate_settings has already moved an older file's value up one step (old
// Low is Medium, old High is High) when the preset is detected.
constexpr PresetRowBeforeVersion1 kPresetRowsBeforeVersion4[] = {
    {"terrain_quality", true, {1, 1, 2, 2}},
};

// settings_version 5 put material_quality and fur_mode into the presets: an
// older file saved on a named preset held their defaults, not a choice.
constexpr PresetRowBeforeVersion1 kPresetRowsBeforeVersion5[] = {
    {"material_quality", false, {2, 2, 2, 2}},
    {"fur_mode", false, {0, 0, 0, 0}},
};

bool option_matches(const RendererOption& option, float value) noexcept {
    switch (option.type) {
    case RendererOptionType::boolean: return option.boolean_value == (value != 0.0f);
    case RendererOptionType::integer: return option.integer_value == static_cast<int>(std::lround(value));
    case RendererOptionType::floating_point: return std::fabs(option.floating_value - value) < 1e-4f;
    }
    return false;
}

} // namespace

const char* graphics_preset_name(GraphicsPreset preset) noexcept {
    switch (preset) {
    case GraphicsPreset::low: return "Low";
    case GraphicsPreset::medium: return "Medium";
    case GraphicsPreset::high: return "High";
    case GraphicsPreset::ultra: return "Ultra";
    default: return "Custom";
    }
}

const char* graphics_preset_summary(GraphicsPreset preset) noexcept {
    switch (preset) {
    case GraphicsPreset::low: return "Lightest. For integrated graphics and older cards: original sky and fog, "
                                     "no grass, simple shadows.";
    case GraphicsPreset::medium: return "Balanced for mid-range cards: most effects at their low levels.";
    case GraphicsPreset::high: return "The intended look. For recent mid to high-end cards.";
    case GraphicsPreset::ultra: return "Everything at its highest level. For high-end cards.";
    default: return "Your own selection of advanced options.";
    }
}

const PlayerGraphicsSetting* player_graphics_settings(std::size_t& count) noexcept {
    count = std::size(kSettings);
    return kSettings;
}

#if !defined(EID3D11_SHADER_EMBED)
const char* const* player_graphics_categories(std::size_t& count) noexcept {
    count = std::size(kCategories);
    return kCategories;
}

std::size_t apply_graphics_preset(RendererConfig& config, GraphicsPreset preset) noexcept {
    if (preset == GraphicsPreset::custom) return 0;
    const auto index = static_cast<std::size_t>(preset);
    std::size_t applied = 0;
    for (const auto& setting : kSettings) {
        if (!setting.in_preset) continue;
        const RendererOption* option = config.find(setting.key);
        if (!option) continue;
        const float value = setting.values[index];
        bool set = false;
        switch (option->type) {
        case RendererOptionType::boolean: set = config.set_boolean(setting.key, value != 0.0f); break;
        case RendererOptionType::integer: set = config.set_integer(setting.key, static_cast<int>(std::lround(value))); break;
        case RendererOptionType::floating_point: set = config.set_floating_point(setting.key, value); break;
        }
        applied += set ? 1u : 0u;
    }
    return applied;
}

namespace {
GraphicsPreset detect_preset(const RendererConfig& config, int saved_version) noexcept {
    for (int index = 0; index < kGraphicsPresetCount; ++index) {
        bool matches = true;
        for (const auto& setting : kSettings) {
            bool in_preset = setting.in_preset;
            const float* values = setting.values;
            if (saved_version < 1)
                for (const auto& row : kPresetRowsBeforeVersion1)
                    if (std::strcmp(row.key, setting.key) == 0) { in_preset = row.in_preset; values = row.values; }
            if (saved_version < 2)
                for (const auto& row : kPresetRowsBeforeVersion2)
                    if (std::strcmp(row.key, setting.key) == 0) { in_preset = row.in_preset; values = row.values; }
            if (saved_version < 4)
                for (const auto& row : kPresetRowsBeforeVersion4)
                    if (std::strcmp(row.key, setting.key) == 0) { in_preset = row.in_preset; values = row.values; }
            if (saved_version < 5)
                for (const auto& row : kPresetRowsBeforeVersion5)
                    if (std::strcmp(row.key, setting.key) == 0) { in_preset = row.in_preset; values = row.values; }
            if (!in_preset) continue;
            const RendererOption* option = config.find(setting.key);
            if (option && !option_matches(*option, values[index])) { matches = false; break; }
        }
        if (matches) return static_cast<GraphicsPreset>(index);
    }
    return GraphicsPreset::custom;
}
} // namespace

GraphicsPreset detect_graphics_preset(const RendererConfig& config) noexcept {
    return detect_preset(config, 5); // the current table
}

GraphicsPreset reapply_graphics_preset_before(RendererConfig& config, int saved_version) noexcept {
    const GraphicsPreset preset = detect_preset(config, saved_version);
    if (preset == GraphicsPreset::custom) return preset;
    apply_graphics_preset(config, preset);
    // A row that has left the preset (depth_of_field) held the old preset's
    // value, not a player choice: back to its default.
    if (saved_version < 1) for (const auto& row : kPresetRowsBeforeVersion1) {
        if (!row.in_preset) continue;
        const auto current = std::find_if(std::begin(kSettings), std::end(kSettings),
            [&](const PlayerGraphicsSetting& setting) { return std::strcmp(setting.key, row.key) == 0; });
        if (current != std::end(kSettings) && !current->in_preset) config.reset_to_default(row.key);
    }
    return preset;
}

GraphicsPreset suggest_graphics_preset(bool integrated, std::uint64_t video_memory_bytes) noexcept {
    constexpr std::uint64_t mib = 1024ull * 1024ull;
    if (integrated || video_memory_bytes < 1800 * mib) return GraphicsPreset::low;  // iGPU, 2 GB class
    if (video_memory_bytes < 6 * 1024 * mib) return GraphicsPreset::medium;  // 3-6 GB class (GTX 1060)
    if (video_memory_bytes < 13 * 1024 * mib + 512 * mib) return GraphicsPreset::high;  // 8-12 GB class
    return GraphicsPreset::ultra;  // 16 GB and up
}

PresentationSettings presentation_settings(const RendererConfig& config, bool developer) noexcept {
    PresentationSettings result{};
    if (developer) return result;
    if (const auto* vsync = config.find("vsync"); vsync && vsync->type == RendererOptionType::boolean)
        result.sync_interval = vsync->boolean_value ? 1u : 0u;
    if (const auto* limit = config.find("frame_limit"); limit && limit->type == RendererOptionType::integer)
        result.frame_limit = limit->integer_value > 0 ? limit->integer_value : 0;
    return result;
}

static_assert(std::size(kFrameLimitChoices) == kFrameLimitChoiceCount, "frame-limit choices");

int frame_limit_choice(int fps) noexcept {
    for (int index = 0; index < kFrameLimitChoiceCount; ++index)
        if (kFrameLimitChoices[index] == fps) return index;
    return kFrameLimitChoiceCount;
}

DisplaySettings read_display_settings(const wchar_t* ini_path) noexcept {
    DisplaySettings result{};
    if (!ini_path || !*ini_path) return result;
    const auto read = [ini_path](const wchar_t* key) {
        return static_cast<int>(GetPrivateProfileIntW(L"graphics", key, 0, ini_path));
    };
    result.mode = read(L"display_mode") == 1 ? DisplayMode::windowed : DisplayMode::borderless;
    result.window_width = (std::max)(0, read(L"window_width"));
    result.window_height = (std::max)(0, read(L"window_height"));
    return result;
}

ClientSize windowed_client_size(const DisplaySettings& settings, int monitor_width, int monitor_height,
                                int max_client_width, int max_client_height) noexcept {
    const bool native = settings.window_width <= 0 || settings.window_height <= 0;
    ClientSize size{native ? monitor_width : settings.window_width,
                    native ? monitor_height : settings.window_height};
    size.width = (std::max)(1, (std::min)(size.width, max_client_width));
    size.height = (std::max)(1, (std::min)(size.height, max_client_height));
    return size;
}

bool renderer_developer_mode() noexcept {
    char value[8]{};
    const DWORD length = GetEnvironmentVariableA("EI_RENDERER_DEVELOPER", value, sizeof(value));
    return length > 0 && length < sizeof(value) && !(value[0] == '0' && value[1] == '\0');
}

#endif
} // namespace owned
