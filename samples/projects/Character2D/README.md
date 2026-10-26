# C++ 2D Character

Open this project in FORGE. The player uses **Character Controller 2D** for collision
and movement. Its editable C++ component and systems live in **Content → Code**.
The project shares FORGE's original visual and painted-map assets with Movement2D.

## Try it

1. Build Project Code with the matching FORGE SDK, then press Play.
2. Capture Game input. Use **A/D** or **Left/Right** to walk and **Space** to jump.
   A gamepad's left stick and south button also work. Holding Jump does not repeat.
   Hold **C** to crouch; release to stand when clearance permits.
3. Select the player. **Live gameplay → Character movement** reports ground state
   and velocity. **Character Movement 2D** exposes speed, jump speed, gravity and
   terminal speed. Live changes are temporary; Stop restores authored values.
4. In Scene view, enable **Selected collision** to see the feet-origin capsule,
   support normal and velocity. The outline ignores visual depth.
5. Stop, change an authored speed or controller setting, save, and Play again.
6. Export using the matching runtime and compiled module kit, then run the exported
   game. The game contains the compiled code; its players do not need a compiler.

The painted floors and stepping stone are solid. The marked sensor does not block
the controller. A/D walks into walls rather than passing through them. Landing on
walkable surfaces permits another jump; air and steep surfaces do not.

Walk slightly left to stand under the green **One-way platform - walk left then
jump through**. Press Jump: the character passes through the underside and lands
on top. Stop and select that platform to edit **One-way collision** or **One-way
margin** in Physics Body 2D. **Selected collision** points an arrow toward its
solid face. Save the scene before exporting the same behavior.

Cross the stepping stone to the right painted ledge, then jump onto the cyan **Moving platform - jump aboard**.
The player enables **Character Controller 2D → Follow moving platforms**. It rides
without changing its walking speed and can jump off. Stop and select the platform:
**Moving Platform 2D** exposes start/end X/Y in world metres and **duration**, seconds
for one direction. Duration is0.1..60s and path speed at most20m/s. Its Kinematic
body receives ordinary C++ targets. Save before Play/export; Stop resets the path.

## What owns what

**Character Controller 2D** owns the physical feet position and bounded movement.
**Character Movement 2D** is game policy: the C++ system reads project actions,
chooses horizontal velocity, integrates gravity and requests a jump. A second
system copies the final grounded observation after physics. The sprite's bottom
pivot follows the feet; there is no hidden Physics Body 2D or Capsule Collider 2D.

This example uses Grounded mode. Floating mode accepts full XY velocity but does
not report floor support. Platform following defaults off on new controllers.
Ordinary Kinematic/Dynamic walkable support can carry; sensors, steep surfaces,
other controllers and moving one-way platforms cannot. The capsule stays upright.
Jumping adds no automatic platform momentum. Crush reports a movement error; leave
enough clearance along the path. Stair stepping and
navigation are outside the controller's current profile. It is separate from the
Dynamic-body [Movement2D example](../Movement2D/README.md).

## Crouch and stand

Walk to the right painted ledge and hold C beneath the pink **Low ceiling - hold C
to crouch**. Release C there: the player stays crouched and Live gameplay reports
blocked standing. Walk clear to stand again. The example changes its unit-scale
sprite height from observed posture; the engine keeps physics and visuals separate.

Stop and select the Player to edit **Character Controller 2D → Crouch height**.
0 automatically halves the effective straight height, with a 0.01 m minimum. Positive
values cannot exceed standing **Height**. Feet and radius stay fixed. Save before
Play/export. Runtime posture is temporary; Stop starts standing again. Project input
owns the C binding; C++ sends checked stance requests through the existing character
FIFO and reads blocked status after Physics. No native handles or second body exist.

## Compare surface response

Open **Scenes/surface-response.scene.json** from Content. The green and amber
characters share the same input. Leave input idle: green holds its ramp position,
while amber slowly drifts downhill. A/D moves both; Space jumps beneath the purple
ceilings. Green stops at ceiling contact, while amber slides. C still crouches.

Stop and select a character. **Character Controller 2D → Stop on slopes** defaults
off; **Slide on ceilings** defaults on. This scene sets green to hold/stop and
amber to the defaults. **Max slope** defines walkable floors and mirrored ceiling
angles. Floating mode ignores both options. They affect requested locomotion,
not moving-platform transport. The C++ example supplies fresh downward gravity
while grounded and integrates observed vertical velocity in air; a blocked jump
therefore falls on later ticks.

Save authored changes. To export this scene, select **Use saved current scene as
startup** in Project Settings, then export with the matching runtime/module kit.
Stop restores authored positions. Both scenes use the same original assets/code.
