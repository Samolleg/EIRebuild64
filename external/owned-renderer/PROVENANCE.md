# Renderer source provenance

Upstream: https://github.com/Ilufus/evil-islands-owned-renderer

This is the EIRebuild embedded integration, based on candidate-74 (`742bda5aa064092549ace5da2b17fd9cc97423c3`) with local
integration changes in branch `eirebuild/c74-embed`, commit `203547bf0e2268721abf20975fb005121b7c99cb`.
This merges upstream into the embedded baseline `4e06826962c84368ad2726abb0d936621cb82112`.
The immutable host-surface contact fix (`cb3e84a`) is retained. The Russian F10
changes (`f69be1d`, `308a547`) are incorporated through the candidate-73
localization layer, with additional embedded translations preserved.
That commit was local and was not available from the upstream repository.
The previous submodule reference could therefore not be fetched on another machine.

The source snapshot is now stored directly in EIRebuild. Imported files are
byte-for-byte copies of the selected integration revision: `Source/`,
`ThirdParty/`, `Assets/`, and the build-time `Tools/shader_embed.cpp`.
Third-party licenses and asset provenance are preserved. Local logs, dumps,
validation reports, build products and private runtime directories are excluded.
Standalone proxy build tools and upstream tests are outside this embedded build.

The initial import changes no renderer behavior. Subsequent changes are tracked
by the EIRebuild repository. To update from upstream, compare against the source
baseline, integrate deliberately with the host adapter, and update this record.
Do not replace this directory with an upstream checkout without carrying forward
the embedded interface and accepted integration changes.

RUPD74 merges candidate-74 into embedded RUPD73B revision 5f30d3563e361380735a5448f36840f4338308b1.
Sea surf, level river outflow, water shading, snow trail shadows and painted-strand
pixel kinds follow candidate-74. The typed host interface, normalized wave glint,
world retirement, precipitation and progress movie ownership remain embedded.
Russian F10 labels include the upstream maps-without-sea setting.
