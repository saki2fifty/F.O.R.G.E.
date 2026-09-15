# Native gameplay iteration

## Create and watch a project
From the FORGE checkout, after building `forge_runtime`:

```sh
python tools/forge_native.py create --project /absolute/path/to/game --language cpp
python tools/forge_native.py watch --project /absolute/path/to/game --work /absolute/path/to/game-work --runtime /absolute/path/to/forge_runtime
```

Use `--language c` for C17. `--cmake` and `--ninja` accept explicit executable paths. Keep the work directory outside the project's source directory. Compilation output appears in the terminal and `build.log` in the work directory. The editor also provides its own native build controls described below.

Each successful build is copied to a unique DLL/shared-library path. A disposable runtime probes it first. Compatible code replaces the active module only at a command boundary; host-owned scene values survive. Compile/probe failures retain the active module. Schema changes restart the play process with host-owned values. A killed/crashed runtime can recover from the latest completed step checkpoint. Crashes after external side effects cannot be rolled back.

## ABI v1
`include/forge/module_api.h` is a C17-compatible header with C++ linkage guards. The module exports `forge_module_v1`, returns a size/version-checked descriptor, and supplies a stateless tick callback. All pointers are borrowed. No allocations, exceptions or C++ standard-library types cross the boundary. Module identity and state schema are compared before in-process replacement.

The initial host service translates Position components in Flecs. This deliberately does not expose raw world pointers. General native component/system registration and migration require a later ABI with registration ownership and lifecycle tests. Do not use v1 for module-owned objects, retained callbacks or background jobs.

## Runtime protocol v1
One JSON request and one JSON response per line on stdin/stdout. Each request has `protocol: 1` and `command`. Errors return `ok: false` and `error`; successful responses include `scene`, `schema` and active module identity.

Commands: `ping`, `snapshot`, `schema`, `replace` (`scene`), `step` (`seconds`, 0–1), `load_module` (absolute `path`), `save` (`path`), `quit`.

This is a local development protocol. It does not expose a network listener. The native controller bounds request waits to five seconds and discards an unresponsive worker.

## Editor plugins
`tools/forge_plugins.py` supplies package staging and startup validation, not a DLL loader. Manifest fields: `id`, `version`, `api_version`, `kind`, `library`, `sha256`; C++ packages also declare `sdk` and `toolchain`. `capabilities` is a list and `dependencies` maps IDs to exact versions.

`stage()` retains the active package; `startup()` validates the prospective dependency graph; the eventual native loader must initialize successfully before `commit_startup()`. `startup(safe_mode=True)` returns no plugins. `disable()` stages a restart change. Do not invoke activation while editor plugins are loaded.

## Native panel

The Windows package includes a minimal SDK and `Run-Forge-Dev.cmd`. With Visual Studio 2022's **Desktop development with C++** workload and **C++ CMake tools** installed, this launcher discovers the compiler using Microsoft's [vswhere workflow](https://github.com/microsoft/vswhere/wiki/Find-VC) and initializes its [x64 developer environment](https://learn.microsoft.com/en-us/visualstudio/ide/reference/command-prompt-powershell?view=vs-2022). It changes only the launched process environment and does not install tools. CMake 3.24+ and Ninja must be available; the Native panel accepts explicit executable paths.

1. Open **Native** and click **Create source**. This creates `Project/Native/gameplay.cpp` and its CMake project without overwriting existing source or changing your scene.
2. Click **Build & Reload**. Configure/build run asynchronously; output appears in Console and `Project/.forge/native/build.log`.
3. Add an entity if needed, then press **Play**. The sample moves positioned entities along X.
4. Edit `gameplay.cpp` in your code editor. Enable **Build on save**, or click **Build & Reload** again.
5. A successful compatible change preserves the running scene. A syntax error or invalid candidate retains the previous module.

Every build creates a unique artifact under `Project/.forge/native/modules`. An isolated probe loads the candidate and performs a zero-time tick. Activation then runs at a command boundary in the play process, followed by another zero-time validation tick. Activation failure restores the previous module and the checkpoint taken immediately before replacement. An incompatible identity/schema creates a fresh runtime with the supported host-owned Position values. This is constrained ABI v1 reload, not arbitrary C++ object migration. Native callbacks must not retain host pointers or register unmanaged objects, threads, or hooks.

A later runtime crash exposes **Recover**, which starts the last completed checkpoint with its module. Recovery is user-triggered to avoid repeatedly running crashing code. **Play/Restart** instead use the authored scene. Neither path modifies authored edits. Editor code and trusted native editor plugins can still crash the editor.

Current limits: build tool discovery is Windows/VS 2022-specific; native sessions select a validated artifact in memory, so rebuild after reopening the editor (the incremental build cache is retained). Source watching checks timestamps and sizes under `Native`, with a quiet period before building. New files must also be referenced by the CMake target. Single build commands time out after three minutes. Runtime responses are limited to 16 MiB and five seconds; Console keeps recent output while the current build log retains complete output. Compiler subprocess descendants are not yet managed as a Windows job; avoid closing the editor during a build. SDL process APIs and ImGui remain editor-only.
