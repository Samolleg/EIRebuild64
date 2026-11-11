# Nala shape inputs

`BodyShape.h` captures RPG strength/dexterity and prototype DB strength/grace/tall
from CUnit. `GetBodyShape()` refreshes live inputs before a render snapshot.
Refresh also follows loading, RPG recalculation, network stat import and camp
setup. The state validates finite inputs and preserves existing RPG precision.

SKN section 8 owns the resolver tables, 45 controls and model authoring program.
The renderer resolves and rebuilds a unit-local bind mesh/rig on first display
and changed inputs. Subsequent animation evaluates bones and GPU skin/contact.
`BodyShapeSchema.h` retains the control ordering shared with the cooker.

The accepted source resolver and calibration tables are under
`tools/nala-to-skn/nala/morphs_head105_v1`. The offline cooker serializes them
without changing their binary64 precision or resolution order. Provenance and
the section contract are recorded in `docs/skn-format.md`.