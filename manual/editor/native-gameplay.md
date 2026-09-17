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

A failed compilation keeps the previous validated module available. Candidates are built as separate artifacts and checked in a worker before activation. Compatible supported changes can preserve the play session; incompatible schema changes restart the play world.

The current interface supports stateless movement callbacks over host-owned transforms, applied through local translation. It does not migrate arbitrary C++ state. Do not retain host pointers or create unmanaged background work in a module.

Rebuild after reopening the editor to select a validated module; previous artifacts remain cached. Wait for compilation to finish before switching scenes or projects. See [Play mode](play-mode.md).
