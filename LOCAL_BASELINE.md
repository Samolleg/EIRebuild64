# First local baseline — 2026-09-16

This commit captures the existing EIRebuild working tree after the R29 migration and its audit. It does not reconstruct historical commits and does not declare full addon parity, a production release, or x64 support. No C/C++ behavior is changed by creating this baseline.

## Repository scope

Track engine and LuaJIT sources, project files, HLSL and source resources, supplied SDK headers/libraries, miniaudio and its license, and existing build/check/migration tools. Historical migration scripts record earlier transformations; do not rerun them blindly against this baseline. Git stores original bytes without line-ending normalization.

Keep outputs, object directories, symbols, package caches, local UI settings and run evidence outside Git, without deleting them. Pinned NuGet dependencies are declared by packages.config; restoring them is needed on another machine. build-local.py regenerates out/include/nlohmann from SDKs/JSON. Visual Studio Build Tools v143, x86 MFC and the Windows SDK remain external prerequisites.

The supplied luajit-x86/lj_vm.obj is deliberately versioned because luajit.vcxproj links it directly. Its source generation is not established. Supplied SDK binary inputs also remain binary dependencies; this snapshot does not claim a fully source-only toolchain.

## Provenance and remaining work

The source originated as a separate copy of the historical EIRebuild DX11 tree, followed by local changes through R29. Existing private manifests and reports stay in evidence/ and are not copied into this commit. R29 package EXE identity recorded by the audit: E68F243AF6FF9F7D26205343BDD2E4AEDE072DA587BFBB24DD2DFA258F025543. This is pre-commit evidence, not a new build from the commit.

The audit found open combat/Lua wiring, NPC load-versus-visit context, physical/logical spell-slot contracts, GetSkill(tame), the empty Bink player and unresolved playable startup. Audio checks were headless. R29 direct compiler output exists but its asynchronous build exit code was not retained; final combined build and gameplay acceptance remain outstanding.

The parent goal remains migration of all required addon functionality. The canonical shared roadmap is ../investigations/project-roadmap.md; no separate roadmap is created here. No installed game file is changed and no merge, push, publication or gameplay test is part of this Git operation.
