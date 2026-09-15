# Build a blockout scene

The packaged example is a small scene containing a floor, walls, columns, a ramp, and colored markers. It demonstrates all four primitive types and varied transforms.

## Open the example

1. Extract the entire Windows ZIP.
2. In FORGE, choose **File → Open project...**.
3. Select the extracted **Examples/Blockout** folder.
4. Choose **Fit scene**, then orbit to inspect the layout.

You can edit the example directly or copy its project folder first. Changes save into the project you opened.

## Try the editing tools

1. Select **Tilted block** and drag its Rotation and Scale fields. Undo the edits to compare.
2. Select **Sphere marker** and change Color.
3. Select **Stretched sphere**, choose Copy transform, select another object, then Paste transform. Undo to restore the original layout.
4. Move a column upward, then choose Place on ground. Its base returns to Y=0.
5. Create another Cylinder and use the move handles with Snap to place it.
6. Save, reopen the scene, and check that transforms, shapes, and colors remain.

## Build your own room

Create a Plane for a floor. Create a Cube and scale it into a wall. Duplicate the wall and change its position or rotation to form another side. Add cylinders for columns and colored spheres as markers.

Use **Fit scene** to check the overall layout and **Save view** to keep a useful camera angle. Play previews the same layout in a separate runtime; this example has no gameplay module and stays stationary.

Continue with [Primitives and color](../editor/primitives.md), [Transforms](../editor/transforms.md), and [Viewport](../editor/viewport.md).
