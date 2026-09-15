# Your first scene

This walkthrough creates two blocks, organizes them, saves a scene, and previews it.

## Open the editor

1. Extract the entire Windows ZIP into a folder on your Windows PC.
2. Keep the DLLs, manual, and SDK folders beside `forge_editor.exe`.
3. Run `Run-Forge.cmd` to open the included scratch Project folder. You can also launch `forge_editor.exe` directly; it normally returns to your last project.
4. Open **Help** to see the build identifier. Include it when reporting problems.

The editor requires Windows x64, an AVX2-capable processor, and a D3D12-capable graphics driver. Native gameplay compilation requires additional developer tools, described under [Native gameplay](../editor/native-gameplay.md).

## Create and organize blocks

1. Select **File → New project**. Enter a project name and choose an existing parent folder, then select **Create**.
2. Select **Add entity** twice. The blocks begin at the same position, so they may look like one block.
3. Select one entity in **World**. In **Inspector**, enter a name and press Enter.
4. Change its Position X value to move it away from the other block.
5. Choose the other entity in **Parent** to organize it underneath that entity.
6. Select **Fit scene** in **Scene** to see both blocks.

Parenting currently organizes entities without moving them or making their positions relative to the parent.

## Save and preview

1. Press Ctrl+S to save the startup scene.
2. Select **Play**. The viewport now previews a separate copy of the authored scene.
3. Select **Stop** to return to authoring. Blocks remain stationary unless a gameplay module changes them.
4. Use **File → Open scene** to reopen your saved scene.

Continue with [Viewport](../editor/viewport.md) for navigation and [Saving and recovery](../editor/saving-recovery.md) for protecting edits.
