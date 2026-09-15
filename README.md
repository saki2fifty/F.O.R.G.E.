# F.O.R.G.E.
**Flecs-Oriented Runtime & Game Editor**

Native ECS game editor under development. Windows/D3D12 is the first editor target; the portable core can be built and tested on Linux.

## Current implementation
- Flecs scene state, stable authored IDs, parent/prefab relationships, reflected position metadata, atomic scene replacement and saves, undo/redo, unknown-component preservation.
- Headless runtime with a versioned process protocol and a minimal C17-compatible gameplay module ABI.
- Native C/C++ project generation, incremental CMake builds, source watching, unique module artifacts, isolated candidate probing, constrained reload and runtime checkpoint recovery through a CLI.
- Restart-bound plugin package validation/staging library. Native editor-plugin loading is not implemented.
- Windows editor source with SDL3, Dear ImGui docking, persistent workspace/tooltips, World/Inspector/Console panels, and a Diligent cube preview. Windows compilation is validated in GitHub Actions; interactive GPU execution remains unverified.

This is a foundation, not a complete game editor. General native component registration/migration, editor-managed play/build integration, asset importing, physics/audio, game export and the first-person sample are still pending. See [implementation status](docs/status.md).

## Build and test
Requires Git, CMake 3.24+, Ninja, Python 3.10+, and a C++20/C17 compiler. Windows requires an MSVC developer shell and Windows SDK.

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

The editor reads and saves `main.scene.json` in the supplied project directory. User layout/settings are stored through SDL's application preferences directory.

See [native iteration](docs/native-modules.md), [scene format](docs/scene-format.md), and [dependency baselines](docs/dependencies.md).
