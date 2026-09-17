# Prefabs

A prefab is a reusable group of objects saved as its own asset. Instances follow the prefab's values until you override a value in that instance. Editing the source updates the instances that still follow it.

## Create a prefab

1. Build a small group of ordinary objects in the Hierarchy, with one parent and its children. A subtree already containing prefab instances or legacy prefab definitions is rejected; nested prefab conversion is deferred.
2. Select the parent.
3. In **Content**, expand **Prefab assets**.
4. Enter a new project-relative filename, such as `Assets/Chair.prefab.json`.
5. Click **Create from selection**.

Your original objects remain in the scene. Create does not replace or delete them. Existing files are never overwritten by Create.

## Add linked copies

Select the prefab's filename in **Content → Prefab assets**, then click **Instantiate**. Repeat to create another copy. Each copy has its own object identities. Instantiation is one scene Undo step.

The Hierarchy marks roots with **[prefab]** and their structured children with **[member]**. Select any member to inspect it. Move, rotate and scale work on individual members; rearranging, renaming or removing the source's interior happens in **Edit source**. You can attach ordinary scene objects beneath a member using the usual Parent control.

## Change one instance

Select an instance or one of its members and edit its normal Inspector fields. Translation, rotation and scale are independent: moving a member does not stop it from following source rotation or scale changes.

An override means you explicitly chose that value. It remains an override even if it equals the source value. Supported color fields can have independent overrides; changing red does not require overriding green and blue. Setting the whole color through the color picker explicitly sets all three channels.

The Inspector's **Prefab instance** section shows the resolved revision and offers **Revert** controls for overridden components and supported individual fields.

## Revert to the source

Instance root names follow the source until you rename the instance; **Revert name** restores that link.

Click the matching **Revert** control in the Inspector. The instance immediately follows the current source value again. Scene **Undo** restores the override, and **Redo** removes it again.

Transform Revert works on a whole channel: translation, rotation or scale. Rotation is one complete rotation, rather than four independently inherited quaternion numbers.

## Edit the reusable source

1. Select the asset in **Content → Prefab assets** and click **Edit source**. Alternatively, select an instance and click **Open prefab source** in its Inspector.
2. In **Prefab source**, select a member.
3. Edit its name or component fields. **Parent member** changes the source hierarchy while keeping the member's local transform.
4. **Member space** selects FollowStructure, World or an explicit source-member attachment while retaining local values. Instance-root attachment is chosen in the scene Inspector.
5. **Add child** creates a member with a new identity. **Remove member subtree** removes a member and its descendants from the source.
6. Click **Publish source**.

FORGE validates the candidate and prepares replacement instances before replacing the saved asset. Invalid values, hierarchy cycles, stale files and failed file replacement keep the previous usable revision and instance state. The error stays visible and your candidate remains in the source window for correction.

A successful publication updates non-overridden values. Existing overrides remain intact. **Discard edits** restores the window's last published contents. Closing the window discards unpublished edits.

**History boundary:** publishing a source change affecting the current scene clears that scene's Undo/Redo history. Scene Undo does not undo shared prefab-asset edits. Save your scene after a source change adds new members so their new object mappings are persisted. Apply from an instance back into its source is not available.

Prefab authoring is disabled while Play, native builds, file dialogs or transform gestures are active. Stop Play before publishing; the next Play session receives the updated prefab definitions.

## Duplicate instances and assets

Use the normal **Duplicate subtree** action on an instance root to make another linked instance with fresh object identities and copied override intent.

For an independent source, select the prefab in Content, enter a new destination filename and click **Duplicate asset**. The copy has a new asset identity and new member identities. Editing it does not edit the original prefab.

## Missing assets and removed members

A removed member keeps its previous object identity and overrides as a **[missing]** entry. References do not silently point to a different object. A missing prefab asset likewise keeps the instance's identity, mapping and override data.

Restore the original asset or member identity, then click **Refresh prefabs**. Renaming or moving a `.prefab.json` file within the project does not change its asset identity. Copying a file by hand preserves its identity and is rejected as a duplicate; use **Duplicate asset** for a new logical asset.

After editing source files outside FORGE, increase the source revision and use **Refresh prefabs**. Invalid external candidates leave the editor's previous revision active. Reopen the source window before publishing if its file changed externally.

## Current boundaries

The source window edits the current built-in components. Unknown plugin data is preserved without interpreting it. Nested prefabs, per-instance interior hierarchy changes, Unpack and Apply to Prefab are deferred. Existing scene-local prefabs remain supported in their original form; opening them does not create asset files or automatically convert them.

See also [Transforms](transforms.md), [Undo and redo](undo-redo.md), [Saving and recovery](saving-recovery.md) and [Play mode](play-mode.md).

## Physics defaults

Body and collider components can be inherited from prefab sources. Source changes propagate to fields without overrides; explicit instance edits remain. Revert removes that intent and is part of scene Undo. Solver objects exist only during Play and are never saved in the prefab. See [Physics](physics.md).

A newly instantiated prefab root keeps the existing per-instance spatial attachment policy. If its body is Dynamic, set that instance root's **Child space** to **World** before Play. FORGE does not silently detach it. Duplicating a configured instance retains its binding.
