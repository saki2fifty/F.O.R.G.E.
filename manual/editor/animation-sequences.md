# Coordinate several animations

An **Animation Sequence** makes several objects use one playback time. Each object
keeps its reusable animation clip and a start time inside the sequence. A second
clip can crossfade transforms and matching property tracks on the same object.
Sequences can also contain other sequences, sharing their root controller's
playback time.

![Timeline with camera shot intervals and camera selection](../images/camera-sequencing.png)

Place camera shots on Timeline to control the view during a sequence.

## Try the included scene

Open **FeatureGallery / Scenes / 62-coordinated-animation.scene.json** and press
**Play → Game**. The camera moves immediately, the door begins after half a second,
and the light and sound begin changing after one second. Finished clips hold their
last values. The whole arrangement repeats every six seconds.

## Make an arrangement

1. Create and publish the [animation clips](transform-animation.md) you need.
2. In **Content → Create / Register**, choose **Create animation sequence**. You can also use **Entity → Create → Animation → Animation Sequence**.
3. Select the controller. In Inspector, set **Duration**, **Loop**, **Play on start** and **Playback speed**. Choose **Open sequence timeline** in Create / Register.
4. Select an object in Hierarchy, then choose **Add selected object** in Timeline. Choose its **Clip** using the asset picker. Repeat for the other objects.
5. Select a track, enter its start in seconds, and choose **Apply track start**. Apply or reset a pending start before selecting another track or changing its clip.
6. Choose **Refresh clips**, then **Preview sequence**. Scrub a track's time strip or the playhead slider. **Play preview** advances the preview; **Pause preview** holds it, and **Stop preview** restores the authored view. Fit/Zoom adjust the view.
7. Choose **Save scene**. Arrangement changes use Scene Undo/Redo and Scene Save. Opening an individual clip uses that clip's separate Save & Publish and history.

Preview changes a temporary Scene view. It runs no gameplay C++, physics or sound.
After Scene edits or publishing changed clips, choose **Refresh clips** again.
Open **Game** while sequence preview is active to see the camera image without
starting gameplay. The **SEQUENCE PREVIEW** label identifies this temporary view.
Use Game Play to hear sound and run gameplay.

## Crossfade two clips on one object

Open **FeatureGallery / Scenes / 145-sequence-clip-blending.scene.json** and press
**Play → Game**. The cube moves and rotates through two clips in a four-second
loop. The first clip starts at zero, the next at one second; the one-second
overlap blends from the first motion into the next.

To make a two-clip track:

1. Create and publish two Transform Animation clips with matching tracks. Select the same transform channels, built-in properties, Material parameters and component fields in both. For clips that only animate properties, leave transform channels off in both.
2. Add the object to an Animation Sequence and choose its **Clip**. Select the object track, then choose **Next clip** using the asset picker.
3. Enter **Primary clip start** and **Next clip start**, then choose **Apply track timing**. Both edited values use one Scene Undo step. **Reset pending timing** reloads the current Scene values. Apply or reset before switching tracks or clips.
4. Choose **Refresh clips**. The upper blue strip shows the primary clip, the lower purple strip shows the next, and the highlighted intersection shows the crossfade. Use **Preview sequence**, scrubbing or frame export to inspect the result.
5. Choose **Save scene**. Game Play and exported games use the same saved clips and timing.

The next clip must start and finish no earlier than the primary clip. Both starts
must be inside the sequence's duration. An endpoint may extend beyond that duration;
the controller stops or loops at its own end. During an overlap, position and scale
blend linearly, while rotation follows the shortest arc. The clips keep their own
key interpolation. If there is a gap, the primary's final pose holds until the next
clip starts, then the pose cuts to that clip. Before and after the whole track,
the first and last poses hold.

Choose **None / Clear** in **Next clip** to return to a single clip. **Remove from
sequence** clears the controller binding and next clip while keeping the primary
clip and its standalone settings. Undo restores the arrangement. A missing or
incompatible clip is reported during **Refresh clips** or Play preparation; correct
its selection, track set or timing and refresh again. After publishing a corrected
clip, allow its replacement to finish loading; the previous diagnostic can remain
until the corrected clip is admitted.

### Crossfade property tracks

