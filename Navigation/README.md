# Nav64 X64NAVCORE working candidate

Not a release. zone1 builds and the start/exit/gate/ramp queries pass.
The exact 1.3/0.8 m sites and site losses still need pilot acceptance. See
work/supervision/X64NAVCORE/REPORT.md, section "Доработка" in the private
workspace. Game installation and launch are not authorized by this task.

The game loads ready navigation. Import, body measurements and quality
reports belong to EIRebuild64NavBake. No content fingerprint or stale-input
comparison exists. Refresh maps on mapper save, on release with --all, and
on model replacement with --model NAME. The mapper integration is separate.

    EIRebuild64NavBake --game-root DIR --mod DIR --map zone1 --mob zone1 --output-root DIR
    EIRebuild64NavBake --game-root DIR --mod DIR --all --output-root DIR
    EIRebuild64NavBake --game-root DIR --mod DIR --model samvlrmp01 --output-root DIR

--map, --all and --model are exclusive; --mob requires --map. --model selects
registered primary MOBs using the model. Script-spawned objects are dynamic.
Exit: 0 complete, 2 command error, 3 setup error, 4 import/build/data error,
5 missing input. Consume BAKE_DONE, QUALITY, HERO_START and SOURCES. No map
is a passed pilot merely because the executable completed compilation.

## Authoring

Optional Maps/<mpr>.navsrc is UTF-8 JSON version 1:

    {"version":1,"dynamic":[10000001],"static":[],"walkable":[{"id":123,"part":"base"}],"doors":[{"id":456,"min":[10,20,0],"max":[11,21,3],"open_state":1}]}

Static/dynamic IDs cannot overlap. Part names are canonical lower case.
Explicit static IDs override automatic script classification. Literal NIDs
in RemoveUnitFromServer/SetCP become dynamic; variable references require
authoring. A door aperture is a world-space box; open_state is the actual
lever state. Authored apertures override automatic MOB isDoor recognition.
An automatically recognized door without a solid panel in its authored
state needs an explicit aperture. Ordinary switches remain TileCache boxes.

## Geometry and live changes

Non-dynamic trees/stones/buildings are static; items, traps, stuff, torches
and ordinary levers are dynamic. New AddMob objects absent from the static
catalogue are dynamic. Static obstacles use FIG triangles, BON and rotation;
BASE/authored parts are walkable. Solid FIG faces stay non-walkable for
levitation too. Dynamic obstacles use oriented boxes of live solid parts.

Recast parameters: climb 0.35 m, slope 40 degrees, flight slope 60 degrees;
no legacy elevation multipliers or NavigationClimb override. Material/water
policy is imported independently. Eight profiles and four pose permissions
remain; complete AI-class policy at a common radius is still open.

Recast v1.6.0 source is unchanged. RC_MAX_LAYERS_DEF=255 uses its supported
compile-time parameter: monotone region IDs are bytes, and the previous
63-entry overlap list could overflow with only four vertical floors.
The 64-cell tiles are 6.4 m wide (cell size 0.1 m, cell height 0.05 m).
Layer headers include actual portal-adjusted height grids. LayerCodec
consumes three compressed grids and accepts Detour's four-grid capacity.

Removed static NIDs enqueue only intersecting tile/profile jobs. A worker
rebuilds from retained, attributed triangles, excluding absent objects;
the simulation thread replaces completed tiles and readdresses live
obstacle layer references. Work is serialized and stale jobs are discarded.
No source import or body measurement runs in the game. The 2 ms application
budget has not been verified or established as a hard bound.

Door areas use material 15. State changes update polygon flags. TileCache
rebuilds restore clearance flags and the current door state. The actual
closed/open geometry and queues still require gameplay acceptance.

## Wire format

- <mpr>--<mob>.<profile>.nav64: container 6, algorithm 13, Recast 0x010600,
  DT_POLYREF64; names/profile; ready shuffled/LZMS tiles; surface schema;
  losslessly compressed multi-level TileCache layers. The whole RLE layer
  section is additionally compressed with LZMS; runtime RLE is unchanged.
- <mpr>--<mob>.navworld: JSON version 2, baked static NIDs.
- <mpr>--<mob>.navgeom: compressed shared source section NSRC version 1,
  original bounds, triangles with object ID and surface ID, class areas,
  water depths, door apertures and reserved off-mesh connection records.
- Surface 1 is terrain. BASE surface IDs encode (unsigned NID << 32) |
  (figure index + 2). Queries resolve exact imported BASE triangle height.
  Server placement/step/restore preserve the surface. Complete client floor
  selection remains open; the current client still uses its world support.
- Off-mesh records hold both endpoints, radius, endpoint surface IDs and
  flags; none are emitted or connected in the current importer.
- bodies.navbody: BODY version 1, algorithm 13, model/constitution, radius,
  profile and pose heights. Effective un*.mod/un*.lnk resources are covered.

Runtime cache state and Detour references are not saved. Existing UInt64
navigation addresses now admit attributed BASE IDs, validated on restore.
CSV losses/placements are private release-data reports, not runtime inputs.

## Remaining acceptance

HERO_START must advance by 0.02 m through the shared query code; the baker
fails if it does not. QUALITY retains total gained_water_m2 and separately
reports gained_forbidden_water_m2 using the original SetWater depth limit.
water-gains.csv records every gained water cell, its limit, surface and
legacy rejection stage (cell policy, cell expansion, gradient or border).
The old mask explanation is read-only and does not supply Recast parameters.
connections.csv uses the named object surface and the class clearance.

Exact narrow-passage sites, classification of remaining site losses,
all spawn/random-goal paths, class policy, runtime timing,
save/load, door crowds and complete client floor selection remain open.
All-map baking and handoff are gated on the full zone1 pilot. No runtime
performance or gameplay result is inferred from a successful build.
