# Physics

Physics makes objects fall, collide, and come to rest during Play. FORGE uses Jolt. The scene stores your body and collider settings; the running game owns movement, velocity, and sleeping state.

## Make a falling cube

1. Create a cube named **Floor**. Set its position to **(0, -0.5, 0)** and scale to **(10, 1, 10)**.
2. In Inspector, use **+ Add Component** to add **Physics Body**, then **Box Collider**. Leave **Motion** at **Static**. The box's default dimensions are one meter; the object's scale makes it match the floor.
3. Create another cube at **(0, 5, 0)**. Add **Physics Body** and **Box Collider**.
4. Set this cube's **Spatial binding** to **World**, then set its physics **Motion** to **Dynamic**.
5. Press **Play**. The cube falls and rests on the floor. No gameplay code is needed.
6. **Pause** freezes simulation. **Step** advances exactly one fixed tick. **Resume** continues.
7. **Stop** returns to the authored positions. Save, reopen, and Play to repeat.

Duplicate the falling cube and move the copy sideways to test several bodies. You can also [create a prefab](prefabs.md) from it.

## Body motion

**Static** uses the authored transform and ignores gravity. Use it for floors and walls.

**Kinematic** follows a target supplied by gameplay. It can push dynamic objects. Merely choosing Kinematic does not add a movement controller.

**Dynamic** is moved by the solver. It requires **Spatial binding: World**, even when it has a structural parent. Moving the parent then does not pull the body through the world. FORGE rejects an unsupported binding instead of changing your hierarchy.

Physics runs only in Play. Editing an object does not run a hidden simulation in the authoring scene.

## Parenting physics objects

A separate **Static** or **Kinematic** Physics Body cannot spatially follow a **Dynamic** Physics Body. This also applies through intermediary objects and **Explicit** spatial-parent links. Before physics realization or the next simulation step, FORGE reports the affected object and Dynamic ancestor and rejects the configuration. Recovery performs the same check.

To keep structural ownership while the bodies act independently, set the child's **Spatial binding** to **World**. This breaks spatial inheritance. For example, a car can structurally own another body without pulling that body along with it. FORGE never changes this setting, your hierarchy, or your transforms automatically.

Objects without a Physics Body—such as a camera mount or visual mesh—can still follow a Dynamic parent. Static and Kinematic bodies can follow supported non-dynamic ancestors. A World-bound intermediary also breaks the spatial chain.

This is an initial restriction on separate simulated bodies. Physically connected collision shapes and bodies need future compound-collider or joint features; transform parenting does not create those connections.

Gameplay translation/rotation targets preserve the current local scale. A target that would require changing that scale, or creating shear, is rejected. Rejected target batches leave existing transforms unchanged.

## Collider sizes

A body currently needs **exactly one** collider, centered on its transform:

- **Box Collider:** `x`, `y`, and `z` are full dimensions in meters, before object scale.
- **Sphere Collider:** `radius` is its radius in meters.
- **Capsule Collider:** `radius` is its radius; `height` is the straight middle section, excluding the two rounded caps. Its long axis is local Y. Total height is `height + 2 × radius`.
- **Cylinder Collider:** `radius` and full `height` are in meters. Its long axis is local Y, with flat ends.

Collider geometry is independent of the visible primitive. Changing a mesh's Shape does not change its collider. Match their dimensions yourself. Collider wireframes are not available in this first integration.

Scaled collider dimensions must stay between .001 and 10000 meters. Boxes support signed nonzero scale on each axis. Spheres and capsules require equal nonzero scale magnitudes; their signs may differ. Cylinders require matching X/Z magnitudes; Y can scale independently. These centered shapes are symmetric, so mirroring does not change their physical solid. Zero/tiny scale rejected by Jolt, shear, unresolved spatial parents, and invalid dimensions are rejected before physics realization. Static and kinematic bodies may follow non-dynamic spatial parents when their final world transform meets these restrictions.

## Weight, friction, and bounce

- `density`: kilograms per cubic meter. Default **1000**.
- `mass`: kilograms. **0** uses density and the scaled collider's volume. A positive value overrides mass; Jolt calculates the corresponding inertia.
- `friction`: resistance to sliding; default **0.5**.
- `restitution`: bounciness, from **0** to **1**; default **0**.
- `gravity_factor`: multiplier for project gravity; **0** disables gravity for this body.

Press **Enter** to commit a numeric edit. Scene Undo/Redo includes these changes. In a prefab instance, editing a supported field records explicit override intent, including an edit equal to the source value. **Revert** restores the source value. the component header’s **Remove component / Revert component** removes local ownership; inherited prefab values can become visible again.

## Project gravity

Open **Project Settings → Physics**. **Gravity XYZ** is acceleration in meters per second squared. The default is **(0, -9.81, 0)** because positive Y is up. Save settings, then start Play again. Project settings have their own save boundary; scene Undo does not reverse them.

## Recovery and current limits

A supported Play recovery restores both the scene/configuration and Jolt's simulation state from one completed tick. It preserves supported velocity, rotation, sleep and contact state. The recovered presentation starts at that pose, without a visual jump from an old interpolation sample.

Recovery is private to the current session and exact runtime build. It is not a saved game. Invalid, incompatible, incomplete, or oversized checkpoints are rejected; the editor reports failure and offers a clean Play restart. Native module globals and external resources are not automatically recovered.

Inline colliders support boxes, spheres, capsules and cylinders. Reusable collision assets add convex hulls, static triangle meshes and compounds. Characters, collision debug drawing and the rest of this Phase8 block are still being integrated; joints, vehicles and cloth are later work.

See [Play mode](play-mode.md), [Transforms](transforms.md), and [Prefabs](prefabs.md).