Open **FeatureGallery / Scenes / 183-sequence-property-crossfades.scene.json** to
try a blue-to-orange light fade and an arrow's Sprite frame switch. The light's
intensity and color blend from one to two seconds; the arrow switches at one and
a half seconds. Select **Four second property loop** and open its Timeline to
preview the tracks. No C++ module is required.

For a light fade, create two clips that both animate **Light intensity**. Add a
Light component to the target, assign the clips to its sequence track, and overlap
their spans using **Next clip start**. Choose **Refresh clips**, then scrub or
play the preview. The light's sampled intensity blends throughout the overlap.
Choose **Save & Publish** for each edited clip and **Save scene** for the
arrangement before exporting the game.

Matching tracks can animate camera field of view/orthographic height, light
intensity/red/green/blue, audio volume/pitch, particle size/red/green/blue, and
Sprite frame. The target needs each corresponding component. Numeric values
blend linearly, and each clip keeps its own key interpolation. Unselected fields
keep their current values. Both clips must select exactly the same tracks.

**Sprite frame (step)** uses the first clip's sampled frame before the overlap
midpoint and the next clip's sampled frame from the midpoint. All frame keys in
both clips must fit the target Sprite's frame configuration, even when the
current playhead is elsewhere. A gap holds the first clip's final values until
the next clip starts, then switches to its first values.

### Crossfade Material and component fields

Open **FeatureGallery / Scenes / 184-sequence-binding-crossfades.scene.json** and
press **Play → Game**. The cube changes color and roughness while the HUD bar's
opacity and width change. Both tracks overlap from one to two seconds and repeat
on a four-second loop. Select **Four second binding loop**, open its Timeline,
choose **Refresh clips** and select the cube or HUD track to preview it. No C++
module or custom Shader is needed.

![Timeline showing matching Material clips on the cube and a separate HUD field track](../images/sequence-clip-blending.png)

The selected cube track's blue and purple clips overlap from one to two seconds.

