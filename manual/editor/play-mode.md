# Play mode

Play starts a separate runtime process with a copy of the authored scene. This lets you test supported gameplay without putting gameplay DLLs inside the editor process.

## Start, restart, and stop

- **Play** starts a fresh play world. The Scene panel previews its positions.
- **Restart** replaces it with a fresh copy of your latest authored scene.
- **Stop** ends play and returns the preview to authored positions.

Inspector edits and Save always affect the authored scene. Runtime movement is not automatically written back. Without an active gameplay module, the diagnostic blocks normally stay stationary.

## Recover after a runtime crash

The editor reports runtime failures in Console. When a checkpoint is available, **Recover** resumes the last completed runtime checkpoint. **Play** starts fresh instead.

The separate runtime process protects the editor from gameplay-process crashes. Editor defects and trusted native code inside the editor can still crash it. Scene autosave recovery is separate from runtime checkpoint recovery.

Changing projects or scenes stops play. Native builds temporarily disable operations that would conflict with compilation. See [Native gameplay](native-gameplay.md) and [Saving and recovery](saving-recovery.md).