A newly instantiated prefab root keeps the existing per-instance spatial attachment policy. If its body is Dynamic, set that instance root's **Spatial binding** to **World** before Play. FORGE does not silently detach it. Duplicating a configured instance retains its binding.

## Physics on animated model nodes

Use **Kinematic** for a body that should follow an imported model animation. **Dynamic**
means physics controls movement; FORGE rejects animation that tries to move or rotate
that same body. A separate visual child can follow a Dynamic parent under the existing
spatial-binding rules.

Collider shape restrictions still apply. For example, an animation that stretches a
Sphere Collider unevenly cannot be applied. FORGE reports the problem and keeps the
previous pose and collider. Change the body/collider arrangement, or remove physics
from a node intended only for visual animation. The source clip is not rewritten.
Use the [Model import document](models.md) to choose a clip and place the model,
then press Play to inspect its supported animation and physics behavior.


## Reusable collision assets

A rendered Mesh does not automatically collide. Create a separate collision asset
when you want reusable or complex collision geometry.

1. In **Content**, choose **New collision...** and enter a new `.collision.json` path.
2. Open the **Collision** document and choose its **Shape**.
3. Set dimensions and local pose. Rotation is entered in degrees. A convex hull or triangle mesh uses the **Source Mesh** picker; it initially selects the engine cube.
4. For a compound, choose **Add box child**, then edit the child shape and local pose.
5. Choose **Save** to save the source and prepare a usable collision revision.
6. Select a scene entity, add **Physics Body** and **Asset Collider**, and choose the collision asset in its picker. Remove any other collider on that entity.

Triangle meshes, including compounds containing them, require a **Static** body.
Geometry comes from the selected Mesh's base positions, without animation or morph
baking. Degenerate triangles are rejected unless **Remove degenerate triangles** is
selected. Geometry and scale errors retain the previous usable collision revision.

The Collision document has its own Undo/Redo history. Scene Undo does not reverse
saved source files or cooked publication. Runtime exports include cooked collision
geometry and do not require its original render Mesh merely to simulate collision.

## Collision layers and sensors

In **Project Settings → Physics → Collision layers**, name the layer slots you need.
Renaming a slot keeps existing references. Clearing a used slot makes that body's
configuration invalid at the next Play; save settings and restart Play to apply changes.

In **Physics Body**, choose the named **Layer** and the layers included in **Mask**.
Two bodies collide only when both masks permit the other's layer. **Enabled** removes
or restores the body in simulation. **Sensor** reports overlap contacts without
solid collision response. Static bodies do not generate static/static contacts;
use an appropriate moving body when that interaction is required.

## Creating collision from a model's Mesh

Expand the imported Model in **Content**, select the Mesh member you want, then
choose **Assets → Create Collision from Mesh...** (also in its context menu).
Choose **Convex Hull** for a solid outer envelope, or **Static Triangle Mesh** for
concave level geometry. Enter a new project filename and choose **Create**. Review
local pose and the **Source Mesh** in the central Collision document, then **Save**.
This creates a separate asset; it does not attach a collider to the rendered object.

Box fields show full XYZ **Size (m)**. Sphere fields show **Radius (m)**. Capsule and
cylinder show radius and **Straight height (m)**; capsule caps add to that height.

## Character Controller (new Phase8 source work)

This batch is still undergoing acceptance and is not included in Build66.

A Character Controller moves a capsule or cylinder through the physics world. It
provides collision, gravity, ground state, slopes, stairs and moving-platform support.
It does not assign movement keys or create a first-person camera.

1. Select the character entity and add **Character Controller** under **Physics**.
2. Set its **Spatial binding** to **World**. Remove or disable a Physics Body on that same entity; the controller supplies its own physical presence.
3. Place the entity's origin at its feet, above collision geometry. Set radius, straight standing height and crouch height to match its visible geometry.
4. Choose **Capsule** or **Cylinder**, then set walkable slope, step height and collision layer/mask for your game.
5. Gameplay code uses the controller service to request movement, jump, crouch or checked placement. Without movement code, Play demonstrates gravity and support.

A crouched character stays crouched when a ceiling prevents standing. A placement
that overlaps solid geometry is refused. Visual children may follow the character;
separate physics bodies need independent World space. Controller settings support
prefab inheritance and Revert. Velocity and ground contacts belong to Play and are
not saved into the authored prefab.

## Inspecting collision in Scene

In **Scene → View**, turn on **Selected collision**, then select a body or character.
The wireframe shows collision geometry separately from its visible Mesh. This option
is saved with your workspace preferences and does not draw into Game View.

- **Green:** prepared collision geometry.
- **Grey:** a disabled body's authored shape.
- **Amber:** preparation is pending, or the displayed geometry is an older revision.
- **Red:** the candidate is rejected; read the message at the bottom of Scene.

Large shapes show a message if the preview reaches its triangle limit; actual
collision is unaffected. This is a geometry inspection tool. Play still validates
spatial parenting and the complete physics configuration.

During Play, a selected character also shows its ground classification, a ground
normal guide and a velocity guide. Its outline follows the current standing or
crouched shape. Blocked standing/placement is reported in the overlay.

Compound children have **Remove child**, which removes that child and its descendants.
The last child cannot be removed: every compound needs at least one shape. Document
Undo restores removed members with their original identities.

For a mesh-based shape, use **Choose mesh geometry...** to inspect its prepared
Mesh, choose an imported LOD, and select individual triangle parts. **All parts**
follows later Mesh revisions. A specific selection records the reviewed revision;
a changed Mesh must be deliberately reselected before collision can be recooked.
**Use geometry** creates one document Undo step; Save validates and publishes it.
