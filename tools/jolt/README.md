# Jolt x64

`Jolt.vcxproj` builds the vendored 5.3.0 library using the normal v143 toolchain.
`EI_Rebuild.vcxproj` references it; the standard task build includes it automatically
and keeps incremental outputs in `builds/rebuild64/cache/work`. No network or CMake
is required at build time. Source, compiler and header timestamps drive MSBuild.

The public ABI uses default Jolt defines: float positions, uint16 object layers,
no JPH_PROFILE_ENABLED / JPH_DEBUG_RENDERER / JPH_OBJECT_STREAM. Host and library
both use C++17, NDEBUG and static CRT. CPU baseline remains SSE2; do not enable
AVX/FMA or change public defines for only one side. Floating-point traps are not
enabled. /Z7 embeds debug records in the library without a compiler PDB server;
the final host PDB contains linked debug records. The license enters the normal kit.
