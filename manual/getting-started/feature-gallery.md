# Explore the Feature Gallery

**118-local-diffuse-irradiance** compares warm, overlapping and cool map-based
diffuse light on receivers and a floor, without direct lights or a global environment.
Select either diffuse volume and adjust **Diffuse intensity**, **Edge fade**,
**Priority** or its bounds. **Intensity** separately controls reflections and is
zero in this example. Save and choose this Scene as startup before export.
See [Scene lighting](../editor/lighting.md).

**117-light-images** compares repeating Directional arrows, a patterned Spot light
and a Point light using a captured cubemap. Select a **... change Light image**
entity and try its image assignment, rotation or intensity. Directional also exposes
world size and tiling. Save and choose this Scene as startup before export. It reuses
included Textures and needs no C++ module or additional import. See
[Use an image to shape a light](../editor/lighting.md).

**116-clustered-projectors** places 64 tinted badge projectors over a lit floor.
They share one material and image. Select a **Paint row ... column ...** entity in
Hierarchy, then change its tint, opacity, position or size. The other patches keep
their own values. Save and choose this Scene as startup before export; no C++ module
or additional import is required. See [Project material channels onto a Mesh](../editor/decals.md).

**115-projected-surface-channels** compares normal-only, roughness-only and combined
material projections. Select a projector and change **Normal influence** or
**Roughness influence** in Inspector. Color influence is 0; the receiver's color
stays unchanged. The scene reuses a ripple normal map and needs no new import or
C++ module. Save and choose it as startup before export. See
[Project material channels onto a Mesh](../editor/decals.md).

**114-weighted-transparency** blends three intersecting colored image panels above
an opaque floor. In Edit, open **Tools > Scene lighting... > Transparency** and
toggle **Weighted transparency** to compare the intersections. The panels share one
Material and Texture with separate object color values. Save and choose this Scene
as startup before export. See [Blend overlapping transparent Meshes](../editor/transparency.md).

**113-projected-color-decals** paints a shared badge image onto two lit cubes and
overlapping color onto a floor. Select a **Decal Projector**, then adjust its box,
tint, opacity, layers or priority in Inspector. The left badge mirrors local X.
Save and choose this Scene as startup before export. See
[Project material channels onto a Mesh](../editor/decals.md) for placement and current limits.

**112-area-lights** compares a warm rectangular emitter with a cool spherical
emitter on two metal spheres and a rough receiver. Press Play, then select either
light and adjust **Emitter Width (m)** / **Emitter Height (m)** or **Emitter Radius
(m)** under **Live gameplay**. Stop, keep the values you want and save. Rotation
aims the rectangle and turns its highlight; entity scale does not resize either
emitter. Area shadows are unavailable. Choose this Scene as startup before export.
See [Scene lighting](../editor/lighting.md) for controls and limits.

**111-contact-hardening-shadows** compares resting and raised boxes on a lit floor.
Select **Soft sun - change Shadow Angular Radius** and compare **Shadow Angular
Radius (rad)** at **0.08** and **0**. A larger source softens separated shadows;
a contact shadow stays sharper. Point and Spot lights use **Shadow Source Radius
(m)**. Save and select this Scene as startup before export. See
[Scene lighting](../editor/lighting.md) for steps, Play tuning and limits.

**110-reflection-map-capture** supplies a saved HDR map and two metal spheres.
Select its captured reflection volume to create a new map through Inspector,
review/import it, save and export. The **Capture reflection maps** section below explains its layer masks and steps.

**109-rough-reflection-filtering** shows alternating amber and blue columns over
a rough metal floor. Press Play to inspect the saved result. Stop, open **Tools >
Scene lighting... → Screen-space reflections**, and toggle **Roughness filtering**.
Compare the reflected detail, then change the floor's **roughnessFactor** below
**Maximum roughness**. Save the Scene, select it as startup and export. The example
uses built-in assets and requires no C++ module or additional import.

**108-material-normal-occlusion** supplies a normal-mapped blue wall and amber
posts. In **Tools > Scene lighting... → Ambient occlusion**, compare **Normal
influence** at **0** and **1**. The surface directions change; geometry and depth
remain the same. Save before exporting; the supplied cooked Material and original
normal texture are included. See [Scene lighting](../editor/lighting.md).

**107-local-reflection-probes** supplies warm and cool imported reflection maps
inside two overlapping boxes. Select either probe to adjust **Reflection map**,
half extents, **Edge fade**, **Priority**, **Map rotation** or **Box projection**.
Compare the smooth metal spheres and floor, then save and export the Scene.
See [Scene lighting](../editor/lighting.md) for the workflow and limits.

**106-screen-space-reflections** shows amber and blue blocks reflected in a smooth
metal floor. In **Tools > Scene lighting... → Screen-space reflections**, toggle
the effect and adjust distance, thickness, strength or maximum roughness. Keep
source objects visible in the same view. Save the Scene before exporting.
See [Scene lighting](../editor/lighting.md) for steps and limits.

**105-ambient-occlusion** shows three amber posts and a lower ledge against a blue
wall. In **Tools > Scene lighting... → Ambient occlusion**, toggle the effect and
adjust Radius, Strength or Depth bias. The built-in environment supplies the
ambient lighting; direct lighting remains unchanged. Save before exporting.

**104-motion-blur** compares a moving checker panel with a stationary panel behind
it. Press Play, then stop to adjust **Tools > Scene lighting... → Motion blur**.
Select its sequence controller to inspect or capture the motion in Timeline.
**103-depth-of-field** compares checker panels at three camera distances, while
**102-temporal-antialiasing** compares moving foreground detail and thin edges.
See [Scene lighting](../editor/lighting.md) for controls, saving and limits.

