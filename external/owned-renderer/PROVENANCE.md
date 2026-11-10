# X64RUPD03 renderer source provenance (2026-11-07)

Upstream main: `d17a6149e5b5d4c2d23873d18e037bc270269050` (193 commits after
`b1ffc736d4792eb36f3528a8d8ee2c358da80d54`). Local task base: `6f29190bd2e5e9c6375b2e672968f0e4b410d534`,
branch `task/X64RUPD03`. Read-only bare reference; final file tree imported
by a three-way file merge, without Git merge or replaying intermediate commits.
Build label: `eirebuild64-X64RUPD03-d17a6149`. LICENSE.md and NOTICE.md retained.

The author's service-node policy is identical to the former local header.
FIG1 neighbour handling, retained/observed native LOD, effective opaque proof,
plugin visuals, typed host inputs, Russian UI and GPU FPS surface remain.
Shared rendering algorithms and HLSL follow the author. Native retail hooks,
Host/Core transport and D3D12 stay outside the embedded D3D11 target.

New world-map preview preparation/draw code is imported but its native frontend
is excluded. A complete typed catalog/load, carrier transform/tint, raw hit-ID,
marker ordering and invalidation adapter remains open under BRIEF item 5
(large host input). Its option is not registered in the embedded target.
The native experimental camera smoothing contract is likewise not exposed.
No fallback, local toggle or placeholder implementation was added.
Runtime and visual acceptance remain open.

## Previous provenance

# X64RUPD02 renderer source provenance (2026-11-02)

Upstream final main tree: `b1ffc736d4792eb36f3528a8d8ee2c358da80d54`,
fetched read-only from Ilufus/evil-islands-owned-renderer. Ancestor:
`ef7e12467db17ef673f8ce3b105063b4b3b16417`. Local source base:
`4f716b512677ef192cf9b2af6c453c4f2f706f2c`, task/X64RUPD02.
Imported as a file-level three-way merge, without Git merge.

This is a modified embedded EIRebuild64 renderer by Ilufus, labelled
`eirebuild64-X64RUPD02-b1ffc736`; LICENSE.md and NOTICE.md are preserved.
Common materials, fur, terrain 2.0, liquid seams and shader bodies follow
the final upstream tree. Necessary typed resource/device/preview inputs,
native NGX lifecycle, C56 hit target, camera anchor, source gamma, packed
normals, FIG1/FIG8 and retained LOD remain local. The FIG1 neighbour patch
has not landed upstream; its existing X64DECOR01 implementation is kept.
The separate Host/Core transport, D3D12 presentation/NGX/FG owner and retail
hooks remain excluded from the embedded D3D11 target. No substitute
backend, transport stub, fallback or local configuration option is added.
The normal shader producer builds from the same source with runtime-only
bodies excluded. Runtime and visual acceptance remain open.

## Historical integration notes

# Renderer source provenance

X64RUPD01 (2026-10-31): current upstream source baseline is main commit
`ef7e12467db17ef673f8ce3b105063b4b3b16417`, read from the existing local
upstream clone without fetch or modifications. The three-way file merge uses
upstream `24e5d54828a8bc0a8a1573c4aec68b7aed34838b` as ancestor and the
EIRebuild64 vendored tree at `7916d68a2882e2c99f0f48cd7bd295c16ea9ccbc`
(`task/X64NAVFIX01`) as the local side. Work is in `task/X64RUPD01`.
The 154-commit upstream range is imported as its final tree, so reverted
temporary diagnostics and experiments are not replayed. The older notes
below are historical and do not designate the current upstream or platform.

This is a modified embedded EIRebuild64 renderer, not an upstream build.
Its build label is `eirebuild64-X64RUPD01-ef7e1246`; the complete engine
source commit supplies the exact modified-tree identity. EID3D11 renderer by
Ilufus. Upstream LICENSE.md and NOTICE.md, including all Required Notice
lines, are preserved beside this document.

Rework 1 (2026-10-31): cave sun retirement, cached-domain selection and
receiver admission follow ef7e1246 exactly. The local sun_shadows bridge
field/argument and its settings-revision domain gate are removed. The
upstream shadows option, its labels and config persistence are preserved.
Conflict blocks use upstream code with the necessary embedded texture,
device, memory-counter, DLSS-refusal and unavailable-scene-thread seams.

Preserved local integration:
- EID3D11_EMBEDDED typed inputs, source-managed world/UI/text/preview and
  active mounts; no retail-x86 address readers, hooks or inline/naked asm
  enter the embedded build. Remote/core-host transports remain unlinked.
- X64R00-03/X64P platform fixes, pointer/size and ABI guards, shader producer
  separation, packed-normal/specular conversion and source gamma ownership.
