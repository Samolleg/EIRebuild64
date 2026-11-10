# Nala source port

Owner-authorized transfer from bahonec/Dejavu-Female-Body,
`0580dff8805fc0bba33f6dce06be7f948451d4d6`, full
`config/natural-nails-runtime.json` (2026-11-10). Source headers preserve the
accepted cumulative identity, original double tables, weights, hand controllers,
lid render topology and garment bindings. No authoring changes are made.

`GamePoseReader.h` is replaced by immutable actor/attachment/geometry values.
`NativeWeaponGeometry.h` reads those values rather than x86 process addresses.
The hands diagnostic exports and Publish function are omitted. The morph
resolver/schema headers alias the existing Game/BodyShape implementation; they
do not duplicate its tables. Game/NalaLameLocomotion.h is the source's pure
capability normalization, applied only to the two Nala unit IDs.

`../nala_runtime.cpp` samples the same selected ANM/ADB skeleton as FIG/SKN.
The host captures CUnit controls, clothing subtypes and wound state before
queuing a frame; workers never retain CUnit pointers. Body/garments are expressed
in hp-local space once. The normal world pose supplies the remaining placement.
Existing hair/weapons retain their FIG/UV/atlas and receive the source's corrected
attachment transform. Body, eye-point and garment replacement is atomic; a
missing resource rejects the visual and reports its first reason, with no stock
body fallback. Generated records serve color, directional/point shadows and UI
previews. CPU geometry is cached while the pose, assembly and input are unchanged.

Copy only the task's `mod-files/Maps/nala` directory to `<mod>/Maps/nala` during
the separately authorized installation. Runtime uses 201.rgb, 202.rle, 211.rgb
and 212.rgb through 231.rgb. These are the 23 exact manifest payloads; 224-231
are the additional cuff/sock fabrics absent from the brief's abbreviated range.
RGB8 bytes and row orientation are unchanged on disk; upload expands RGB to
BGRA8 without resampling. Wound masks use the original RLE decoder and compositor.
GGX uses the shared authoring fabric roughness constants (.9/.8/.52/.5/.48).
The former D3D7 lighting/material/texture hooks and NativeGarment modules are
not included. The source's optional form-light vertex shading is not applied.

Build and in-game acceptance remain pending: this session cannot write the
external cache required by the unmodified tools/build-task.ps1. No tests or game
runs were added. The coordinator must build, then inspect world/inventory,
shirt/bandeau and pants/underwear switching, all fabrics, wounds, morphs,
hair/weapon grip and animation transitions in the game.
