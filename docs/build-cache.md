# Selecting a build cache

`tools/build-task.ps1 -CacheRoot <path>` selects an isolated cache explicitly.
For callers that start this script in a child PowerShell process, including
`task accept`, the process environment variable `EIREBUILD64_BUILD_CACHE` supplies
the same parameter's default. An explicit `-CacheRoot` takes precedence; without
either value the standard `builds/rebuild64/cache` is used.

This changes only build storage. Existing path/reparse checks, exclusive locking,
cache hash validation and size limits remain in force. Do not reuse another
running build's mutable `work` directory. A temporary acceptance cache can live
under the parent task, outside its generated `accept-main-*/wt` build task; it is
removed by the normal task closeout. Set the variable only for that process.

## Reusing accepted shader resources

`tools/build-task.ps1 -ShaderResourceManifest <manifest.json>` builds CPU code
while reusing four existing `.res` payloads beside the manifest. The version 1
manifest pins the current accepted EXE path and SHA, its main commit, the exact
changed-file list and SHA values, each resource SHA, and the cached DirectXTex
and DirectXTK library/PDB SHA values. The caller must first prove that the
resource contents match the accepted EXE and review the listed changes as
non-shader changes.

While holding the normal build lock, the script checks the acceptance receipt,
clean worktree, unchanged library sources and all hashes. It then copies the
verified resources and disables the renderer shader, DirectXTex and DirectXTK
build targets. Missing or incompatible inputs fail the build rather than fall
back to shader compilation. The build log and resource hashes are checked
again after MSBuild; independently compare the resulting EXE's resources to the
accepted EXE before considering the kit verified. The same path can be supplied
as `EIREBUILD64_SHADER_RESOURCE_MANIFEST` to a child process, including
`task accept`; explicit command-line input takes priority. For an acceptance
build, the checked-out main may be the merge of the pinned base and the exact
manifest changes. Both resulting EXEs are checked against every manifest
resource before the build can complete or installation can start.

A pinned accepted shader source may be `current` or `previous`; both still require
the exact EXE hash and acceptance receipt. The build HEAD must include current
main, and the manifest covers the complete diff from the accepted source commit,
including explicit deleted non-shader files verified against Git. This permits
CPU-only intervening acceptances without silently recompiling shaders.
