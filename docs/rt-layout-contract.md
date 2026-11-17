# RT structured-buffer layouts

The embedded port extends two GPU records beyond the author renderer's layouts:

| CPU record | Port stride | Required RT shader member |
| --- | --- | --- |
| `Renderer::NativeWorldVertexGpu` | 80 bytes | `RtWorldVertex::tangent`, after the 64-byte prefix |
| `Renderer::NativeWorldStateGpu` | 28 bytes | `RtWorldState::sknLod`, between `pose` and `preparations` |

The renderer 1126 import retained 64-byte `RtWorldVertex` and 24-byte
`RtWorldState` declarations in `renderer_native_d3d12.cpp`. The repack shader
therefore read wrong positions/UVs from prepared world geometry, and the probe
read wrong geometry generations. Setting the SRV's correct CPU stride alone did
not correct these reads in the D3D12/DXIL tests on WARP or the hardware adapter.

`rtRepackCS` feeds the vertex arena used to build static BLASes. Wrong reads can
produce huge coordinates or NaNs; these are geometry defects upstream of AO,
not AO strength settings. `rtProbeCS` can also reject a valid prepared range.
Both HLSL declarations must retain their complete matching CPU layouts even
when the shader only consumes fields near the beginning of the record.

## Regression check (no game)

Run from a rebuild64 checkout inside the Evil Islands project:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/check-raytracing-layout.ps1 -OutputDirectory <task-check-directory> -Hardware
```

The script extracts the actual `rtRepackCS`/`rtProbeCS` shader strings and CPU
structure declarations, compiles DXIL with the pinned DXC, dispatches on WARP
and optionally hardware D3D12, then compares readback with known inputs.
It creates no window, swap chain, game process or game copy.

Fourteen cases per adapter cover nonzero prepared-vertex base, multiple vertices,
UV shifts, relative indices, matching/mismatching/missing generations, and the
unposed object-space path. `-ShaderSource <previous-renderer_native_d3d12.cpp>`
provides the control run against old shader definitions with current CPU data.

Before the fix: five failures (all four posed vertices and one valid generation);
one output position was `(NaN, -1.70141e38, 0)` instead of `(201, 202, 203)`.
After the fix: all fourteen cases pass on WARP and hardware. The object-space
path and relative indices pass on both versions.

This proves the preparation defect and its correction. The owner's black-patch
scene, the final RT image and frame rate still require owner gameplay validation.
