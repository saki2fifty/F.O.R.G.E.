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
bodies, generate contact events or change the authored scene. The observation
counter and one-shot launch state survive worker recovery. Export includes the
compiled module; players do not need a compiler or these C++ source files.
