# X64D3D12 embedded target operations, stage 15 (2026-11-11)

Incomplete runtime integration. renderer_backend=1 still creates D3D11.
CDX11 BeginFrame, RefreshOwnedTargets and ClearColor/Z now use renderer-owned
target operations, instead of borrowing a D3D11 device/context. D3D11 keeps
the same target binding, viewport and clear calls. The new synchronous seams
drain pending scene work before accessing current target state.

D3D12 draws retain their own target bindings (world history/MRTs, hit-test
and popup targets must not be overwritten by a generic host bind). Clears
use the author's executor background/full-rectangle operations; the executor
clips to the actual active target and preserves the requested depth/stencil.
They deliberately do not call legacy Clear, which can composite the world.
Executor failures use the prepared embedded journal/player error path.
CaptureC56MaterialState reads its viewport through Renderer too; D3D11 keeps
RSGetViewports, while D3D12 uses the current world/UI pixel dimensions.
No author HLSL, rendering algorithms, SDKs or runtime DLL lists changed.

Activation still requires CDX11 InitializeDX's D3D11 states and device/output
leases, source texture/SRV adapters and screenshot/capture boundaries to be
adapted. Then latch renderer_backend before device creation, call the prepared
ensure_embedded_d3d12, enable readiness/begin_scene and local simulation/render
boundaries together with world/SR/FG/present dispatch and removal reporting.
Do not substitute a D3D11 device or set core_host_. Host transport adaptation
remains Source/embedded_d3d12.{h,cpp}. Full BRIEF remains open.
No compilation, shader compilation, tests or game run was performed.
Build/link, D3D11 regression and D3D12 GPU behavior remain unverified.

# X64D3D12 native startup preparation, stage 14 (2026-11-11)

Incomplete runtime integration. renderer_backend=1 still creates D3D11.
Prepared Renderer::ensure_embedded_d3d12 adapts ensure_host_d3d12 to the
local executor/NGX, native game window/input and embedded sky. It initializes
the native pipelines, neighbour preparer, settings, telemetry and SR caps,
with a journal event and player error message on startup failure. It never
creates a D3D11 device or sets core_host_. It is not yet called by ensure().

DebugOverlay now accepts the executor's real DXGI adapter without a D3D11
device/context when its external OverlayRenderer is supplied. First-run
recommendations and video-memory budgets retain the author's algorithms;
the normal native window subclass/input ownership stays in the game.
RenderProfiler initialization, begin_frame and resolve now accept its GPU
timestamps without a D3D11 context. Existing D3D11 callers are unchanged.
No HLSL, rendering algorithms, SDKs, DLL lists or pose/SKN changes this stage.

Concrete activation dependency: Game/dx11/RendererDX11.cpp::InitializeDX
unconditionally calls VisitDevice/VisitOutput after ensure, then creates
D3D11 states. BeginFrame, RefreshOwnedTargets, ClearColor/Z and screenshot
also require those leases; CaptureC56MaterialState reads a D3D11 viewport.
The source texture/surface adapters still use D3D11 device/context/SRVs.
Switching ensure to the local executor alone therefore fails before the menu.
Next adapt those source GPU boundaries to typed renderer operations/resources,
then load/latch renderer_backend before device creation and call the prepared
startup; enable ready/begin_scene, composite/present dispatch and local
simulation/render boundaries together. Add explicit runtime/removal errors.
Do not route the source through core_host_ or a substitute D3D11 device.
Host transport adaptation remains Source/embedded_d3d12.{h,cpp}.
No compilation, shader compilation, tests or game run was performed.
Build/link, initial/repeated startup and GPU behavior remain unverified.

# X64D3D12 executor retirement boundary, stage 13 (2026-11-11)

Incomplete runtime integration. renderer_backend=1 still creates D3D11.
Renderer::release now detaches its borrowed overlay-renderer and profiler
timestamp interfaces after their shutdown, while the executor is alive.
The remaining CritterState GPU meshes, skins, instance/bone buffers and
ready/failure gates retire with the other D3D12 subsystem owners. Its
value-only asset/shader futures join when the state drops; neither worker
captures Renderer or the executor. A map-only critter release would keep
the immutable GPU objects and ready gate, so it is insufficient here.

