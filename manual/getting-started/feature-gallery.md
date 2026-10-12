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

The gallery contains ordinary saved project content. It needs no C++ compiler. The included [reference game](../reference-game.md)
remains a separate combined gameplay example.

## Two-clip skeletal animation

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
