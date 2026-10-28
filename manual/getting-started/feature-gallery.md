# Explore the Feature Gallery

The packaged **Examples/FeatureGallery** project gives you focused scenes to
open in the editor and export as standalone games. You can see how each effect is
authored, change it, then compare the result in Play and in the exported game.

Copy the whole gallery folder to a writable location before editing it. Choose
**File → Open project...** and select that copy. Use Content's scene list to open
**Transforms**, **Lighting**, **Physics**, **Cameras**, **Editable Mesh**, **Sculpt**, **Vertex Paint**, or **Transform Animation**. The project's `README.md`
gives a short experiment and expected result for each scene.

To export one, open and save that scene. In **Tools → Project Settings**, select
**Use saved current scene as startup** and **Save Settings**. Then use **Run →
Export Game...**, selecting the `runtime-kit` beside the packaged editor and an
output folder outside the project. Run the resulting `forge_game.exe`. Repeat
with another startup scene to export a different demonstration.

The gallery contains ordinary saved project content. Most scenes need no C++ compiler; optional C++ examples require the matching development tools to build. The included [reference game](../reference-game.md)
remains a separate combined gameplay example.

## Top-down 2D navigation

Scene **80-xy-navigation** contains a walkable rectangle, an orange obstacle and
three Sprite agents. Play to see the cyan agent route around the obstacle and
the green/amber pair steer past each other. Select a region to see its outline;
press **F** to frame it and **W** to resize with its gold edge handles. You can also
change Width/Height or Obstacle in Inspector while stopped, then rebuild
through **Content → Create / Register → Navigation → Plane → 2D (XY)**.
During Play, select an agent and use Live gameplay to change its Speed or destination.
Pause before setting several goal coordinates. Stop restores authored values unless
you keep chosen settings through the runtime tuning review.
Navigation progress shows concrete failed details and a **World coordinates**
section for valid route samples.
Save and select the Scene as startup before export. See [Navigation](../editor/navigation.md).

## 2D physics

Scene **79-one-way-platforms** compares landing from above and passage from below.
Select a green platform to edit **One-way collision** and **One-way margin**.
**Selected collision** shows the solid-face arrow. Save and choose this scene as
startup to export the same behavior.

Scene **75-2d-physics** contains falling boxes, a circle and a capsule, a moving
platform and a pass-through sensor. Change body settings in Inspector while
stopped, then compare Play. **Selected collision** shows the authored collider.
The ordinary Sprite children follow the capsule. Save and select this Scene as
startup before export. See [2D physics](../editor/physics-2d.md).

## Tile Maps

Scene **77-painted-2d-collision** lets ordinary 2D bodies land on painted floors
and platforms. Open **Assets/Tiles/PaintedPlatforms.tilemap.json**, erase the
left raised platform, save/publish and Play: the amber box falls to the lowest
floor. Undo and publish to restore it. The Map actor's **Tile Map Collider 2D**
chooses its solid layer by name; decoration stays on another layer.

Scene **74-tile-maps** contains an editable two-layer arrow course and
a mirrored copy of the same map. Open **Assets/Tiles/ArrowCourse.tilemap.json**
from Content to paint, erase and try stroke Undo/Redo. Save/publish before Play or
export. See [Tile Sets and Tile Maps](../editor/tile-maps.md).

## World sprites and frame animation

Scene **71-world-sprites** compares image sizes, tints, mirrored images, pivots
and transparent overlap. Scene **72-sprite-frame-animation** cycles a large arrow
through four image-sheet frames while four smaller arrows show the individual
cells. Select the animated Sprite to inspect its grid and animation, or open its
clip under **Assets/Sprites** in Timeline. Save before exporting either scene.
See [World sprites and image sheets](../editor/sprites.md).

Scene **73-sprite-image-fill** compares complete-image stretch,
preserved borders and repeated image fill. Two smaller examples show compressed
borders and mirrored partial repeats. Its original panel art uses one shared
Material. Try Image fill, Width and Height in Inspector; choose the Scene as
startup before exporting.

## Sequence sound cues

Scene **70-sequence-audio-cues** plays three original tones at zero, two and four
seconds while a gold panel moves and the camera cuts between views. Select its
sequence controller and open Timeline to edit the sound points. Sources own pitch,
volume and mixer routing. Timeline preview is silent; use Play to hear changes.
Save the Scene before exporting. See [Animation sequences](../editor/animation-sequences.md).

## Create Prefabs from C++

