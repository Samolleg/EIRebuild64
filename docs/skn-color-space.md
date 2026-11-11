# SKN albedo and the native renderer's working colour space

X64NALASKN05: the albedo was sampled correctly but crossed into the legacy
combiner in the wrong colour space. This affects SKN materials on both D3D11
and D3D12, including Low and interface/model-preview draws.

## Resource and numerical evidence

The supplied body_face.rgb is 2048 x 2048 RGB8, SHA256
`efcefe68d2548334b240a8166fca5c907f640bc7e063046e48a80eb108df2585`.
The supplied deployed-201.dds is 5,592,580 bytes, 12 mips, DXGI 99 (BC7 sRGB).
Pillow's BC7 decoder was used to compare mip0 byte values against the RGB8
array at identical (x,y) coordinates; no image colour-management conversion.

| Measurement (RGB, byte units) | Result |
| --- | --- |
| Source mean, all 4,194,304 pixels | 82.26034, 59.43970, 51.52809 |
| DDS mip0 mean | 82.24089, 59.41423, 51.51379 |
| Mean absolute error | 0.14835, 0.15905, 0.14529 |
| Root mean square error | 0.43645, 0.43247, 0.41769 |
| Source mean over 2,007,125 nonblack source pixels | 171.90004, 124.21159, 107.67864 |
| DDS mean over the same mask | 171.85891, 124.15799, 107.64840 |

Mean absolute error averaged across channels is 70.09774 if vertically flipped,
and 20.54220 if R/B are exchanged. Neither is the actual conversion.
Samples (zero-based x,y): (1500,1500) source (204,148,120), DDS (202,148,120);
(1700,1400) source (206,152,123), DDS (206,152,124);
(1536,1536) source (213,155,128), DDS (213,155,129).

For the last sample, hardware sRGB decoding produces linear values equivalent
to (169.67377,83.58342,55.97919) in byte units. Feeding these directly into
the display-RGB combiner changes R/G from 1.37419 to 2.02999. Applying the
standard inverse transfer restores (213,155,129), without a fitted coefficient.
A float32 calculation over every decoded mip0 component gives maximum
round-trip error 0.00001526 byte units and zero rounded-byte mismatches.
This is a numerical colour-space check, not a shader or game execution.

## Code path comparison

* bahonec's analysis_reference/unity_native_body_v1/head105_v1/NativeAlbedoTexture.h
  reads resource 201 as RGB8, indexes (y*size+x)*3 without a flip, and packs
  p[0]/p[1]/p[2] into the actual D3D7 surface's R/G/B masks. It requires a
  16-bit surface; quantization is (value*maskMaximum+127)/255. It does not
  hardcode RGB565 versus ARGB1555 and performs no sRGB-to-linear transfer.
* NalaFormLighting.h scales all diffuse/specular RGB channels by the same
  factor; default strength 70 gives factors 0.65..1.0 (form 0.50..1.0).
  Alpha/fog is preserved. This is directional shading, not a red skin tint.
* tools/skn_texpack/main.cpp reads RGB8 in the same order/orientation,
  linearizes for mip averaging, re-encodes to sRGB bytes and compresses BC7.
  The measured differences above are compression error, not a colour shift.
* skn_runtime.h::dds requires DXGI 99 for base colour. native_prebuilt.cpp,
  renderer_surfaces.cpp and renderer_native_d3d12.cpp retain BC7_UNORM_SRGB
  for the GPU resource/view; hardware sampling therefore returns linear RGB.
* tools/nala-to-skn/bind_mesh.h authors white base (1,1,1,1), zero emission,
  roughness 0.6, metallic 0, AO 1. retained_static_submission.cpp forwards
  these factors and uses white default diffuse with ordinary world lighting.
  There is no authored red tint. native_shader.h adds GGX sun/lamp/sky response
  to the game's colour; it does not constitute a fully linear-light pipeline.
* native_shader.h passes t through nativeCombineColor and returns result.color
  without an sRGB encode. renderer_device.cpp creates a B8G8R8A8_UNORM
  swapchain/target. renderer_frame.cpp applies only the game's gamma ramp,
  which is neutral until changed; this is not a linear-to-sRGB output stage.

## Correction and remaining verification

native_shader.h now encodes the linear sampled albedo times its linear base
factor to display RGB immediately before the shared legacy colour path.
The exact piecewise sRGB transfer is used (threshold 0.0031308, slope 12.92,
exponent 1/2.4). Alpha is unchanged. The sRGB resource/view remains intact,
so filtering/mips continue to operate in linear space. Other material maps,
GGX parameters, light values and non-SKN materials are unchanged.

No DDS regeneration is needed. No tests were added and no build/game was run,
as requested by BRIEF. Claude must build and verify the original head scene
and related SKN preview/Low paths. This fixes a demonstrated colour-space
mismatch; exact frame equality with bahonec is not established, since its
presentation shading and the native GGX/world lighting differ.
