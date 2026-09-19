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

Custom runtime components are not automatically Inspector-editable or persisted.
Reflected C++ registration alone does not create a supported authored component.
The bounded custom authoring contract is architecture work; existing built-in
components keep their normal Inspector and prefab behavior.

For manifest fields, ownership and the installed sample, see the SDK installation's
`sdk/docs/extension-guide.md`. Compiler output stays in your build terminal; runtime
module messages appear in FORGE's diagnostics.
