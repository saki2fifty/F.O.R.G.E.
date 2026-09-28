# Native gameplay

Gameplay Code creates and builds C++ source for the current project. **C++ Sources** lets you edit it inside FORGE. Use **Create C++ gameplay project** for Flecs components and systems. The separate **Create source** / **Build & Reload** controls retain the constrained ABI1 movement workflow described below.

## Prepare Windows tools

Install Visual Studio 2022 C++ build tools with the Windows SDK, CMake 3.24 or newer, and Ninja. Launch **Run-Forge-Dev.cmd** from the extracted package so FORGE receives the compiler environment. The regular launcher is sufficient for editing without compilation.

## Build your first module

1. Open your project and add an entity.
2. In **Gameplay Code**, select **Create source**. Existing source files are not overwritten.
3. Expand **Compiler setup** and check **CMake** and **Ninja**. Use command names on PATH or full executable paths.
4. Select **Build & Reload** and watch the status and Build output in the same panel.
5. Select **Play** after a successful build. The sample moves the block along X.

Edit `Native/gameplay.cpp` in your code editor. Enable **Build on save** to watch supported source changes and start incremental compilation automatically. The current complete build log is stored in `.forge/native/build.log`.

## Understand reload results

A failed compilation keeps the previous validated module available. Candidates are built as separate artifacts and checked in a worker with one real fixed tick before live activation. Compatible supported changes can preserve the play session; incompatible schema changes restart the play world.

While paused, a successful load displays **Reload pending first tick**. **Step** validates it with exactly one tick and stays paused; **Resume** validates it through normal running. The previous module remains the known-good artifact until that tick succeeds. A failed first live tick restores the checkpoint and module from before reload, including whether you were paused. **Stop** cancels pending activation; another successful build explicitly supersedes it.

The current interface supports stateless movement callbacks over host-owned transforms, applied through local translation. It does not migrate arbitrary C++ state. Do not retain host pointers or create unmanaged background work in a module.

Rebuild after reopening the editor to select a validated module; previous artifacts remain cached. Wait for compilation to finish before switching scenes or projects. See [Play mode](play-mode.md).

## Exact SDK projects

An exact SDK project declares native modules in `forge.project.json`. Those modules
can register Flecs components and systems in the separate runtime process. They
never load into the editor. This remains an experimental, exact-version C++ SDK;
it is not the stable ABI1 movement interface described above.

### Create and build gameplay in FORGE

1. Launch **Run-Forge-Dev.cmd** to provide the matching Visual Studio compiler environment. Use the NativeSdk shipped with this editor. In **Gameplay Code**, choose **Create C++ gameplay project**. Existing files in Native are never overwritten.
2. Choose **Build gameplay**. FORGE configures CMake, compiles the module, collects its runtime dependencies and validates the candidate in a separate process. On success it updates the project module declaration. A failed build preserves the last good module and settings.
3. Choose **Inspect components**. Select a scene entity, use **+ Add Component**, search **Gameplay Counter**, and add it. Its **rate** determines the value added each simulation second; **value** is its starting value. Save the scene.
4. Press **Play**. The template's Flecs system advances the counter in the isolated fixed-step runtime. Choose **Open C++ source** in Gameplay Code to edit `Native/gameplay.cpp` inside FORGE; an external code editor remains optional. **Stop**, rebuild, then **Play** again. Rich SDK registrations are restart-bound; this is not hot reload.
5. Expand **Compiler setup** for CMake/Ninja paths, or **SDK build output** for compiler diagnostics. **Cancel build** leaves the previous module active. The complete compiler log is `.forge/sdk-build/build.log`.
6. Use **Run → Export Game...** after saving your scene and setting startup/game defaults. FORGE selects the managed module kits automatically, including after reopening the project. The exported folder contains the runtime dependencies and does not need the editor, compiler or source project.

The starter opts a plain reflected component into authoring explicitly. It links the
installed SDK's shared Flecs; do not add a second Flecs implementation to the DLL.
Native/Builds contains immutable deployment kits, not source code. Do not delete
a kit still referenced by `forge.project.json`.

### Existing externally built SDK projects

