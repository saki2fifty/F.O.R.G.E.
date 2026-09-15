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
