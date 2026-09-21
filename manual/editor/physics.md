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

Collider geometry is independent of the visible primitive. Changing a mesh's Shape does not change its collider. Match their dimensions yourself. Collider wireframes are not available in this first integration.

Scaled collider dimensions must stay between .001 and 10000 meters. Boxes support signed nonzero scale on each axis. Spheres and capsules require equal nonzero scale magnitudes; their signs may differ. These centered shapes are symmetric, so mirroring does not change their physical solid. Zero/tiny scale rejected by Jolt, shear, unresolved spatial parents, and invalid dimensions are rejected before physics realization. Static and kinematic bodies may follow non-dynamic spatial parents when their final world transform meets these restrictions.

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

This integration supports boxes, spheres, capsules, a default static/moving collision filter, and a gameplay raycast/target service. Mesh colliders, compound authoring, characters, joints, vehicles, cloth, and a collision-layer editor remain future work.

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
The complete animated-model placement/rendering workflow is still under integration.
