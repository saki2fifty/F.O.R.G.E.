# Play mode

Play runs a copy of your scene in a separate runtime process. Your editor scene stays unchanged while you test gameplay. The editor and the runtime are always separate processes — there is no in-process gameplay world inside the editor.

Play becomes available after the initial source scan and queued asset imports finish. If scene preparation rejects an incompatible mesh or animation revision, FORGE reports the error and keeps the previous active scene instead of waiting indefinitely.

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

A system's changes may be small in a single tick. Use Tick to check the exact
step. C++ code/schema changes use **Stop → Save → Build → Play**; there is no
in-place C++ DLL reload. Runtime crashes leave authored scene files untouched.
A game-specific Save Game / Continue flow restores only what its save schema owns.
The separate process protects the editor from gameplay-process crashes; trusted
editor code and native editor plugins can still crash it.

## Structured prefabs in Play

Play receives the current validated prefab definitions and stable member mappings. Changes made by gameplay remain in the isolated runtime. Stop Play before editing a prefab source. See [Prefabs](prefabs.md).

Project [Simulation Hz](project-settings.md) sets the next Play session's frequency. [Gameplay input](input.md) uses explicit Game capture; Esc releases, F6 pauses/resumes and F7 steps. Console's Gameplay input section shows fixed-tick values and edge counts.

## Loading, cancel and error feedback

- Pressing **Play** starts the runtime in a separate process. FORGE prepares the scene and its resources automatically; use **Cancel loading** if it is offered.
- If the runtime offers a new candidate while play is already running, the editor stages it for you. The previous world stays active until the new candidate activates. If you decide you do not want the new scene, press **Cancel loading** to discard it. Your previous world is preserved.
- **Cancel loading** discards the in-flight candidate and keeps the current world. The first Play run has no previous world to retain — a failure there surfaces in Console and leaves Play inactive.
- Runtime exceptions during play are surfaced through Console and the status bar. Quit from the in-game menu returns a usable editor.

## Response timing and runtime failures

Responses received within the five-second limit remain valid if the editor takes longer to apply them. A runtime timeout, disconnect, or invalid response is reported in Console. Receiving responses in the background does not prevent UI stalls during expensive editor work.

## Save and Load (gameplay, not the authored scene)

Use the in-game **Save Game** / **Continue** menu to save and restore the gameplay session. These writes go to the runtime's user-specific storage area and never touch the editor's authored scene.

Save storage is keyed by the operating-system user base plus the project's `game.application_id`. Reopening the same project under the same OS user restores its saves. A second project that uses the same `application_id` shares the same saves; unrelated games should use distinct application identifiers.

Stop Play before editing or saving the **authored scene** — runtime Save Game writes never enter the authored scene.

## Native SDK folder

**Build → Gameplay setup → Native SDK folder** points at the matching installed Native SDK. The editor uses this folder to launch the runtime that drives your gameplay module. The field defaults to empty; when empty, FORGE uses the NativeSdk shipped alongside the editor and only switches to your selected folder when you override it.

The toolkit used to build your gameplay module must match the folder you point at. A mismatch refuses to start Play and the editor does not load the module. Your authored scene stays untouched.

Code changes require Stop, Save, Build and Play. Runtime values can be tuned in the Live gameplay Inspector without compiling.

## Keyboard in play

**Esc** releases the physical relative mouse capture and forwards the down/up to the gameplay module. Whether the game opens Pause or its in-game menu on Escape is project policy, not something FORGE controls. The dedicated pause/resume/step keys are listed in [Gameplay input](input.md).

## Scene and Game are separate views

**Scene** always shows the authored world and editor tools. **Game** shows the snapshot from the one isolated Play process. Starting Play opens Game; switching tabs does not start another simulation. Game renders through the scene's enabled **Camera** components. It does not borrow the Scene navigation camera. If no valid camera is enabled, Game stays black and reports the missing camera.

To add a view, stop Play, create **Camera** from the Create menu or command palette, and position it with its Transform. Configure projection and clipping in Inspector. FORGE cameras look along their local **+Z** axis. Start Play to check the result. Moving the Scene navigation camera changes only your editing view.

Several enabled cameras render in their **Order**, with their own viewport rectangles, layer masks and color/depth clear choices. A fixed aspect ratio fits inside the assigned rectangle with uncovered areas left black. See [Scene lighting](lighting.md) for Game exposure and environment settings.

Global Play/Pause/Step/Stop are also in **Run**, the command palette and the **More** (three-dot) button at narrow widths. Game labels Starting, Playing, Paused, Stopped or Crashed/Recovery available; the permanent status bar also identifies runtime state.

Use **Capture gameplay input** in Game. Escape, focus loss, hiding Game, or clicking outside its image releases input. Editor buttons receive that outside click. Runtime HUD and Reload UI belong to Game. Authoring stays locked during Play.
