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
Open **Game** while sequence preview is active to see the camera image without
starting gameplay. The **SEQUENCE PREVIEW** label identifies this temporary view.
Use Game Play to hear sound and run gameplay.

## Export preview frames

1. Stop Game Play. Finish any pending Timeline fields, then choose **Refresh clips**.
2. Choose **Export frames…** in the sequence Timeline. Game opens to show the
   sequence preview. Set **Start time**, **Frames per second** and **Frame count**.
3. Choose a parent folder and a new folder name. Check the output dimensions and
   sampled time range. Keep Game open at the same size, then choose **Start export**.
4. Wait for **Sequence frames saved** in the status bar or Console. Open the new
   folder to find the numbered PNGs and `frames.json` listing completed frames.

Every sampled time must be before the sequence's end. For a six-second sequence,
30 FPS and 180 frames sample 0 through 5.966667 seconds. Playback speed does not
change these chosen times. The current Game image size determines output size.
The settings area scrolls in small windows; Start and Cancel remain below it.

**Cancel export** keeps completed images and stops further frames after any
already accepted file finishes. Your previous preview position and playing state
return afterward. Changing the Scene, sequence, assets or Game size stops the
export. An existing output folder is preserved; choose a new name for each run.

These frames contain the silent sequence camera image. Gameplay C++, physics,
runtime UI and sound do not run during this preview. For a single image while
playing, use [Game → Save image](viewport.md#save-an-image).

## Camera shots

Open **FeatureGallery / Scenes / 68-sequence-camera-shots.scene.json**. Play shows
the front view for two seconds, then the side view, then a close view. A short gap
returns to the normal overview camera before the six-second sequence repeats.

To author shots in your own sequence:

1. Create the cameras and position them in the Scene. Standby cameras can have **Enabled** turned off; a shot activates its chosen camera in the rendered view.
2. Open the controller's sequence Timeline. Select a camera in Hierarchy and choose **Add shot from selected camera**. The shot fills the next free interval at or after the playhead.
3. Select its row under **Camera shots**. Choose **Shot camera**, enter **Shot start** and **Shot end**, then choose **Apply shot fields**. The edit uses one Scene Undo step. Apply or reset pending fields before switching tracks.
4. Choose **Refresh clips**, then **Preview sequence** and open **Game**. Scrub a shot's strip or the playhead slider to inspect the cuts. Stopping preview restores the authored camera settings.
5. Choose **Save scene**. Play and export use this saved arrangement; no C++ module is required.

Adjacent shots cut exactly at their shared boundary. Shots cannot overlap and must
fit inside the controller's duration. Gaps use normal enabled cameras and their
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

**Remove from sequence** clears the binding and keeps the clip. An object can have
one Transform Animator clip. This sequence mode does not blend overlapping
tracks, blend between shots, edit bones or nest sequences.

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
