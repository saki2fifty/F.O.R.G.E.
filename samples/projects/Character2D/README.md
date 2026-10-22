# C++ 2D Character

Open this project in FORGE. The player uses **Character Controller 2D** for collision
and movement. Its editable C++ component and systems live in **Content → Code**.
The project shares FORGE's original visual and painted-map assets with Movement2D.

## Try it

1. Build Project Code with the matching FORGE SDK, then press Play.
2. Capture Game input. Use **A/D** or **Left/Right** to walk and **Space** to jump.
   A gamepad's left stick and south button also work. Holding Jump does not repeat.
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

## What owns what

**Character Controller 2D** owns the physical feet position and bounded movement.
**Character Movement 2D** is game policy: the C++ system reads project actions,
chooses horizontal velocity, integrates gravity and requests a jump. A second
system copies the final grounded observation after physics. The sprite's bottom
pivot follows the feet; there is no hidden Physics Body 2D or Capsule Collider 2D.

This example uses Grounded mode. Floating mode accepts full XY velocity but does
not report floor support. Stair stepping, moving-platform carry, crouching and
navigation are outside the controller's current profile. It is separate from the
Dynamic-body [Movement2D example](../Movement2D/README.md).
