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
- **Tile Map Collider 2D**: full-cell or convex tile collision from one published Tile Map layer, on a Static or Kinematic body; Sensor makes a trigger.

Set **Spatial binding** to **World** before choosing Dynamic. Static bodies
stay in place; Kinematic bodies follow supported authored/animated targets;
Dynamic bodies respond to gravity and collisions. A Sprite's width and height
are separate from its collider dimensions. **Selected collision** in the Scene
view displays the selected body's authored collision outline.

Circle and capsule shapes need equal absolute X/Y scale. Bodies and their
effective parents must remain in XY with Z-only rotation and no shear. A body
cannot also run an enabled 3D body or Character Controller. Visual children can
follow a Dynamic body; another simulated body cannot inherit its movement.

## Create a one-way platform

1. Add **Physics Body 2D** and **Box Collider 2D** to an ordinary entity.
2. Set **Motion** to **Static**, leave **Sensor** off, then enable **One-way collision**.
3. Set the Box width and height to match the platform. Objects can pass through from below and land on its local positive Y face. Z rotation changes that face; negative Y scale reverses it.
4. Enable **Selected collision** in Scene. The arrow points toward the solid face.
5. Leave **One-way margin** at `0.02 m` initially. It controls tolerance near the solid face; the permitted range is `0` to `1 m`, with a minimum effective contact tolerance of `0.005 m`.
6. Save and Play. Stop restores the authored scene. Save before exporting with the matching runtime kit; the saved platform behavior also works in the game.

Try **Scenes/79-one-way-platforms.scene.json** in FeatureGallery. The two left
circles fall onto ordinary and one-way platforms. On the right, reverse gravity
pushes two circles upward: the ordinary platform stops one, while the one-way
platform lets the other reach the ceiling. No Project Code build is needed.

In **Examples/Character2D**, walk slightly left under the green platform and jump.
The character passes through its underside and lands on top. Its C++ code still
chooses jump speed and gravity; the controller handles the platform response.

This setting supports Static nonsensor Box platforms and painted Tile Maps.
Moving one-way platforms are unavailable. Geometric ray, overlap and sweep
queries still see the whole shape. Checked character placement rejects embedded
positions even when the character could pass through during ordinary movement.

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
example uses a Dynamic body and capsule sweeps. For the dedicated controller,
use **Examples/Character2D** as described below. Its optional **Step Height**
handles bounded stair rise; 2D navigation remains unavailable.

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

## Painted Tile Map collision

Open **Scenes/77-painted-2d-collision.scene.json** and Play. Colored bodies fall
onto the green painted floors; the blue decorative arrows let them pass through.
Stop and open **Assets/Tiles/PaintedPlatforms.tilemap.json**. Choose **Solid
platforms**, erase the left raised platform, then **Save / Publish**. On the next
Play, the amber box drops to the lowest floor. Undo the source edit and
**Save / Publish** again to restore it.

For your own Map, put **Tile Map Renderer**, a Static or Kinematic **Physics Body 2D**
and **Tile Map Collider 2D** on the same entity. Choose its **Collision layer** in the
Inspector. Use this collider instead of a Box, Circle or Capsule Collider 2D.
Publish the Map before Play or export. **Selected collision** outlines the
published layer; unsaved painting is not yet the active collision.

Choose **Full cell**, **None** or **Convex polygon** for each tile in the Tile Set
Inspector. Full cells include transparent image pixels; None is decorative and
convex polygons can form slopes. Apply and publish the Set, then republish the Map.
Choose Kinematic for a moving Map or enable Sensor for triggers. Dynamic Maps are
unavailable. Static nonsensor Maps using full cells can use **One-way collision**;
custom polygons cannot. See [Tile Maps](tile-maps.md)
for profile controls, limits, moving courses and trigger examples.

## Character movement

Open a writable copy of **Examples/Character2D** for a player controlled by C++ and
**Character Controller 2D**. Build Project Code with the matching SDK, then Play.
Capture Game input and use A/D or arrows to walk; press Space to jump. Holding
Space does not repeat after landing. A gamepad's left stick and south button work too.

