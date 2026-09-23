# F.O.R.G.E.

[![CHANGELOG — Browse daily updates](changelog/badge.svg)](changelog/README.md)

**Flecs-Oriented Runtime & Game Editor**

Native ECS game editor under development. Windows/D3D12 is the first editor target; the portable core can be built and tested on Linux.

## User manual

Read the [FORGE User Manual](manual/README.md) for feature explanations and how-to guides. Windows packages include an offline edition under **Help → User Manual**. Technical implementation documents remain in `docs/`.

## Current implementation

- Flecs scene state, stable authored IDs, independent local translation/rotation/scale, derived world transforms, structured prefabs, reflected properties, atomic scene replacement and saves, undo/redo, and unknown-component preservation.
- Headless runtime with its own fixed clock, Pause/Step/Resume, derived hierarchical pose interpolation, protocol 2 and a minimal C17-compatible gameplay module ABI.
- Native C/C++ project generation, incremental CMake builds, source watching, unique module artifacts, isolated candidate probing, constrained reload and runtime checkpoint recovery through a CLI.
- Restart-bound plugin package validation/staging library. Native editor-plugin loading is not implemented.
- Windows editor source with SDL3, Dear ImGui docking, persistent workspace/tooltips, Hierarchy/Inspector/Content/Scene panels, CPU performance diagnostics, and shared Diligent Scene/Game rendering with imported models, PBR materials, cameras, lights, skins and morphs. Windows compilation is validated in GitHub Actions; the initial editor and subsequent UI controls have been exercised on a Windows desktop.

- Structured prefab assets with stable member identities, candidate publication and independent property overrides/Revert.
- [Skeletal animation](manual/editor/animation.md): validated glTF conversion, Animator playback, fixed-clock recovery and debug bones.
- Experimental exact-version shared-Flecs gameplay SDK, [Jolt physics](manual/editor/physics.md), and [WAV audio sources/listeners](manual/editor/audio.md).

- Model/texture/material/shader importing, cooked resources and dependency-aware content packages; source-free game export remains in progress.
- [Navigation](manual/editor/navigation.md), [runtime UI](manual/editor/runtime-ui.md), and the [asset workflow](manual/editor/content-browser.md).
- [Phase8 runtime foundations](docs/game-foundation.md): reusable scene sessions, separate versioned player saves/settings, and OS user-data storage. These internal services do not yet provide a graphical standalone game or a complete Export Game workflow.

Full game export, the first-person reference game, advanced authoring systems and a broad public plugin SDK remain planned. See [implementation status](docs/status.md) for validation and the current delivered build.

## Build and test
Requires Git, CMake 3.30+, Ninja, Python 3.10+, and a C++20/C17 compiler. Windows requires an MSVC developer shell and Windows SDK.

```sh
cmake --preset core
cmake --build --preset core
ctest --preset core
```

The presets place local builds and dependency working directories in the workspace sibling `AgentFiles`. For another layout, configure explicitly:

```sh
cmake -S . -B /absolute/path/to/work/build -G Ninja -DFORGE_BUILD_EDITOR=OFF
cmake --build /absolute/path/to/work/build
ctest --test-dir /absolute/path/to/work/build --output-on-failure
```

Windows editor (requires D3D12-capable hardware to run):

```sh
cmake --preset windows-editor
cmake --build --preset windows-editor --target forge_editor forge_runtime
../AgentFiles/build/windows-editor/forge_editor.exe /absolute/path/to/game-project
```

The editor opens the project manifest startup scene, with legacy `main.scene.json` folders supported. See [Projects](manual/editor/projects.md) and [Scenes](manual/editor/scenes.md). User layout/settings are stored through SDL's application preferences directory.

See [native iteration](docs/native-modules.md), [scene format](docs/scene-format.md), and [dependency baselines](docs/dependencies.md).

## Editor controls

- **Ctrl+Minus**: reduce interface size to fit more controls on screen.
- **Ctrl+Plus** (or **Ctrl+Equals**): increase interface size.
- **Ctrl+0**: reset to 100%. Keypad plus/minus/zero are supported; zoom is saved between launches.
- **Play**: preview an isolated copy of the current authored scene.
- **Restart**: replace the play world with a fresh copy, including your latest authored edits.
- **Stop**: return the viewport to authoring. Inspector edits and Save always affect the authored scene.

Use **Native → Create source → Build & Reload**, then Play to run gameplay. **Build on save** watches native source files; Console shows compiler diagnostics. See [native iteration](docs/native-modules.md) for the Windows developer launcher and tool requirements.
