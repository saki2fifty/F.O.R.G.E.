# Animation

An **Animator** plays one skeletal animation clip on an object during Play.
For now, FORGE shows the joints and bones as a green-and-white overlay. It does
not yet draw a skinned character mesh. Model importing is available through the
[model import tools](models.md).

## Try the included animation

1. Copy `Examples/Animation/two-joints.gltf` from the extracted ZIP into your project's `Assets` folder. This small example contains two joints and a clip named **Lift**.
2. Open **Content → Create / Register → Animation assets**. In **glTF in project**, enter `Assets/two-joints.gltf`.
3. Choose **Convert/Register Animation**. Wait for the completion message in the Console. **Cancel conversion** stops an unfinished conversion.
4. Create an entity and select it. Use **+ Add Component → Animation / Animator** in Inspector.
5. Choose the converted asset in **Skeleton**, then choose **Lift** in **Clip**. Leave **Enabled**, **Play On Start**, and **Loop** checked.
6. Press **Play**. One joint stays at the object's origin; the other moves upward and repeats. Orbit or zoom the viewport to see the connecting bone.
7. Press **Pause**: the bones hold their pose. **Step** advances one simulation tick. **Resume** continues, and **Stop** returns to the authored scene.
8. Save, reopen, and repeat. The scene retains asset references and settings, not temporary bone poses.

The debug overlay is visible through geometry. It is intended to verify animation,
not to show final character rendering. Scale and rotate the owning entity to inspect
how its world transform affects the skeleton.

## Animator controls

- **Skeleton:** the converted joint layout and rest pose.
- **Clip:** a clip converted for that exact skeleton revision.
- **Enabled:** evaluate and show this Animator during Play.
- **Play On Start:** begin when the runtime first creates this Animator. Changing this field on an existing player does not restart it.
- **Loop:** repeat at the end. Without Loop, playback holds the final pose.
- **Speed:** `1` is normal speed, `2` is double speed, and `0` holds time. The supported range is `0–4`. Enter commits the edit.

Animator edits use scene Undo/Redo. Prefab instances can independently override
properties; even an edit equal to the source remains an explicit override. Use the
existing prefab **Revert** controls to follow the source again.

## Convert your own source

Use a small animation-only **glTF2 `.gltf`** file with named clips and skeletal translation, rotation or scale channels. Morph-weight channels are rejected. Embedded buffers
and relative buffer files inside the project are supported. Keep all dependencies
inside the project. Images, glTF extensions, FBX, mesh/material import and arbitrary
`.ozz` imports are not supported by this initial workflow.

Conversion runs outside the editor with resource limits. A failed conversion leaves
the previously registered skeleton and clips selected. The Console explains failure,
including invalid assets, unsupported generated formats, or incompatible skeletons.
The included translation fixture and automated rotation fixture are tested; broader
DCC export compatibility is not yet claimed.

## Update an animation

Edit the glTF and run **Convert/Register Animation** again using the same registered
source. Successful conversion keeps its logical asset identities and replaces the
whole generated skeleton/clip set together. Restart Play to use the new revision.
Renaming or removing clip names is currently rejected to avoid silently changing
what an existing reference means.

Conversion is a project asset operation. **Scene Undo does not undo conversion.**
Supported catalog relocation preserves identity; manually moving files without
updating the catalog makes them missing.

## Recovery and current limits

Runtime recovery restores clip time and playing state only when the referenced
assets still have compatible exact revisions. FORGE reloads assets and rebuilds
sampling state. If assets changed, recovery is rejected; start a fresh Play session.

This foundation has single-clip playback and debug bones. Animation graphs, blending,
IK, retargeting, root-motion application and skinned mesh rendering are future work.

See [Play mode](play-mode.md), [Prefabs](prefabs.md), and
[Saving and recovery](saving-recovery.md).

### Imported parent transforms

Animation-only imports preserve a parent's matrix-authored rest transform when
only its child has animation keys. FORGE leaves the original file unchanged.
A malformed or unsupported matrix produces a conversion error and keeps the
previous successful import selected.

### Clips imported with a model

The [model import tools](models.md) also register Skeleton and Clip assets.
You can assign those references to an Animator for the existing bone preview.
Skeleton and Clip must come from the same successfully imported model revision.

These cooked assets load in the background during Play. While they load, that
Animator has no pose and a complete runtime recovery snapshot is unavailable.
Loading also completes while paused; it does not advance animation time. If loading
fails, the Console reports the affected asset. Restart Play after reimporting to
select the new model revision.

When an Animator belongs to a model instance, a translation-only clip leaves the
nodes' rotation and scale alone, including values inherited from a prefab. Two
instances can use different playback speeds. A node moved outside its model root
stops receiving that model's animation and produces a warning. Duplicate source-node
identities within one instance produce an error instead of choosing an arbitrary
object. Public animated-model placement and skinned rendering are still being
integrated; the existing standalone bone-preview workflow remains available.

This addition supplies playback poses and morph weights. Drawing an imported
skinned or morphed mesh is still being integrated.

Gameplay code replacement needs a complete recovery snapshot. If model assets are
still loading, FORGE keeps the current code and asks you to retry the build after
loading finishes. A crash before loading completes requires a fresh Play session.


### Model binding errors

The in-progress model rendering path checks that mesh, skeleton and clip belong
to the same imported model revision. If a required joint is missing, duplicated or
moved outside the model instance, FORGE reports a binding error and holds the last
complete rendered pose. It does not borrow a joint from another copy of the model.
Restore the matching hierarchy or restart Play after a successful reimport. A
failed pose does not change the authored scene. Native acceptance of this rendering
path and the complete animated-model placement workflow are still pending.

### Physics conflicts

An animated model node with a **Dynamic** Physics Body cannot also take movement or
rotation from a clip. Use a suitable **Kinematic** body for animation-driven motion,
or keep the animation on a separate visual child. If an animated scale is invalid
for its collider, FORGE keeps the previous pose and reports the reason. Fixing the
conflicting component lets the animation resume. Scene source values remain intact
when Play stops.

### Bones on model instances

For a model instance, the bone overlay follows the actual scene nodes, including
inherited transforms and their spatial bindings. It does not apply the model root
transform twice. Missing or ambiguous joints are omitted. Standalone animation
previews still show the clip's skeleton relative to the selected Animator entity.
The overlay remains a diagnostic display that can be seen through geometry.