Select the Player. **Character Movement 2D** exposes speed, jump speed, gravity and
terminal speed. Change authored values while stopped and save, or try temporary
changes in **Live gameplay**. The live **Character movement** section reports
ground state, velocity, posture and blocked placement/standing. Stop restores authored values.

Enable **Scene → View → Selected collision** to see the feet-origin capsule and,
during Play, its support normal and velocity. The player can stand on the painted
floors and box stone. Walls block it; the marked sensor does not.

For your own player, add **Character Controller 2D**, then supply C++ behavior
through Project Code. The example source is under **Content → Code**. It reads
input, chooses movement, applies gravity and permits a jump from walkable ground.
Use an upright object with **Child space: World**; its origin is its feet. Keep
positive Y scale and equal absolute X/Y scale. Attach visual children if they
need independent rotation. Do not add an enabled Physics Body or another controller
to the same object.

Grounded mode reports floor support and can follow a nearby descending surface.
Floating mode accepts XY movement without floor support. Grounded mode can use
optional **Step Height** for bounded stair rise; 2D navigation remains unavailable.
See the example's **README.md** for the editable source and export experiment.

### Ride a moving platform

1. Open **Examples/Character2D**, build Project Code and press Play.
2. Use A/D and Space to cross the stepping stone to the right painted ledge, then jump onto the cyan **Moving platform - jump aboard**. Once landed, the player travels with it while standing still and can walk or jump off.
3. Stop and select the player. **Character Controller 2D → Follow moving platforms** enables this behavior. It is off by default on new controllers; the example turns it on.
4. Select the platform. **Moving Platform 2D** contains start/end X/Y positions and **duration**, the seconds for one direction of travel. Keep duration between `0.1` and `60`, and path speed at most `20` metres/second. Its **Physics Body 2D → Motion** is **Kinematic**.
5. Change an endpoint or duration while stopped, save the Scene and Play again. Export with the matching runtime and module kit to keep the saved path and controller setting in the game.

For your own platform, use an ordinary Kinematic or Dynamic nonsensor body and
move it through the corresponding C++ body controls. Only walkable support carries
a Grounded character. The capsule stays upright on rotating platforms. Jumping
detaches without automatic momentum from the platform. Reported character velocity
excludes transport; support velocity remains world-space. This does not parent
transforms or follow a teleported platform.

Walls can stop the carried character. If an elevator traps it against a ceiling
or another solid, Play reports an embedded/crushed movement error. Stop and leave
enough clearance or change the path. The engine does not stop the platform for
the character. Moving one-way platforms are unavailable.

### Crouch under a ceiling

1. Open **Examples/Character2D**, build Project Code and press Play. Capture Game input.
2. Cross the stepping stone to the right painted ledge. Hold **C** while walking beneath the pink **Low ceiling - hold C to crouch**.
3. Release C beneath the ceiling. The player stays crouched and **Live gameplay → Character movement** reports **Standing blocked by solid geometry.** Walk clear to stand again.
4. Stop and select the Player. **Character Controller 2D → Crouch Height** sets straight capsule height while crouched. `0` automatically uses half the standing straight height, with a `0.01` metre minimum. A positive value cannot exceed **Height**. Radius and feet position stay fixed.
5. Save the Scene after changing authored settings. Play starts standing; the example's sprite follows the observed posture. **Selected collision** shows the actual crouched capsule during Play. Export with the matching runtime/module kit to use the same C-key behavior.

Your C++ code requests crouch/stand through `sdk::Client::crouch_character_2d`.
Place the starting player where its standing capsule fits; crouch is requested
during Play.
The return value says the request was queued. Read copied character state after
Physics for the result; blocked standing preserves position, motion and posture.
The example retries standing each tick while C is released. The engine does not
create an input binding or resize visual models automatically.

## Slopes and ceilings

Open **Examples/Character2D**, then open **Scenes/surface-response.scene.json**
from Content. Build Project Code with the matching SDK and press Play. The green
and amber characters share A/D, Space and C input. Capture Game input to control
both. The green character holds its place on the ramp when idle; the amber one
slowly slides downhill. Jump beneath the purple ceilings to compare stopping and
sliding. Stop returns both to their saved positions.

