# RS02b resource bootstrap foundation

This directory is **not yet an integrated renderer**. `ResourceBootstrap.h`
accepts already resolved host paths and resource mounts. `Startup.cpp` calls it
from the game startup after settings load and before world/device creation.
The game still uses its previous renderer. Do not package this executable as
the completed RS02b rendering slice.

`EID3D11_EMBEDDED` is a build definition of OwnedRenderer.lib, not a runtime
setting. It selects host-provided paths for renderer configuration, mounts and
neighbour-map layouts. The shader generation executable retains its native
build configuration. Renderer.props pins the exact local submodule commit and
rejects tracked modifications, as before.

Run the isolated console contract check using:

```powershell
MSBuild OwnedRenderer/HostPathsCheck.vcxproj /t:Build /p:Configuration=Release /p:Platform=Win32
out/Win32/Release/OwnedRenderer/HostPathsCheck.exe C:/absolute/new-empty-fixture-directory
```

The caller creates the empty directory first. The test creates only its two
INI fixtures there. It checks rejection before initialization, invalid paths,
mount order and duplicates, snapshot isolation, rejection of reconfiguration,
configuration creation/replacement/readback, and preservation of the legacy INI.

`ResourceMapping.cpp` tracks actual CMappedFile views, not resource object
addresses. Provenance is captured from the OS mapping, ranges are bounded by
the actual file size, writable views cannot prove immutable source identity,
and an entry retires with its successful unmap. The caller must own the view
during a synchronous query; the query does not grant a lifetime lease.

`NeighbourTerrain.cpp` publishes only CTerrain's prepared placement and clears
it on Free. The prepare/upload algorithms live in the renderer submodule's
`embedded_neighbour_terrain.cpp`, using its existing decoder and immutable MPR
assets. No renderer geometry implementation is copied into this repository.
SITES.csv boundaries: RS01-1075 / RS01-1078 (terrain draw/free), RS01-3253 through
RS01-3260 (mapped resource lifetime, concretely CMappedFile in this source tree).

The EXE project references OwnedRenderer.lib, but this is not proof that the
complete Renderer is linked: a constructor/destructor link probe still has
106 unresolved symbols. The remaining work includes scene generation,
membership, appearance/poses, camera/light, UI/focus and supporting bridge
dependencies. Device/presentation ownership stays unchanged until the next
reviewed stage. No fake bridge implementations are supplied here.

Additional console checks (neither launches the game nor creates a GPU/window):

```powershell
MSBuild OwnedRenderer/ResourceMappingCheck.vcxproj /t:Build /p:Configuration=Release /p:Platform=Win32
out/Win32/Release/OwnedRenderer/ResourceMappingCheck.exe C:/absolute/new-fixture-file.bin
MSBuild OwnedRenderer/NeighbourTerrainCheck.vcxproj /t:Build /p:Configuration=Release /p:Platform=Win32
out/Win32/Release/OwnedRenderer/NeighbourTerrainCheck.exe C:/absolute/map.mpr C:/absolute/first-textures.res C:/absolute/fallback-textures.res
```

The terrain check reads explicit files, validates and decodes actual geometry,
compares worker/submit streams, and exercises placement publication/retirement.
Its texture archive arguments are an explicit test mount set, not automatic
selection of the installed profile. It does not test offscreen rendering,
camera composition, frame timing, or the complete game scene lifecycle.

## Paired capture after integration and owner authorization

1. Use a separate test installation and the approved RSB c56 reference.
   Keep the main installation unchanged. Select the same resources, save,
   resolution, renderer settings, camera and game time for both runs.
2. Capture `ZoneMainMenuNew`, then `zone7`, separately for reference and candidate.
   Record exact save identity, camera and game time in `CameraAndWorldState`.
   Allow shader compilation and loading to finish before capturing.
3. Run `OwnedRenderer/Capture-Pair.ps1` manually, supplying `Role`, `Scene`,
   `GameProcessId`, `RendererIni`, `CameraAndWorldState` and a new
   `OutputDirectory`. During its five-second delay, activate the game window.
   The script captures its visible client area and records executable, settings
   and image hashes. It does not launch/install the game or move its camera.
4. Reject obstructed captures and pairs with different dimensions or scene state.
   Compare terrain cliffs/blending/anti-tiling, sunlight, static models,
   characters/equipment and the basic interface. Check optional enabled effects
   without forcing them off.
5. CPU/GPU instrumentation and its data export are still outstanding. This
   screenshot script does not measure performance. Once wired, collect the same
   warm-up and measurement interval for each scene and compare frame-time median
   and p95 separately: both candidate/reference ratios must be at most 1.2.
   A screenshot or a successful build cannot satisfy that gate.
