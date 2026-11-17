# Scorch marks on smoothed terrain

The terrain renderer rounds cliff geometry with GPU Phong tessellation. A scorch
mesh projected onto the original flat faces intersects that rounded surface: only
parts of its triangles pass the depth test, exposing dark polygon boundaries.

Fire prints now retain whole base-terrain triangles and their authoritative slope
normals. Their hull/domain stages use the terrain's `terrainTessEdgeFactor` and
`terrainTessPhong`, including flat-edge, submerged-ground and map-border limits.
The original 1 cm lift is applied after smoothing. Clipping to the original atlas
cell and projection ceiling happens per pixel, so clipping cannot change the
surface's control points. With terrain smoothing off the same patches stay flat.

Texture selection, rotation, alpha, lifetime, fading and fixed-function blending
remain owned by `CFirePrints`. Bridge projections keep their existing mesh path.
Blood and foot prints retain their existing path. No setting was added or disabled.
The particle buffer (48 bytes) and constants (208 bytes) keep their existing ABI.

Checks cover compilation of the new VS/HS/DS and existing effect programs. The
fireball scenario on the affected rock was exercised in game on D3D11 and D3D12
with fireprints and terrain tessellation enabled; frames during and after the
blast were inspected. PhysX debris, pause and expiry remained functional. This
does not establish full-campaign, bridge, snow-trail or performance acceptance.
