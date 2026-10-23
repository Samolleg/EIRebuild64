# Renderer source provenance

Upstream: https://github.com/Ilufus/evil-islands-owned-renderer

This is the EIRebuild embedded integration, based on candidate-79 (`791acfb9b10ece99c0afa832862687eb4ae115b0`) with local
integration changes in branch `eirebuild/c79-embed`, commit `c4d1466e40e3dcf074ff227597c67d4282e001a2`.
This merges upstream into the candidate-74 embedded revision `34015b2449ecacf9eb7eb55d89236696375cd57c`.
The immutable host-surface contact fix (`cb3e84a`) is retained. The Russian F10
changes (`f69be1d`, `308a547`) are incorporated through the candidate-73
localization layer, with additional embedded translations preserved.
CRASHCW01: the current embedded revision is branch `eirebuild/c79-fix`, commit `b25858678aa800a853f94aa99bf3e7259dc57a45`,
descending from `c4d1466e40e3dcf074ff227597c67d4282e001a2`. It adds embedded-only
composite stage/HRESULT diagnostics and preserves allocation failures as
`E_OUTOFMEMORY`; shaders and the proxy rendering path are unchanged.

FOGOBJ01: current revision is branch `eirebuild/c79-fogobj`, commit `9ad0e0be735e893ffc6f5d3456844b5c74547169`,
descending from `b25858678aa800a853f94aa99bf3e7259dc57a45`. The addon object
border-fog correction is shared through typed extent/distance/enabled inputs;
the host uses its real terrain state for retained, direct and world-preview figures.
The seam curves, shaders and non-neighbour behavior are unchanged.

That commit was local and was not available from the upstream repository.
The previous submodule reference could therefore not be fetched on another machine.

The source snapshot is now stored directly in EIRebuild. Imported files are
byte-for-byte copies of the selected integration revision: `Source/`,
`ThirdParty/`, `Assets/`, and the build-time `Tools/shader_embed.cpp`.
Third-party licenses and asset provenance are preserved. Local logs, dumps,
validation reports, build products and private runtime directories are excluded.
Standalone proxy build tools and upstream tests are outside this embedded build.

The initial import changes no renderer behavior. Subsequent changes are tracked
by the EIRebuild repository. To update from upstream, compare against the source
baseline, integrate deliberately with the host adapter, and update this record.
Do not replace this directory with an upstream checkout without carrying forward
the embedded interface and accepted integration changes.

RUPD74 merges candidate-74 into embedded RUPD73B revision 5f30d3563e361380735a5448f36840f4338308b1.
Sea surf, level river outflow, water shading, snow trail shadows and painted-strand
pixel kinds follow candidate-74. The typed host interface, normalized wave glint,
world retirement, precipitation and progress movie ownership remain embedded.
Russian F10 labels include the upstream maps-without-sea setting.

FX74 adds synchronous typed host inputs (embedded_game_effects.h) to the same
candidate-74 ribbon, selection, print and combat-text GPU producers. The host
retains lightning/selection/print simulation and the visual damage list; renderer
code expands the strips and meshes, groups print texture/state runs, chooses the
text anchor, and lays out/scales glyphs. Embedded inputs do not read retail
addresses. The proxy producers and shaders are unchanged.

OWN74-P1 extracts the proxy cursor producer without changing its image algorithm:
keyed extraction, EPX/area upscale, canvas/hotspot placement, cache and load
cursor selection are shared with the embedded host. Confinement and coordinate
helpers are shared too; retail import redirection is excluded from embedded builds.
Embedded-only load cursor synchronization permits the host snapshot owner to
update the held-picture cursor without reading game objects on the scene thread.
OWN74-P2 adds typed begin/text/complete/consume/retire boundaries over the existing
CharacterPaneOwnedText and RetainedOwnedTextFrames. Embedded pane and colored
chat lines use the c74 glyph shaping, output-size rasterization, shadow, clipping
and Renderer::queue_character_pane_text; retail callers keep their verified
hooks. Font snapshots, pane content wake values and the 100 ms Lua display
refresh predicate are shared helpers, without moving Lua or gameplay into the
renderer. Host text measurement/font scaling use the shared interface unit.
Combat UI receives the pane's actual unit incarnation, effects, catalog, font and
local clip. Generic GDI operations continue through owned_ui_layers.
OWN74-P3a connects completed unit health/effects, current and queued attack or
offensive spell targets, selected AI aggression, source portrait slots and pause.
The c74 CombatUiRenderer and reference-body plate projection are unchanged.
Embedded-only entries reset per-draw anchors and retire world-owned display
values. Token-aware unit retirement preserves a reused resource incarnation.
The retail portrait memory reader is excluded from embedded calls. HUD atlas
and icon lane producers remain for OWN74-P3a-2; WS layout remains P3b.

