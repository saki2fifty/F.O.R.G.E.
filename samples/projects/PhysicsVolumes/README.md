# C++ physics volumes

Open `forge.project.json` in FORGE. The matching compiled C++ module is included
in the Windows package; editable source is under `Native`. Add a camera and light
when copying the component to a new scene.

Press Play: a red sensor moves through two cyan static cubes. **Volume Probe**
queries a sphere centered at the origin every physics step. Its `last_count`
changes from 2 to 3 and back to 2 as the moving sensor intersects the sphere.
The Console reports count changes. This queries current native geometry rather
than interpreting contact-cache events as continuous occupancy.

Stop Play and adjust radius, center, speed or `include_sensors` in the Inspector.
With sensors excluded the count stays at 2. Speed is the initial launch velocity;
changing it after launch does not issue another command. The blue floor square
is a footprint marker for the default radius, not an exact sphere visualization;
it has no collider. The solids and moving sensor each have one cube collider.

The query returns copied unique scene/entity identities in deterministic order.
Native contact tolerance applies at boundaries. Queries do not wake sleeping
bodies, generate contact events or change the authored scene. Ordinary runtime-worker recovery retains reflected counters and launch progress.
SDK Play restarts from the authored scene. Export includes the
compiled module; players do not need a compiler or these C++ source files.

## Complete rays in 3D and XY

The **Ray query** entity in `Scenes/body-volumes.scene.json` casts from (-5,0,0)
to (5,0,0). Its **Ray Probe** shows `last_count` and `nearest_fraction` in Live
Gameplay. The default count is 3 until the moving sensor leaves the segment;
excluding sensors keeps 2. The nearest fraction is 0..1, or -1 on a miss.

Open `Scenes/planar-rays.scene.json` for two static XY boxes and a stationary
sensor, drawn as cubes. Leave **Ray Probe → planar** on. Count is 3, or 2 without
sensors. Move a box off the line in Edit, save and Play to see a changed count.
The source uses typed Physics Body 2D/Box Collider 2D geometry, not 3D collisions.
The Z fields in Ray Probe are unused in this mode. Before exporting this scene,
save it and choose **Use saved current scene as startup** in Project Settings.

`Native/Components/RayProbe.hpp` and `Native/Systems/RayProbeSystem.cpp` use the
matching public SDK clients for copied multi-hit rays. Each object contributes its
nearest hit, including compounds, and results follow distance along the segment.
A complete result is limited to 256 bodies; exceeding a budget reports failure.
Keep saved observed/count/nearest at false/0/-1. Stop restores saved values; the
queries do not mutate source geometry or add recovery state of their own.