After renderer resource retirement, release clears the raw d3d12_ dispatch
pointer and shuts down EmbeddedD3D12. NGX has already stopped, scene and
neighbour workers have joined, and overlay/profiler shutdown has finished.
Executor teardown waits pipelines/GPU work, drains presenter/pacer, retires
Reflex and invalidates the shared resource owner. External snapshots and
preparers can still release their old allocations; their session identities
prevent submission to a replacement executor. D3D11 and author HLSL and
rendering algorithms are unchanged. No new SDKs or runtime DLLs this stage.

Next: bind startup configuration/backend selection before D3D11 creation,
initialize overlay and profiler from the native executor without requiring
a D3D11 device/context, then wire local simulation/render boundaries and
world/SR/FG/present dispatch. Audit embedded facade readiness/device gates
alongside those entry points. Preserve local window/input ownership rather
than setting core_host_. Startup and device-removal failures still need
explicit journal/player messages without a D3D11 fallback. Host transport
adaptation remains Source/embedded_d3d12.{h,cpp}; full BRIEF remains open.
No compilation, shader compilation, tests or game run was performed.
Build/link, repeated device lifetimes and GPU behavior remain unverified.
Earlier sections describe preceding stages, not the current shutdown state.

# X64D3D12 prepared uploads and resource submission ownership, stage 12 (2026-11-11)

Incomplete runtime integration. renderer_backend=1 still creates D3D11.
Preparer and all three preparation paths now retain the executor's shared
session identity. finish_buffer/finish_texture reject uploads from another
session with DXGI_ERROR_DEVICE_RESET before recording commands or consuming
the prepared resource, including workers surviving the original executor.

The executor validates resource ownership before slot/index/target binding,
buffer/texture writes and copies, indirect draws/dispatches, clears, mip
creation, capture and readback. Null bindings still unbind normally. Invalid
resource submission latches a named backend failure; size/format inspection
remains non-fatal. Bindless validation precedes even a cached-index return.
External NGX/FSR recording validates every texture before opening a command
list or transitioning any resource. UI validates all supplied textures before
uploading or drawing. These HRESULT interfaces reject obsolete resources
with DXGI_ERROR_DEVICE_RESET; malformed arrays return E_INVALIDARG.
This stage changes no HLSL, rendering algorithms, D3D11, SDKs or DLL lists.

The executor still stays alive in Renderer::release(). Next: finish auditing
retained raw pointer/worker lifetimes at shutdown, detach d3d12_ and retire
EmbeddedD3D12 after all renderer clients. Then bind startup configuration,
backend/overlay, simulation/render boundaries and world/present dispatch,
with explicit startup/device-removal failures. Host transport adaptation
remains in Source/embedded_d3d12.{h,cpp}; full BRIEF remains open.
No compilation, shader compilation, tests or game run was performed.
Build/link and GPU behavior remain unverified. Earlier stages follow.

# X64D3D12 surface ownership and remaining subsystem retirement, stage 11 (2026-11-11)

Incomplete runtime integration. renderer_backend=1 still creates D3D11.
The executor's texture_info and texture_matches now require the allocation's
shared resource owner to match this executor, in addition to the existing
shape checks. Owner identity survives allocator/address reuse and lets an
external SurfaceImage outlive device teardown without passing a later
executor's upload-generation hit. Failed texture_info clears its outputs.

Snapshot upload validates ownership before the single-level fast path. An
obsolete pixel-free snapshot can rebuild only through the surface table's
exact source generation. Missing source bytes fail explicitly. GPU-captured
content whose allocation no longer matches returns DXGI_ERROR_DEVICE_RESET
instead of uploading stale CPU pixels. The BC1 terrain path with missing
bytes now uses the same validated upload before retaining the texture.

Embedded release() drops surface-table GPU references and upload shapes,
retaining source bytes and GPU-capture markers. It resets the entire impostor
atlas, bake targets, slice identities and failure state. D3D12 fog, water
reflection and cave allocations now retire beside the shared validity and
capacity resets already performed by their common release functions. This
stage adds no SDKs, DLLs, HLSL or rendering algorithms. D3D11 is unchanged.

The executor still stays alive. Before shutting it down, finish the remaining
lifetime audit, including prepared uploads and external resource submission
sites that bypass surface upload. Then bind startup configuration, backend,
overlay, local simulation/render boundaries and world/present dispatch with
explicit startup/device-removal errors. Host transport adaptation remains
in Source/embedded_d3d12.{h,cpp}, with Renderer integration still incomplete.
Full BRIEF remains open. No compilation, shader compilation, tests or game
run was performed. Build/link and GPU behavior remain unverified.
Earlier sections record the preceding stages.

