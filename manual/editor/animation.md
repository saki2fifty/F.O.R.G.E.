# Animation

An **Animator** plays one skeletal animation clip on an object during Play.
For now, FORGE shows the joints and bones as a green-and-white overlay. It does
not yet import or draw a skinned character mesh.

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
- **Animation clip:** a clip converted for that exact skeleton revision.
- **Enabled:** evaluate and show this Animator during Play.
- **Play on Start:** begin when the runtime first creates this Animator. Changing this field on an existing player does not restart it.
- **Loop:** repeat at the end. Without Loop, playback holds the final pose.
- **Playback speed:** `1` is normal speed, `2` is double speed, and `0` holds time. The supported range is `0–4`. Enter commits the edit.

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
