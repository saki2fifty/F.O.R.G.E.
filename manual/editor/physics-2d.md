# 2D physics

2D bodies move in the world's XY plane. Positive Y is up; rotation is around Z.
They use the same Play, Pause, Step and Stop controls as the rest of FORGE.
Their saved settings belong to the Scene; the running simulation owns velocity,
sleep and contact state. 2D and 3D bodies do not collide with each other.

## Try the gallery

Copy **Examples/FeatureGallery** to a writable folder and open it. Open
**Scenes/75-2d-physics.scene.json**, press Play, then switch to Game. Three colored
boxes, a circle and a capsule fall through a green sensor strip. The blue
Kinematic platform moves beneath them; the floor and walls are Static solids.
The capsule's Sprite children follow its moving body.

Stop and select **Dynamic amber box**. In **Physics Body 2D**, change
**Restitution** to `0.8`, then Play to compare its bounce. Stop and change
**Motion** to Static to hold it in place. Undo restores the earlier setting.
Stop always returns to the authored starting layout.

Select **Green sensor** and disable **Sensor**. Play again: the strip is now
solid. Stop and Undo to restore the pass-through zone. The sensor exposes copied
Begin/End observations to C++; it does not change color automatically.

## Set up your own body

Use an ordinary entity with **Sprite Renderer** for the image. In Inspector,
choose **+ Add Component → Physics Body 2D**, then add one of:

- **Box Collider 2D**: full width and height, centered on the entity.
- **Circle Collider 2D**: radius, centered on the entity.
- **Capsule Collider 2D**: radius and straight segment height along local Y. Total height includes both rounded ends.

Set **Spatial binding** to **World** before choosing Dynamic. Static bodies
stay in place; Kinematic bodies follow supported authored/animated targets;
Dynamic bodies respond to gravity and collisions. A Sprite's width and height
are separate from its collider dimensions. **Selected collision** in the Scene
view displays the selected body's authored collision outline.

Circle and capsule shapes need equal absolute X/Y scale. Bodies and their
effective parents must remain in XY with Z-only rotation and no shear. A body
cannot also run an enabled 3D body or Character Controller. Visual children can
follow a Dynamic body; another simulated body cannot inherit its movement.

## Settings and C++

Density is kg/m². Mass `0` derives mass from density and scaled area; a positive
mass overrides it. Friction, restitution, gravity factor, drag, sleeping, axis
locks and Bullet are editable body settings. Layers and masks use the existing
named Project Physics slots. Project gravity XY drives 2D bodies.

**CodeExamples/Physics2D.hpp** provides queued launch velocity and sensor-reading
helpers for the matching C++ SDK. Read events in PostPhysics; their availability
lasts for the current tick. Epochs distinguish a retired body from a replacement
with the same authored identity. See the gallery's **CodeExamples/README.md** for
units, permissions and registration context.

Bullet improves high-speed solid collision with supported receivers. It does
not guarantee exact projectile timing or every fast sensor crossing. The native
continuous sensor path retains eight candidates per fast body per step.

## Export

Save the Scene and choose **Use saved current scene as startup** in Project
Settings. Export using the matching runtime kit. The saved bodies, Sprite
materials and prepared animation clip travel with the game. Optional C++ behavior
travels as its compiled module. Players need neither source code nor a compiler.

This first profile does not generate collision from Tile Maps and does not add
2D joints, 2D character controllers or 2D navigation. Build261021-000195 predates
this feature; use a delivery containing the 2D physics milestone.
