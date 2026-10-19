# Renderer source provenance

Upstream: https://github.com/Ilufus/evil-islands-owned-renderer

This is the EIRebuild embedded integration, based on candidate-74 (`742bda5aa064092549ace5da2b17fd9cc97423c3`) with local
integration changes in branch `eirebuild/c74-p6a4`, commit `3d31e6824fd396dfb0abe480176d763287a64caf`.
This merges upstream into the embedded baseline `4e06826962c84368ad2726abb0d936621cb82112`.
The immutable host-surface contact fix (`cb3e84a`) is retained. The Russian F10
changes (`f69be1d`, `308a547`) are incorporated through the candidate-73
localization layer, with additional embedded translations preserved.
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
