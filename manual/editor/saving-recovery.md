# Saving and recovery

Save writes the active scene file. Recovery snapshots separately preserve unsaved edits so you can restore them after an interruption.

## Know whether changes are saved

A `*` in the title and **Unsaved changes** in Hierarchy indicate edits that differ from the saved scene. Undoing back to the saved state clears the dirty indicator. A new untitled scene needs a filename before it can be saved.

Press Ctrl+S or select **Save**. For an untitled scene, choose a file inside the project. Ctrl+Shift+S opens Save scene As... when Scene is the active task.

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

## Older scenes and identity records

Opening an older scene prepares its permanent identifiers without replacing its source file. Keep the neighboring `.forge-identity.json` record until migration is saved; it makes reopen and failed-save retries retain the same identifiers. Save keeps a `.v1.backup` of the original before replacing the scene atomically with format 3. Format-2 scenes already have permanent IDs; their first format-3 save retains an exact `.v2.backup` instead.

If an older scene changes externally after its identity record was created, opening it reports a conflict and preserves both files. Do not delete the identity record to force a retry: doing so would lose the assigned identifiers. Restore the matching source and record from your backup/source control, or retain both versions for deliberate reconciliation. Automatic cross-file/plugin reference reconciliation is not implemented.

If saving fails because another program holds the file open, close that program and retry. The editor keeps the current scene, the original disk file, and the identity record. Migration preserves the old placement and independent parent behavior using **Space → World**. New parenting uses **Follow parent**. See [Transforms](transforms.md).
