# Touch input

FORGE maps up to eight simultaneous touch contacts to project input actions. Touch
uses the same Game capture, action bindings, Pause/Step and C++ queries as other
input. A touchscreen and a supported desktop SDL touch driver are required for
physical input; desktop mouse clicks do not become touch contacts.

## Test a contact

1. Stop Play and open **Tools → Project Settings**.
2. In **Input actions**, add a digital action named `Touch down`. Choose **touch.0.down** as its **Control**.
3. Add three **axis1** actions with controls **touch.0.x**, **touch.0.y** and **touch.0.pressure**. Keep their scales at1 and deadzones at0. Save Settings.
4. Start Play. Open Console and expand **Gameplay input**. In Game, choose **Capture gameplay input**.
5. Touch inside the Game image. Contact0 shows held; X/Y range from0 to1 relative to the image's top-left. Pressure also ranges from0 to1; some devices report a constant pressure.
6. Move the finger. Coordinates update on fixed ticks. Lift it to release the contact and clear its coordinates and pressure. Run Play to test native input; Pause shows the last completed tick. Clock/capture controls may release input, so lift and touch again after recapturing.
7. Touch with a second finger to use **touch.1.down/x/y/pressure**. Add bindings for those controls when your game needs them.

Use separate axis1 actions for independent screen coordinates. An axis2 action
normalizes continuous vectors longer than1, which changes screen coordinates near
some corners. A digital pressure action can use **Press threshold**; touch values
have no negative direction. A rebind listener captures contact-down, not incidental
coordinate motion. Coordinate bindings remain selectable directly in Settings.

## Contact slots and capture

A new contact takes the first free slot from0 to7. A held finger keeps its slot;
other contacts do not shift when a finger lifts. A newly touching finger may reuse
a freed slot. Slots are temporary input channels, not player or device identities.
Contacts beyond the eight-slot limit are ignored until they lift and touch again.

A touch must start inside the Game image. A drag that starts outside cannot join
partway through. A tracked finger can move beyond the image; coordinates clamp to
its edge. Capture/focus loss, Stop, a changed Game image or window size, and input
neutralization clear contacts. Lift and touch again after these changes.

Without editor capture, touch keeps SDL's normal mouse-style editor interaction.
A touch on an in-game UI control belongs to that UI; it does not also press a game
action. While the UI owns its pointer gesture, other fingers do not join gameplay.
Touch-generated mouse events do not duplicate gameplay clicks or mouse motion.

## Gallery example and C++

Open **Examples/FeatureGallery**, then **Scenes/88-touch-input.scene.json**. The
project already contains the four Touch0 actions. You can test their values in
Console without C++.

For a moving visual marker, compile **CodeExamples/TouchCursor.cpp** and
**TouchInput.hpp** in your matching project module and call
`forge_register_system_TouchCursor(host)` from its system-registration function.
The SDK sample target **touch_input_gameplay** provides a complete module wrapper.
It declares `project.touch_input`, implementation1, with `forge.input` and
`forge.transforms` dependencies. Use the matching module fingerprint and normal
project module registration described in [C++ code](native-gameplay.md).

The optional system moves Gallery88's Sprite on its fixed16-by10 canvas, changes
its size with pressure, and hides it after release. It affects the running copy.
Stop restores the authored marker. The helper's ActionIds match the saved Gallery
input map; keep those IDs when using the example unchanged.

Save the project input settings and Scene, build the matching C++ module if used,
and export normally. The exported Windows host maps touch to the full game window.
UI-owned gestures remain with its UI. It does not require editor panels or a
compiler on the machine running the export. Physical Windows touch behavior still
depends on the device and driver.

## Limits and troubleshooting

- No touch events: confirm the Game owns input, use a real touch device, and lift then touch again after focus/capture/size changes. A mouse-emulated touch event is ignored.
- Unexpected contact number: a lower-numbered finger lifted and a new finger reused that free slot. Use contact-down/release transitions to track your own gesture.
- Coordinates change near corners: use separate axis1 actions with zero deadzone and scale1.
- Marker stays still: the Scene alone displays the authored marker. Build/register the optional C++ system, retain the Gallery ActionIds and capture Game input.
- Native touch contacts are supported in the desktop host. Two-finger pan/pinch/rotation are available. Single-finger gesture classifiers, pen data, sensors, mobile application packaging and assignment of devices to multiple players are unavailable.

## Two-finger pan, pinch and rotation

Create bindings for these controls in **Tools → Project Settings → Input actions**:

- **touch.gesture.active** — digital. Held while at least two admitted contacts remain.
- **touch.gesture.pan_x** — axis1. Horizontal movement of the pair's center, in fractions of the Game image width.
- **touch.gesture.pan_y** — axis1. Vertical movement of the pair's center; positive moves down.
- **touch.gesture.pinch** — axis1. Change in logarithmic pixel distance; positive spreads the fingers.
- **touch.gesture.rotation** — axis1. Change in angle, in radians; positive is clockwise on screen.

Pan, pinch and rotation are **relative** values. They accumulate until a tick uses
them, then return to zero. Use scale1; analog deadzones and response curves do not
apply. Your code should apply a delta once without multiplying it by tick duration.
A quick gesture can end before a tick and still supply its movement for that tick.
Use Active to show gesture status, rather than discarding deltas when it is up.

The pair keeps its two fingers while both remain. Additional fingers do not change
it. If a paired finger lifts and two contacts remain, FORGE chooses a new pair and
starts measuring from its current position, without a jump. Rotation handles the
angle wrap at a full turn. If fingers are less than one image pixel apart, scale
and rotation wait for a useful separation and then start from there.

### Visual example

Open **Scenes/89-touch-gestures.scene.json** in FeatureGallery. Its project already
contains the five gesture actions. Compile **CodeExamples/TouchGestures.cpp** with
**TouchGestures.hpp** and call `forge_register_system_TouchGestures(host)` from your
module's system registration. The installed SDK sample target
**touch_gestures_gameplay** provides the complete `project.touch_gestures` module,
implementation1, with `forge.input` and `forge.transforms` dependencies.

Start Play and capture Game input. Put two fingers inside the image, move them,
spread/close them and turn the pair. The optional C++ system pans, scales and rotates
the canvas Sprite. Its example scale stays between0.25 and4, and its position stays
within X±32/Y±20 world units. The authored camera uses a16-by10 canvas. These are
example choices; the engine reports input rather than imposing camera movement.

Save the Scene/input settings and build/register the matching module before
exporting. The exported game uses the same actions and code. Stop or restart
restores the authored canvas; gesture movement is not automatically saved. The
Scene alone displays the canvas and lets you monitor input in Console.

Keep UI-owned gestures in the UI. Capture/focus changes require fresh contacts.
Gesture results depend on the touch driver supplying distinct finger positions;
pressure is unnecessary for pan, pinch and rotation. Double-tap, long-press, swipe
classification and inertia are unavailable.
