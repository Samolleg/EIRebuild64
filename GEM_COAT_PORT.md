# Author gem and coat port

Source: Ilufus/evil-islands-owned-renderer, author branch claude/cand-1114.

- gem: 27745c89f039c2aaec3f5faadb32108e701ea076, ce2c49ca (facet contrast), 781509bb (gem-only lamp loop).
- coat: 39e81465b78f6bce1ad426484f65938fc1d8cdef. Preserves bare as a zero coat multiplier. Coefficients are clamped to 0..7.9 and quantized at 1/32, packed as 256 bytes for D3D11/D3D12.

Only the six relevant source files changed. Local x64 integration, synchronous material preparation, custom rendering paths and unrelated materials are retained. No gem scenery/asynchronous material code was imported. Shader gem effect is for figure pixels, excludes Low, and follows the author's spectral highlight, facet contrast and light-through formulas. The one boundary adaptation is the RGBA8 gem threshold: F0 .12 encodes as byte 122, so the shader compares >=122/255 rather than >.48, which incorrectly excluded the author's gem class at its minimum allowed F0. Ordinary F0 .04 stays below the threshold.

Author coat functions and gem lamp loop were compared exactly (ignoring whitespace). Boundary checks cover coat 0/.25/1/3.5, clamps, undefined=1, bare priority; prepared classes pass the author's gem validation. Hair and armour roles retain their own paths. At material_quality 0 all fur is disabled as in the author's coat commit; the submission bridge and drawing pass share fur_mode().

Required data: GEMCOATDATA01. Contains gem/crystal_cd/hide_sleek/mane, zebra00 binding and region map, plus eight gem region maps preserving existing non-gem labels and sidecar restrictions. Existing boar bone.bare is retained. Code and data must be installed together: the new loader rejects the old unmarked high-F0 diamond class.

No gameplay/GPU screenshot acceptance is claimed. Build status is recorded in the task REPORT and build evidence, not inferred from source parity.

Integration correction: the gem response block uses the same NATIVE_OFF_MATERIALS/NATIVE_LOW guards as nativeGemLampThrough. Initial full shader compilation exposed undeclared calls in no-material x64 specializations; the guarded block fixes those without changing the effect formulas.
