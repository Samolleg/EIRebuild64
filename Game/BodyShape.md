# Nala CPU shape profile

`BodyShape.h` resolves RPG strength/dexterity and prototype DB strength/grace/tall
into 45 double controls in `BodyShapeSchema.h` order, plus the visual scale.
`BodyShapeResolver.h`, `BodyShapeData.h` and `BodyShapeSchema.h` are copied from
bahonec `analysis_reference/unity_native_body_v1/morphs_head105_v1`:
`NalaMorphResolver.h`, `NalaMorphData.h`, `EditorSchema.h`. Only include names and
the enclosing BodyShape namespace differ. Transfer is authorized by the owner
(X64SKIN02 FORMAT.md, owner decisions of 2026-11-10).

Tables are constexpr doubles compiled with the game. This preserves source
decimal literals and evaluation order without introducing an unversioned SKN
section, runtime parsing or a dependency on the reference checkout. This is the
Nala head105-v1 driver profile, not a generic replacement for other bodies.
The resolver retains its input saturation mask, finite/limit rejection,
multiplicative height/stature corrections and forced headSize=1.05.
State clears controls/scale on rejection; consumers must check `valid`.

`CUnit::BindNalaSknBody()` binds this profile to a SKN body instance and computes
its transient state. The asset owner calls `UnbindSknBody()` on detach and rebinds
after loading; controls are derived data, not a new save/network payload.
`GetBodyShape()` refreshes from live stats and the current prototype before
returning the state; null means no bound Nala SKN body. Refresh also happens
after RPG recalculation, export, network stat import and camp/default setup.
Server inputs use effective RPG doubles promoted from floats; clients use the
existing replicated RPG bytes. This module does not alter that wire precision.

There is no SKN asset loader in this task's base commit (255ca103). Its future
Nala attachment path must call the binding API; no legacy unit is silently
treated as SKN. No renderer submission, deformation, legacy complection,
FIG/BV, navigation or combat calculation consumes this state.

The task-local comparator compiles this module and the original resolver in
one MSVC Release x64 translation unit. It compares all 45 doubles by bytes,
scale and saturation over the Cartesian RPG/DB knot grid, calibration preset
contexts and saturated inputs. It is intentionally outside the repository.