# X64D3D12 map/effect cache retirement, stage 10 (2026-11-11)

Incomplete runtime integration. renderer_backend=1 still creates D3D11.
Embedded release() now retires ambient and ground-cover device owners through
release_ground_cover/release_ambient_world after the final native flush.
This also invalidates the same-map CPU identity gates, joins cover shader
preparation and releases active/neighbour contact maps and terrain2 heights.
The remaining cover terrain2 class/color resources, terrain2 frame table,
cache parameter buffer, snow surface and their validity/revision gates are
cleared locally. Liquid maps/waves retire after their workers have joined;
the existing generation/serial gates force uploads on a replacement device.
Saved loading pictures and their hold state retire at this boundary too.
The executor stays alive. These additions run only in the embedded D3D12
release branch. No HLSL, rendering algorithms, SDKs or DLLs changed here.

Remaining lifetime work includes external SurfaceImage/snapshot reuse,
core surface tables, impostor/device caches and any remaining subsystem
owners. External textures can survive teardown for release, but the current
surface upload fast paths still lack a replacement-device ownership check.
Resolve those before executor shutdown and backend activation. Then bind
startup configuration, backend/overlay, local simulation/render boundaries
and world/present dispatch, with explicit startup/device-removal failures.
Full BRIEF remains open. No compilation, shader compilation, tests or game
run was performed. Build/link and GPU behavior remain unverified.
Earlier sections record the preceding stages.

# X64D3D12 native resource retirement, stage 9 (2026-11-11)

Incomplete runtime integration: renderer_backend=1 still creates D3D11.
Embedded release() now clears the complete NativeD3D12 buffer/texture owner,
its ready/failed gates, layer upload revision and instance capacity after
scene join, the final native flush and pose/instance retirement. Geometry
pages and instance records already retire earlier in release(). Terrain
page textures and prepared uploads, their retained CPU terrain owner, the
bindless terrain-frame staging and scene mip copies now retire too.
Material identities are retained, but their GPU profiles and fixed-unit or
composition serial gates are reset and profile indices invalidated so the
next device rebuilds them even without a source composition change.
The executor remains alive through all these releases. D3D11, upstream Host,
HLSL and pose/SKN algorithms are unchanged. No SDKs or DLLs added this stage.

This completes only native/terrain-page/material/scene-mip cache retirement.
Other owners still need an audit: ambient/cover/contact maps and terrain2,
liquids, loading-picture copies, external SurfaceImage/snapshot resources
and their reuse across replacement devices. Do not shut down the executor
or activate backend selection before those lifetimes are resolved. Then
bind startup config/backend, overlay, local frame boundaries and dispatch,
with explicit startup/device-removal failures. Full BRIEF remains open.
No compilation, shader compilation, tests or game run; build and GPU behavior
remain unverified. Earlier sections describe the preceding stages.

# X64D3D12 frame-image retirement, stage 8 (2026-11-11)

Incomplete runtime integration: renderer_backend=1 still creates D3D11.
Embedded release() now retires its D3D12 world, postprocess, SR, AO,
shadow-history, prepass, hit-test, particle-depth, gamma and HUD-less images.
It clears cached FG capabilities, device-specific failure reasons, pacing
statistics and temporal validity, including first-Present deferral, so a
replacement device cannot inherit those values. Retirement follows scene
join, NGX shutdown, the final native flush and overlay/profiler teardown;
the executor remains alive. The D3D11 and upstream Host paths are unchanged.

This is only frame-image retirement, not complete executor shutdown. Native
buffers/instances, terrain/prepared uploads, surface and subsystem caches
still require a device-lifetime audit and retirement before owner shutdown.
Next finish those owners, then bind startup config/backend, overlay, frame
boundaries and dispatch with explicit startup/device-removal errors. Backend
activation and full BRIEF completion remain open. No new SDKs or DLLs.
No compilation, shader compilation, tests or game run; build and GPU behavior
remain unverified. Earlier sections document the preceding stages.

# X64D3D12 local renderer SR/FG/present bodies, stage 7 (2026-11-11)

Incomplete runtime integration: renderer_backend=1 still creates D3D11.
Renderer now contains the local EmbeddedD3D12 owner, declared before GPU
members so its executor outlives their destruction. release() stops local
NGX after the scene thread joins, before releasing renderer resources.
D3D11 retains its existing separate NGX owner and optimal-size calls.