- FIG1/FIG8 static LOD, retained world-space indexing, pose-node scale from
  X64NAVFIX01, opacity batching and asynchronous samgbur01 GPU/F10 readback.
- Immutable resource sharing/retirement, embedded texture composition,
  neighbour border fog and upstream cave shadow-domain selection.
- Native NGX backend with the EIRebuild64 ProjectID and explicit DLSS refusal.
  Upstream Host/dlss lifetime fix and dedicated NGX worker are adapted here
  without the remote protocol. All NGX entry points execute on that worker;
  init info and both path buffers survive through shutdown.

New common frame-plan, terrain/cliff/road, cover/caster/wind, cave bounce,
alpha-test and UI changes use upstream algorithms and HLSL. Camera anchor
height comes from CCamera through the typed visual frame; the cave flag,
hour, region, terrain grid, point lights and mounted text resources are real
host publications. GenerateHitTestDDS brackets the lossless mask frame from
source; allocation failure refuses its capture and its targets retire with
their owning device. Core-only slim texture/terrain transport is excluded. The effect-list age rule
uses the embedded text session. New upstream labels remain in renderer_ui_ru.h.
No new local runtime toggle, OpenEI/Miles path or rendering fallback is added.
The shader build compiles only the common cave shader producer; game resource
loading and light execution are excluded from EID3D11_SHADER_EMBED.

Standalone Host, proxy hooks, harness, test/build/deployment scripts and
shader-lab targets are not part of this engine integration. The source and
build map is maintained privately at work/supervision/X64RUPD01/MAP.md.
Build and packaging do not establish owner gameplay/visual acceptance.

RUPD01 (2026-10-25): current imported upstream is
`accepted/water-untess-fix-20261025`, commit
`24e5d54828a8bc0a8a1573c4aec68b7aed34838b` (from candidate-79).
The source host integration is maintained in EIRebuild `task/RUPD01`, based
on `3053bac`. The older revision notes below record the preserved local work,
not the current upstream identity. Local changes follow the new renderer and
owned-world translation units: typed embedded ingress, composite failure
reporting, object border fog, resource sharing and world retirement, and
x86 size guards. Source managers and renderer use the same ordered, typed
figure/texture archive chains, including redress and menus.
The x64 remote device/host and core transport are not linked. Embedded calls
use the local neighbour stream; remote-only API entry points reject use.
FSR 2 remains the upstream local-device implementation. Upstream DLSS needs
its x64 host and is unavailable in EIRebuild x86; no substitute is supplied.
Temporary pane, focus/startup, corpse-shadow, shadow summary, VA and camera
trace diagnostics are excluded for EID3D11_EMBEDDED. The permanent game
journal and accepted composite diagnostics are retained.
Game gamma-ramp application is imported. The source host builds the original
display ramp from its brightness/contrast/gamma settings, supplies it at
initialization and on slider changes, and restores saved values on leaving
the options screen. See RUPD01/REPORT.md for the acceptance limits.

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

INT-MEM-FOG: current revision is branch `eirebuild/c79-int`, commit `8d73c26a661658c0d91878c831afe5dbd0e34604`, merging
`9ad0e0be735e893ffc6f5d3456844b5c74547169` (FOGOBJ01) and
`5f5d37c26ad06587dc6f7ffa8895c0b1cf82735b` (MEM01). Shared neighbour object fog,
world retirement and memory attribution are retained together; no shader changes.

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

MEM01: branch `eirebuild/c79-mem`, commit `5f5d37c26ad06587dc6f7ffa8895c0b1cf82735b`, based on `b25858678aa800a853f94aa99bf3e7259dc57a45`. Identical live memory selections share immutable backing and decoded identity after an exact content comparison. The embedded host retires world GPU resources before loading its successor and reads allocation-free resource counters for its permanent journal. No neighbour content, detail, shader or rendering policy changes.

X64DLSS01: renderer-owned dlss_backend.h/.cpp adapts the NGX lifecycle and
feature creation from upstream Host/dlss.cpp/.h at
ae33899bce960a72f6d6dca2cdf76b5e60a0a645 (local read-only reference).
No IPC/protocol/resource IDs or upstream ProjectID are imported. Each embedded
Renderer owns its NGX instance; capability and optimal settings use the real
D3D11 device. Frame evaluation and settings integration remain X64DLSS02/03.
SDK v310.9.16 and distribution notices: repository THIRD_PARTY.md.

X64SKIN03 (2026-11-10): completes the local SKN v1 GPU material path
(BC7 sRGB, BC5 tangent normals, ORM, emission and alpha/two-sided state)
through the existing GGX/Schlick lighting. Authored discrete LOD uses camera
error with persistent hysteresis and matching shadow levels. Joint influence
spheres supply pose-conservative CPU/GPU culling envelopes. Resource-store
paths, gameplay and upstream reference sources are unchanged.
