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

## Two-clip sequence crossfades

A Transform Animator can select one optional next transform clip and its start on
an existing Animation Sequence. Matching transform channels crossfade through the
natural overlap, holding endpoints outside the span and cutting after gaps. The
existing complete pose candidate, controller clock and resource owner remain
responsible for both clips. Property, material and skeletal curves keep their
existing paths.

Timeline shows both clips and their overlap, with a typed Next clip picker and two
pending starts committed as one Scene Undo step. Inspector and automation/MCP use
the same registered values. Removing a track keeps the primary and standalone
settings while clearing the next slot and controller; Undo restores the pair.

Private recovery records both clip revisions and admits the complete resource pair
before validating the new profile. Old scenes and empty next slots retain their
single-clip behavior. Public native component values require a matching rebuilt
SDK module; callback tables and clip source/cook formats are unchanged. Gallery145
contains two original published clips and an exportable demonstration.


## Nested sequence placements

An Animation Sequence can place another controller at a start in parent seconds.
The root governs all descendant timing and controls while each child retains its
standalone settings. Same-Scene typed bindings reuse Scene/Prefab history and
reference remapping. Graph validation rejects cycles and invalid active parents
within the existing controller budget. Ancestor ends truncate child placements.

Timeline provides nested spans, one pending start, apply/reset, local child/parent
navigation and detachment. Root seek/restart stages descendant poses together;
independent bound controls reject. Camera cuts and sound cues use translated root
timing, with exact entry/end/wrap behavior and cold recovery. Inspector and generic
automation/MCP expose the same registered fields and revision/history owners.

Gallery146 demonstrates an eight-second parent with a six-second child at one
second, using existing original motion, cameras and three pitched sound cues.
The manual includes an authentic Timeline capture and instructions for authoring,
preview, Prefabs and export. Native component values require a matching rebuilt
SDK module; C callback layouts and Scene/Prefab/clip formats are unchanged.
Windows/full-editor/physical acceptance remains pending; no numbered delivery is implied.
