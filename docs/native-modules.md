# Native gameplay iteration

## Create and watch a project
From the FORGE checkout, after building `forge_runtime`:

```sh
python tools/forge_native.py create --project /absolute/path/to/game --language cpp
python tools/forge_native.py watch --project /absolute/path/to/game --work /absolute/path/to/game-work --runtime /absolute/path/to/forge_runtime
```

Use `--language c` for C17. `--cmake` and `--ninja` accept explicit executable paths. Keep the work directory outside the project's source directory. Compilation output appears in the terminal and `build.log` in the work directory. Editor integration is pending.

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
