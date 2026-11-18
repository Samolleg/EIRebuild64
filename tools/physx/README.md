# PhysX 5.6.1 GPU dependency

Official tag `107.3-physx-5.6.1`, commit `5ca9f472105a90d70d957c243cb0ef36fe251a9f`.
Source archive SHA256 `b20b5c497ae51e6556519323a999447adf1c24a8b6265ecaee23ac40fb62be12`.

Run `fetch-dependencies.ps1 -ProjectRoot C:\Dev\EvilIslands` explicitly to fetch
the pinned PhysX source and CUDA 12.8.1 redistributable compiler components. This
does not install a system toolkit or driver. SHA256 and original download URLs
are in that script. `build.ps1` and normal host builds never use the network.

`build.ps1` uses VS 2022 v143, Ninja/CMake, static CRT and Release x64. GPU and host
libraries come from the same source. All upstream default CUDA architectures are
retained. Only build-system patches disable unused GLUT copying and bound nvcc's
internal workers to two. Source is retained in `deps/physx/5.6.1/source`; its build
copy is separate. Output SDK contains includes, six static host libraries, GPU DLL
and PDB, original licenses, recipe hash and a SHA256 manifest checked on reuse.
Public CMake names the GPU output `PhysXGpu.dll`; packaging names the exact same
binary `PhysXGpu_64.dll`, as required by the SDK's compiled loader.

`prepare-worktree.ps1` verifies/builds the local SDK and copies it to ignored
`dependencies/physx/5.6.1`. `PhysX.targets` links the host, copies the GPU runtime
beside the executable, and includes notices. `build-task.ps1` packages DLL/PDB
and checks static CRT imports. CUDA Toolkit is not needed by the player: the
GPU runtime needs a compatible NVIDIA GPU (Volta/SM 7.0 or later) and CUDA driver.

At runtime the application first loads `nvcuda.dll` from System32 and the exact
GPU DLL beside its executable, verifies module path and exports, then creates a
CUDA context and a GPU dynamics / GPU broadphase scene. No CPU fallback is used.
Scene or runtime errors stop this decorative subsystem with a journal diagnostic.
CPU work still includes terrain extraction/cooking, event handling, SDK scheduling,
and reading poses for the existing D3D11/D3D12 mesh renderer. The rigid-body solver,
broadphase and GPU-compatible convex/triangle contacts run on CUDA.

Correctness checks: `tools/check-physics-fx.ps1` exercises the actual GPU, including
contact-buffer/solver allocations, lifecycle and a separate missing-DLL process.
`tools/build-task.ps1 -PhysicsFxCheck` builds and executes native render checks
under the same build-cache lock. These are not comparative benchmarks.