Set **Native SDK folder** to the matching installation containing `bin` and `sdk`.
Leave it blank for NativeSdk beside the editor. This path is personal machine state.
Build your external modules with that SDK's CMake package, declare their IDs,
fingerprints/dependencies/project-relative libraries, then Play. The editor's
managed starter does not overwrite external build layouts or additional module kits.
Select their module kits explicitly for export. Compiler, architecture, configuration,
flags and CRT must match the installed SDK.

ABI1 **Build & Reload** and **Build on save** do not operate on rich SDK modules.
After a rich runtime crash, restart Play from the authored scene; FORGE does not
claim recovery of arbitrary custom C++ state.

## Author opted-in project components

Reflection alone does not make arbitrary C++ objects editable. Your module must
explicitly opt a supported plain-value component into authoring using the exact
SDK's `authoring_type` callback, a stable namespaced key, positive schema version
and declared defaults. Native pointers and arbitrary resource-owning objects are
not generic authored values.

1. Stop Play and build the module with the matching Native SDK.
2. In **Gameplay Code**, set **Native SDK folder**, then click **Inspect components**.
3. Wait for the available-type count. FORGE runs the inspection in a separate process; a failed inspection keeps the last admitted types and values.
4. Select an entity and use **Inspector > + Add Component**. Search the opted-in type's friendly name or category, then edit its reflected properties.
5. Save the scene and press Play. The matching runtime receives the authored values by stable field names. A missing or incompatible schema rejects the runtime load rather than silently ignoring those values.

Nested properties, collections, flags, references, defaults and prefab overrides
use the shared [Inspector controls](inspector.md). **Inspect components** does not
change existing values to new defaults or automatically migrate an older schema.

FORGE writes `forge.components.json` at the project root after successful
inspection. Keep this file in source control with your project: it records prior
schemas needed for migration and reserves a removed component's key for its
original module owner. It contains copied metadata, not gameplay code, and does
not activate a module by itself. Changing a type's structure requires a new schema
version; changing presentation labels or defaults does not rewrite existing values.
The history is bounded to1024 recorded declarations and32MiB; exceeding that limit
rejects activation without dropping old records.

## Migrate a changed component schema

Build the new schema version and **Inspect components** first. Old values stay
read-only until explicitly migrated. Keep your earlier `forge.components.json`
when upgrading the module; FORGE needs its source schema to interpret old values.

1. Choose **Migrate scene values...**, or open a prefab source and choose **Migrate open prefab draft...**.
2. Select the component's old and new versions. Confirm which document is the target.
3. Leave both rule lists empty to retain existing fields and add declared new-field defaults. Add an explicit alias for a rename, as shown below.
4. Click **Prepare migration**. Validation runs in a separate process and leaves documents unchanged.
5. After a successful preparation, choose **Apply scene migration** for one scene Undo step, or **Use migrated prefab draft** and review the prefab before its separate Save/Publish.

For a field renamed from `hitpoints` to `health`, use:

```json
{
  "aliases": [{"path": ["hitpoints"], "name": "health"}],
  "defaults": []
}
```

A nested collection path uses `*`, for example `["items", "*", "old_name"]`.
When a new field inside each list entry needs a value, add an explicit default such
as `{"path": ["items", "*", "added"], "value": 7}` to the `defaults` list.
Aliases use original source-field paths. Conflicting destinations, cycles, invalid
values and unsupported representation/unit changes reject the candidate.

Removed and unknown fields remain stored. Changed defaults affect new components,
not existing owned values. A partial prefab override does not gain extra top-level
overrides. Migrating a prefab source and its scene overrides are separate reviewed
operations; this is not Apply to Prefab and is not one cross-document transaction.
Scene Undo can restore an older schema payload read-only without losing its data.
If the document changes during review, prepare a fresh candidate.

For developer diagnostics, `bin/forge_runtime --inspect-sdk <project-folder>`
prints opted-in metadata without starting gameplay. The editor's Inspect command
adds worker supervision and transactional admission around that extraction.

For manifest fields, ownership and the installed sample, see the SDK installation's
`sdk/docs/extension-guide.md`. Compiler output stays in your build terminal; runtime
module messages appear in FORGE's diagnostics.

