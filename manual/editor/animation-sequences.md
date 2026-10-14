# Coordinate several animations

An **Animation Sequence** makes several objects use one playback time. Each object
keeps its own reusable animation clip and a start time inside the sequence.

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
Use Game Play to see the animated camera image and hear the sound.

## Playback rules

Before a track starts, its clip holds its first value. After it ends, it holds its
last value. A sequence's end can cut a clip short. The controller's loop, speed and
playback intent govern all its tracks; the individual clips' standalone settings
stay stored for when they are removed from the sequence.

**Remove from sequence** clears the binding and keeps the clip. An object can have
one Transform Animator clip. This first sequence mode does not blend overlapping
tracks, edit bones, cut between cameras or nest sequences.

Pause, Step and simulation Speed work as they do for other animations. C++ can use
the existing animation playback/control/seek operations on the controller. Bound
tracks can be inspected, but their clocks cannot be controlled independently.

## Prefabs and export

Prefab instances can join a sequence through their Scene settings. Duplicate a
controller together with its objects to make an independently bound copy. Copying
only an object keeps its link to the existing controller.

Complete arrangements currently belong to Scenes. Create Prefab rejects a subtree
containing bound tracks. Create an unbound prefab first, place it, then bind its
instance in the Scene. A prefab definition does not capture a portable arrangement.

Save the Scene and publish its clips before exporting. Export includes the saved
arrangement and prepared clips and sounds. The exported game needs no editor or
C++ compiler for this feature. Existing native C++ modules must be rebuilt with the
matching SDK when upgrading to this engine version.
