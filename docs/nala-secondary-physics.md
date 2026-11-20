# Nala secondary motion

The world, character information panel and inventory sample the same sparse skin overrides in metre space. Each view owns a separate simulation. Pause freezes its clock, and seeks, teleports over half a metre or gaps over 250 ms reset it to the authored pose. A worker advances fixed 1/120 s ticks between the latest animation samples. Hair guides run in the character worker independently of the debris scene.

## Body

The original skeletal animation and the procedural layers drive the body directly.
The rib cage and fitted clothing keep the 4.4-second breathing cycle in idle; the seat
keeps its side-specific shape change from the difference in thigh directions during
a stride. The idle/active blend uses the existing 75 ms time constant. Only hair
produces simulated offsets; chest and glute bones have no inertial bodies or joints.

## Hair

The body and four wearable hair meshes use 20 optional guide joints defined in the mod's human.v1 profile. They follow four guides with five samples each for the loose left and right sections, ordinary braid and armour braid. Authored vertices above the guide root stay attached to their original bones. Each guide follows its animated scalp/neck/spine rest frames and the skin blends neighbouring samples. The older 378-joint model still animates breathing and weight shifts without hair guides.

Hair runs on the character worker with a Verlet chain and one distance pass per fixed tick. World gravity is 40 m/s² down, velocity retention 0.8666667044. Root mobility is zero. Four other mobilities are sampled with cubic Hermite interpolation from Stilo_Q_Corto for loose hair and Stilo_Q for both braids. These are adaptations to Nala's two hair lengths. The source uses GPU strand particles. Nala uses bone guides, without strand-level contacts, external wind or dynamic joint-area providers. The source radius (0.00666 m) and wind multiplier (1) do not produce collisions or wind by themselves.

## Checks

The hair check covers anchored roots, lag, rewind and worker publication. The native
rig check covers breathing, stride loading, shared UI placement and pause.
