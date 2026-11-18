# Nala breast secondary motion

The world adapter enables presentation-only PhysX 5.13.0 GPU motion for the main
hero when the selected GLB contains the existing `PHYSICS_Pecho.L/R` branches
(including `.001` and `.002`). No model, weights, animation clips, save format or
combat state are changed. Private model previews retain the authored animation.

Two 0.45 kg dynamic bodies track the animated outer breast points in world
space under gravity (9.81 m/s²). The 55 N/m springs and 3.6 Ns/m damping
allow settling and inertial lag. The 8.5 cm travel budget includes the static
load. The spring preload sets the static drop to 4.1 cm. Initialization and teleports seed the gravity-loaded equilibrium.

The actor owns a separate GPU scene sharing the existing PhysX foundation,
CUDA context and dispatcher with debris. At up to 60 Hz it advances to the latest
animated targets in one GPU submission using the elapsed game time. The renderer
samples two timestamped results one 60 Hz frame behind its clock and may project
their motion forward by at most one such frame, within the travel limit.
World time is supplied by the native completed-visual seam;
combat pause and duplicate samples do not advance simulation. Rewind, explicit
seek, gaps over 0.25 s and attachment jumps over 0.5 m reset momentum. Destruction
of the retained actor releases its scene. Missing CUDA/runtime errors emit
`nala_physx_unavailable` and leave the ordinary animated pose; no CPU simulator
is substituted. Successful initialization emits `nala_physx_ready`.

`gltf_breast.h` samples only the required ancestor chains. It rotates each side around its animated
PeChoBase attachment towards the displaced outer point. Axial compression and
extension are limited to 15%. The same affine bend applies to the three branches
and their descendants. Skin weights blend the anchored torso into the moving
surface. Base and tracker bones keep the authored pose. The sparse
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
