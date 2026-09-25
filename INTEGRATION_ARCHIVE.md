# Addon integration snapshot, 2026-09-25

Owner requested preserving this work on a separate local branch before starting a clean EIRebuild baseline. This snapshot preserves the accumulated source changes through R105, including unverified work. It is not a clean original-engine baseline or a production release.

Branch: archive/addon-integration-r105-20260925. Parent: 1a9fc9d84971bd059d9e6a089810c7d9ecf1a9de (already contains integration through R29). The parent must not be mistaken for pre-integration sources.

Included: current Game sources and existing local build scripts. Excluded from Git but retained on disk: evidence, binaries, symbols, caches and build outputs. Historical transformation scripts are evidence of previous work and must not be rerun blindly. No source behavior is changed by this snapshot.

Latest prior build evidence: evidence/addon-integration/stage2-r105/REPORT.md. Recorded EXE SHA-256: 8C0F7D7188FD7E64AA7EF226405D20F739C203F8B11E59ADE1F7E509A2C1E51C. No new build was run for this archive. In particular R104/R105 animation queue, AIDX and companion-save integration have compilation evidence but lack gameplay acceptance. Other integration gaps and reported gameplay defects remain open.

The clean-base effort must use a separate working directory and independently establish pre-integration source provenance. Preserve this branch, working directory and private evidence. The canonical roadmap is ../investigations/project-roadmap.md, task GAME-EXE-CLEAN-BASE-001; the old integration parent is deferred, not completed. No installed game, starter, mod or save was changed. No remote operation is authorized.
