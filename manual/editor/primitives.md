# Primitives and tint

Primitives are built-in shapes for blocking out a level before detailed assets are available. They share the existing preview renderer, picking, bounds and navigation geometry. They are not imported Mesh or Material assets.

## Create an entity

Use **Add Entity (+)** in Scene or Hierarchy, **Entity → Create**, Hierarchy's right-click **Create**, or [Command palette](commands.md). Every entry point uses the same creation recipes and placement setting. Creation selects the new entity, focuses its Scene task and creates one scene Undo step. It does not publish or discard an open prefab/settings draft.

**Placement: World Origin** creates at **0, 0, 0**. **Placement: At View Target** uses the Scene camera's pivot. The choice persists. Origin placement may put half a solid below the grid; use Transform / Place on ground when desired.

**Empty Entity** creates a transform with no visible geometry. Select it in Hierarchy; its move handles remain available. Audio Source, Audio Listener, Navigation Agent and UI Document recipes also have no geometry. Required asset references must still be assigned in Inspector. Navigation Surface creates a Plane with the surface component; build its navigation asset through Content.

## Supported blockout shapes

Choose **3D Primitive**:

- **Cube**, **Sphere**, **Cylinder**, **Plane** retain their existing geometry. Cube is one unit per side; Sphere and Cylinder have unit diameter. Cylinder is one unit high along Y. Plane is a two-sided local XZ square, initially scaled to four units per side.
- **Capsule** is one unit high along Y with radius 0.25. **Cone** and **Frustum** are unit-height solids along Y; the frustum has bottom radius 0.5 and top radius 0.25.
- **Quad** is a unit square in local XY. **Disc** is a local XZ disc of radius 0.5. **Ring** has outer radius 0.5 and inner radius 0.25.
- **Torus** lies around Y, with major radius 0.35 and tube radius 0.15. **Tube** is a hollow unit-height cylinder, outer radius 0.5 and inner radius 0.3, with annular ends.
- **Pyramid**, **Tetrahedron**, **Octahedron**, **Triangular Prism**, **Hexagonal Prism** and **Wedge** are fixed blockout solids within a unit local bounding box. Scale/rotate them to fit your level.
- **Hemisphere** is a capped upper half-sphere of radius 0.5. **Icosphere** is a low-resolution sphere built by subdividing an icosahedron once.

Shapes use fixed tessellation and proportions. No topology editor, subdivision control, UV/material authoring or automatic collider creation is included. Shape geometry and collider components are independent; choose/configure an existing supported collider explicitly.

## Change shape or tint

Select the entity and find **Inspector → Blockout Geometry**. **Shape** preserves transform and tint. **None** disables its geometry; choosing a visible shape restores it. **Tint** opens an opaque RGB color picker. Each drag commits one Undo step. Preview lighting modulates the displayed color; colors persist and appear in Game.

This remains a fixed-shading blockout renderer. Imported meshes, editable materials, texture maps, transparency and shadows are future work.

## Picking and framing

Picking uses the rendered mesh triangles and honors rotation/nonuniform scale, including holes in Ring/Torus/Tube. Framing uses transformed mesh bounds. The amber outline is a simple rotated bounding box rather than an exact wireframe. Empty entities have no surface to pick; use Hierarchy.

See [Transforms](transforms.md) and [Build a blockout scene](../getting-started/blockout.md).
