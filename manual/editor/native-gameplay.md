# Native gameplay

The Gameplay Code panel creates and compiles a small C++ gameplay module for the current project. The supplied sample moves entities along X. This is a constrained gameplay interface; general component/system registration is not available yet.

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

## Experimental engine SDK

Your existing **Create source** and **Build & Reload** workflow is unchanged. It uses the limited gameplay API described above.

An internal engine SDK now has a separate build profile for testing direct ECS registration. It is not a new editor command or a stable public plugin SDK. Its registering code requires a fresh runtime process when changed; it does not use the ordinary gameplay reload workflow. No additional test or C++ edit is needed for normal editor use.

If a project explicitly declares an experimental SDK module, editor Play reports that the separate SDK runtime is required. It does not silently skip the declared module. Ordinary ABI1 projects are unaffected.
