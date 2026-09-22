# Native gameplay

The Gameplay Code panel creates and compiles a small C++ gameplay module for the current project. The supplied sample moves entities along X. This default workflow uses the constrained ABI1 interface. Projects that need direct Flecs component/system registration use the separate exact SDK workflow below.

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

1. Obtain the Native SDK built from the same FORGE source as your editor.
2. Open **Gameplay Code**. Set **Native SDK folder** to the installation containing `bin` and `sdk`. This is a personal machine setting, not shared project data. Leave it blank when `NativeSdk` is installed beside the editor executable.
3. Build your project modules using that installation's CMake SDK package and supported compiler/configuration. Declare the module IDs, exact fingerprint, dependencies and project-relative library files in the project manifest.
4. Press **Play**. FORGE checks the runtime profile/source and the runtime validates module compatibility. The normal **Pause**, **Step**, **Stop**, input capture, Game view and runtime diagnostics then work through the isolated runtime.
5. To change rich SDK registrations, **Stop**, compile a replacement in your external developer terminal, then **Play** again. Preserve the previous good library if compilation fails. This starts from authored scene state.

The ABI1 **Create source**, **Build & Reload** and **Build on save** controls do not
operate on an exact SDK project's code. They are replaced by its SDK setup and
restart instructions. In-place rich SDK reload is unavailable. After a rich SDK
runtime crash, restart with Play; FORGE does not offer a partial checkpoint as if
it could restore arbitrary custom C++ state.

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
