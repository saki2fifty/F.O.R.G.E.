# Projects

A project folder keeps your scenes and gameplay source together. FORGE also stores temporary native build results and recovery snapshots inside its `.forge` subfolder.

## Create a project

1. Choose **File → New project**.
2. Enter **Name / folder**. This becomes the new project subfolder's name.
3. Enter an existing **Parent folder**, or select **Browse...** to choose one.
4. Select **Create**. If the current scene has unsaved edits, resolve the save prompt first.

The new folder contains `Scenes/main.scene.json`, `Assets`, `Native`, and `forge.project.json`. Choose a new folder name: existing folders are not overwritten. The Assets folder is reserved for future asset workflows; creating it does not enable asset importing.

## Open or return to a project

Choose **File → Open project...** and select the project folder. **Recent projects** lists the last eight successfully opened projects. Directly launching the editor returns to the last project; launchers or command-line project arguments can select another one.

Each project opens its configured startup scene. The last scene edited is not automatically made the startup scene. Legacy project folders containing `main.scene.json` are supported.

## Choose a startup scene

Save the desired scene inside the project, then close the editor and edit `forge.project.json`. Set `startup_scene` to its relative path, for example `Scenes/LevelTwo.scene.json`. Keep the manifest's version at 1. There is no startup-scene picker in the editor yet.

Switching projects stops play and selects the new project's native source/build directory. Wait for an active native compilation to finish before switching. See [Scenes](scenes.md) and [Native gameplay](native-gameplay.md).

## One editor per project

FORGE holds exclusive project writer ownership while a project is open. Opening the same project in another FORGE editor fails with a diagnostic. The current project stays open if a requested project switch fails. Close or switch the first editor before trying again.

If an editor process crashes, the operating system releases its ownership. Open the project again and use the normal recovery prompt to restore unsaved work. The empty **.forge/writer.lock** marker can remain after shutdown; its presence alone does not mean the project is locked. Do not delete it to force another editor into a running project.

Keep active projects on a local writable filesystem. Network-share locking and simultaneous editing with older FORGE builds are not supported by this ownership contract. External text editors can still change scene files; FORGE's Save protection detects changes against its last saved version.

A trusted script can work through the owning editor using [Live automation](live-automation.md).

## First launch and the active project

Launching `forge_editor.exe` without a project argument first reopens your last project. On a fresh installation, it creates a **Scratch** project under your personal FORGE settings folder, outside the application ZIP folder. If the last project cannot be opened, Console explains the fallback to Scratch. An explicitly supplied project that cannot be opened reports an error instead.

The Content panel shows the project name; hover it for the complete folder path. The title bar also identifies the project and scene. Use File → New project or Open project to choose where your game belongs. Launchers with an explicit Project argument keep using that directory. Existing projects and scenes are retained.
