# Project Code build and runtime contract

FORGE has one supported project gameplay model: exact-SDK C++ code registers Flecs
components and systems in a separate runtime process. Components carry data;
systems query matching entities at the registered lifecycle phase. A system is
not attached to one entity. `Native/` is project source shown under **Content → Code**. It is not an AssetId asset or a scene Hierarchy entry. The [user guide](../manual/editor/native-gameplay.md)
shows the editor workflow.

## Binary and ownership boundary

The exact SDK uses a versioned C-shaped entry descriptor, an exact fingerprint,
shared Flecs, descriptor size checks and module identity/dependency admission.
That compatibility mechanism does not make arbitrary native code safe. The editor
never loads project gameplay libraries. The isolated runtime validates and owns
registrations, callbacks and world-scoped services. Module contexts and code leases
outlive Flecs teardown; callbacks cannot run after their library retires. The
[engine module contract](engine-modules.md) and [extension contracts](extension-contracts.md)
describe the detailed ABI and lifecycle rules. No rich C++ in-place reload exists.
Code and schema iteration is **Stop → Save → Build → Play**.

Runtime protocol 2 carries correlated JSON-line requests. The runtime owns its
fixed clock; Step means one fixed tick. Editor FPS and transport polling never
supply simulation time. Source save and scene save have separate document history.

## Managed source and registration

**Set up C++ code** copies the matching installed SDK template into
an empty `Native/` directory. It does not overwrite existing source. The template
still supports `Native/gameplay.cpp`; further `.cpp` and `.hpp` files can be
created in FORGE. The optional `Native/forge.sources.cmake` registers managed
translation units. User CMake is never silently rewritten.

**Content → Create / Register → C++ → Component** creates a reflected, authorable data scaffold in
`Native/Components/`; **Create C++ System** creates a Flecs fixed-gameplay system
in `Native/Systems/`. Their registrations are generated through
`Native/forge.registration.hpp`, with source provenance for the Inspector's
**Open C++ Definition** action. Unmanaged or hand-written registrations do not
claim a source mapping. C++ source and headers are browsed in Content → Code. Build owns compilation and component inspection.
The ordinary Inspector's generic schema and property drawer author component
values, including prefab intent, scene persistence and Undo/Redo.

## Build and source identity

`SdkBuild` is the single managed compiler task. It uses the installed matching
SDK, selected CMake/Ninja/MSVC environment, incremental CMake build directory,
isolated metadata inspection and immutable candidate deployment. A failed or
cancelled candidate does not replace the last good project module declaration.
`Native/Builds` holds deployment kits, not editable source. The build log is
`.forge/sdk-build/build.log`; contained compiler file/line/column diagnostics open
in the built-in C++ document. **Test compiler tools** uses a disposable starter
and never publishes it. Externally maintained exact-SDK layouts retain their
external build workflow.

For managed projects, the admitted module records a deterministic identity of
all saved ordinary files in managed `Native/` (excluding build/deployment directories) and the selected exact SDK fingerprint. This conservatively includes custom CMake inputs and nonstandard include suffixes.
Generated deployment and build caches are excluded. Files, paths and bytes are
bounded; redirected/special inputs reject. Source hashes are checked on revision
and a throttled interval in the editor, plus at build/Play/export boundaries, not
each frame. A build rejects source changed while it was preparing its candidate.
**Project Code: Current** means the admitted module exactly matches saved source.
**Source Dirty**, **Build Required**, **Building**, and **Build Failed** remain
distinct. Build on Save is optional convenience. A failed build retains Last Good,
but does not claim it is Current.

Play with dirty or unbuilt managed source offers **Save, Build & Play** and waits
for candidate admission; a failure leaves Play stopped. Export rejects dirty or
stale managed source. The standalone package contains the admitted compiled
module, matching runtime, declared dependencies and cooked content, not source,
editor or compiler. Normal export remains relocatable. An older project whose
`Native/gameplay.cpp` has no managed SDK marker is diagnosed as unsupported; its
source is preserved for deliberate manual migration, without unsafe automatic
C/C++ translation.

## Live values and recovery

Admitted reflected values can be tuned in Play's **Live gameplay** Inspector.
The editor sends session/generation, scene AssetId, EntityId, type, property and
typed value through the runtime transport. The runtime validates and applies on
its owner thread between simulation ticks, then acknowledges with a snapshot.
Invalid/stale/unadmitted updates reject; authored scene and prefab overrides are
unchanged. Stop discards transient tuning. Schema or C++ code changes still need a
new build and fresh Play. No arbitrary native-object checkpoint or hot reload is
claimed. Runtime crashes cannot mutate the editor's authored scene; game Save
Game/Continue persists only the game-defined save schema.

## Editor plugins

Native editor plugins are trusted, validated and restart-bound. Staging and
manifest checks in `tools/forge_plugins.py` do not constitute a live editor DLL
loader. This is separate from C++ gameplay modules.
