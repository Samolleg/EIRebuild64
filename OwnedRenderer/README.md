# candidate-56 build preparation (RS02a)

`external/owned-renderer` is a git submodule pinned to
`b63db5947438a648152065d8fa6800110cec14d9`. Renderer source is not copied into
the EIRebuild repository. Initialize it with
`git -c safe.directory=* submodule update --init external/owned-renderer`.

Use the existing `build-local.py` preparation/build entry point, or prepare
`out/include/nlohmann` from the bundled `SDKs/JSON` and run:

```
MSBuild.exe EI_Rebuild.sln /t:Build /p:Configuration=Release /p:Platform=x86
```

The two new projects support Release Win32 only, use v143, C++17, /MD,
/EHsc, /W4 and no precompiled header. `OwnedRendererRoot` can point at an
external checkout; both projects check its exact commit and tracked diff.
Output is `out/Win32/Release/OwnedRenderer`, with separate intermediate
directories for the library and shader tool.

`OwnedRenderer.lib` contains the loaders, retained scene/submission, GPU
passes, terrain decode, resource runtime, HUD publication, meshoptimizer
and supporting source explicitly listed in `OwnedRenderer.vcxproj`.
It excludes proxy/exports, engine_bridge, game_sites implementation,
addon hooks, camp_inventory and owned_ui_layers hook implementations,
and source-host API1-5. It is an archive awaiting a host adapter, not a
standalone linkable renderer. Undefined bridge, UI, configuration and
telemetry symbols are intentional dependencies for RS02b; there are no
dummy implementations. The archive still includes the old resource-runtime
registry acquisition entry point, which RS02b must replace before calling it.

`RendererShaders` is a separate build-time executable using the unchanged
production source list from `build-local-x86.cmd`, including its static LOD
and meshoptimizer additions. As upstream does, the tool links the retail
frontend but calls only the shader enumeration/compiler entry point. These
frontend objects are not in `OwnedRenderer.lib` or EIRebuild. It generates
`eid3d11_shaders.bin`, `.rc`, `.res` and `eid3d11_camp_art.res`. It does not
build the proxy DLL. Ordinary shader tables are generated, without the
optional preset cache, as in the default upstream build.

EIRebuild has no project reference, linker input or calls to either new
project. The resources also remain unlinked. RS02b must link the resources
explicitly into the eventual EXE and implement typed resource/world/UI
publications and lifecycle; linking the archive alone will not do that.
Do not resolve its outstanding dependencies by adding engine_bridge or
proxy back into the game. All candidate-56 UI functionality remains in the
overall migration scope, including the excluded hook-backed frontends.

This stage makes no runtime or visual acceptance claim. See the private
RS02a report for source/binary audit results and the adapter inventory.
