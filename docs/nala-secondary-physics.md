# Nala secondary motion

The world, character information panel and inventory sample the same sparse skin overrides in metre space. Each view owns a separate simulation. Pause freezes its clock, and seeks, teleports over half a metre or gaps over 250 ms reset it to the authored pose. A worker advances fixed 1/120 s ticks between the latest animation samples. Body joints use the shared PhysX 5.13 GPU runtime and a private TGS scene. An unavailable CUDA runtime leaves the animated pose and reports an error.

## Body

Four PhysX D6 acceleration drives track the moving end of each breast and the centre-of-mass marker of each glute. The authored skin is the loaded resting shape. Gravity on these four tracking bodies is disabled and the drive target is zero. An acceleration drive uses angular frequency squared for stiffness and twice the damping ratio times angular frequency for damping. This makes tuning independent of the mass assigned to the tracking body.

| Region | Lateral / vertical tangent / outward frequency | Damping ratio | Visible displacement radii as fractions of the local lever length |
|---|---|---|---|
| Breast | 5 / 4 / 6 Hz | 0.95 | 0.065 / 0.095 / 0.045 |
| Glute | 7 / 6 / 8 Hz | 1.0 | 0.035 / 0.055 / 0.025 |

The support frame follows the animated torso or pelvis, including the current physique. Displacements are bounded in a local ellipsoid when mapped to the skin. The breast base stays attached; the three weighted endpoints receive 20%, 55% and 100% displacement. The pelvis attachment remains pinned and the glute only rotates towards its displaced marker, preserving volume. There are no idle-specific switches. Quiet breathing, sudden acceleration and stopping feed the same solver.

## Hair

The body and four wearable hair meshes use 20 optional guide joints defined in the mod's human.v1 profile. They follow four guides with five samples each for the loose left and right sections, ordinary braid and armour braid. Authored vertices above the guide root stay attached to their original bones. Each guide follows its animated scalp/neck/spine rest frames and the skin blends neighbouring samples. The older 378-joint model still runs body simulation with no hair guides.

Hair runs on the character worker with a Verlet chain and one distance pass per fixed tick. World gravity is 40 m/s² down, velocity retention 0.8666667044. Root mobility is zero. Four other mobilities are sampled with cubic Hermite interpolation from Stilo_Q_Corto for loose hair and Stilo_Q for both braids. These are adaptations to Nala's two hair lengths. The source uses GPU strand particles. Nala uses bone guides, without strand-level contacts, external wind or dynamic joint-area providers. The source radius (0.00666 m) and wind multiplier (1) do not produce collisions or wind by themselves.

## Checks

Run tools/check-physics-fx.ps1 for the actual GPU solver, asymmetric motion, pinned hair, duplicate frames, reset and missing runtime. The native render harness decodes the selected game's GLB, exercises idle/walk/run and verifies that UI placement, equipment palette indices and pause stay consistent. Its D3D11/12 images exercise the debris path. An actual game session is required for visual evaluation of Nala and her equipment.
