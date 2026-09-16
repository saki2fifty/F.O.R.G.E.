# Saving and recovery

Save writes the active scene file. Recovery snapshots separately preserve unsaved edits so you can restore them after an interruption.

## Know whether changes are saved

A `*` in the title and **Unsaved changes** in Hierarchy indicate edits that differ from the saved scene. Undoing back to the saved state clears the dirty indicator. A new untitled scene needs a filename before it can be saved.

Press Ctrl+S or select **Save**. For an untitled scene, choose a file inside the project. Ctrl+Shift+S always opens Save As.

## Resolve an unsaved-change prompt

When closing the editor or replacing the scene, choose one of these actions:

- **Save and continue** writes the current scene before continuing. A save failure preserves your edits.
- **Discard changes** continues without saving. The old recovery snapshot is removed after the requested operation succeeds.
- **Cancel** keeps you in the current scene with your edits intact.

Cancelling the filename dialog during Save and continue returns you to the unsaved-change decision.

## Restore a snapshot

FORGE writes changed unsaved scenes to `.forge/recovery` approximately every 30 seconds. **File → Create recovery snapshot** writes one immediately. These snapshots do not replace your scene file.

1. Reopen the project and the scene you were working on.
2. If **Recovery available** appears, select **Restore recovery**.
3. Inspect the restored scene, then Save to keep it.

**Later** leaves the snapshot available. **Keep saved scene** deletes that offered snapshot. **File → Recover current scene** also restores a matching snapshot as an undoable edit.

An untitled scene uses one recovery slot per project. **File → Recover untitled scene** restores it even if you have reopened the project's startup scene. Give the restored scene a filename with Save As.

## When disk contents changed

Save rejects an active scene file changed or deleted outside FORGE. Use Save As with a different filename to preserve your edits, or Reload from disk after deciding what to discard. There is no automatic merge.

Recovery also checks the saved baseline. If it no longer matches, FORGE keeps the snapshot and reports a diagnostic rather than replacing the scene. There is no recovery browser or merge tool yet. Snapshots for other named scenes are discovered when those scenes are opened.
