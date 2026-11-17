# Decorative stone debris (JOLTFX01)

Jolt 5.3.0 handles client-only stone chips from fresh `PTID_FIREBLAST` events.
Fireball and explosive shot already share this effect. Damage, projectile travel,
navigation and network/save formats are unchanged.

The source must be within 0.35–2 metres (bounded by the effect radius) of stone,
rock or high-rock base terrain. Water/lava/swamp, Astral, upper layers and known
bridges are not impact sources. No static-object, actor or debris/debris collision
is introduced. Terrain is the source mesh, not camera LOD or shader cliff expansion.
This first stage is intended for open terrain; walls/trees/boulders are not colliders.

Physics owns up to 128 bodies and 16 local terrain patches. Adjacent effects may
have overlapping patches; per-patch collision layers prevent duplicate contacts.
Shapes have six vertices and eight facets, shared by the physical and render shapes.
Eight to twelve fragments, 3.6–12 cm across, live for 3–5 game seconds, fading in
their last 0.6 seconds. A patch extends 12 m from its centre; outlying fragments
retire before its boundary. Shapes and patches are released when no longer needed.

Each 55 ms client tick advances four 13.75 ms physics steps. Samples interpolate
all four steps; drawing never advances simulation. Combat pause freezes age and
poses. No wall-clock catch-up exists. Zone reset destroys all physical state.
Events are keyed by effect ID/start tick, consumed during initial sync, and stale
or repeated visible incarnations are ignored. Cosmetic randomness is private.

The render bridge queues four cached meshes through the existing native figure
path. It uses world lighting and depth, no own shadows, and draws before water
and visibility-fog overlays. Alpha fade uses ordered transparent draws. Source
visibility remains the server's effect admission; border fog is also respected.

Files: `PhysicsFxCore` (Jolt and event gate), `PhysicsFxTerrain` (source geometry),
`PhysicsFxClient` (game boundary), `OwnedRender/E1PhysicsDebris` (native meshes).
`CWorldClient` owns lifecycle; `CEffectParticle` submits the existing blast event.
`E1PhysicsTerrainSource` provides immutable source values outside render-frame scope.

Checks:

```powershell
powershell -NoProfile -File tools/check-physics-fx.ps1 -OutputDirectory <task>/checks/cpu
powershell -NoProfile -File tools/build-task.ps1
powershell -NoProfile -File tools/check-physics-fx-render.ps1 -OutputDirectory <task>/checks/gpu
```

CPU checks cover event replay, source materials/topology, pause, framerate independence,
CCD/contacts, capacity, cleanup and mesh transforms. Render checks use production
Jolt trajectories and native submissions on D3D11 WARP and D3D12 hardware, recording
pixels at several moments. These checks do not replace real spell casts, saved-game
loading, pause/menu/speed regression and performance acceptance in the game.

The `PHYSICS_FX/stone_debris` journal event reports counts, source preparation and
simulation milliseconds; it is diagnostic data, not a gameplay event. Journal
outputs stay in the private task directory. No persistent player option is added.

## Validation, 2026-11-17

CPU integration checks passed, including a 128-body slope/CCD run (local synthetic
p95: 0.9399 ms per 55 ms game tick). Native D3D11 WARP and D3D12 hardware checks
passed at ticks 0, 6, 18, 40 and 60; captured frames were visually inspected.
The render fixture uses directed light to expose facet shading. Release x64 kit
was built and its manifest produced. These are standalone results, not in-game
acceptance. The available MCP launcher only starts the installed executable;
no candidate game session or installation was performed. Real spells, menu/background
pause, save/load, zone transitions and whole-game performance remain unverified.
