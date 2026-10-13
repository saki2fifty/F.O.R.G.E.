# Transform animation timeline

Animate an object's position, rotation and size without writing code. A reusable
**transform animation** appears in Content as an `.animation.json` file. A
**Transform Animator** component connects that clip to an object in the scene.

## Create a clip

1. Create and select a cube. Save the scene.
2. In **Content → Create / Register**, choose **New transform animation...**. Enter a project path such as `Assets/CubeMotion.animation.json` and choose **Create**. The Animation Timeline opens.
3. Choose the cube in **Preview object**. The chosen object is only a preview target until you attach the clip.
4. Set **Duration (seconds)** and press Enter. Check the channels you want to animate: **Position**, **Rotation**, and/or **Scale**.
5. The new clip has one key at time zero. Enter its local position, rotation in degrees and scale, then choose **Add / Update key at playhead**.
6. Click farther along the ruler, or set **Key time (seconds)** and press Enter. Enter another pose and choose **Add / Update key at playhead**. A key at the same time is updated; keys at other times stay in place.

Values are local to the object's parent. A clip can be attached to several objects;
its keys have the same local values on each object. It does not remember a scene
object's name or contain C++ code.

## Edit and preview

Select a diamond or a row in **Keys** to inspect its pose. Edit the fields and use
**Add / Update key at playhead** to commit. A new key becomes selected.

Hold **Ctrl** while clicking diamonds or rows to add or remove keys from the
selection. **Select all keys** and **Clear key selection** are in **Key selection**;
gold diamonds identify selected keys. Pose and curve fields edit the active key.

Drag a selected diamond to move the whole selection while preserving its spacing.
The ruler shows temporary key positions while held; the preview motion uses the
applied clip until release. Release commits one Undo step. Escape, changing window
focus, losing application focus, entering Play, closing the timeline or changing
the document cancels a held drag. Drag offsets snap to one millisecond; clicking a
key without dragging preserves its exact saved time.

Enter **Key offset (seconds)**, then choose **Move selected keys** or **Duplicate
selected keys**. Positive offsets place keys later; negative offsets place them
earlier. Duplicates retain their poses, curves and settings and get new identities.
**Delete selected keys** removes the selection; at least one key must remain.
Offsets outside the clip or landing two keys at the same time are rejected without
replacing the clip. Each batch uses one source Undo step.

Position and scale interpolate between keys. Rotation follows the shortest turn
between the saved orientations. **Hold until next key** keeps this key's pose
until the next key, then changes immediately. Before the first key and after the
last key, the nearest pose is held.

Scrub the ruler or **Playhead**, then use **Play preview**, **Pause preview**,
**Stop preview**, **Loop preview**, and **Preview speed**. The colored PREVIEW
message identifies the temporary Scene view. Preview leaves the saved scene,
scene Undo history and physics unchanged. Stop, entering Play, closing the timeline,
changing projects or deleting the preview object ends preview.

**Capture authored pose** copies the chosen object's saved local transform into
the fields. It does not capture the temporary preview. Timeline **Undo / Redo**
affects clip edits; scene Undo affects attachment and ordinary scene edits.

## Adjust timing

To make an entire clip slower or faster, enter **Retimed duration (seconds)**,
then choose **Scale whole clip timing**. For example, changing a four-second clip
to eight seconds spreads its keys out and adjusts curve slopes for slower motion.
The original **Duration (seconds)** field changes only the clip's end time.

To place selected keys on frame boundaries, set **Frame grid (FPS)** in **Key
selection**, then choose **Snap selected keys to frames**. The grid accepts whole
values from 1 to 240. At 30 FPS, keys land on the nearest thirtieth of a second.
This is a key-placement tool; it does not change the game's frame rate.

Both actions use one Undo step. Keys keep their poses and identities. Frame snapping
keeps curve slopes unchanged, so it can change the motion between keys. If an action
would put two keys at the same time, move a key past the end, or create invalid
curves, Forge keeps the previous clip and shows an error. Save & Publish applies
the result to Play and exported games.

