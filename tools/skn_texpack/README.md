# skn_texpack

Offline x64 SKN texture cooker, vendored DirectXTex jun2024 (2024.6.5.1).
BC compression uses up to four CPU workers over independent block rows at default
quality, without an OpenMP runtime.
Build Release|x64 in EI_Rebuild.sln after tools/prepare-worktree.ps1 as in BUILD.md.
No renderer or GPU/device dependency. Output paths must not already exist.

```
skn_texpack --role color --space srgb --input body.rgb --width 2048 --height 2048 --output body.dds
skn_texpack --role normal --space linear --input normal.png --output normal.dds
skn_texpack --role orm --space linear --ao-value 1 --roughness rough.png --metallic-value 0 --output orm.dds
```

PNG, TGA and tightly packed RGB8 `.rgb` accepted; raw input needs explicit dimensions.
Each ORM channel requires exactly one grayscale map (`--ao`, `--roughness`,
`--metallic`) or unit-range constant (`--ao-value`, etc.). Constant-only ORM needs
dimensions. Maps must have identical dimensions. Color/emission are explicitly
declared sRGB, normal/ORM linear; --space is mandatory and mismatches fail.
PNG metadata is ignored in favor of this mandatory declaration.
Normals use RGB encoded XYZ in [0,1], positive Z hemisphere; zero vectors fail.
They are decoded, normalized, averaged as vectors and renormalized at every mip.

Optional `--cutoff 0.5` adjusts alpha scales against base-level coverage.
Discrete mip texel counts limit representable coverage, especially at 1x1;
the closest representable coverage is chosen. BC7 may further perturb alpha.
Optional `--islands mask.png`: RGB island labels, black outside UVs. At every
mip, nearest occupied RGB is extended into unused texels (alpha unchanged).
Labels are reduced by majority among occupied samples, ties by label order.
Mask and input dimensions must match; an empty mask fails.

Color uses exact sRGB transfer functions around linear box filtering; alpha and
ORM are linear. All levels down to 1x1 are compressed as BC7 sRGB, BC5 linear or
BC7 linear, with forced DDS DX10 header. Invalid dimensions, missing LFS pixels,
nonfinite/out-of-range data, wrong role/space and degenerate normals fail.
Top-level width and height must each be multiples of four in 4..16384;
1x1 and 2x2 top levels are rejected. Lower mips still continue through 1x1.
The tool prints bytes, mip count and arithmetic mean of per-R/G/B PSNR in dB
against decoded base-level DDS samples (encoded byte domain, excluding alpha).
PSNR is meaningful for color/emission; exact channels report infinity.

`python bake_bahonec.py --clone <bahonec> --exe <skn_texpack.exe> --out <out>`
cooks RGB8 resources from `config/natural-nails-runtime.json`, verifying source
size and SHA-256. It also cooks authoring albedo maps and pairs roughness/metallic with AO=1.
An absent metallic map uses 0; an LFS pointer is reported and skipped.
Missing maps and LFS pointers are listed in `out/results.json`; no checkout or
downloads are attempted. Use a fresh output directory for each run.
