# 2026-11-03

## Seeded foliage placement

**Tools > Foliage placement...** and the shared `mesh.scatter` authoring command
place ordinary independent Mesh Renderer children under one named group. Explicit
horizontal center/radius, minimum origin spacing, count/seed and scale/yaw bounds
create reproducible layouts with new independent identities. Renderer values and
template local rotation/scale are copied; gameplay, animation, children, colliders
and Prefab links are not copied. Asset references remain shared.

The complete detached candidate validates before Scene publication. **Check layout**
validates without changing the Scene; **Place foliage** creates one Undo/Redo step.
Invalid or unfittable requests create no partial group. Existing command budgets,
permissions, project/target, revisions, request replay and document saving apply.
MCP discovers the generated `forge_mesh_scatter` tool from the same schema.

Gallery 144 contains an original base-pivot tapered blade Mesh,128 green and 64
amber ordinary placements, a separate template, ground, camera, light and moving
marker. Normal runtime resources, existing static Mesh instancing, picking and
source-hidden relocated content/export use current owners. The illustrated manual
teaches placement, ordinary editing, saving/export and current limits.

This is bounded fixed-plane placement, without surface painting, terrain projection,
collision avoidance, wind or world streaming. Scene format, installed SDK and
third-party pins are unchanged. Windows/full-editor/physical acceptance remains
pending; no numbered delivery is implied.
