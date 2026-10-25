AMD FidelityFX Super Resolution 2.2.1 (FSR 2), Copyright (c) 2022-2023
Advanced Micro Devices, Inc. Used under the MIT licence in LICENSE.txt.

Upstream: https://github.com/GPUOpen-Effects/FidelityFX-FSR2
Tag v2.2.1, commit 1680d1edd5c034f88ebbbb793d8b88f8842cf804, fetched on
2026-10-24 (sparse checkout of src/ffx-fsr2-api; nothing from it was run).

Files here are unmodified copies:
- shaders/: the HLSL headers and the six compute passes the renderer runs
  (luminance pyramid, reconstruct previous depth, depth clip, lock,
  accumulate, RCAS), from src/ffx-fsr2-api/shaders.
- ffx_fsr2_maximum_bias.h: the upsample maximum-bias table, from
  src/ffx-fsr2-api.

Use in this project:
- Tools/embed-fsr2.py joins the passes into one source (Source/fsr2_embedded.h,
  comments stripped, the licence kept at its top); the renderer's shader table
  compiles it with FXC for cs_5_0, one program per pass (FSR2_PASS define),
  FP32 path, SPD without wave intrinsics.
- Source/fsr2_upscaler.h is an independently written D3D11 dispatcher. AMD
  publishes FSR 2 backends for D3D12 and Vulkan only; the pass order,
  constants, resource formats, look-up tables and reset rules follow the
  upstream host code (src/ffx-fsr2-api/ffx_fsr2.cpp). No AMD library or DLL
  is linked or shipped.
