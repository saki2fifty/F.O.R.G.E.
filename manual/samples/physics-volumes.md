# C++ physics volume and ray example

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
to study the original implementation. It uses the matching SDK's `raycast_all`
and `raycast_all_2d` C++ helpers. Players receive the compiled module.
