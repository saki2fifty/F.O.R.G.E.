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

## World sprites and frame animation

Scene **71-world-sprites** compares image sizes, tints, mirrored images, pivots
and transparent overlap. Scene **72-sprite-frame-animation** cycles a large arrow
through four image-sheet frames while four smaller arrows show the individual
cells. Select the animated Sprite to inspect its grid and animation, or open its
clip under **Assets/Sprites** in Timeline. Save before exporting either scene.
See [World sprites and image sheets](../editor/sprites.md). These examples are
included in Build261019-000193 and later; build192 predates them.

Development Scene **73-sprite-image-fill** compares complete-image stretch,
preserved borders and repeated image fill. Two smaller examples show compressed
borders and mirrored partial repeats. Its original panel art uses one shared
Material. Try Image fill, Width and Height in Inspector; choose the Scene as
startup before exporting. Scene73 and these modes are absent from Build193;
Windows/export acceptance remains pending.

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

Scene **35 — Moving audio** animates two emitters past a camera/listener: one
has Doppler enabled and the comparison has it disabled. Select either during Play
and use **Live sound → Play / Restart** or **Stop sound** to compare. Loop jumps
reset motion without a pitch spike. Export uses the saved animation and cooked
tone without development sources. See [Audio controls](../editor/audio.md).