The author's composite_world_d3d12, upscaler-mode changes, DLSS evaluation,
DLSS-G/FSR FG preparation, HUD-less capture and present_frame_d3d12 bodies
are now included in the embedded runtime translation units. The D3D12 NGX
calls in these bodies and DLSS render-size selection go directly to the
local owner, using the author's value descriptors; no Host transport or
remote stubs are called. Embedded first-Present deferral records a local
successful executor present, not the absent Host's presented flag. Embedded
SR/FG diagnostics refer to the engine journal. HLSL and rendering algorithms
are unchanged. The shader generator still excludes runtime bodies.

This does not activate those bodies: embedded ensure(), composite_world()
and present_frame() still retain the old startup/dispatch guards. Next bind
configuration before device creation, initialize the owner and overlay,
finish resize/resource retirement for repeated release/ensure, wire the
local simulation/render boundaries and enable dispatch with explicit
startup/device-removal errors. Do not set core_host_ blindly: source frame
and overlay/input ownership differ from Host/core_host.cpp. No claim of a
working D3D12 or frame generation backend is made; task remains open.
No additional libraries or DLLs beyond stage 2. No compilation, shader
compilation, tests or game run; build/link and GPU behavior are unverified.

# X64D3D12 local simulation/render boundaries, stage 6 (2026-11-11)

Incomplete runtime integration: renderer_backend=1 still creates D3D11.
EmbeddedD3D12 now supplies serialized begin_simulation / begin_render /
finish_render boundaries adapted from upstream d17a6149 Host/core_host.cpp.
Sleep runs before local input sampling; the simulation QPC and increasing
serial reach the existing executor/pacer via Reflex::set_frame.
Simulation/end and render/start replace taking a Host present record;
finish_render closes failed or omitted presents as well as shown frames.
The executor retains author render/end deduplication, present markers and
asynchronous real-image latency sampling. No IPC release delay is copied.
These APIs are not yet called by Renderer. D3D11 behavior is unchanged.
Next: bind Renderer ownership/config/backend selection and these boundaries,
then enable world/SR/FG/present bodies with local NGX and explicit failures.
No new SDKs, DLLs, HLSL or algorithms. No compilation, tests or game run;
full D3D12/FG integration and GPU behavior remain unverified and task open.

# X64D3D12 Reflex device retirement, stage 5 (2026-11-11)

Incomplete runtime integration. renderer_backend=1 still creates D3D11.
The embedded executor now retires the author's process-global Reflex device
at teardown, after presenter destruction and pacer join, while the D3D12
device is alive. The borrowed device, applied mode, frame markers and latency
samples are cleared. Successful process-level NVAPI initialization survives;
a replacement device can retry SetSleepMode after an earlier GPU rejected it.
An executor that never configured Reflex cannot clear a different owner.
This is required for repeated embedded Renderer/device lifetimes. D3D11 and
the separate upstream Host target retain their existing code paths.
No SDK, runtime DLL, HLSL or rendering algorithm changed in this stage.

Host transport remains unbound at Renderer. Continue with local frame and
simulation boundaries, Renderer ownership/teardown, configuration before device
creation, the excluded world/SR/FG/present bodies and explicit failure messages.
Stages 2/3 contain the prepared SDK/DXIL/NGX/executor inputs for that work.
The task remains open. Compilation, shaders, tests and game were not run;
GPU teardown and the complete requested D3D12/FG behavior remain unverified.

# X64D3D12 external resource retirement, stage 4 (2026-11-11)

Incomplete runtime integration. renderer_backend=1 still enters D3D11 and
frame generation remains unavailable. This stage changes resource ownership
in Host/d3d12/d3d12_frame.cpp, without changing the author's HLSL or rendering
algorithms. D3D11 code and dependencies are unchanged in this stage.

All six texture creation/adoption sites and the common buffer adoption site
now attach a shared retirement owner. It serializes texture/buffer destruction
with executor teardown. The executor drains GPU work, presenter and pacer,
then clears the borrowed Core before its members are destroyed. A surviving
SurfaceImage/snapshot can subsequently release its COM resources without
calling retired descriptor heaps, Core::release_later or hazard callbacks
belonging to a later executor. Placed textures release their resource before
the backing heap. Frame-thread resource use/release remains the author contract.
This supersedes the stage 3 warning about late snapshot destruction only.

