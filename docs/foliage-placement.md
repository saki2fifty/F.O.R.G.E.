# Bounded foliage placement

Foliage placement is a shared Scene authoring operation, `mesh.scatter`. It creates
ordinary Mesh Renderer children under one ordinary group. The Scene, spatial
transform codec, stable entity identity, normal history, asset references, shared
render batching and runtime/export consumers keep their existing ownership.
There is no foliage runtime component, asset database, population manager or
serialized placement recipe. The installed native SDK and Scene format are unchanged.

## Template and generated intent

The source is an effective Scene entity with a nonempty Mesh Renderer Mesh
reference. Definition entities cannot be templates. Copy only the effective
MeshRenderer value and local rotation/scale; do not copy source position, parent,
spatial binding, descendants, gameplay, colliders, animation or Prefab links.
Material bindings, numeric material parameters, variant selection, visibility,
shadow and layer values remain part of that renderer. Asset identity/availability
and renderer resource admission retain their ordinary semantics. Imported meshes
do not acquire a model animation/skin instance by being placed this way.

Create a root at the requested world center with identity rotation/scale. Each
child has a horizontal disk offset, zero local Y, a seeded +Y yaw preceding the
template local rotation, and a uniform multiplier applied to the template local
scale. Existing transform validation admits or rejects the complete candidate.
Every entity uses the existing independently allocated UUID owner; IDs are never
derived from the seed, location, source asset or array index. Later source entity
edits do not change existing groups. Shared Mesh/Material asset edits continue to
use ordinary asset ownership.

## Layout and limits

The complete command requires a name, source entity, center, radius, spacing,
count, seed, minimum/maximum scale multiplier and minimum/maximum yaw.

| Value | Admitted range |
| --- | --- |
| Center | Finite world coordinates, each -1e12 to 1e12 meters |
| Radius | 0.01 to 1000 meters |
| Minimum spacing | 0 to 2000 meters |
| Count | Integer 1 to 512 |
| Seed | Integer 0 to 4294967295 |
| Scale multipliers | 0.01 to 100, minimum no greater than maximum |
| Yaw | -360 to 360 degrees, minimum no greater than maximum |

The source local scale times each multiplier must also satisfy the existing local
scale bounds. The resulting Scene remains within the ordinary 10000-entity/8MiB
command budget. Template orientation and signed/nonuniform scale are preserved.

An explicit unsigned 32-bit sequence samples a square and rejects points outside
the disk. A bounded local cell index checks spacing without scanning all prior
placements for each candidate. The operation allows at most 65,536 attempts. Failure
to fit is an error, never a partially populated group. The same settings retain the
same local layout within the engine's numeric implementation; platform math is not
a promise of bit-identical quaternion results across compilers.

Spacing compares the published double-world origin points within this new group
only. Candidate admission also checks the rounded world position against the disk
radius. Distance uses a scale-safe norm, including subnormal positive spacing;
quantized duplicate world positions cannot pass a positive spacing request. The
cell index uses bounded center-relative coordinates and a conservative neighborhood.
It does not compare old groups, mesh bounds or colliders. There is no terrain projection, arbitrary
surface painting, slope filter, brush stroke, wind simulation or world streaming.
The requested Y is an origin plane; a suitable Mesh pivot is needed for plants
that should rest on the ground at every scale. A horizontal placement workflow is
not a claim of a complete large-world precision workflow.

## Transactions and access

Detached authoring prepares the entire group and validates it before Scene
publication. One successful command produces one Undo/Redo entry. Invalid arguments,
unfittable layouts and budget failure retain the previous Scene/history. The editor
**Check layout** control runs the same complete detached command without publication.
The panel holds only unsaved tool choices, not another authoritative document.

Automation uses `scene.apply` with `mesh.scatter`; MCP exposes the generated
`forge_mesh_scatter` binding from the same command schema. Existing project/target,
permissions, revision, request replay, history and document-save rules apply.
No parallel agent implementation is introduced. Discovery states limits and the
fixed-plane/independent-object semantics. See [MCP](mcp.md) and the
[user guide](../manual/editor/foliage.md).

## Evidence boundary

Focused layout/authoring checks, actual ImGui input, MCP and Gallery consumers
exercise the same owners. Gallery 144 contains an independently authored tapered
blade Mesh and two ordinary placed groups. Runtime resources and source-hidden
relocated content are tested separately from physical Windows acceptance.
Measurements of local CPU authoring and software rendering do not establish
physical GPU performance, hardware input/DPI acceptance or Windows execution.
