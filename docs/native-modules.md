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

The initial host service requests world displacement of host-owned Flecs transforms; the host updates LocalTranslation with parent compensation. This deliberately does not expose raw world pointers. General native component/system registration and migration require a later ABI with registration ownership and lifecycle tests. Do not use v1 for module-owned objects, retained callbacks or background jobs.

## Runtime protocol v1
Runtime protocol **2** uses correlated JSON-line requests over process pipes. The runtime owns a fixed 60 Hz clock; `step` means exactly one paused fixed tick, never caller-supplied seconds. `hello` establishes a transient session and each request has an increasing ID. Successful responses separate uninterpolated recovery `scene` from derived `effective_scene`, and report timing and candidate activation state. See [runtime timing and transport](runtime-timing.md) for commands, limits, configuration and backpressure.

## Editor plugins
`tools/forge_plugins.py` supplies package staging and startup validation, not a DLL loader. Manifest fields: `id`, `version`, `api_version`, `kind`, `library`, `sha256`; C++ packages also declare `sdk` and `toolchain`. `capabilities` is a list and `dependencies` maps IDs to exact versions.

`stage()` retains the active package; `startup()` validates the prospective dependency graph; the eventual native loader must initialize successfully before `commit_startup()`. `startup(safe_mode=True)` returns no plugins. `disable()` stages a restart change. Do not invoke activation while editor plugins are loaded.

## Gameplay Code panel

The Windows package includes a minimal SDK and `Run-Forge-Dev.cmd`. With Visual Studio 2022's **Desktop development with C++** workload and **C++ CMake tools** installed, this launcher discovers the compiler using Microsoft's [vswhere workflow](https://github.com/microsoft/vswhere/wiki/Find-VC) and initializes its [x64 developer environment](https://learn.microsoft.com/en-us/visualstudio/ide/reference/command-prompt-powershell?view=vs-2022). It changes only the launched process environment and does not install tools. CMake 3.24+ and Ninja must be available; the Native panel accepts explicit executable paths.

1. Open **Gameplay Code** and click **Create source**. This creates `Project/Native/gameplay.cpp` and its CMake project without overwriting existing source or changing your scene.
2. Click **Build & Reload**. Configure/build run asynchronously; output appears in Console and `Project/.forge/native/build.log`.
3. Add an entity if needed, then press **Play**. The sample moves positioned entities along X.
4. Edit `gameplay.cpp` in your code editor. Enable **Build on save**, or click **Build & Reload** again.
5. A successful compatible change preserves the running scene. A syntax error or invalid candidate retains the previous module.

Every build creates a unique artifact under `Project/.forge/native/modules`. A disposable probe loads it and executes one real fixed tick against representative current state. Probe success does not guarantee all future native behavior. Live replacement first pauses at a tick boundary and acknowledges an authoritative uninterpolated checkpoint. Loading changes activation to **LoadedPendingFirstTick**. Only the first completed live fixed tick commits the candidate as **Active**.

Running sessions resume automatically. Paused sessions display **Reload pending first tick** and wait for Step or Resume; Step still advances exactly one tick and stays paused. Stop cancels pending activation. A newer pending candidate supersedes the prior transaction from the original known-good checkpoint, without stacking transactions. Failure during load or first live tick restarts the previous artifact/checkpoint and restores the prior run/pause policy. The old DLL may already be unloaded; retaining it means preserving the artifact for process recovery. Restart resets the clock accumulator, session and interpolation samples. Python tooling uses the same pending/activation boundary.

An incompatible identity/schema explicitly starts a fresh process with supported host-owned transform values. This is constrained ABI v1 reload, not arbitrary C++ object migration. Native callbacks must not retain host pointers or register unmanaged objects, threads or hooks. The ABI `tick(float seconds)` layout is unchanged; every gameplay invocation now receives the runtime's fixed dt. There is no zero-delta validation path.

A later runtime crash exposes **Recover**, which starts the last completed checkpoint with its module. Recovery is user-triggered to avoid repeatedly running crashing code. **Play/Restart** instead use the authored scene. Neither path modifies authored edits. Editor code and trusted native editor plugins can still crash the editor.

Current limits: build tool discovery is Windows/VS 2022-specific; native sessions select a validated artifact in memory, so rebuild after reopening the editor (the incremental build cache is retained). Source watching checks timestamps and sizes under `Native`, with a quiet period before building. New files must also be referenced by the CMake target. Single build commands time out after three minutes. Runtime responses are limited to 16 MiB and five seconds; Console keeps recent output while the current build log retains complete output. Compiler subprocess descendants are not yet managed as a Windows job; avoid closing the editor during a build. SDL process APIs and ImGui remain editor-only.
