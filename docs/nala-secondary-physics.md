# Nala secondary motion

The world, character information panel and inventory sample the same sparse skin overrides in metre space. Each view owns a separate simulation. Pause freezes its clock, and seeks, teleports over half a metre or gaps over 250 ms reset it to the authored pose. A worker advances fixed 1/120 s ticks between the latest animation samples. Body joints use the shared PhysX 5.13 GPU runtime and a private TGS scene. An unavailable CUDA runtime leaves the animated pose and reports an error.

## Body

The SMA 0.10.4e base, middle and tip D6 presets form a three-link chain on each side. The frames run from PeChoBase through the three PHYSICS_Pecho points. Each joint drives its corresponding weighted subtree. Each glute rotates DEF_Glute about its own origin with its mass towards CENTER_OF_MASS_Glute, with the inverted lateral frame. World gravity is 9.81 m/s². Use 21 position iterations, one velocity iteration and angular drag 0.05. The tip locks all rotation. Glutes lock all linear axes and use twist limits −27.5°/+45° and swings of 23°/9°. The drive targets (2°, 3°, 0°, 12°) and axis-specific spring, mass blending and damping percentages are in Game/PhysicsBreast.inl.

The supplied density coefficients are relative. SMA's final runtime masses, character scale modifiers, exact collider volumes and playback trajectories are unavailable in the reference. At neutral size the three breast masses share Nala's previous 0.45 kg per side in proportion to 0.66, 0.1446192 and 0.0041712. Glutes use coefficient 4.5 on the same scale. Each part scales by the cube of its measured segment length ratio. Drive effective mass includes descendants. Travel is based on animated segment lengths at reset. The base target is −45% of its segment length, its travel 85%, middle 40%, tip 55%. Four positional passes enforce the asymmetric travel and locked axes after the D6 solve.

## Hair

The body and four wearable hair meshes use 20 optional guide joints defined in the mod's human.v1 profile. They follow four guides with five samples each for the loose left and right sections, ordinary braid and armour braid. Authored vertices above the guide root stay attached to their original bones. Each guide follows its animated scalp/neck/spine rest frames and the skin blends neighbouring samples. The older 378-joint model still runs body simulation with no hair guides.

Hair runs on the character worker with a Verlet chain and one distance pass per fixed tick. World gravity is 40 m/s² down, velocity retention 0.8666667044. Root mobility is zero. Four other mobilities are sampled with cubic Hermite interpolation from Stilo_Q_Corto for loose hair and Stilo_Q for both braids. These are adaptations to Nala's two hair lengths. The source uses GPU strand particles. Nala uses bone guides, without strand-level contacts, external wind or dynamic joint-area providers. The source radius (0.00666 m) and wind multiplier (1) do not produce collisions or wind by themselves.

## Checks

Run tools/check-physics-fx.ps1 for the actual GPU solver, asymmetric motion, pinned hair, duplicate frames, reset and missing runtime. The native render harness decodes the selected game's GLB, exercises idle/walk/run and verifies that UI placement, equipment palette indices and pause stay consistent. Its D3D11/12 images exercise the debris path. An actual game session is required for visual evaluation of Nala and her equipment.
