# C++ physics volumes, rays and exclusions

Copy `Examples/PhysicsVolumes` to a writable folder and open it as a project.
The combined Windows package includes matching compiled Project Code.

Press Play. A red sensor moves through two cyan solids. **Volume Probe** counts
objects intersecting a sphere at the origin: initially2, then3 while the moving
sensor intersects it, then2 again. The Console reports count changes. Select the
moving sensor to see `last_count` in Live Gameplay.

Stop and edit **Volume Probe** in the Inspector:

- `radius`: sphere radius in metres.
- `center_x`, `center_y`, `center_z`: world-space query center.
- `include_sensors`: turn off to keep the default count at2.
- `speed`: initial X launch velocity in metres/second.

Leave saved `launched`, `observed` and `last_count` at false/false/zero. They hold
runtime observations while Play runs. Stop restores the saved scene. Changing
speed after launch does not relaunch the body. Native tolerance applies at the
sphere's boundary; the blue floor square is only a default footprint marker and
has no collider.

Content → Code opens the original Component and System. Stop before editing,
save and Build Project Code using the matching SDK and installed compiler tools.
For Export Game choose `runtime-kits/shared-native-sdk` and module kit parent
`Native/Builds/example`. Players receive compiled code and need no compiler.

## Detect several objects along a ray

In the same project, open **Scenes/body-volumes.scene.json** and select **Ray query**
in Hierarchy. Its **Ray Probe** sends a line from **(-5, 0, 0)** with displacement
**(10, 0, 0)** each physics step. Displacement is the full vector from the start to
the end, so the default ray ends at **(5, 0, 0)**.

Press Play and select **Ray query**. In **Live gameplay**, `last_count` shows how many
objects the line hits. It starts at **3**: the two solids and moving sensor. The
count falls to **2** when the sensor leaves the segment. `nearest_fraction` is the
first hit's position along the segment: **0** at the start and **1** at the end.
It is **-1** when nothing is hit. The Console reports count changes.

Stop Play before changing the saved **Ray Probe** settings:

- `origin_x`, `origin_y`, `origin_z`: world-space start in metres.
- `delta_x`, `delta_y`, `delta_z`: segment displacement in metres.
- `include_sensors`: turn off to query only the two solids.
- `planar`: leave off for this scene's 3D bodies.

Leave saved `observed`, `last_count` and `nearest_fraction` at **false**, **0** and
**-1**. They are observations updated by game code. Save your scene settings before
export; Stop restores those saved values.

### Try the XY scene

Open **Scenes/planar-rays.scene.json**, select **Ray query** and leave `planar` on.
Press Play. The line hits two static boxes and a stationary red sensor: count **3**,
or **2** with `include_sensors` off. Move a box away from the line in Edit, save
and Play again to see the count change. Z ray fields are unused in this mode.
The visible cubes are presentation meshes; their **Physics Body 2D** and **Box
Collider 2D** components supply the XY query geometry.

A ray returns one nearest hit per object, even when that object has several
collider parts. Results are ordered along the segment. Initial overlap in XY
reports fraction **0** with a zero normal; it is not a penetration direction.
The query supports at most **256** objects and rejects an incomplete result.
For export, save the scene and choose **Use saved current scene as startup** in
Project Settings, then use the matching runtime/module kits described above.

Open **Code → Components → RayProbe.hpp** and **Code → Systems → RayProbeSystem.cpp**
to study the original implementation. It uses the matching SDK's
`raycast_all_excluding` and `raycast_all_2d_excluding` C++ helpers. Players receive
the compiled module.

## Leave selected objects out of a query

Open **Scenes/excluded-rays.scene.json**, select **Left solid**, and find
**Ray Exclusion** in Inspector. With `enabled` checked, the example's ray leaves
that cyan object out. It remains visible and still participates in physics.

Press Play and select **Ray query**. The initial `last_count` is **2** instead of
**3**. Select **Left solid** and turn off its **Ray Exclusion → enabled** value in
**Live gameplay**: the next physics step brings the ray count back to **3**.
Turn it on again to return to **2**. These live changes do not save your scene.
Stop, change the authored checkbox and save to keep a new default.

For XY bodies, open **Scenes/planar-excluded-rays.scene.json** and try the same
steps. With the left box excluded, the count is **2**, or **1** if you also turn
`include_sensors` off on Ray Probe. The ordinary volume probe in the 3D scene
continues to count both cyan solids; the exclusion applies only to the demo's rays.

**Ray Exclusion** is a Component defined by this project's C++ code. It selects
objects that **RayProbeSystem.cpp** passes to the SDK query; it is not a global
collision setting. Put it on the actual body object. A compound collider counts
as that one object; independently simulated children have their own identities.
The example collects at most **16** enabled, registered exclusions.

In your own code, pass canonical references to the SDK's `*_excluding` helpers.
Use `entity_reference` during a fixed callback to obtain the calling object's
reference when you need to ignore its own body. The helpers support closest and
complete rays, overlaps and linear sweeps. An empty exclusion list keeps normal
query behavior. Save your chosen scene as startup before exporting with matching
rebuilt runtime/module kits.