Scene **64-runtime-prefab-creation** starts with a floor, camera and light.
Its optional original C++ example creates two blue robots with gold caps, then a
third after two seconds. They rotate independently. Follow **Runtime Prefab
creation** in the Gallery's `CodeExamples/README.md` to add and build the example
in a writable Gallery copy. The C++ code is optional; without it, this scene stays
an empty stage. `Assets/RuntimeRobot.prefab.json` is already declared in the
Scene's Runtime Dependencies. Save the startup scene/settings and export after
building. The exported game carries the compiled behavior and Prefab; a player
does not need a compiler. See [C++ project code](../editor/native-gameplay.md).

## Portable prefab rigs

Open **63-portable-prefab-rigs** and press Play. The left door runs at normal
sequence speed and the right at half speed. The pendulums above them use separate
body links and gravity settings. Both rigs come from the same prefab source.

Select a rig or one of its members to inspect inherited values and overrides.
Open **Assets/PortableSequenceRig.prefab.json** to inspect its member reference
fields. Save the scene before exporting. The exported game includes its prefab
definition and prepared clip; it needs no source project or C++ compiler.

Scene **55-layered-heightfield-terrain** contains editable height layers on two
adjoining tiles and three spheres that land on the cooked static surface. Open
its source under **Assets/Terrain**, paint a layer and publish, then rebuild its
Collision asset before testing physics again. See [Terrain](../editor/terrain.md).

## Two-clip skeletal animation

For object animation timing, open **45-animation-timing**. Three lanes compare
the original clip, a slower retimed clip and frame-snapped keys. Open the sources
under **Assets/AnimationTiming** to experiment with the [timing controls](../editor/transform-animation.md).

Scene **09-animation-blending** compares Idle, an equal mix and Reach on three
skinned columns. Select the middle root and open **Preview skeletal animation...**
from Content to scrub or try smooth transitions. See [Preview and blend two animations](../editor/animation-blending.md). Both clips are included when you export this scene.

## Named states and timing markers

Scene10 compares Idle/Walk/Run on real characters. Scene11 carries editable Pulse
markers on Walk/Run. The optional C++ example shows those timing points changing
the character height; normal clip playback requires no compiler. See
[Choose named states](../editor/animation-states.md) and
[Trigger C++ actions](../editor/animation-markers.md).

## Physics joints

Scene **20-physics-joints** demonstrates a distance tether and hinged pendulum.
Select a joint entity to assign bodies, adjust local anchors, and change distance
or angle limits. See[Connect bodies with a joint](../editor/physics.md). Play and
standalone export use the same engine-owned physics components.

Scene21 demonstrates a point joint's freely rotating pendulum and a fixed joint's
falling rigid pair. They use the normal Physics Joint component and saved scene
body references, with no gameplay compiler required.

Scene **22-continuous-collision** compares blue continuous and amber discrete cubes
falling onto very thin platforms. Press Play and watch the landing area. The example
uses normal project gravity and engine components; no C++ compilation is required.
The camera starts below the falling cubes, so they enter from above.

### Spatial sound comparison

Scene **31 — Audio falloff** contains three cubes with inverse, linear and
exponential distance curves and a camera/listener. Only blue starts automatically.
Stop to switch the source's **Play On Start**, move the camera and compare.
**Falloff strength** changes distance loss without replacing the sound; strength0
keeps positional panning. [Audio controls](../editor/audio.md) explain the limits.

Scene **32 — Directional audio** shows a speaker aimed toward its camera/listener.
Select the speaker for its angular guide. Rotate **Aim parent** Y180degrees to
hear the quieter rear side; **Outside volume** and full openings control that
difference. [Audio controls](../editor/audio.md) explain direction and limits.

Scene **33 — Audio mixer** plays three pitches through Music, Effects and Dialogue.
Select each emitter for **Mix channel**. Change its shared volume in **Project
Settings → Game defaults**, save settings while stopped, then Play/export to
compare. Master affects every channel.

While playing, use **Project Settings → Live audio mix → Apply live mix** to
adjust the current four volumes. Saved defaults stay unchanged; Stop and Play
restores them. The section also reports output, voice counts and clip cache use.

Scene **34 — Audio import processing** compares original stereo and processed
mono sound. Open its WAVs from Content to compare waveforms and audition them.
Change mono, peak normalization and silence trimming under Import settings,
then reimport. The source remains unchanged; Play and export use the processed
clip. See [Process a clip during import](../editor/audio.md).