![Game showing the cube's warm Material color and a HUD displaying opacity 0.8 and width 220 px](../images/sequence-binding-game.png)

At two seconds, the incoming clips set the cube's appearance and the HUD values.

For a Material fade, add the same object Material parameters to both clips. Each
pair must use the same mesh slot, parameter key and semantic type. A color and a
vector of the same width remain different types. The target needs a Mesh Renderer
with a compatible selected Material. Numeric lanes blend throughout the overlap;
shared Material assets retain their own values.

For component fields, add the same inspected fields to both clips. The component
schema, value type and complete member or collection path must match. A named
UI Value must already exist on the target. Collection tracks change existing
leaves; they do not add, remove or resize entries. Keyed rows retain their identity
when reordered; positional rows follow their index. Track order within the clips
may differ.

Continuous numeric fields blend. Boolean, integer, enum and flag fields select the
first clip's value before the overlap midpoint and the next clip's value from the
midpoint. Integer values retain their exact storage, including 64-bit values.
Both clips' complete curves must fit the target's declarations and ranges, even
when the playhead is outside the incoming clip. Fix an incompatible track, publish
it and choose **Refresh clips** before previewing again. Use **Save & Publish** for
clip edits and **Save scene** for the arrangement before export.

## Place a sequence inside another

Open **FeatureGallery / Scenes / 146-nested-sequences.scene.json** and press
**Play → Game**. The parent loops every eight seconds. Its six-second child starts
at one second: the three tones sound at parent times one, three and five seconds,
the gold panel moves, and the child switches between two cameras. The normal
overview camera appears before and after the child placement.

![Timeline showing a child sequence placed at one second inside its parent](../images/nested-sequences.png)

Select the child row to edit its start or open its own Timeline.

1. Create two **Animation Sequence** controllers in the same Scene. Give the parent enough **Duration** for the arrangement; the child's duration controls its placement length.
2. Open the parent's sequence Timeline. Select the child controller in Hierarchy, position the playhead and choose **Add selected sequence**.
3. Select the child under **Nested sequences**. Enter **Start in parent**, then choose **Apply sequence start**. This uses one Scene Undo step. **Reset pending sequence start** discards the pending value. Apply or reset before opening or removing the placement.
4. Choose **Refresh clips**, then **Preview sequence**. The green strip marks the child placement. Scrub the parent to inspect its descendants together. Game preview and frame export use the same composed camera image.
5. Choose **Open nested timeline** to edit the child's own objects, shots, sound cues or further child placements. Its local preview uses its own stored timing. **Open parent timeline** returns to the parent arrangement; refresh the parent to review the combined result.
6. Choose **Save scene**. Game Play and export use the saved parent bindings and prepared clips and sounds.

While placed, the child's clock, speed, loop and start/stop intent come from the
root controller. Its own **Playback speed**, **Loop** and **Play on start** stay
stored for local preview or standalone use. Pause, seek or restart the root to
control the arrangement. A bound child cannot be controlled independently.

Child starts must be inside their immediate parent's duration. Ancestor ends
truncate longer placements. Object tracks hold their first and last values
outside the placement; camera shots direct only within it, including their exact
final frame when a final shot reaches the child's end. Sound cues fire at their
translated parent times during forward playback, including each root loop.
Seeking and silent Timeline preview do not play skipped sounds.

**Remove sequence placement** clears the parent link and keeps the child, tracks,
start value and standalone settings. Undo restores the link. Inspector also exposes
**Parent sequence** and **Start in parent**. Circular links, missing controllers
and cross-Scene parents are reported during refresh or Play preparation. Include
the complete linked group when making a Prefab. Up to 64 enabled controllers can
participate; one controller with camera shots can direct each loaded Scene.

## Export preview frames

1. Stop Game Play. Finish any pending Timeline fields, then choose **Refresh clips**.
2. Choose **Export frames…** in the sequence Timeline. Game opens to show the sequence preview. Set **Start time**, **Frames per second** and **Frame count**.
3. Leave the current Game size, or enable **Fixed output size** and choose 720p, 1080p, Square or your own width and height. Choose a parent folder and a new folder name. Check the output dimensions and sampled time range, keep Game open.
4. Under **Additional output**, choose **PNG frames only**, **WebP animation** or **AVI movie (Motion JPEG)**. WebP needs at least two frames. AVI also supports one frame and exposes **JPEG quality**.
5. Choose **Start export**. Wait for the saved status in the status bar or Console. Open the new folder to find the numbered PNGs and `frames.json` listing completed frames. The selected additional output is `animation.webp` or `animation.avi`.

Every sampled time must be before the sequence's end. For a six-second sequence,
30 FPS and 180 frames sample 0 through 5.966667 seconds. Playback speed does not
change these chosen times. Fixed-size output keeps its resolution when you resize
the editor. Its Game preview fits the available space without stretching. Custom
dimensions must be from 1 to 4096 pixels per side. With fixed size off, keep Game
at the same size until export ends. The settings area scrolls in small windows;
Start and Cancel remain below it.

**Cancel export** keeps completed images and stops further frames after any
already accepted file finishes. Your previous preview position and playing state
return afterward, and Game returns to its normal panel resolution. While frames
are being captured, changing the Scene, sequence or assets stops export. Changing Game's render size also stops a
current-size export. An existing output folder is preserved; choose a new name
for each run.

### Save a repeating WebP animation

Choosing **WebP animation** under **Additional output** makes a lossless, full-size animation from the same saved
PNG frames. Transparency and frame order are preserved. It repeats indefinitely,
with each frame replacing the whole canvas. Open `animation.webp` in a viewer
that supports animated WebP. The PNG frames and receipt remain available for
other tools or individual inspection.

![Sequence export options with WebP animation selected](../images/webp-sequence-export.png)

Choose **WebP animation** under **Additional output** to keep PNG frames and add a repeating animation.

Use 2–4096 frames and a positive rate up to 240 FPS. Container durations use
integer milliseconds rounded from cumulative frame times, so fractional FPS does
not accumulate rounding drift. Players may clamp short delays; their displayed
playback rate can differ from the saved timing. PNG sample times stay exact in
the receipt. Each duration must fit 1–16777215 milliseconds, with at most
2147483647 milliseconds in total.

After the PNG frames finish, your previous Timeline preview returns and the status
shows **Encoding lossless WebP animation…**. **Cancel export** can stop this stage.
Cancellation or encoding failure keeps completed PNGs and records the outcome in
`frames.json`. An animation that has already committed remains complete. Choose
a new folder for another attempt.

The packaged asset worker is required. Completed PNG snapshots must fit 256 MiB
in total, with at most 68 MiB per frame. Compressed frame data is limited to
64 MiB, the finished animation to 68 MiB, and encoding to 120 seconds. Larger
exports can keep PNG frames alone, use smaller dimensions or fewer frames.
This profile produces silent sRGB RGBA8 animation; it does not export HDR,
16-bit animation or audio. Importing the result as an animated Texture is a
separate workflow and is not supported.

### Save an AVI movie

Choose **AVI movie (Motion JPEG)** under **Additional output** to write
`animation.avi` alongside the PNG frames. It is a silent movie of the sequence
preview. Open the AVI in a player that supports Motion JPEG. FORGE's image preview
continues to show an individual PNG frame.

![Sequence export options with an AVI movie selected and JPEG quality control](../images/avi-sequence-export.png)

Set **JPEG quality** from 1 to 100; the default is 90. Higher quality usually
makes a larger file. JPEG is lossy and requires opaque sRGB frames. PNG frames
retain the original pixels; use PNG or WebP to keep transparency.

AVI supports 1–4096 frames at 0.001–240 FPS. Common fractional rates are retained
as a rational frame rate. The movie starts at its first captured frame; `frames.json`
keeps the original sequence sample times. If a custom rate cannot fit the supported
precision, choose a common rate such as 24, 25, 30, 60 or 29.97 FPS.

The packaged asset worker is required. Completed PNGs must fit 256 MiB in total,
with at most 68 MiB per frame. The AVI can be at most 256 MiB, and encoding has a
120-second limit. Reduce the size or frame count if an export exceeds these limits.

After the PNGs finish, the Timeline preview is restored and the status shows
**Encoding AVI movie…**. **Cancel export** can stop encoding before publication.
A failed or cancelled encoding keeps completed PNGs and records its outcome in
`frames.json`. A movie that has already committed remains complete. Choose a new
folder to try again.

For an example, open the Feature Gallery's **181-sequence-movies** Scene and its
sequence Timeline, refresh clips, then export a short range. The movie follows the
sequence's moving panel and camera transition. This example has no audio cues.

![An exported Gallery movie frame with colored props and the moving gold panel](../images/sequence-movie-frame.png)

These frames contain the silent sequence camera image. Gameplay C++, physics,
runtime UI and sound do not run during this preview. For a single image while
playing, use [Game → Save image](viewport.md).

## Camera shots

Open **FeatureGallery / Scenes / 68-sequence-camera-shots.scene.json**. Play shows
the front view for two seconds, then the side view, then a close view. A short gap
returns to the normal overview camera before the six-second sequence repeats.

To author shots in your own sequence:

1. Create the cameras and position them in the Scene. Standby cameras can have **Enabled** turned off; a shot activates its chosen camera in the rendered view.
2. Open the controller's sequence Timeline. Select a camera in Hierarchy and choose **Add shot from selected camera**. The shot fills the next free interval at or after the playhead.
3. Select its row under **Camera shots**. Choose **Shot camera**, enter **Shot start**, **Shot end** and **Shot blend in**, then choose **Apply shot fields**. The edit uses one Scene Undo step. Apply or reset pending fields before switching tracks.
4. Choose **Refresh clips**, then **Preview sequence** and open **Game**. Scrub a shot's strip or the playhead slider to inspect the cuts. Stopping preview restores the authored camera settings.
5. Choose **Save scene**. Play and export use this saved arrangement; no C++ module is required.

Keep **Shot blend in = 0** for an exact cut. To move smoothly into an adjacent
shot, enter a positive duration no longer than that incoming shot. During this
interval the view eases from the previous camera's current position, orientation
and lens to the incoming camera. Animated and parented cameras keep moving during
the transition. First shots, gaps and the start of each loop use a cut.

For a blend, both cameras must use the same projection, basis, aspect policy,
viewport, flips, Layers, Order, infinite-far, Clear and background settings.
Orthographic cameras must both use automatic width or both use fixed width.
Their positions, rotations, lens values and Enabled flags can differ. Camera shots
require main Game output; cameras that write Render Textures run independently.
An incompatible pair appears in Problems and prevents the sequence from preparing.

Open **153-camera-transitions** to try a six-second arrangement: the view moves
from a narrow left camera to a wider right camera, then returns. Both cameras
share an animated rig. Select the incoming shot under **Camera shots**, adjust
**Shot blend in**, choose **Apply shot fields**, then **Refresh clips** to preview.

![Sequence Timeline with the incoming Camera Shot selected and its blend duration editable](../images/camera-transition-editor.png)
The selected incoming shot controls the duration of its smooth view and lens transition.

During Play, select a shot and adjust **Blend in (seconds)** in Inspector's
**Live gameplay** section. This changes the running duration. Stop restores the
authored value; use **Keep runtime values** to review a value for the Scene before
saving. Camera links, interval bounds and Enabled remain Edit controls.

Shots cannot overlap and must fit inside the controller's duration. Gaps use normal enabled cameras and their
composition order. A final shot ending at the controller duration holds when a
nonlooping sequence finishes. Disabled controllers and unbound or disabled shots
do not direct the camera.

One enabled controller with camera shots can direct each loaded Scene. Disable an
unused director before enabling another; ordinary object-only sequences can still
run independently. Camera shots can be renamed, copied and included with their
controller and cameras in a Prefab. The links use IDs, so equal camera names stay
independent. **Remove shot from sequence** clears its controller link and retains
the shot entity and camera reference.

## Sound cues

Open **FeatureGallery / Scenes / 70-sequence-audio-cues.scene.json** and press
**Play → Game**. A lower tone sounds at the start, a middle tone at two seconds,
and a higher tone at four seconds. The sequence repeats every six seconds while
the gold panel moves and the camera changes view.

To add a sound to your own sequence:

1. Create an **Audio Source** and assign an Audio Clip. Turn **Play on start** off if this sound should start only from cues. The source owns volume, pitch, looping and mixer settings; cue playback requests a one-shot effect.
2. Open the controller's sequence Timeline. Select the source in Hierarchy and choose **Add cue from selected Audio Source**. It creates a cue at the playhead.
3. Select the cue under **Audio cues**. Choose **Cue source**, enter **Cue time**, then choose **Apply cue fields**. Source and time use one Scene Undo step. Apply or reset pending fields before changing selection.
4. Choose **Save scene**, then use Game Play to hear it. Timeline preview and scrubbing stay silent. Export includes the saved cues and their prepared sounds.

The diamond marks when the sound is requested. Cues fire when forward playback
crosses them; a cue at zero also fires when the sequence starts or restarts. Pause
holds the sequence. Seeking does not play skipped sounds or stop sounds that have
already started. Audio Source Stop still stops its effects.

**Remove cue from sequence** keeps the entity, source and time for later reuse.
Copy a whole controller/source/cue group to keep its links independent, including
inside Prefabs. This feature places sound events on the fixed game clock; it does
not provide audio trimming, waveforms or sample-accurate music synchronization.

## Playback rules

Before a track starts, its clip holds its first value. After it ends, it holds its
last value. A sequence's end can cut a clip short. The controller's loop, speed and
playback intent govern all its tracks; the individual clips' standalone settings
stay stored for when they are removed from the sequence.

**Remove from sequence** keeps the primary clip and clears its controller and next
clip. Each object supports one primary clip and one optional compatible next clip.
Camera shots support cuts and compatible view/lens transitions. This sequence mode
does not crossfade rendered images or edit bones. Matching Material and component-field
tracks use the two-clip workflow above. Use the
preview export options above to save a silent movie. Each controller has at most one
parent placement; place a copy of a complete linked group to reuse it independently.

Pause, Step and simulation Speed work as they do for other animations. C++ can use
the existing animation playback/control/seek operations on the controller. Bound
tracks can be inspected, but their clocks cannot be controlled independently.

## Prefabs and export

Prefab instances can join a sequence through their Scene settings. Duplicate a
controller together with its objects to make an independently bound copy. Copying
only an object keeps its link to the existing controller.

To reuse an arrangement, put its controller and linked objects under one parent
and create a prefab from that whole group. FORGE stores member links and resolves
each placed copy independently. In **Prefab source**, reference fields choose
members of that source. A link outside the selected group must be cleared or its
target included before capture. See [Links between members](prefabs.md)
and Gallery63. Individual instances can still join a Scene-owned sequence.

Save the Scene and publish its clips before exporting. Export includes the saved
arrangement and prepared clips and sounds. The exported game needs no editor or
C++ compiler for this feature. Existing native C++ modules must be rebuilt with the
matching SDK when upgrading to this engine version.