Try **FeatureGallery / Scenes / 45-animation-timing.scene.json**. The blue lane
uses the original four-second clip, the middle lane stretches it to eight seconds,
and the last lane rounds its keys to a two-FPS grid. Open the sources under
**Assets/AnimationTiming** to inspect or adjust them.

## Shape motion with curves

Select a key in **Keys**, then expand **Vector curves**. Choose **Cubic** in
**Outgoing vectors** to ease position and scale toward the next key. Zero slopes
slow the motion at each end. **Linear** keeps a constant change between values.
Rotation still follows the shortest turn between orientations.

**Position incoming/outgoing (units/s)** and **Scale incoming/outgoing (units/s)**
set how quickly each value changes at the selected key. Incoming affects a cubic
segment arriving from the previous key; outgoing affects the next cubic segment.
For example, positive outgoing Y and negative incoming Y on the next key create
an upward arc between equal heights. A longer segment produces a larger arc for
the same slopes. **Hold until next key** overrides the outgoing curve.

Choose **Curve value** and **Axis** to inspect position or scale. The blue curve
shows the applied values; gold guides show the slope fields. Drag a gold handle
up or down to change that slope. The key's time and value stay fixed. Release
commits one Undo step and updates preview; Escape or losing focus cancels the drag.
Incoming handles appear for a cubic segment from the previous key, and outgoing
handles for a cubic segment toward the next key. Held segments have no handles.
**Fit curve and handles** brings distant handles into view; **Fit curve** fits just
the motion. Fitting does not edit the clip.

Select one or several keys, then choose **Flat tangents** for eased ends,
**Linear tangents** to match each neighboring segment's slope, or **Smooth
tangents** for a shared slope that flattens peaks and valleys. Presets affect
position and scale on the selected keys, leave Hold unchanged and use one Undo
step. Applying Linear to both ends of a segment gives constant motion.

Entering numeric slopes leaves them pending. **Apply curve settings**, **Save &
Publish**, another clip edit or changing key selection commits them. Undo first
cancels pending fields. A preset replaces pending slopes. Invalid or excessive
curves retain the previous clip and show an error so you can correct the fields.
Closing asks you to save, discard or keep editing pending changes. **Save &
Publish** updates Play and exported games.

Try **FeatureGallery / Scenes / 13-transform-curves.scene.json**. Three platforms
compare linear motion, smooth easing and an arc. Open **EasedPlatform.animation.json**
or **ArcPlatform.animation.json** in Content to change the curves. Curve clips need
a current FORGE build; older builds reject their format. These timeline editing
changes do not themselves require rebuilding your C++ module. Use a matching SDK
when an engine update changes the native module interface or build configuration.

## Use in Play and export

Choose **Save & Publish**, then **Attach animation to object**. The Inspector's
**Transform Animator** exposes the clip, **Enabled**, **Play On Start**, **Loop**
and speed (`0–4`). A speed of zero holds time. Without Loop, playback holds the
end pose. Restart Play after changing an already published clip.

Add an enabled game camera and light before trying Play. Pause holds animation;
Step advances one simulation tick. Stop restores the authored scene. Save the
scene and [export the game](runtime-content.md): the exported folder
contains the animation clip and plays it without the editor, source files or a C++
compiler.

A Transform Animator cannot also control an object driven by a dynamic Physics
Body, Character Controller, active navigation movement or skeletal Animator.
FORGE reports the conflicting controller. Existing physics shape/scale checks also
apply to animated poses. Use an ordinary visual object for the first experiment.

Try **FeatureGallery / Scenes / 08-transform-animation.scene.json**. The blue
platform travels between two markers, turns and changes height over four seconds.
Open `Platform.animation.json` in Content to inspect and change its keys.

This timeline edits object transforms. Skeletal animation has its own state,
blend-space and rig tools; they do not add bone tracks to this timeline. Object
transform clips currently have no reverse playback, event or blending tracks.

Try **FeatureGallery / Scenes / 18-animation-key-batches.scene.json**. Three lanes
compare original eased travel, the same two keys moved one second later, and
copies placed three seconds later. Open **BatchOriginal.animation.json**,
**BatchShifted.animation.json** or **BatchRepeated.animation.json** in Content to
inspect their key times. Play and export use the published clips.
