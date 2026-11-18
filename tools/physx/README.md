# PhysX 5.13.0 GPU dependency

Official tag `ovphysx-0.8.0`, commit `7d6f6ac8b83e1610bdf715f1f2f1d6b718aee586`.
Source archive SHA256 `96bceacc0171af106f63f19c7e5e2baa6f318e27e83a76d67fdead5b59cb9720`.

Run `fetch-dependencies.ps1 -ProjectRoot C:\Dev\EvilIslands` explicitly to fetch
the pinned PhysX source and CUDA 13.5.0 redistributable compiler components. This
does not install a system toolkit or driver. SHA256 and original download URLs
are in sdk-lock.json. `build.ps1` and normal host builds never use the network.

`build.ps1` uses VS 2022 v143, Ninja 1.13.2 and CMake 4.5.0, static CRT and Release x64. GPU and host
libraries come from the same source. SM 75, 80, 86, 89, 90, 100 and 120, plus forward-compatible PTX 120, are
retained. The local recipe disables unused GLUT copying and bounds nvcc's
internal workers to two. It passes null for the new CUDA 13 context options to create the regular GPU context. Source is retained in `deps/physx/5.13.0/source`; its build
copy is separate. Output SDK contains includes, five static host libraries (including cooking in PhysXCommon), GPU DLL
and PDB, original licenses, recipe hash and a SHA256 manifest checked on reuse.

`prepare-worktree.ps1` verifies/builds the local SDK and copies it to ignored
`dependencies/physx/5.13.0`. `PhysX.targets` links the host, copies the GPU runtime
beside the executable, and includes notices. `build-task.ps1` packages DLL/PDB
and checks static CRT imports. CUDA Toolkit is not needed by the player: the
GPU runtime needs a compatible NVIDIA GPU (Turing/SM 7.5 or later) and CUDA driver.

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
