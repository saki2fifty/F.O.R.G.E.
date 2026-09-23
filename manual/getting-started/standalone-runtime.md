# Run the standalone development host

This Phase8 feature is available from source builds. **Build66 does not include
it.** It opens a separate game window using the scene's camera and game UI.
The source-build [Export Game workflow](../editor/runtime-content.md) prepares a
standalone folder; its native Windows acceptance is still in progress.

## Before launching

Build the Windows target `forge_game` with `FORGE_BUILD_GAME=ON`. Keep its generated
DLLs and `resources/ui` folder beside the executable. Your prepared project needs:

- A saved, registered startup scene with an enabled Camera.
- Imported assets with usable cooked data for the Windows target.
- Saved **Game defaults** in **Project Settings**, including a persistent Application ID.
- A matching shared-SDK host if the project uses native SDK gameplay modules.

Use **Set up game defaults** in Project Settings for ordinary editing. For a
headless development project, the equivalent `game` JSON member is shown below.
Choose your own application ID and title. Keep the ID unchanged when renaming the game, so its
personal settings and saves remain in the same place.

```json
"game": {
  "version": 1,
  "application_id": "com.example.mygame",
  "title": "My Game",
  "profile": "development",
  "save_schema": 1,
  "display": {"mode": "windowed", "width": 1280, "height": 720, "display": 0, "vsync": false},
  "audio": {"master_volume": 1},
  "input": {"mouse_sensitivity": 1}
}
```

Launch from PowerShell:

```powershell
.\forge_game.exe --project "C:\Projects\MyGame"
```

The host uses the configured startup scene and window settings. Required meshes,
materials, animation, navigation, audio and game UI are prepared before gameplay.
The title shows the preparation stage. Missing required resources cause a startup
error instead of silently starting with missing content.

## Settings and game UI

Project defaults and existing personal settings determine the display, VSync,
audio volume, action bindings and mouse sensitivity. Personal data lives outside
the project in the OS user-data directory. There is no standalone settings screen
or Save/Load menu yet.

RmlUi buttons can invoke Pause, Resume and Step. Keyboard and mouse input goes to
the UI first, then the project's actions. Losing window focus releases held inputs.
Full controller support and cursor-capture/menu behavior are still in progress.

Close the window to stop. Messages appear in `runtime.log` inside the game's
user-data folder; failures also appear in the launching console. On Windows the
base is `%APPDATA%\FORGE\Games`, with a `game-APPLICATION_ID` subfolder.

## Remaining Phase8 work

The character controller, full input/menu workflow, gameplay SDK session/save
access and integrated reference game remain in progress. A successful export
validates the declared content closure; it does not prove every possible request
a native gameplay module could construct.

## Export and verify a game folder

Use [Export Game](../editor/runtime-content.md) to create a relocatable Development
standalone folder. A normal launch runs `forge_game.exe` with no arguments and
uses its validated startup manifest. `--project PATH` remains the explicit
development-project launch option.

For automated startup diagnosis, `forge_game.exe --verify-startup` admits the
package, creates the software D3D12 device and offline audio, waits for startup
resource readiness, renders several frames and exits with a success/failure code.
This verifies the production executable; it does not measure physical GPU or
audio-device behavior, or replace testing gameplay interactions.
