# Baked SKN motion (SKM 1 / SKC 2)

Optional `<model>.skm` accompanies the exact SKN bytes. `<model>.hero.skn` and
`<model>.hero.skm` select a visual only for the host's main hero; absence uses
ordinary model lookup, while a present invalid resource rejects the visual.
The host's MOD/BON/ANM/ADB and gameplay skeleton are unchanged. Author nodes
are appended only to the immutable renderer binding and addressed by name.

SKM declares the mesh hash, skeleton identity, body shape, conservative bounds,
author node names/parents/rebases, full attachment targets/anchors, sparse
post-skin corrections and equipment contexts. A context has an exact equipment
signature, a neutral clip and a name-to-resource/hash table. Missing equipment
contexts or clips reject instead of selecting an unrelated motion library.
The first asset supplies sword03 + shield01. Additional weapon sets are data.

SKC stores explicit monotonically increasing phases in original 15 Hz ticks.
It never changes the selected ADB duration, events or controller time. Each
track contains either one exact constant key or one key for every phase.
Affine factors are unit WXYZ quaternion, full symmetric 3x3 stretch, translation.
Playback uses SLERP, linear stretch/translation, then parent-first composition.
Roots and anchor-relative mounts use the same interpolation. Full affine shear
is retained; no projection to legacy fixed BON or uniform quaternion scale.

All integers and floats are little endian. Both formats have four-byte magic
and u32 version. Reader limits, identities, counts, finite values, hierarchies,
phase order, clip hashes and exact payload lengths are checked before use.
`skm_asset.h` is the canonical binary reader. `tools/skm_probe.cpp` exercises
that production reader without a device. Resource-store PAK decoding remains
responsible for archive decompression and integrity.

SKC version 2 encodes each float32 factor's bit pattern as unsigned modulo-2^32
difference from the preceding key (first key relative to zero), then stores
four byte planes per component. Reconstruction is exact unsigned accumulation.
No quantization, key removal, reduced precision or bone reduction occurs.
This layout feeds the existing PAK zstd-3 encoder; no second compression layer
or new codec dependency is added. PAK stores each clip separately.

The retained evaluator samples the selected clip on the CPU and publishes an
immutable model-local pose snapshot. The sink applies current actor placement
when uploading the ordinary native pose-node allocation. This supports late
portrait transforms and both D3D11/D3D12. GPU skinning, shadow passes, pose
generation invalidation and copy-on-write allocation lifetimes are shared with
the existing path. Ordinary assets continue using the existing GPU ANM sampler.
Only the selected clip is held by each actor; immutable clip loads are shared
while referenced. The entire 325-clip library is not loaded for every actor.

Sparse corrective records follow the real pose nodes. Each internal skin
vertex carries an optional index to its corrective record; seam/chunk/LOD
remapping preserves this identity. Positions are applied after skinning,
normals are replaced by the baked target, and tangents are re-orthogonalized.
Actor placement and profile scale are applied exactly once. Legacy FIG morph
fields retain their existing meaning; author procedural solvers are not shipped.

An explicit SKN base DDS overrides the human assembled atlas, including alpha,
cull and shadow policy. An empty base DDS retains the previous atlas path.

Validation for NALA378: all 325 clips/61110 phases decode; truncated payloads
are rejected. Forty independent fractional surface samples on actual quantized
SKN data stay within 0.5 mm (body) and 1 mm (equipment) of the corrected bake.
This is numerical validation; visual gameplay acceptance is a separate step.
