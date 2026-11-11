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
section-8 source shape program, section-9 render correspondences, section-13
pose rig and section-14 attachment/hand/sole calibration.
Original duplicate lid edits execute in order; the last write wins. The cooker
reads its actual container through the shared decoder, checks the rig round
trip, rebuilds the default shape using the decoded program, compares its
vertices/normals/rig with the accepted authoring result, and compares serialized
bind contacts to NalaLidContact::Render.

The older command remains available:

```
out/x64/Release/tools/nala-to-skn --export-pose-section ../tmp/unhufe.section13
```

The game resolves section-8 shape data once per unit input change and evaluates
section-13 bones for animation;
the native shader skins sources and blends section-9 contacts. The former
CPU geometry output is removed. Full engine build and game acceptance remain
with the coordinator. Model/authoring headers reside in this offline project.
Hair, clothes, underwear, layers, wounds and pose corrections are not cooked.
The container decoder and runtime accept the bind contact subset.
See docs/skn-format.md for its GPU record and palette contract.