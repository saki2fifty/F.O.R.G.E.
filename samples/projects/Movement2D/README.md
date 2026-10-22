# C++ 2D Movement

Copy **Examples/Movement2D** to a writable project location and open its project
in FORGE. Packaged copies include a matching compiled C++ module and prepared
assets. From a source checkout, build Project Code and publish the assets first.
This example requires the 2D swept-query integration; Windows Build201 predates it.

Press Play, open Game, and choose **Capture gameplay input**. Use **A/D** or the
**left/right arrows** to move, and **Space** to jump. A gamepad's left stick and
south button use the same project actions. Escape releases the editor's input.
The amber player starts over the left painted platform. Jump onto the small blue
box stepping stone, then the higher painted platform on the right. The translucent
strip is a sensor: it never supports the player. Blue walls keep the player inside
the course. Stop restores the saved starting position.

Select **Player — C++ 2D Movement** and change **2D Movement** in the Inspector:

- `speed`: horizontal speed in metres/second, from `0` to `20`.
- `jump_speed`: upward launch speed in metres/second, from `0` to `20`.
- `ground_probe`: support-check distance, from `0.02` to `1` metre.
- `floor_mask`: the project's collision-layer bits to consider as support.
- `grounded`: this example's runtime observation; leave the saved default false.

Play's **Live gameplay** values affect the current run. Stop before changing the
saved values used by the next run/export. Hold Space after landing: jumping needs
a fresh press, so it does not repeat automatically. Increasing `jump_speed` raises
the jump; changing `speed` changes its horizontal reach. A support probe can allow
a jump just above a floor within `ground_probe`; this deliberately small tolerance
is not a general character controller or penetration recovery.

The painted surfaces use the same original FORGE assets as Feature Gallery77.
Open **Assets/Tiles/PaintedPlatforms.tilemap.json**, select **Solid platforms**,
erase cells and Save / Publish. The next Play/export uses those edited floors.
The ordinary blue stepping stone uses a separate Box Collider2D. Collision outlines
show the player's capsule; its amber Sprite is a simple rectangular marker.

## C++ and ownership

Content → Code contains **Components/Movement2D.hpp** and
**Systems/Movement2DSystem.cpp**. The fixed system reads project input, reads copied
body state, casts its capsule down, and requests a horizontal/vertical velocity.
It excludes itself, sensors and initial side-wall overlaps from the support query.
A surface needs a sufficiently upward normal; walls and ceilings are not ground.
The Physics Body2D remains the sole writer of motion and pose. There is no Lua,
runtime editor, replacement solver, automatic stair climbing or native controller
component. This example requires a Dynamic solid capsule, unit scale and locked
rotation. Keep that profile when experimenting with movement settings.

Stop, save edited C++, **Build Project Code**, then Play again. Input mappings are
ordinary Project Settings actions. The compiler is a development tool and is not
shipped to the player.

## Export

Save the Scene and publish any changed assets. Export Game to a new destination,
using **runtime-kits/shared-native-sdk** and this project's selected module-kit
parent folder. Packaged projects initially use **Native/Builds/example**; an
editor build creates/selects its own deployment. The exported game includes the
compiled C++, prepared Sprites/Map and matching runtime dependencies. Run
**forge_game.exe** on Windows; A/D/arrows and Space use the same input map without
the editor or C++ source.