Exact SDK gameplay can request cooked Mesh, Material, Texture and Shader assets
through the host's Resources capability and inspect their load state and revision.
Texture requests can choose published color, data, normal-map or HDR variants.
Failed replacements keep the last usable revision. These requests load CPU data;
they do not grant access to the graphics device or guarantee a complete draw is
ready. The packaged SDK extension guide documents the
callback contract, lifetime and limits. Rebuild native modules when the exact SDK
fingerprint changes.

SDK gameplay can also request a new runtime entity, wait until it is ready, and
assign a MeshRenderer and local transform components using Flecs. It appears in
Game presentation through the normal scene membership. Creation takes effect at
the next game tick; when paused, use **Step** or **Resume**. It does not add an
object to the editor's saved scene or its Undo history. The SDK guide includes the
code, scene-selection rules, cancellation and lifetime details.

The migration review stays within the available editor area at larger interface scales. **Rules (JSON)** appears above its text field so the label remains readable. Scroll the review when its contents need more space; opening it does not change the scene or prefab.

## Check your compiler tools

In **Gameplay Code**, create a C++ gameplay project, then expand **Compiler setup**.
Choose **Test compiler tools**. FORGE finds supported installed Visual Studio2022
C++ tools and tests the compiler, Windows SDK, CMake, Ninja and matching FORGE SDK
by building and checking a separate starter module. Your gameplay and project
settings stay unchanged. Wait for **Compiler ready**; a failure explains the
missing tool or incompatible build in the status/output.

**Get C++ Build Tools** opens Microsoft's official download page. Install the C++
Build Tools and Windows SDK, then test again. There is no portable compiler download
or automatic installation. CMake/Ninja fields accept explicit executable paths.
Changing tool paths requires another test. Editing source alone and running an
exported game do not require the compiler.

## Edit C++ inside FORGE

1. In Gameplay Code, select **Open C++ source** to open `Native/gameplay.cpp` in the central **C++ Sources** window. Its **Source path → Open file** controls let you open another project C++ file.
2. Edit with C++ highlighting, line numbers and indentation. Each file has its own tab and Undo/Redo. **Ctrl+F** focuses Find; **Find next** selects a match and wraps.
3. **Save source** or **Ctrl+S** saves the active file. The star identifies unsaved source. Saving code is separate from saving your scene. Close asks you to save, discard or cancel when code is unsaved.
4. Save every edited source before **Build gameplay**. **SDK build output** lists clickable project-file diagnostics: click one to open the reported line/column. External SDK/generated-file errors remain visible in the raw log.
5. After a successful build, **Inspect components**, add an opted-in component to an entity, and Play. Changing code does not automatically expose a new component.

**New source filename → Create source file** creates a `.cpp` or `.hpp` directly
inside Native and opens its tab. New `.cpp` files are registered for the managed
build; `.hpp` files are headers. Existing files are never overwritten. An old
unchanged FORGE starter is updated automatically to support source registration.
Custom build recipes require the displayed CMake include instruction; files copied
in externally also need explicit CMake registration.

Enable **Build on Save** in C++ Sources to queue a build after saving. It waits
until Play stops, existing work finishes and all source drafts are saved. It never
silently saves your drafts. You may keep typing during a build, but save after it
finishes. **Stop → Build → Play** remains the exact-SDK iteration workflow; neither
Windows nor the editor needs a reboot for an ordinary gameplay change.

If another editor changes a file, FORGE shows a notice. A conflicting Save keeps
your draft and rejects the overwrite. Reopening a file without unsaved edits refreshes
its disk contents and source history. **Reload from disk** asks before discarding
that draft and its history. Sources must be valid UTF-8, at most 1 MiB per file;
close a tab before exceeding 32 open files. This is a source editor, not a full IDE:
autocompletion, debugger integration and unrestricted C++ hot reload are not provided.

Export includes the compiled gameplay module and runtime dependencies. It does
not include the C++ editor, compiler or source files.

Export uses the last successfully built gameplay module. Save and build your code
changes before exporting; unsaved drafts and failed builds are not included.
