meshoptimizer by Arseny Kapoulkine. Used under the MIT licence in LICENSE.md.

Upstream: https://github.com/zeux/meshoptimizer, version 1.1
(MESHOPTIMIZER_VERSION 1010). Copied unmodified from the copy vendored in
GD3D11 (github.com/kirides/GD3D11, D3D11Engine/include/meshoptimizer at
1396ecc) on 2026-10-11. Only the simplifier and its allocator are used: the
renderer builds the reduced index levels of static object meshes with
meshopt_simplifyWithAttributes (renderer_static_lod.cpp).

SHA-256 of the files:
- src/meshoptimizer.h: 6639dec5362e20e7f2093e7eb11d6728d0d0c87f102b7ffa4e52248e22e1af2a
- src/simplifier.cpp: b645cdaffe9e3eaf64ff77bc82c3e3b8c57c0a4459b7405a0b3d2716ed92d928
- src/allocator.cpp: 0bff407ee165d338026badacce8bd37320372cc687631a2a8054afa7147f1e26
- LICENSE.md: f03037ca7bad1e3eb7f4a63fa6084a8baabd5ba30d3c239a9a7f35705d873e26