Next stage must instantiate EmbeddedD3D12 in Renderer and integrate config,
backend selection, world/SR/FG bodies, present and local Reflex boundaries.
Renderer teardown must first join the scene/upload callers, stop NGX, retire
overlay/profiler and clear its resource caches. Snapshots that survive executor
shutdown are release-only: old resources must never be reused by a new device.
Startup/removal failures still require explicit journal and player messages.
The existing guards and startup choice have not been activated in this stage.
The task remains open and is not ready for acceptance.

No compilation, shader compilation, tests, preparation or game run was
performed. Compilation/linking, GPU teardown and full runtime integration
remain unverified. The stages below describe historical progress.

# X64D3D12 local executor/NGX ownership, stage 3 (2026-11-11)

Incomplete runtime integration; renderer_backend=1 still enters D3D11.
Source/embedded_d3d12.{h,cpp} now owns the author's frame executor and NGX
worker locally. OwnedRenderer.vcxproj includes the nine runtime executor
translation units, Host/dlss.cpp and Host/reflex.cpp. Diagnostic bring-up
is deliberately not compiled: its separate window is not a game backend.
The new owner is not yet instantiated by Renderer, so adding these archive
objects does not activate D3D12 or make frame generation available.

The owner exposes direct SR/FG capability, optimal-size, evaluate, release
and VRAM queries. Only the author's value descriptors are shared; no ring,
replay, remote globals, process or Host control block is used. Algorithms
and HLSL remain the author's. Host diagnostic call sites route to the
engine journal through remote::host_log in embedded_d3d12.cpp.
The embedded factory adopts the native top-level window as both target and
parent (the author's child-window minimization test otherwise rejects it).

Host/dlss.cpp has an embedded-only D3D12 initializer with the existing
EIRebuild64 ProjectID. It starts NGX directly on the executor device without
creating an intermediate D3D11 instance. DLL search remains beside the EXE;
NGX data goes to LocalAppData/EIRebuild64/NGX, pipeline data to its D3D12
sibling. Path strings/info and the GPU-idle callback remain alive through
NGX shutdown. The worker shuts down before executor/device destruction;
stop_ngx separates NGX retirement from later renderer resource retirement.
The embedded shutdown clears borrowed device pointers for repeated teardown.
Source/dlss_backend.cpp and its existing D3D11 worker are unchanged.
No further dependencies, DLLs or packaging changes beyond stage 2.

Next stage must bind this owner to Renderer, choose renderer_backend before
D3D11 creation, load local overlay/config, enable the excluded D3D12 world,
upscaler and present bodies, replace their x64remote calls with owner calls,
and supply local Reflex frame boundaries. Startup/removal failures still
need engine-log and player-message handling with no D3D11 fallback.
Resource lifetime is a required integration subtask: D3D12Texture and
D3D12Buffer in Host/d3d12/d3d12_frame.cpp contain a borrowed Core pointer
and their destructors call Core::release_later. Renderer-held GPU references,
external SurfaceImage/snapshots and upload jobs must therefore retire before
EmbeddedD3D12::shutdown; merely resetting the executor in release is unsafe.
Do not activate backend selection until this ownership boundary is resolved.
This task is open and is not ready for acceptance.

No compilation, shader compilation, tests, preparation or game run was
performed. Build/link ABI and runtime behavior are unverified. Historical
stage 1/2 status below describes those stages, not the current source list.

# X64D3D12 dependency and DXIL integration, stage 2 (2026-11-11)

Incomplete runtime integration. The coordinator supplied the pinned packages
listed in BRIEF. Local deps now contain FidelityFX SDK 2.3.0, NVIDIA NVAPI
70d337db9186e968eab622f7e786de7e437faf3d, DXC 1.9.2609 and the DLSS-G
v310.9.16 headers/runtime. tools/prepare-worktree.ps1 copies them offline
into dependencies. OwnedRenderer/D3D12Sdk.props supplies includes, imports,
runtime output copies and license copies. Full license texts are retained
in third_party/d3d12. THIRD_PARTY.md records the pins and redistribution.
The permitted build-task.ps1 DLL list now includes nvngx_dlssg.dll and the
two signed FidelityFX DLLs. DXC/DXIL compiler DLLs remain build inputs.

RendererShaders now requests shader_embed --dxil-sources and includes the
author's D3D12-only program list after writing the existing DXBC table.
OwnedRenderer/D3D12Shaders.mak invokes DXC using the generated author rules
and compiles the five Host diagnostic/overlay entry points. Both DXIL
resource tables are linked into EIRebuild64.exe. The shared gamma HLSL and
program descriptors moved byte-for-byte into gamma_shader_programs.h so the
shader producer can enumerate them without compiling frame runtime code.
The author's HLSL, compiler flags and empty-resource failure convention are
preserved. Host/d3d12 sources themselves remain unchanged and unlinked.

Next stage must complete executor/NGX ownership, local frame and config
initialization, backend selection before D3D11 creation, frame boundaries,
resize/teardown, direct SR/FG/Reflex calls and explicit startup/removal
failure reporting. In particular renderer_native_d3d12.cpp still excludes
composite_world_d3d12, apply_upscaler_mode_d3d12 and upscale_world_d3d12 for
embedded callers, and embedded_remote_stubs.cpp still describes transport
absence. Replacing only ensure_host_d3d12 or setting core_host_ would leave
these execution/lifetime seams unresolved. Keep the D3D11 NGX worker owner.
Use the embedded ProjectID from Source/dlss_backend.cpp for D3D12 NGX.
renderer_backend=1 still creates D3D11 and FG remains unavailable until
that stage is finished. This task is open, not ready for acceptance.

No preparation script, compilation, shader compilation, tests or game run
was executed. The build/kit, dependency import ABI and runtime behavior
remain unverified. The sections below describe the earlier stage.

# X64D3D12 source import, stage 1 (2026-11-11)

Incomplete integration. Imported the 21 files under `Host/d3d12/` and
`Host/dlss.{h,cpp}`, `Host/reflex.{h,cpp}` from upstream
`d17a6149e5b5d4c2d23873d18e037bc270269050`, using Git archive from the
read-only reference. All 25 source files retain the upstream bytes.
The existing LICENSE.md and NOTICE.md apply to these author sources.

This stage includes the device/queue/heaps/fences, barriers, upload rings,
pipelines, timestamps, frame executor, presenter, DLSS-G pacer, FidelityFX
FSR 3.1 FG, NGX D3D12 SR/FG, Reflex, diagnostic bring-up and HLSL/resources.
They are staged sources and are not linked into OwnedRenderer yet.
`Source/dxil_table.h` and the shared renderer D3D12 algorithms were already
present. The embedded runtime still creates D3D11 for renderer_backend=1,
and frame generation remains unavailable. BRIEF completion is open.

Local dependency blocker: the project tree has no FidelityFX SDK 2.3.0,
NVAPI SDK or DXC. The installed DLSS SDK v310.9.16 has nvngx_dlss.dll,
but lacks nvsdk_ngx_helpers_dlssg_d3d.h and nvngx_dlssg.dll required by
Host/dlss.cpp. Nothing was downloaded and no runtime DLL or kit was added.
See THIRD_PARTY.md for the exact upstream dependency paths and runtime files.

Continuation requires the embedded Host adaptation and build integration:
- Own the executor through Renderer lifecycle, route renderer_backend before
  D3D11 creation, and report D3D12 startup/removal errors to the engine log
  and player. create_frame_executor returns a reason on failure. Its upstream
  comment about keeping D3D11 must not become an embedded fallback.
- Adapt ensure_host_d3d12, overlay/config initialization, frame boundaries,
  resize and teardown to the native window and existing embedded scene input.
  Host transport has not been adapted in this stage. Do not copy the remote
  Core/Host replay or set core_host_ blindly: the embedded game submits locally.
- Route NGX SR/FG and Reflex calls directly, with engine logging and local
  paths. Replace the imported author's NGX project ID with the existing
  EIRebuild64 identity from Source/dlss_backend.cpp. Keep D3D11 NGX ownership.
- Compile the author's DXIL through shader_embed --dxil-sources and DXC,
  include the D3D12-only program list currently guarded out for embedded,
  and link both DXIL resource tables with adapted resource paths. The raw
  d3d12_shaders.rc currently names the author's build/x64-host/dxil directory.
- Prepare licensed SDK inputs locally and package their required runtime DLLs
  through the permitted build integration. tools/build-task.ps1 is forbidden
  to edit by this BRIEF. Claude performs the full build after integration.

No compilation, tests, gameplay or visual acceptance was performed.
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