OWN74-P3a2 exposes the same HudVisualPublication and build_hud_visual_view
to typed embedded carrier values. The four icon lanes no longer load or draw
host meshes or mutate host atlas pixels. Manager role generations and source
retirement feed the shared publication/debug counters. Portrait models and
remaining non-lane atlas/WS work are still open; see the task REPORT.


OWN74-P3a3 adds synchronous source portrait model groups and current pose/morph
to shared placed-figure state and native GPU submission. Logical texture cells
use the existing HUD resolver, conversion, generations and UV addressing; the
source host no longer draws portraits or owns physical portrait atlases.
The separate addon autocast producer still needs an authoritative source slot.

OWN74-P3b shares c74 integer coordinate ingress, local rectangle mapping,
content hit-surface offsets, campaign clock formatting and tooltip sizing.
The source frontend uses these helpers for centred screens, global map,
chat and tooltips. Remaining WS HUD/native-geometry/tutorial work is tracked
per row in OWN74/MAP.md; this revision does not claim the full WS package.

OWN74-P3b2 extracts native XY anchors, complete flat-bar ingress/raw nesting,
final pixel rounding and integer minimap/portrait placement from c74. Source
HUD producers call these helpers; live scaling/docking stays in HudLayoutContext.
Retail hook values and stock geometry/UV bodies are preserved. Messages,
tutorial and actual-rect display projection remain tracked separately.

OWN74-P3c extracts shared native message geometry from refresh_message_layout
and the c74 semantic tutorial table/resolver. Embedded message reflow runs at
Init/Idle/SetOutputRect and before text ingress; tutorial uses a value snapshot
of the active mission captured before modal entry, including actual skill types
and spell slot areas. The proxy keeps its native readers and c74 values.

OWN74-P4 extracts the c74 grid and label enumeration, row clamp/reveal/picking,
scroll rail geometry/draw/drag, quick and spell figure producers, item placement
and surface remap/card occlusion into camp_frontend.h. Both proxy and embedded
adapters call these synchronous typed entries. Source-side camp background,
card state, remaining role input and lifecycle integration remain open in P4.

OWN74-P4b extracts c74 camp background/panel enumeration, decoration and popup
plates, native role/polygon hit routing, spell reveal and slot dispatch into
camp_presentation.h. Both proxy and embedded source callers use these bodies.
The complete native trade/repair/constructor draw retains the same shared
construction canvas scope. Gameplay, card content selection/lifetimes and full
model preview remain with the source owners and are tracked as open work.
OWN74-P4c shares camp card publication (hover item/help/skill, constructor
blueprint/result, Shift comparison and modifier refresh) and session input
retirement, wheel remainders and scrollbar capture in camp_session.h.
Typed source callbacks retain native formatting, independent display Parties
and gameplay owners. Full typed model previews, figure LRU and manager
teardown remain open in the task REMAINING.md; they are not claimed here.

OWN74-FIX1 exposes the shared HUD repaint rejection (stage, member and resource
code) to typed callers. Embedded text publications reject zero surface generations
and report bounded, specific transaction/lifetime failures. Native rendering
algorithms and gameplay policy are unchanged.

OWN74-P5 exposes homogeneous source geometry through a synchronous typed input
to the same fixed-function translator tail as proxy DrawPrimitive and
DrawIndexedPrimitive. The FVF frontend and embedded frontend share state, fog,
blend, clipping and GPU submission; the c74 shader bodies are unchanged.
Source W, UV, diffuse and specular values survive without native FIG repacking.
Polylines submit only authored points; the spell area ribbon keeps its source
mesh. VisFog already uses the shared c74 particle producer.
OWN74-P6a shares packed figure stream assembly and raw material response in
native_placed_figure.h. Source placed/lit wrappers supply current geometry,
morphs, material and light values directly; CPU morph expansion and legacy
light/material setup are removed from these wrappers. The native GPU path,
source picking and RUPD73 r4 world publications remain intact. Object-preview
assembly, retained scheduling/visibility, cache lifetime and live display
projection are still tracked as open work in the task REPORT.

OWN74-P6a2 shares typed model-part admission/set traversal, preview invocation
scope and final material/default-color state with the retail preview producer.
Create and actual-rect clip-scale builders share the same matrix algorithm.
Whole preview pose/lifetime and embedded live display Apply remain open.

