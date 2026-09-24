# FORGE / FIELD TEST

A standalone game consumer of FORGE's experimental exact-version gameplay SDK.
The module implements its own FPS controls, camera, beacon interaction, menus and
save schema. Engine services supply input routing, physics, UI, session transitions
and storage. No SDL, Jolt, renderer or private host headers are included.

## Build the gameplay module

Install the matching FORGE `NativeSdk` component first. Use the same compiler,
architecture, CRT and build profile as the runtime kit:

```sh
cmake -S samples/reference_game -B /path/to/reference-build -G Ninja \
  -DFORGE_NATIVE_SDK=/path/to/installed-sdk -DCMAKE_BUILD_TYPE=Release
cmake --build /path/to/reference-build
```

On Windows, install component `GameplayRuntime` to create the `project.reference`
module kit. This module is not a separate executable; the exported game launches
through `forge_game.exe`.

## Content and acceptance

`tests/reference_project_fixture.py` builds the compact test project using normal
asset imports and the `forge_reference_project_fixture` tool. It needs the matching
asset tools, navigation worker, Ozz converter and official sample assets selected
by FORGE's test configuration. It is a build-time content generator, not a runtime
requirement.

`tests/reference_package_test.py` demonstrates production export with the runtime
and gameplay module kits. It removes the source project, relocates the game, runs
native input/menu acceptance, then relocates and relaunches to load the save.
The fixture executable records input assertions and captures; the production
`forge_game.exe` has no fixture controls.

See the [player manual](../../manual/reference-game.md) and
[SDK service contract](../../docs/gameplay-services.md). Physical mouse/controller
feel, speakers, GPU and DPI acceptance are separate from hosted input tests.
