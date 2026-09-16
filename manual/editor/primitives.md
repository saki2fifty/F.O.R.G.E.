# Primitives and color

Primitives are built-in shapes for blocking out a level before detailed assets are available. FORGE supplies Cube, Sphere, Cylinder, and Plane meshes. Each can have its own transform and opaque color.

## Create a shape

Open **Create** in the top toolbar and choose a shape.

- **Cube** starts one world unit wide, high, and deep.
- **Sphere** starts with a diameter of one world unit.
- **Cylinder** starts one unit high with a diameter of one unit, along local Y.
- **Plane** lies flat in local XZ and starts four units wide and deep. It has no thickness and is visible from either side.

Normal creation places Cube, Sphere, and Cylinder at world position 0, 1, 0; Plane starts at 0, 0, 0. Enable **Create → At view target** to create at the camera's orbit target instead. That choice persists between launches. The same placement setting applies to Create and command-palette creation.

The new object becomes selected. Move it with the handles or edit its position in Inspector. Creation is undoable.

## Change shape or color

Select the object and find **Inspector → Primitive appearance**. **Shape** changes its mesh while keeping position, rotation, scale, and color.

Click **Color** to open the color picker. Its value is an opaque RGB tint; the preview's fixed lighting makes faces appear brighter or darker. Each picker drag can be undone. Colors are saved with the scene and appear in Play mode.

This is a blockout renderer with fixed directional shading. Texture maps, editable materials, transparency, shadows, and imported meshes are not available yet.

## Select and frame shapes

Viewport picking uses the same mesh triangles that are drawn, including rotation and nonuniform scale. The amber selection outline shows the object's rotated bounds, so a sphere or cylinder has a box-shaped outline. Fit scene and Frame selected account for transformed mesh bounds.

See [Transforms](transforms.md) and [Build a blockout scene](../getting-started/blockout.md).
