# Nala breast secondary motion

The world adapter enables presentation-only PhysX 5.6.1 GPU motion for the main
hero when the selected GLB contains the existing `PHYSICS_Pecho.L/R` branches
(including `.001` and `.002`). No model, weights, animation clips, save format or
combat state are changed. Private model previews retain the authored animation.

Two 0.35 kg dynamic bodies follow kinematic animated targets through D6 joints.
Each translation drive uses stiffness 160 N/m and damping 6 Ns/m; rotation is
locked and the combined translation limit is 0.025 m. Rest gravity is zero:
the authored shape already supplies the resting appearance. This is bounded
secondary motion, not a soft-body tissue or cloth simulation. There are no
body/arm/armor collision shapes and no material-specific armor stiffness.

The actor owns a separate GPU scene sharing the existing PhysX foundation,
CUDA context and dispatcher with debris. Fixed 1/120 s steps interpolate the
animated targets. World time is supplied by the native completed-visual seam;
combat pause and duplicate samples do not advance simulation. Rewind, explicit
seek, gaps over 0.25 s and attachment jumps over 0.5 m reset momentum. Destruction
of the retained actor releases its scene. Missing CUDA/runtime errors emit
`nala_physx_unavailable` and leave the ordinary animated pose; no CPU simulator
is substituted. Successful initialization emits `nala_physx_ready`.

`gltf_breast.h` samples only the required ancestor chains. It offsets the six
existing physics roots and their descendants, retaining all animated linear
transforms. In the installed Nala export these are 24 nodes, including affine
helper nodes; the base and tracker branches remain unchanged. The sparse
snapshot is keyed by animation, placement, game time and the legacy palette
base, so changing equipment on pause cannot reuse obsolete joint indices.

D3D11 and D3D12 upload these final world matrices after `nativePoseCS` and before
surface preparation. GPU animation remains shared and unchanged. Body,
weighted clothing, depth/shadows and ray geometry consume the same pose palette.
The overall equipment envelope includes the displacement allowance. Rigid
weapons and other joints are unaffected. No shader code or GPU record layout
changes are required.

Validation entry points:

- `tools/check-physics-fx.ps1 -OutputDirectory <task/check>`: actual GPU spring
  response, independent sides, limit, settling, pause, reset/lifetime, missing
  GPU DLL, and the existing debris regressions.
- `tools/build-task.ps1 -PhysicsFxCheck -ShaderResourceManifest <manifest>`:
  builds the runtime and native render harness with pinned accepted shaders.
  `check-nala-physics.inl` decodes the installed GLB, checks animated sparse
  matrices, localized weighted deformation, duplicate samples, pause/resume
  and equipment palette remapping. The existing render harness exercises
  native D3D11/D3D12 debris rendering; it does not by itself prove the Nala
  world-render integration or artistic acceptance.
- A background game run is required to inspect the actual Nala world path.
  Gameplay logs and screenshots belong to task evidence, never this repository.
