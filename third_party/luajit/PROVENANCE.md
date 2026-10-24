# LuaJIT source pin (X64P12)

- Official upstream: https://luajit.org/git/luajit.git
- Branch: v2.1 (rolling); commit: `a4c064539ea931274d407e1249b4bb575424a0f4`.
- Commit date: 2026-10-21 15:09:24 +02:00; version timestamp: `1792588164`.
- Imported on 2026-10-24 using `git archive` of that exact commit, including
  `.relver`, DynASM, buildvm, all VM descriptions, documentation and `COPYRIGHT`.
- Upstream's `export-subst` expands `.relver` to the pinned commit timestamp;
  this is the standard source-archive versioning step, not a local source patch.
- Source is unmodified. This provenance file is the only added vendor file.
- License: MIT; full notice in [COPYRIGHT](COPYRIGHT).
- Build integration: [tools/luajit](../../tools/luajit/README.md).
  Its private build copy adapts upstream `msvcbuild.bat`; vendor source stays
  unchanged. No prebuilt VM object or binary is imported.

Historical `luajit-x86/` matches upstream
`8ff09d9f5ad5b037926be2a50dc32b681c5e7597` (2021-08-12), ignoring line endings,
for 142 C/header files. The only changed C/header file is `lj_debug.c`: a comment,
indentation and traceback display `in function <%s %s %d>` instead of
`in function <%s:%d>` (the extra string is `ar.namewhat`). That presentation-only
change is listed for P14 review, not carried into the new VM. The host calls
`luaL_traceback`; no custom API was added. Historical generated headers and the
old CMake recipe are not inputs of the new build.
