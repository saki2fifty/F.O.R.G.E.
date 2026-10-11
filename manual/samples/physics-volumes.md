# C++ physics volume example

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
runtime progress for worker recovery. Stop restores the saved scene. Changing
speed after launch does not relaunch the body. Native tolerance applies at the
sphere's boundary; the blue floor square is only a default footprint marker and
has no collider.

Content → Code opens the original Component and System. Stop before editing,
save and Build Project Code using the matching SDK and installed compiler tools.
For Export Game choose `runtime-kits/shared-native-sdk` and module kit parent
`Native/Builds/example`. Players receive compiled code and need no compiler.