While stopped, select either character and find **Character Controller 2D**:

- **Stop on slopes** stops downward-only movement on a walkable floor. It defaults off. Horizontal movement and upward jumps remain controlled by C++; steep surfaces still behave as walls.
- **Slide on ceilings** defaults on. Turn it off to stop the current upward movement at a ceiling. C++ must supply gravity or another movement request afterward; the engine does not add gravity automatically.
- **Max Slope** also defines the ceiling angle: a ceiling must face downward within this angle of vertical. Steeper surfaces keep ordinary wall sliding. These two options apply in Grounded mode; Floating mode ignores them.

Use Undo/Redo to restore a setting. Save before Play or export. **Live gameplay →
Character movement** shows the resulting velocity and support; runtime changes are
temporary. Moving-platform transport remains independent of these controls, so an
idle character can still ride a platform. To export this comparison, choose
**Use saved current scene as startup** in Project Settings before exporting.

## Walking up steps

Open **Examples/Character2D → Scenes/stair-steps.scene.json** in Content. Build
Project Code, Play and capture Game input. Hold **D**: the green character climbs
three 0.25 m treads; the amber character stops at the first. Both share A/D, Space
and C. The red barrier is too tall for automatic stepping; jump to cross it.

While stopped, select a character. **Character Controller 2D → Step Height** is the
maximum automatic rise in world metres, from 0 to 1. It defaults to 0 (off); the
green character uses 0.3. Presentation scale does not multiply this distance.
Use Undo/Redo, save, then Play again. Choose **Use saved current scene as startup**
in Project Settings before exporting this comparison.

Stepping needs walkable ground, horizontal movement, a non-upward request,
headroom and a walkable tread ahead. It uses the current standing or crouched
capsule. A jump, Floating mode or movement in air does not gain an automatic step.
Narrow or obstructed treads can remain blocked; increase clearance or jump.
Stepping never adds horizontal distance beyond the movement request. The character
can briefly report **In air** while crossing a rounded leading edge; releasing movement
there lets it fall normally. Vertical correction is separate from jump velocity.
For walking down larger drops, **Floor Probe** controls ground following; ordinary
gravity applies when the next floor is beyond that distance.

Saved settings and the compiled game code travel with export. Stop restores the
saved character positions; runtime inspection does not save a new staircase pose.

## Query objects along a line

Project Code supports closest and complete multi-hit XY rays. Complete rays return
one nearest hit per object, ordered along the segment, with optional sensor and
layer filtering. A compound or painted Tile Map contributes one object result.
A ray starting inside a shape reports fraction **0** and zero normal. A read does
not wake bodies or change the saved scene. Open the
[physics volume and ray example](../samples/physics-volumes.md)
for ready-to-run XY scenes, adjustable ray settings and a project-code example
that leaves selected objects out of the ray.

## Export

Save the Scene and choose **Use saved current scene as startup** in Project
Settings. Export using the matching runtime kit. The saved bodies, Sprite
materials and prepared animation clip travel with the game. Optional C++ behavior
travels as its compiled module. Players need neither source code nor a compiler.

The two C++ examples use the same export workflow. Character2D exports its compiled
movement code and controller settings. Movement2D uses a Dynamic body and a support
sweep. Their source files are not needed to run the exported games.

## Visual examples

![Boxes, circle and capsule on a floor below a sensor and moving platform](../images/2d-physics-runtime.png)

Gallery75 demonstrates 2D bodies, shapes, a moving platform and a sensor.

![2D capsule on a platform with live ground, velocity and placement feedback](../images/2d-character-inspector.png)

Character Controller 2D reports ground contact and movement in Live gameplay.

![Amber player jumping above painted floors](../images/2d-movement-runtime.png)

The editable Movement2D project combines C++ input with walking and jumping.

![Dynamic shapes resting on green painted tile platforms](../images/painted-collision-game.png)

Painted solid tiles act as floors while decorative tiles remain non-colliding.
