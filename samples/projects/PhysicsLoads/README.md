# C++ Physics Loads

This ordinary FORGE project shows how C++ Components and Systems push and rotate
physical bodies. It is separate from the compiler-free Feature Gallery.

Copy the complete `Examples/PhysicsLoads` folder to a writable project location.
Open it with **File → Open project...**. The combined Windows package includes the
matching compiled module in `Native/Builds`, so you can press **Play** immediately.
Pause after about two seconds to compare the six cubes. Stop restores their saved
positions and resets the example's progress. There is no gravity in these cubes.

| Cube, left to right | Behavior |
| --- | --- |
| Force — 1 kg | Six Newtons upward for two seconds, then coasts. |
| Force — 2 kg | Same force, twice the mass: half the acceleration. |
| Impulse — 1 kg | One upward six Newton-second impulse, then coasts. |
| Torque — 1 kg | One Newton-meter around world Z for two seconds, then coasts. |
| Angular impulse — 1 kg | One Newton-meter-second around world Z, then coasts. |
| Off-center impulse — 1 kg | Same upward impulse, one meter along world X from the body origin; rises and spins. |

The scene camera is framed for the first two seconds. Free moving cubes eventually
leave it; Stop and Play repeat the comparison. Native damping slowly reduces motion.

Select a cube and change **Body Loads** in the Inspector. `kind` is 0 force,
1 impulse, 2 torque, or 3 angular impulse. `x/y/z` are world vector components in
the units above. `duration` controls sustained force or torque; impulses happen
once. `at_point` is only valid for force/impulse. Its offsets use world axes from
the current body origin. `elapsed` and `triggered` track this example's runtime
progress; keep their saved defaults at zero/false. Change Physics Body `mass` to
compare acceleration. Play's Live gameplay edits affect the running copy.

## Inspect and change the C++

Content → Code contains `Components/BodyLoads.hpp` and
`Systems/BodyLoadsSystem.cpp`. The Component holds reflected data; one fixed-step
System runs on all matching bodies. It requests physics loads and lets the engine
own velocity and transforms. It does not write rotation or position itself.

To change source, stop Play, save the C++ document, and **Build Project Code** with
the matching installed SDK and compiler tools. From the repository source, build
once before Play because compiled binaries are created by packaging, not stored
in Git. The managed source markers support the editor's ordinary build workflow.
The compiler is not included in the FORGE ZIP.

## Export

Save the scene. Choose Export Game, select a new destination, choose
`runtime-kits/shared-native-sdk` from the FORGE installation. **Module kits** takes
the parent folder containing the `project.gameplay` kit: use `Native/Builds/example`
for the supplied deployment. That child kit contains `forge.module-kit.json`
beside the compiled `gameplay.dll`. An editor rebuild creates its own new
deployment under `Native/Builds`. Build's selected kit parent is authoritative.
Export and run `forge_game.exe` on Windows. The game contains compiled C++ and
matching runtime dependencies; it does not compile source or need an editor.
