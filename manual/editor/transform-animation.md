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
**Add / Update key at playhead** to commit. Drag a diamond to change its time;
release commits one Undo step. Escape cancels the drag. **Delete selected key**
removes a key; the last key cannot be deleted.

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

This timeline currently edits object transforms. Bone editing, IK, blending,
reverse playback, event tracks and animation graphs remain future work.
