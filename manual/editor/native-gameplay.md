# C++ gameplay

FORGE uses Flecs components for data and systems for behavior. Give a Cube a
`Rotator` component; a `RotationSystem` then processes every entity whose query
matches. Systems are registered for the world, not attached to individual objects.
C++ files live in your project's `Native/` folder and open in the central **C++
Sources** document. They are project code, not Content assets or Hierarchy entries.

## Prepare the compiler

Extract the matching optional Developer Kit into the editor folder, then launch
**Run-Forge-Dev.cmd**. Install Visual Studio 2022 C++ build tools with the Windows
SDK, CMake 3.30 or newer, and Ninja. In **Gameplay Code**, expand **Compiler setup**
and choose **Test compiler tools**. Normal editing and an exported game do not need
these tools. A matching installed Native SDK is required to build and Play C++.

## Create a component and system

1. Open **Gameplay Code**. Choose **Create C++ gameplay project** for a new project. Existing `Native/` source is never overwritten.
2. In **Project C++ sources**, choose **Create C++ Component** and enter `Rotator`. This creates `Native/Components/Rotator.hpp` with a reflected `speed` value.
3. Choose **Create C++ System**, select `Rotator`, and enter `RotationSystem`. This creates `Native/Systems/RotationSystem.cpp`, which queries Rotator, reads any effective rotation or identity, and runs on the fixed gameplay tick.
4. Click either source path to edit it. The tab and **Save source** button mark unsaved changes with `*`. **Ctrl+S**, document Save and the button save the active C++ file. Scene Save is separate.
5. Choose **Build Gameplay**. With unsaved drafts the action becomes **Save & Build Gameplay**. It compiles saved files, validates the candidate in an isolated worker and publishes after admission. **Build on Save** is optional.
6. Wait for **Gameplay Current**. Create a Cube, select it, use **Inspector → + Add Component** to add Rotator, set Speed and save the scene. Right-click the component header for **Open C++ Definition**.
7. Press **Play** to see the cube rotate. Code or schema changes use **Stop → Save → Build → Play**. FORGE offers **Save, Build & Play** for unbuilt source and never silently runs stale code.


**Components** in Gameplay Code lists component source files; **Systems** lists
system source files. **Registered systems** shows wizard-created systems with their
source and fixed-gameplay query component. Other project source and headers appear
under **Other source / headers**. Ordinary `.cpp` and `.hpp` files can also be
created. Source organization is flexible; CMake must register any extra `.cpp`.

## Tune values while playing

During Play, the Inspector has a **Live gameplay** section for admitted reflected
properties. Select the runtime entity and change Rotator Speed from 90 to 360. The
runtime validates the typed update at its owner-thread boundary and acknowledges
it; behavior changes without compiling. **Stop** discards this transient tuning:
the authored scene and prefab override intent stay at their saved values. A
read-only runtime field remains read-only. Code or schema edits still require a
new build and fresh Play session.

## Build status and failure

**Source Dirty** means a C++ tab has unsaved edits. **Build Required** means saved
source differs from the admitted module. **Building**, **Build Failed**, and
**Gameplay Current** distinguish the remaining states. A changed Developer Kit requires a compatible new build; a missing matching kit blocks managed Play until it is installed. A failed build keeps the
last good artifact, but Play and Export do not treat it as current. Click a
compiler diagnostic in **Compiler output** to open its source line. The full log
is `.forge/sdk-build/build.log`; source remains editable. Build on Save uses the
same build operation after source is saved. You can cancel a candidate without
replacing the previous good module.

## Export

Save the scene, build current gameplay, then choose **Run → Export Game...**.
Export refuses dirty or unbuilt managed source. The standalone folder contains
the admitted compiled C++ module, matching runtime and cooked dependencies. It
runs after relocation without the source project, editor or compiler. Externally
built exact-SDK projects keep their explicit module kit/deployment workflow.

Older projects with the retired movement-only module format are preserved on disk
but cannot Play or Export with that code. Create a new managed C++ project and
migrate the source deliberately; FORGE does not automatically translate C/C++.

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
4. Save every edited source before **Build gameplay**. **Compiler output** lists clickable project-file diagnostics: click one to open the reported line/column. External SDK/generated-file errors remain visible in the raw log.
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

You can change **Build on Save** while a build is running. Turning it off affects
future saves; it does not stop a build already in progress. Use **Cancel build**
in Gameplay Code to stop the current build.
