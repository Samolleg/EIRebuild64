# Decorative stone debris (PhysX 5 GPU)

PhysX 5.6.1 GPU handles client-only stone chips from fresh `PTID_FIREBLAST` events.
Fireball and explosive shot already share this effect. Damage, projectile travel,
navigation and network/save formats are unchanged.

The source must be within 0.35–2 metres (bounded by the effect radius) of stone,
rock or high-rock base terrain. Natural grass/soil/snow/dry-grass cover also emits
stone on source triangles steeper than the 40-degree walk limit: the base sound
type alone does not describe a rocky cliff. Flat cover, sand and ice do not gain
stone emission. This geometric decision is independent of graphics settings.
Water/lava/swamp, Astral, upper layers and known
bridges are not impact sources. No static-object, actor or debris/debris collision
is introduced. Terrain is the source mesh, not camera LOD or shader cliff expansion.
This first stage is intended for open terrain; walls/trees/boulders are not colliders.

Physics owns up to 128 bodies and 16 local terrain patches. Adjacent effects may
have overlapping patches; per-patch collision layers prevent duplicate contacts.
Shapes have six vertices and eight facets, shared by the physical and render shapes.
Forty-eight to sixty-four fragments: 5–12 cm chips and 6–8 larger 16–26 cm chunks, live for 3–5 game seconds, fading in
their last 0.6 seconds. A patch extends 12 m from its centre; outlying fragments
retire before its boundary. Shapes and patches are released when no longer needed.

Each 55 ms of game time advances the GPU debris scene once. The worker publishes
both solved endpoints. Drawing interpolates a short history of completed results
on the render clock, including when the worker finishes between client ticks.
Combat pause freezes age and
poses. No wall-clock catch-up exists. Zone reset destroys all physical state.
Events are keyed by effect ID/start tick, consumed during initial sync, and stale
or repeated visible incarnations are ignored. Cosmetic randomness is private.

The fragments sample the game's `stones00` rock atlas through the normal texture
manager/provider order. Every facet has separate inset UVs into one of its four
cells. The texture is retained with the physics client and its completed surface
publication is pinned for the render frame; world reset releases it. No shader
changes, new texture files or particle-option switches are required. Missing art
is logged as `texture_unavailable`; fragments are not drawn as untextured stand-ins.

The render bridge queues four cached meshes through the existing native figure
path. It uses world lighting and depth, no own shadows, and draws before water
and visibility-fog overlays. Alpha fade uses ordered transparent draws. Source
visibility remains the server's effect admission; border fog is also respected.

Files: `PhysicsFxCore.h` / `PhysicsFxPhysX.cpp` (GPU simulation and event gate), `PhysicsFxTerrain` (source geometry),
`PhysicsFxClient` (game boundary), `OwnedRender/E1PhysicsDebris` (native meshes).
`CWorldClient` owns lifecycle; `CEffectParticle` submits the existing blast event.
`E1PhysicsTerrainSource` provides immutable source values outside render-frame scope.

Checks:

```powershell
powershell -NoProfile -File tools/check-physics-fx.ps1 -OutputDirectory <task>/checks/core
powershell -NoProfile -File tools/build-task.ps1 -PhysicsFxCheck
```

GPU correctness checks cover event replay, source materials/topology, pause, framerate independence,
CCD/contacts, capacity, cleanup and mesh transforms. Render checks use production
PhysX GPU trajectories and native submissions on D3D11 WARP and D3D12 hardware, recording
pixels at several moments. These checks do not replace real spell casts, saved-game
loading, pause/menu/speed regression and performance acceptance in the game.

The `PHYSICS_FX/stone_debris` journal event reports counts, source preparation and
simulation milliseconds; it is diagnostic data, not a gameplay event. Journal
outputs stay in the private task directory. No persistent player option is added.

## GPU execution and availability

GPU dynamics, broadphase and GPU cooking are mandatory. Every convex hull is
checked for GPU compatibility. Speculative CCD protects small fragments; scene
pair buffers cover the full 128-body/16-patch budget before collision filtering.
The NVIDIA GPU runtime loads only from beside the executable. Context/scene setup
and SDK warnings/errors are checked. An unavailable GPU disables this decorative
effect and emits `PHYSICS_FX/gpu_unavailable`; it never switches to CPU simulation.
`gpu_ready` records the device; `stone_debris` includes GPU flags, heap/solver bytes
and contact count. CPU still extracts/cooks local terrain, queues events and reads
poses for the existing renderer. There is no CUDA/D3D direct rendering interop.

The GPU context is prepared when the zone terrain becomes available. All state is
released on zone reset. Dependency provenance and reproducible offline build are
in `tools/physx/README.md`. No new settings or player switches are introduced.

## Validation status

The standalone GPU check passed on NVIDIA GeForce RTX 4070 SUPER, including
GPU contact/solver/broadphase allocations, 128 bodies on a slope, pause, event
admission, framerate independence, expiry and runtime recreation (147 checks).
The separate missing-runtime process passed and created no CPU bodies. Native
D3D11 WARP and D3D12 hardware checks passed at ticks 0, 6, 18, 40 and 60; captured
frames were visually inspected. Release x64 kit and static CRT imports passed.
In-game fireball impacts also produced GPU-simulated fragments on rock; pause and
expiry were exercised on D3D11 and D3D12. The revealed scorch/terrain mismatch is
fixed as described in `terrain-prints.md`. These checks are distinct from owner
gameplay/visual acceptance.
