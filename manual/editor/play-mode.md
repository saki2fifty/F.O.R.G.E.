# Play mode

Play runs a copy of your scene in a separate runtime process. Your editor scene stays unchanged while you test gameplay.

## Play, Pause, Step, Resume, and Stop

- **Play** starts a fresh play world. With the sample gameplay module, objects move along X.
- **Pause** freezes gameplay. You can still navigate the editor camera and read diagnostics.
- **Step** advances exactly one simulation tick, then stays paused. The default is 60 ticks per second, so one step advances 1/60 second of gameplay.
- **Resume** continues from the paused state. Time spent paused is not played back afterward.
- **Restart** starts again from your authored scene.
- **Stop** ends play and returns to authored positions.

The runtime keeps its own clock. Editor FPS and snapshot polling do not set gameplay speed. Running presentation blends between completed simulation poses; paused presentation shows the latest completed state. Without a gameplay module, objects normally stay stationary even though the tick counter increases.

Large scene updates are sent in bounded pieces while gameplay continues. If an update is still being received, the next control waits for that update to finish; it is not silently dropped.

Stop Play before editing or saving. Runtime movement is never automatically saved into the authored scene.

## Check one tick

Open **Console** while playing. The runtime status shows **Tick**, **Fixed**, **Dropped**, and **Clamped**.

1. Press **Pause** and note Tick.
2. Press **Step** once.
3. Tick increases by exactly one and the game stays paused.

The sample moves one meter per second along X. One default step moves about 0.0167 meters, which may be hard to see at a distant camera position. Use Tick to verify the step precisely.

## Reload while paused

A compiled candidate is tested in a separate disposable runtime first. If you rebuild while paused, the status says **Reload pending first tick**. Loading it does not move your game.

Press **Step** to test it with one tick and stay paused, or **Resume** to continue. Activation finishes after that first live tick succeeds. If it fails, FORGE restores the previous module and the checkpoint captured before reload, keeping the previous paused/running policy.

**Stop** cancels a pending activation without running it. A newer successful build supersedes the pending candidate; the original known-good checkpoint remains the recovery point. See [Native gameplay](native-gameplay.md).

## Recover after a runtime crash

Console reports runtime failures. After a later gameplay crash, **Recover** starts the last acknowledged checkpoint with its module. **Play** starts fresh instead. A restarted process has a new tick sequence beginning at zero.

The separate process protects the editor from gameplay-process crashes. Editor defects and trusted native editor plugins can still crash the editor. Scene autosave recovery is separate from runtime checkpoint recovery.

Changing projects or scenes stops play. Compilation temporarily disables conflicting file operations; waiting for a paused candidate's first tick does not block Step, Resume or Stop. See [Saving and recovery](saving-recovery.md).

## Structured prefabs in Play

Play receives the current validated prefab definitions and stable member mappings. Changes made by gameplay remain in the isolated runtime. Stop Play before editing a prefab source. See [Prefabs](prefabs.md).


Project [Simulation Hz](project-settings.md) sets the next Play session’s frequency. [Gameplay input](input.md) uses explicit Game capture; Esc releases, F6 pauses/resumes and F7 steps. Console’s Gameplay input section shows fixed-tick values and edge counts.

## Physics recovery

[Physics](physics.md) runs on the same fixed clock. Recover restores the last complete supported physics checkpoint, including velocity and sleeping state. An incompatible or incomplete checkpoint produces a failure message and a clean Play restart option. This is session recovery, not a save-game feature.

## Scene and Game are separate views

**Scene** always shows the authored world and editor tools. **Game** shows the snapshot from the one isolated Play process. Starting Play opens Game; switching tabs does not start another simulation. Game renders through the scene's enabled **Camera** components. It does not borrow the Scene navigation camera. If no valid camera is enabled, Game stays black and reports the missing camera.

To add a view, stop Play, create **Camera** from the Create menu or command palette, and position it with its Transform. Configure projection and clipping in Inspector. FORGE cameras look along their local **+Z** axis. Start Play to check the result. Moving the Scene navigation camera changes only your editing view.

Several enabled cameras render in their **Order**, with their own viewport rectangles, layer masks and color/depth clear choices. A fixed aspect ratio fits inside the assigned rectangle with uncovered areas left black. See [Scene lighting](lighting.md) for Game exposure and environment settings.

Global Play/Pause/Step/Stop are also in **Run**, the command palette and the **More** (three-dot) button at narrow widths. Game labels Starting, Playing, Paused, Stopped or Crashed/Recovery available; the permanent status bar also identifies runtime state.

Use **Capture gameplay input** in Game. Escape, focus loss, hiding Game, or clicking outside its image releases input. Editor buttons receive that outside click. F6 pauses/resumes, F7 steps while paused; Ctrl+Plus/Minus remains available for editor zoom. Runtime HUD and Reload UI belong to Game. Authoring stays locked during Play.