OWN74-P6a3 shares the draw-only native figure reference across cache trim
and draw exits. Typed texture-manager identity drives the same retire/join
boundary as the proxy manager hook, before native surfaces are destroyed.
Full preview assembly/pose, LRU storage ownership and live Apply remain open.

OWN74-P6a4 shares retained preview geometry/pose evaluation in
model_preview_geometry.h for synchronous native, scene-queued native and
embedded UI object draws. Source private previews use immutable source
assemblies and separate prepared resources, including negative-id display
copies. No UI pose is published into the world. P4 wrapper clocks/placement,
light admission, item figure LRU and live display Apply remain open.

OWN74-P6a5 extracts c74 preview clock/anchor and per-view prepared pose ownership
into model_preview_clock.h. Native preview and source inventory/pane/display
wrappers call the same owner with their actual clock values and source rates.
Source SetExactPosition callbacks, world publication and picking are preserved.
Full placement/light/cache/generated-MMP/live display owners remain open.

OWN74-P6a6 shares the complete render-execution loop of source, immediate retail
and queued retail object previews in model_preview_producer.h: ordered part
admission/bindings, private GPU pose placement, geometry, final material and wound
passes. Source adapters keep immutable assets and raw current facts; no world pick
flags or completed world pose publication are changed. model_lighting.h shares
ambient/sun preparation and point-light contract admission with native c74 reads;
the source preview iterator supplies raw values to the renderer's local selection.
Full P4 composition, source callbacks, normal visibility, LRU/generated-MMP and live
display transactions remain open in OWN74-P6a6/REMAINING.md. No upstream merge.

OWN74-P6a7 shares current-object preview placement and part9 item glow in
model_preview_presentation.h. Typed source inventory/pane/display scopes pass
render-only placement, ambient, fog and colors without changing world object
CP/orientation/scale or FIG color flags. Native admitted preview placement uses
the same value builder; ordinary world routes are unchanged. Source animation
callbacks remain source-owned. The full P4/P6 remainder is still open; see
OWN74-P6a7/REMAINING.md. No upstream merge.

OWN74-P6a8 moves the complete source single-item/head presentation sequence,
source inventory/pane semantic composition and depth ordering, and the single
source LRU storage into renderer-owned typed producers. Raw resource allocation,
source object callbacks, item user-data/RNG and API submission stay in adapters.
Millisecond ties cannot stall trim or evict the current unpinned texture.
The retail stock frontend has not yet been replaced by these new producers;
full native/source parity, allocator/session policy, normal visibility,
generated-MMP and live display Apply remain open. No upstream merge.

OWN74-RET1 returns source-only single item/head sequences, inventory/pane
composition and depth ordering, owning ModelPreviewCache, per-invocation
presentation/clock TLS and object-relative preview light selection to
Game/OwnedRender. The preceding P6a5-P6a8 ownership/open-work notes are historical
and superseded by OWN74-SCOPE: stock DrawItem/DrawUnit/LRU and texture recipes
remain source-owned and require no renderer frontend replacement.
The renderer retains generic placement math and part9 glow, phase/prepared-pose
ownership, ambient/sun conversion and basic point admission, the common lower
object-preview geometry/material/wound producer, ModelDrawReference and manager
retire/join. These are c74 producers or generic ingress/lifetime support.
Only the listed source-only code is removed; no upstream merge or fallback.

OWN74-P6b (partial) changes only the source host publication seam. The renderer
working branch eirebuild/c74-p6b remains at 34015b2449ecacf9eb7eb55d89236696375cd57c;
Source/ThirdParty/Assets/Tools and Renderer.props identity remain this snapshot.
Live unit materials and hidden parts are published before retained frame capture,
only when changed. This is not the unit route/intent migration: the completed
object-pose adapter is still present and full P6b remains open.

RUPD79 carries the candidate-79 liquid/refraction/ground-mark changes and the
shared interface profiles, menu language and Reset UI confirmation. The
extracted typed HUD context follows c79 panel padding/clipping and independent
quest-item geometry using the host's existing area/shift/slot publications.
Stance and clock identities follow the expanded HudPanel enumeration.
Embedded profiles use rddraw_profiles beside the host-selected rddraw.ini;
proxy profiles retain eid3d11_profiles. Russian menu strings remain in
renderer_ui_ru.h. The c79 character-pane text fix is included; visual
acceptance remains with the owner. No reverted diagnostic modes are restored.
