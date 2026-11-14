# Author scenery profiles for static rocks

Request: enable Gem on mineral inclusions of samobsrock01 using samobsrocktex01 and sammalrocktex01. Owner explicitly included the x64 scenery.json port.

Author sources: Ilufus/evil-islands-owned-renderer 8c89eec464d6556e8f237a889c7a9be0b7aebc6d (scenery profiles), 942fd01f3b7e926e5bfb58538f5ddf723d7a5556 (no figure ambient-relief darkening on scenery), found in claude/cand-1114. Port restricted to the material library, profile binding and two shader conditions. Preserves x64 synchronous profile creation and existing gem/coat/bare. No new switches; uses existing material_world gate.

Optional scenery.json is read after units.json. Units take precedence on duplicate names. Listed static textures receive class/region profiles on D3D11 and D3D12. Unlisted world objects keep their existing texture-derived response. Explicit profiles enter the already existing gem shading path; no shader colour effect was reinvented for rocks. Author scenery roughness variation and suppression of figure-only ambient correction are retained.

Checks: exact load_units function parity with author; compiled C++ harness with actual small_json reader covers both target names, units precedence, empty table, unknown class and malformed JSON. Optional-file load and both API binding gates reviewed. Full build result is in task REPORT. Actual game/GPU appearance is not claimed.

Data is prepared separately in ROCKGEM01. Deploy support for scenery.json is SCENERYDEP01. Code and data remain unaccepted/uninstalled until explicitly requested.