Scene **87-sprite-frame-regions** uses unequal rectangles from the original image
sheet, including animated, cropped, Sliced and mirrored Tiled examples. Edit the
Sprite's **Frame regions** list or use **Choose frame...**. See [Sprites](../editor/sprites.md).

Scene **86-one-way-tile-maps** shows falling bodies and a reversed-gravity
circle passing through painted rectangles. Select the Map's **One-way collision**
option and inspect its solid-face arrows. See [Tile Maps](../editor/tile-maps.md).

Scene **85 — Audio filters** compares a dry tone with low-pass and high-pass
processing of the same recording. Stop the dry speaker and restart either
filtered speaker under **Live sound**. See [Filter a sound](../editor/audio.md).

Scene **84 — Independent audio** compares ordinary gameplay sound with music
that continues while game Play is paused. The UI speaker can play a one-shot from
**Live sound** during Pause. Select each speaker's **Pause with game** option to
inspect its policy. See [Audio pause and playback](../editor/audio.md).

Scene **83 — Audio source codecs** compares the same original stereo recording
as WAV, FLAC and MP3. Only WAV starts automatically. Enable one speaker at a time
with **Play On Start**, or open a clip from **Assets/AudioCodecs** to preview it.
All formats share import processing and exported PCM playback. See
[Audio sources and importing](../editor/audio.md).

Scene **35 — Moving audio** animates two emitters past a camera/listener: one
has Doppler enabled and the comparison has it disabled. Select either during Play
and use **Live sound → Play / Restart** or **Stop sound** to compare. Loop jumps
reset motion without a pitch spike. Export uses the saved animation and cooked
tone without development sources. See [Audio controls](../editor/audio.md).

## Touch contacts

Open **Scenes/88-touch-input.scene.json** to test the project's Touch0 input actions. Capture Game input and read the values in Console's Gameplay input section. The optional **CodeExamples/TouchCursor.cpp** system moves and resizes its Sprite from those actions; the Scene alone shows the authored marker. Follow [Touch input](../editor/touch-input.md) to build the matching module, test contacts and export.

## Touch gestures

Open **Scenes/89-touch-gestures.scene.json** for a canvas Sprite and the project's gesture actions. The optional **CodeExamples/TouchGestures.cpp** system pans, zooms and rotates that Sprite from two-finger input. Build/register its matching module and capture Game input; the Scene alone supports Console input monitoring. Follow [Touch input](../editor/touch-input.md) for units, behavior and export steps.

## Audio echo

Open **Scenes/90-audio-echo.scene.json** for a dry recording and two echo settings. Only the dry source starts automatically, and each clip plays once. Use **Live sound → Play / Restart** on one source at a time to compare the original recording with quarter-second or half-second repeats. Source Pause freezes a remaining echo; Resume continues it, while Stop clears it. Save authored settings before exporting. See [Audio](../editor/audio.md) for the controls and limits.

## Tap, hold and swipe

Open **Scenes/91-single-touch-gestures.scene.json**. The seven Single touch actions
are saved in the project. The optional **CodeExamples/SingleTouch.cpp** system turns
the Sprite on Tap, resets it on Double tap, resizes it on Long press and moves it
on directional swipes. Register its matching module and capture Game input, or
monitor actions in Console without code. See [Touch input](../editor/touch-input.md)
for timing limits, C++ setup and export.

## Four local controllers

Open **Scenes/92-multiple-gamepads.scene.json** for four colored markers and separate **Player 1 move** through **Player 4 move** actions. In Game, use **Gamepads...** to check assignments. Capture gameplay input and watch each player's left stick in Console. Build/register **CodeExamples/MultipleGamepads.cpp**, or the matching SDK's `multiple_gamepads_gameplay` target, to move the markers. Save the Scene and Settings, choose it as startup, build current code and export. The exported game assigns connected controllers independently of the editor session. See [Gameplay input](../editor/input.md) for assignments, limits and vibration.

## Raw controller motion

Open **Scenes/93-gamepad-motion.scene.json** for acceleration and gyroscope X/Y markers. Build/register **CodeExamples/GamepadMotion.cpp** with its fixed system and unscaled control callback, or build the matching SDK’s **gamepad_motion_gameplay** sample. Installing the sample opts into enabling Player 1’s supported sensors. Capture Game input to receive readings; open **Gamepads... → Motion sensors** to inspect modes and **Console → Controller motion** to monitor raw values. Save Scene and Settings, select this Scene as startup, build current project code, then export. See [Gameplay input](../editor/input.md) for units, prerequisites and limits.
