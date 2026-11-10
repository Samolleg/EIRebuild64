# nala-to-skn (incomplete)

Offline x64 project in EI_Rebuild.sln. No hooks or D3D7 calls.

From the worktree, build the cooker and existing texture tool with MSBuild:

```
MSBuild EI_Rebuild.sln /t:"nala-to-skn;skn_texpack" /p:Configuration=Release /p:Platform=x64 /nr:false
out/x64/Release/tools/nala-to-skn --export-bind-mesh ../mod-files/Maps/unhufe.skn
out/x64/Release/skn_texpack/skn_texpack --role color --space srgb --input <bahonec>/assets/runtime/natural_nails_20261110/body_face.rgb --width 2048 --height 2048 --output ../mod-files/Maps/nala/201.dds
```

Create the output parent directories first. Outputs must not exist. The skin
input must be manifest resource 201 from config/natural-nails-runtime.json,
SHA256 efcefe68d2548334b240a8166fca5c907f640bc7e063046e48a80eb108df2585.
The source clone is read-only; texture bytes are cooked outside Git.

The bind snapshot contains the resolved default body/face/eyes, accepted UVs,
proxy vertices, four original skin slots per sampling vertex (UNORM16),
section-9 render correspondences and the complete section-13 pose rig.
Original duplicate lid edits execute in order; the last write wins. The cooker
reads its actual container through the shared decoder, checks the rig round
trip and compares serialized bind contacts to NalaLidContact::Render.

The older command remains available:

```
out/x64/Release/tools/nala-to-skn --export-pose-section ../tmp/unhufe.section13
```

Neither output is ready for installation. Section 8 is absent; shape changes,
per-frame bone palette integration and GPU skin/contact submission are pending.
The existing Nala CPU route has not been removed and still precedes file lookup.
Hair, clothes, underwear, layers, wounds and pose corrections are not cooked.
The container decoder accepts the bind contact subset; runtime SKN selection
explicitly rejects it until submission is implemented. See docs/skn-format.md.