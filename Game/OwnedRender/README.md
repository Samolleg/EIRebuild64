# RS02b resource bootstrap foundation

This directory is **not yet an integrated renderer**. `ResourceBootstrap.h`
accepts already resolved host paths and resource mounts. Its only caller is
currently `HostPathsCheck.cpp`; the game still uses its previous renderer.
Do not package the unchanged game executable as the RS02b rendering slice.

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

Remaining integration: resolve paths and mounts from SettingsProfile/ModRuntime
at the real startup boundary; connect resource/lifetime publications, scene
generation, membership, object appearance/animation, camera and lighting;
replace device/presentation ownership and the world/UI consumers; implement the
remaining bridge dependencies with game data; link the embedded renderer into
the executable. No fake bridge implementations are supplied here.

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
