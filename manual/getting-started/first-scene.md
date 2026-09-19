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
2. Select **Scene Add (+) → 3D Primitive → Cube** twice. The blocks begin at the same position, so they may look like one block.
3. Select one entity in **Hierarchy**. In **Inspector**, enter a name and press Enter.
4. Change its Position X value, or drag its red X move handle in Scene, to move it away from the other block.
5. Choose the other entity in **Parent** to organize it underneath that entity.
6. Select **Fit scene** in **Scene** to see both blocks.

Choosing a Parent preserves the object’s placement, makes its coordinates local to that parent, and lets it follow later parent motion. Older scenes retain independent **World** binding until you change it. See [Transforms](../editor/transforms.md).

## Save and preview

1. Press Ctrl+S to save the startup scene.
2. Select **Play**. The viewport now previews a separate copy of the authored scene.
3. Select **Stop** to return to authoring. Blocks remain stationary unless a gameplay module changes them.
4. Use **File → Open scene** to reopen your saved scene.

For more shapes, color, rotation, and scale, try [Build a blockout scene](blockout.md).

Continue with [Viewport](../editor/viewport.md) for navigation and [Saving and recovery](../editor/saving-recovery.md) for protecting edits.
