# Play mode

Play runs a copy of your scene in a separate runtime process. Your editor scene stays unchanged while you test gameplay. The editor and the runtime are always separate processes — there is no in-process gameplay world inside the editor.

## Play, Pause, Step, Resume, and Stop

- **Play** starts a fresh play session from a copy of your current authored scene. Opening a project normally opens its startup scene; a subsequent user **Open Scene** that you make editable is also playable from its current authored state.
- **Pause** freezes gameplay. You can still navigate the editor camera and read diagnostics.
- **Step** advances exactly one simulation tick, then stays paused. The default is 60 ticks per second, so one step advances 1/60 second of gameplay.
- **Resume** continues from the paused state. Time spent paused is not played back afterward.
- **Restart** starts again from your authored scene.
- **Stop** ends play and returns to authored positions.

The runtime keeps its own clock. Editor FPS and snapshot polling do not set gameplay speed. Running presentation blends between completed simulation poses; paused presentation shows the latest completed state. Without a gameplay module, objects normally stay stationary even though the tick counter increases.

Large scene updates are sent in bounded pieces while gameplay continues. If an update is still being received, the next control waits for that update to finish; it is not silently dropped.

Stop Play before editing or saving the **authored scene**. Runtime movement is never automatically saved into the authored scene.

## Check one tick

Open **Console** while playing. The runtime status shows **Tick**, **Fixed**, **Dropped**, and **Clamped**.

1. Press **Pause** and note Tick.
2. Press **Step** once.
3. Tick increases by exactly one and the game stays paused.

The legacy sample gameplay module (ABI1) moves one meter per second along X. One default step moves about 0.0167 meters, which may be hard to see at a distant camera position. Use Tick to verify the step precisely.

## Reload while paused (legacy ABI1)

A compiled candidate is tested in a separate disposable runtime first. If you rebuild while paused, the status says **Reload pending first tick**. Loading it does not move your game.

Press **Step** to test it with one tick and stay paused, or **Resume** to continue. Activation finishes after that first live tick succeeds. If it fails, FORGE restores the previous module and the checkpoint captured before reload, keeping the previous paused/running policy.

**Stop** cancels a pending activation without running it. A newer successful build supersedes the pending candidate; the original known-good checkpoint remains the recovery point. See [Native gameplay](native-gameplay.md).

This reload/checkpoint flow is part of the legacy ABI1 host. The exact-SDK Play path does not implement partial live DLL reload, partial recovery, or transactional external native callbacks. Stop Play to rebuild the project module externally, then Start Play again.

## Recover after a runtime crash (legacy ABI1)

Console reports runtime failures. After a later gameplay crash, **Recover** starts the last acknowledged checkpoint with its module. **Play** starts fresh instead. A restarted process has a new tick sequence beginning at zero.

The separate process protects the editor from gameplay-process crashes. Editor defects and trusted native editor plugins can still crash the editor. Scene autosave recovery is separate from runtime checkpoint recovery.

Changing projects or scenes stops play. Compilation temporarily disables conflicting file operations; waiting for a paused candidate's first tick does not block Step, Resume or Stop. See [Saving and recovery](saving-recovery.md).

The runtime-checkpoint recovery described here belongs to the legacy ABI1 host. The exact-SDK Play path uses the explicit Save Game / Continue flow instead and does not perform partial recovery.

## Structured prefabs in Play

Play receives the current validated prefab definitions and stable member mappings. Changes made by gameplay remain in the isolated runtime. Stop Play before editing a prefab source. See [Prefabs](prefabs.md).

Project [Simulation Hz](project-settings.md) sets the next Play session's frequency. [Gameplay input](input.md) uses explicit Game capture; Esc releases, F6 pauses/resumes and F7 steps. Console's Gameplay input section shows fixed-tick values and edge counts.

## Physics recovery (legacy ABI1)

[Physics](physics.md) runs on the same fixed clock. Recover restores the last complete supported physics checkpoint, including velocity and sleeping state. An incompatible or incomplete checkpoint produces a failure message and a clean Play restart option. This is session recovery, not a save-game feature.

