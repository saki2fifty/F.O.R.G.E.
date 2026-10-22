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

For a complete C++ walk/jump project, open a writable copy of
**Examples/Movement2D**. Capture gameplay input in Game, move with A/D or arrows,
and jump with Space. Select the amber Player and change **2D Movement** speed or
jump speed in the Inspector. The example checks for ground with a capsule sweep,
ignoring itself and sensors. Painted floors and a separate box stepping stone
both support it. Stop restores saved state; save/publish before export. See the
project's **README.md** for the
experiment, C++ source and matching runtime/module-kit export workflow. This
requires the swept-query integration after Build201; it does not add a native
character-controller component, automatic stairs or 2D navigation.

Bullet improves high-speed solid collision with supported receivers. It does
not guarantee exact projectile timing or every fast sensor crossing. The native
continuous sensor path retains eight candidates per fast body per step.

## Connect bodies

Create a separate entity, add **Physics Joint 2D**, and choose **Body A** and
**Body B** from the 2D-body pickers. Both must be enabled nonsensor bodies, and
at least one must be Dynamic. The joint entity's own transform does not position
the connection. **Anchor A/B X/Y** are points in each body's local coordinates.

- **Distance** holds the anchors at a fixed distance. A positive **Spring Frequency** makes it elastic; **Spring Damping** controls how quickly it settles.
- **Hinge** keeps the anchors together and lets the bodies rotate. Frame angles define the rest orientation; optional angle limits and a torque-limited speed motor control rotation.
- **Slider** allows travel along the joint frame's X axis. Optional travel limits and a force-limited speed motor control its movement.

Distances use metres; frame/limit angles use degrees. Hinge motor speed is
degrees per second; Slider motor speed is metres per second. A positive speed
moves Body B relative to Body A's frame. **Collide Connected** controls whether
the pair can collide; ordinary body layers/masks still apply. If several joints
connect the same pair, any joint disabling connected collision takes precedence.

Select a joint in Scene to see its authored guide. These are editing aids, not
live force measurements. Stop before editing saved settings, then Play to test.
The same property history and Prefab reference behavior apply. Matching C++ can
change runtime settings before Physics using **CodeExamples/Physics2DJoints.hpp**.

Try **Scenes/76-connected-2d-bodies.scene.json**: a cyan pendulum, amber rigid
tether, rose spring and green motor carriage. Stop, select **Slider - positive
rail motor**, change its speed to `-0.9`, and Play to reverse its direction.

## Export

Save the Scene and choose **Use saved current scene as startup** in Project
Settings. Export using the matching runtime kit. The saved bodies, Sprite
materials and prepared animation clip travel with the game. Optional C++ behavior
travels as its compiled module. Players need neither source code nor a compiler.

This profile does not generate collision from Tile Maps or add 2D character
controllers or navigation. Build261021-000199 includes the first body milestone;
the connected-body extension requires a later delivery containing Physics Joint 2D.
