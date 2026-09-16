# EIRebuild — local source baseline after R29

First local Git snapshot authorized on 2026-09-16. This repository records the current engine sources, project files, shaders, SDK inputs and development tools. It is a private local development repository; no remote or publication is configured by this operation. The source migration remains incomplete.

The canonical roadmap remains `../investigations/project-roadmap.md`, task `GAME-EXE-EIREBUILD-ADDON-INTEGRATION-001`. Current audit: `evidence/addon-integration/audit-20260916-r29/REPORT.md` (local evidence, intentionally outside Git). See `LOCAL_BASELINE.md` for snapshot scope and remaining limitations.

## Historical initial-build notes (R1)

The notes below describe the original build stage, not the current R29 implementation or current Git status.

# EIRebuild: isolated full-engine x86 build

Owner-authorized working copy of `work/src_eirebuild/EIRebuild Good dx11`, created 2026-09-16. This is the next full-engine reconstruction baseline; `work/engine` remains a supporting prototype. Neither is the installed addon baseline. Canonical roadmap: `work/investigations/project-roadmap.md`, GAME-EXE-EIREBUILD-BUILD-001 under GAME-EXE-RECONSTRUCTION-001.

## Build

Run `build-local.py` with the bundled Python and an optional new log label. Requires VS2022 BuildTools, v143 with x86 MFC, and Windows SDK. Build target is Release / x86 (project Win32). Output stays in `out/Win32/Release`; intermediate files stay in `obj/<project>/Win32/Release`. The original projects are preserved in the reference directory. No fetch, install, or game launch occurs.

Working project fixes:

- Use installed v143 including MFC. v142 compiler exists but its MFC does not.
- Match `UseOfMfc=Dynamic` to existing `_AFXDLL` and `/MD`; compile LuaJIT with the same release CRT.
- Redirect historical absolute game output paths into this working tree.
- Add a project dependency on LuaJIT so the library is built before the engine links.
- Remove unused sibling ImGui/DirectXTK include paths. Existing pinned NuGet packages supply DirectXTK, DirectXTex and XAudio2; Paint.cpp using ImGui is not part of the application project.
- Normalize child-process PATH casing and disable MSBuild node reuse to avoid the host environment's duplicate Path/PATH failure.

No C/C++/HLSL game implementation was rewritten. `prepare-handoff.py` verified 615 declared source/header/resource/object inputs against the reference snapshot. The supplied LuaJIT `lj_vm.obj` remains a binary input; this is not a proof of a fully source-reproducible LuaJIT VM.

## Result and limits

Full engine application, LuaJIT static library and five shader binaries built successfully. `evidence/build-03-console.log` records the successful build; build-01/02 retain the environment failures before compilation. Six warnings remain: two LuaJIT enum-type arithmetic warnings and four existing geometry-shader vector truncation warnings. Engine warnings were already disabled by its historical project settings; success does not establish warning cleanliness.

Private build-only handoff: `work/handoffs/eirebuild-x86-20260916-r1`. EXE SHA-256: `1c7fa651cd366d4929cc6fb591169c19446d942ef942bb74fd57e70a5f25d617`. Exact PDB retained separately in `symbols/20260916-r1`. Manifest, dependency locations and source hashes: `evidence/handoff.json`; PE imports: `evidence/pe-imports.txt`.

Static artifact check: x86 PE32 GUI; 25 direct DLL import entries accounted for (Windows API-set entries delegated to OS); compiled DXBC shaders present in both normal and debugger paths expected by DXShader.cpp. Miles DLL copied from the installed Engine into the private handoff. No proxy/addon DLL copied. DLL export compatibility, recursive/dynamic loads and execution remain untested.

No executable was launched, no installed file or registry value changed. Branch/worktree/commit: N/A, separate copied source tree; no merge/push/publication authorized or performed. No new runtime toggle introduced.

## Exact next step

Prepare an isolated runtime resource/config layout and isolate the registry key before launching: GameMain.cpp uses `Software\\Nival Interactive\\EvilIslands`, reads global path/settings values and writes them back on shutdown. Merely choosing a different working directory does not isolate settings. Then test startup, a rendered map and clean exit; record the first actual failure. Follow with movement/combat/save-load and mod-compatibility testing. Do not load the installed addon/dinput hooks into this differently linked EXE: original address compatibility is not established.

This build establishes that the existing EIRebuild source is viable as a complete compilation unit. It does not prove correct game behavior, compatibility with JamaisVu data/saves, parity with the installed game.exe, or x64 readiness. Parent full-engine task remains OPEN.
