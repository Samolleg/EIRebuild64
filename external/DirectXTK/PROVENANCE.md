DirectXTK, Microsoft, tag jun2024, MIT; see LICENSE.
Source: https://github.com/microsoft/DirectXTK/archive/refs/tags/jun2024.zip
Archive SHA256: 5832e5db408560d602698be95334fb2d50de5f4bf614436691b81f4a0c846683.
Unchanged BufferHelpers, BasicPostProcess, CommonStates, ScreenGrab, WICTextureLoader and their private/public
headers, plus public headers included by the host. Other implementations,
samples and platform projects omitted. Local vcxproj builds x64 Release /MT
or Debug /MTd; required PostProcess shaders use upstream SM4 entry points and profiles, with stripped bytecode. No separate DLL or NuGet binaries are used.