**101-tile-collision-profiles** shows a cyan full-cell floor, mirrored amber
convex ramps and pink decoration with no collision. Press Play: circles roll on
the ramps and the box falls through the decoration to the floor. Stop, open
**Assets/Tiles/CollisionProfiles.tileset.json** and select **Convex ramp** to
edit its collision in Inspector. Save/publish the Set and Map before Play or export.
See [Tile collision profiles](../editor/tile-maps.md).

**100-ui-animation** shows width/opacity keyframes, a translating marker,
hover transitions and a pause-state color transition. Press Play, capture gameplay
input and Pause: UI motion continues while the tick counter holds. Edit
**Assets/UI/Motion/hud.rcss**, save and use **Reload UI**. Set this Scene as startup
and export to run the same saved interface without a C++ module.

**99-sprite-frame-filtering** compares Disabled, Nearest and Linear filtering from
left to right. The rows show fine checks, stripes, transparent pixels and ring
edges. Select a Sprite and change **Image fill → Mip filtering**, or change its
size to compare different reductions. See [Sprite filtering](../editor/sprites.md).

![Gallery comparison of base pixels and two independent frame filtering modes](../images/sprite-frame-filtering.png)

Left: Disabled. Center: Nearest. Right: Linear. Each column uses the same source frames.

**98-distance-fog** places colored columns at increasing camera distances. Open
**Tools > Scene lighting... → Distance fog**, adjust Start/End or toggle fog,
then save and export. See [Scene lighting](../editor/lighting.md) for controls and limits.

**97-packed-sprite-fills** compares original and packed artwork in Sliced and Tiled
modes. Change both members of a pair together, then save and export. See
[Sprites](../editor/sprites.md) for original-canvas borders, density and transparent
margin behavior.

The packaged **Examples/FeatureGallery** project gives you focused scenes to
open in the editor and export as standalone games. You can see how each effect is
authored, change it, then compare the result in Play and in the exported game.

Copy the whole gallery folder to a writable location before editing it. Choose
**File → Open project...** and select that copy. Use Content's scene list to open
**Transforms**, **Lighting**, **Physics**, **Cameras**, **Editable Mesh**, **Sculpt**, **Vertex Paint**, or **Transform Animation**. The project's `README.md`
gives a short experiment and expected result for each scene.

**96-automatic-exposure** animates ordinary sunlight from dim to bright and back.
Press Play to watch brightness settle after each change, then stop and open
**Tools > Scene lighting... > Automatic exposure** to adjust the rates, gain limits
or target. Save and export the Scene to compare the same effect in a standalone
game. See [Scene lighting](../editor/lighting.md) for controls and current limits.

To export one, open and save that scene. In **Tools → Project Settings**, select
**Use saved current scene as startup** and **Save Settings**. Then use **Run →
Export Game...**, selecting the `runtime-kit` beside the packaged editor and an
output folder outside the project. Run the resulting `forge_game.exe`. Repeat
with another startup scene to export a different demonstration.

The gallery contains ordinary saved project content. Most scenes need no C++ compiler; optional C++ examples require the matching development tools to build. The included [reference game](../reference-game.md)
remains a separate combined gameplay example.

## Capture reflection maps

Open **Scenes/110-reflection-map-capture.scene.json** and press Play. Two smooth
metal spheres and a floor use the supplied captured HDR map. Amber and blue
capture sources use **Layers = 1**; the game Camera uses **Layers = 2** to keep those
panels out of its direct view. The spheres use **Layers = 2**, so capture with **Layers = 1**
excludes the spheres themselves. The shared room geometry uses **Layers = 3**, combining both bits.

Stop Play, select **Captured reflection volume - create a new snapshot** and click
**Capture reflection map…**. Set **Face size (pixels)** to **128**, **Layers** to **1** and choose a new folder
below **Assets**. Click **Capture map**, then keep **Import with defaults after
copying** enabled and click **Copy sources** in the generated-source review. Wait
for publication, close the dialog and save the Scene. Each capture creates a new
map; changing a source panel does not update an old map automatically. Compare
**Enabled** on the probe, or change a panel and capture again to see the difference.
Box projection starts off for this directional-map comparison.

Set this Scene as Startup Scene and export after saving. The captured Texture
travels with the game; the editable DDS source is not required at runtime. No C++
module is required. See [Capture a reflection map](../editor/lighting.md)
for clipping, layer masks, cancellation and assignment rules.

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

## Packed Sprite images

Scene **94-sprite-atlas-packing** compares the original Badge and Panel on the left
with the same pixels selected from one generated PNG on the right. The bottom
Sprite shows the whole atlas. Inspect the two packed Sprites' **Frame regions**;
Frame0 selects Badge and Frame1 selects Panel. See [Sprite atlas packing](../editor/sprites.md)
to generate your own PNG and apply its ordered frame rectangles.

## Trimmed Sprite canvases

Open **Scenes/95-sprite-frame-metadata.scene.json**. The originals on the left and
packed frames on the right keep the same visible size, alignment and transparent
canvas. The second pair uses Flip X; the bottom image shows clockwise storage.
Inspect **Frame regions**, including the canvas dimensions, retained offsets and
`rotated_clockwise`. Use Simple image mode and a Blend Material with depth writes
off. Save the scene and set it as startup to export this comparison. See
[Sprites and atlas packing](../editor/sprites.md) for the complete workflow.
