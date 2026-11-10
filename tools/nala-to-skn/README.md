# nala-to-skn (incomplete)

Offline x64 project in `EI_Rebuild.sln`. The implemented command extracts the
accepted bahonec internal rig into the section-13 payload described in
`docs/skn-format.md`. It preserves every bind slot and controller parameter,
including binary64 precision, without invoking a CPU vertex generator.

Build from the worktree:

```
MSBuild tools/nala-to-skn/nala-to-skn.vcxproj /p:Configuration=Release /p:Platform=x64 /nr:false
out/x64/Release/tools/nala-to-skn --export-pose-section ../tmp/unhufe.section13
```

Output must not exist. The writer decodes and re-encodes the actual payload
before publishing it; mismatched data fails. The common reader rejects invalid
table ranges/references, nonfinite values, singular transforms and hierarchy
cycles. `nala_pose::palette` evaluates only bones using the transferred kernel.
Neither tool nor reader has a renderer/device dependency or game-memory hooks.

This is a partial cooker, not a model conversion. It does not produce
`unhufe.skn` or DDS, and `.section13` must not be installed in Maps. Container
loading, GPU morph/skin/lid integration and CPU-output removal remain undone.
In particular, `NalaBodyAuthoring.h::Apply` changes each vertex by an inverse
weighted sum of its old bind matrices and a weighted sum of its new bind
matrices. Copying the five resolver inputs or exporting only fat deltas does
not implement that operation. It still needs explicit GPU lowering, together
with `NalaUpperBody::Map` and the source face/proxy corrections.