Physics checkpoint recovery is part of the legacy ABI1 host. The exact-SDK Play path does not preserve a separate physics checkpoint; a game-specific Save Game restores only the state explicitly supported by that game's save schema. The exact-SDK path does not offer legacy physics-checkpoint recovery.

## Loading, cancel and error feedback

- Pressing **Play** starts the runtime in a separate process. FORGE prepares the scene and its resources automatically; use **Cancel loading** if it is offered.
- If the runtime offers a new candidate while play is already running, the editor stages it for you. The previous world stays active until the new candidate activates. If you decide you do not want the new scene, press **Cancel loading** to discard it. Your previous world is preserved.
- **Cancel loading** discards the in-flight candidate and keeps the current world. The first Play run has no previous world to retain — a failure there surfaces in Console and leaves Play inactive.
- Runtime exceptions during play are surfaced through Console and the status bar. Quit from the in-game menu returns a usable editor.

## Save and Load (gameplay, not the authored scene)

Use the in-game **Save Game** / **Continue** menu to save and restore the gameplay session. These writes go to the runtime's user-specific storage area and never touch the editor's authored scene.

Save storage is keyed by the operating-system user base plus the project's `game.application_id`. Reopening the same project under the same OS user restores its saves. A second project that uses the same `application_id` shares the same saves; unrelated games should use distinct application identifiers.

Stop Play before editing or saving the **authored scene** — runtime Save Game writes never enter the authored scene.

## Native SDK folder

**Gameplay Code → Native SDK folder** points at the matching installed Native SDK. The editor uses this folder to launch the runtime that drives your gameplay module. The field defaults to empty; when empty, FORGE uses the NativeSdk shipped alongside the editor and only switches to your selected folder when you override it.

The toolkit used to build your gameplay module must match the folder you point at. A mismatch refuses to start Play and the editor does not load the module. Your authored scene stays untouched.

The exact-SDK Play path does not implement partial live DLL reload, partial recovery, or transactional external native callbacks. Stop Play to rebuild the project module externally, then Start Play again.

## Keyboard in play

**Esc** releases the physical relative mouse capture and forwards the down/up to the gameplay module. Whether the game opens Pause or its in-game menu on Escape is project policy, not something FORGE controls. Legacy Play sessions that used Escape as the universal release still work — pressing Escape returns the cursor and stops sending motion to gameplay. The dedicated pause/resume/step keys are listed in [Gameplay input](input.md).

## Scene and Game are separate views

**Scene** always shows the authored world and editor tools. **Game** shows the snapshot from the one isolated Play process. Starting Play opens Game; switching tabs does not start another simulation. Game renders through the scene's enabled **Camera** components. It does not borrow the Scene navigation camera. If no valid camera is enabled, Game stays black and reports the missing camera.

To add a view, stop Play, create **Camera** from the Create menu or command palette, and position it with its Transform. Configure projection and clipping in Inspector. FORGE cameras look along their local **+Z** axis. Start Play to check the result. Moving the Scene navigation camera changes only your editing view.

Several enabled cameras render in their **Order**, with their own viewport rectangles, layer masks and color/depth clear choices. A fixed aspect ratio fits inside the assigned rectangle with uncovered areas left black. See [Scene lighting](lighting.md) for Game exposure and environment settings.

Global Play/Pause/Step/Stop are also in **Run**, the command palette and the **More** (three-dot) button at narrow widths. Game labels Starting, Playing, Paused, Stopped or Crashed/Recovery available; the permanent status bar also identifies runtime state.

Use **Capture gameplay input** in Game. Escape, focus loss, hiding Game, or clicking outside its image releases input. Editor buttons receive that outside click. Runtime HUD and Reload UI belong to Game. Authoring stays locked during Play.

Legacy gameplay modules (ABI1) keep their own host and are not part of the SDK Play acceptance flow.
