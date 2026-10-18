# Renderer source provenance

Upstream: https://github.com/Ilufus/evil-islands-owned-renderer

This is the EIRebuild embedded integration, based on candidate-74 (`742bda5aa064092549ace5da2b17fd9cc97423c3`) with local
integration changes in branch `eirebuild/c74-p2`, commit `ed4262d5d5c0849b3bb68f21a1b40b6cb7463960`.
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