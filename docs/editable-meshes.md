# Editable Mesh source and cooked projection

`.mesh.json` is a version-1 `forge.editable-mesh` document with one persistent Mesh `asset_id`, `next_id`, `vertices` with stable IDs, XYZ positions and optional RGBA colors, and `faces` with stable IDs and ordered corners. Each corner names a vertex ID and owns a two-component UV. The source is authored data; it is not loaded by gameplay. Unknown versions reject explicitly; there is no implicit conversion of imported Model subassets.

`AssetSourceDocument` owns candidate-before-commit edits, a revision check, source save and independent Undo/Redo. Its checkpoints are bounded to 64 entries and 8 MiB per history direction; source JSON is capped at 2 MiB,500,000 parser events and depth16. The event budget admits the supported maximum-vertex/corner profile; byte/depth limits remain unchanged. A tool changes one candidate, validates it, then creates one history entry. Element IDs are never reused. The existing source-document identity hooks preserve the `next_id` high water through Undo/Redo, new history branches and save/reopen, and reject edits that lower it or allocate a new element below it. Undo restores geometry but can leave the source dirty because reserved IDs must be saved. Extrusion replaces the selected face with a new cap ID and new side-wall IDs; the editor selects the cap. A rejected edit changes neither source nor history. This small source format avoids a second asset/scene ownership system.

Validation admits at most 4,096 vertices, 4,096 faces and 16,384 face corners; each face has 3–16 strictly convex, planar corners. Vertices referenced by a face must exist and be unique within it; shared edges may have at most two uses with opposite winding. Coordinates must be finite and within the source bounds. These checks are the supported first modeling slice, not a general polygon repair or automatic unwrap system.

The `forge.mesh.editable` trusted CPU importer triangulates face fans into the existing immutable `MeshData` format, with POSITION, NORMAL and independent TEXCOORD_0 streams. A smooth-shading flag averages vertex normals; optional vertex colors cook to COLOR_0. The Model preview offers a bounded smooth sphere preset, sculpt displacement along averaged normals, and vertex-color painting. Each stroke is a single candidate edit and Undo step after mouse release. A rejected stroke leaves the source unchanged. It publishes `mesh.bin` through the existing candidate/cache/catalog selection pipeline as a root Mesh asset. A failed candidate leaves the last good selected revision. The existing Mesh resource pool and Mesh Renderer load that root Mesh; scene instance transforms and material bindings retain their existing owners. Runtime packaging validates the selected cooked artifact and strips the authored `.mesh.json` source. Desktop Windows D3D12 and Linux CPU cook targets share the source schema; no new runtime ABI or editor code is shipped with a standalone game.

Texture UV origin is top left. Planar projection is deterministic and uses the dominant face-normal axis; it does not claim island packing, distortion control or automatic unwrap. Source edits and preview are bounded to the supported small-mesh profile. This is a low-poly sculpt and vertex-color paint slice. It does not paint texture pixels, dynamically subdivide the Mesh or provide high-resolution sculpting. Those workflows need separate bulk storage and evaluation design.

## Face inset and signed extrusion

`inset_face(face_id, positive_distance)` moves each edge inward by the same local-space distance, intersects adjacent offset lines, and retains the selected face ID for the inner polygon. It allocates one inner vertex and ring quad per original corner; outer vertices and neighboring faces remain unchanged. This is an edge offset, not centroid scaling. New UVs and optional colors interpolate on the source face's existing fan triangles. Outer corner data and smooth-shading intent remain intact.

Collapsed or crossed offsets, unstable corners, nonfinite/nonpositive distances and source/allocator limits reject before mutation. The operation retains the original corner count; it does not clip polygons, repair arbitrary geometry, or perform boolean/self-intersection removal. Extrusion accepts bounded nonzero signed distance: positive moves outward along the face normal, negative moves inward. Side-wall UV depth uses the absolute distance. A recess that intersects another surface is outside automatic repair.

The existing editor Model preview evaluates a copied candidate. Preview transform, inset and extrusion are exclusive; Accept creates one source history step. Cancel or focused Escape clears the preview without committing it. The original inset inner face stays selected for subsequent extrusion; an accepted extrusion selects its new cap. Source format1, MeshData, publication/resource ownership and standalone stripping are unchanged. Gallery scene12 contains original framed, raised and recessed panels with Windows-target cooked assets and the existing checker Material.

## UV islands and canvas editing

`MeshUvCorner` identifies a source face ID and zero-based corner. `uv_island(face)` validates the supported manifold source, derives adjacency from geometric shared edges with exactly equal UV endpoints for the same vertex IDs, and returns stable face/corner order. Reversed edge direction is canonicalized by vertex ID. A UV seam, disconnected topology or coincident UV position alone does not join islands. There is no persisted island graph or fuzzy welding.

The multi-corner `transform_uv` accepts at most16,384 unique existing references and finite translation/rotation with signed nonuniform scale (each absolute scale greater than1e-5 and at most10,000). It transforms around the mean of selected face corners in canonical order. Matching endpoints receive identical arithmetic. The old single-face positive uniform-scale API retains its admission limits. `fit_uv` uses the bounding rectangle's longest extent, centers it in [0,1] with padding0–0.49, and preserves aspect ratio, including line selections. A point-only selection rejects. Normalized subtraction/division avoids an overflowing fit multiplier for tiny spans. Both operations validate a copied candidate before replacing the document; geometry, IDs, colors and extension fields remain unchanged.

The UV workspace holds one transient multi-face selection and a revision/topology-scoped cache. Whole-Mesh wireframe and UV tile remain derived source views. Signed numeric transforms, mirror and fit use the same source operation owner. Canvas Corner/Face/Island selection, Ctrl selection, pointer-anchored bounded zoom, middle-button pan and framing are navigation/editor state. Drawing clips double-precision UV lines before conversion to screen floats. The canvas owns wheel input while hovered; it does not scroll the parent document or send scene tool input.

A drag keeps a fixed source revision and selection, captures canonical selected UV doubles/shared mean once, and evaluates bounded transient pairs for display. Held movement does not copy or validate the entire JSON source. Release still calls the full existing source transform and commits one validated source-document edit. Transient coordinates are not an authoritative UV database or persisted mesh representation. Final release coordinates are included even if no held frame observed the movement. Returning to the start refreshes the candidate instead of retaining an old displacement. Focused Escape, close/project/tab switch, changed revision/topology, Play lock or pending publication cancels it. Numeric preview has explicit Apply/Cancel and disables direct dragging. No source history, resource adoption, renderer ABI or format version is added.

Gallery14 uses original connected/split/mirrored-fitted meshes, the existing checker Material, selected Windows/D3D12 cooks and ordinary Mesh Renderer/export closure. Source-free relocated package checks compare the exact cooked UV, normal, color and topology streams. Automatic unwrap, welding, atlas packing and texture-pixel editing remain outside this slice.

## Local brush symmetry

`MeshBrushSymmetry` combines X/Y/Z local-origin reflection planes. Old sculpt and
paint signatures delegate to None, preserving their symbols and behavior. The
new overloads reject unknown bits and retain the256-sample admission limit.
For a selected axis, influence evaluates `abs(vertex)-abs(sample)`; otherwise it
uses the ordinary difference. Euclidean distance equals the nearest of all
axis-reflected stamps. Maximum pressure-weighted squared falloff combines the
stamps, so overlapping/on-plane stamps do not add strength or opacity. Work stays
one distance per source vertex/sample, without expanded sample arrays.

Sculpt uses each vertex's existing averaged source normal; a symmetric point set
with asymmetric triangle incidence can have different paired normal directions.
This mirrors influence, not topology or normal ownership. Paint changes only
vertex color. Source/cooked formats, IDs, corner UVs, immutable resource adoption
and runtime ABI are unchanged.

The Model tools expose Mirror X/Y/Z and project reflected hover stamps. A stroke
captures its radius, strength/color/opacity, axes, mode and source revision once;
those controls are disabled while dragging. Lock, pending publication, focus loss,
Escape, Undo/Redo, close/load, UV switch or changed source revision cancels it.
Release includes the final in-canvas pointer sample, even without a prior held
frame. It calls the existing operation and commits one validated source-document
edit. Gallery15 compares one-sided sculpt, X-mirrored sculpt and XYZ-mirrored
paint. Selected source-free relocated exports preserve exact cooked streams.

## Authored triangulation

`triangulate_faces` accepts1–4096unique existing face IDs. It uses the same ordered
corner fan as cooking: (0,1,2), (0,2,3), and so on. It retains the polygon ID for
the first triangle and reserves further face IDs in source/document order, using
existing next_id. Positions, vertex colors, complete corner UV/extension data and
face extension data are copied; unrelated faces and source extensions stay intact.
Triangle-only selections are a no-op. Missing/duplicate faces, count/corner bounds
and allocator exhaustion reject before replacement. Source and final candidate
validation retain the existing manifold/convex/winding owner.

Model actions **Triangulate face** and **Triangulate Mesh** commit one source Undo
step and clear topology-scoped UV state. They honor read-only/pending/stroke and
preview locks. The retained face stays selected. Undo/branch/reopen keep allocator
high-water, so discarded triangle IDs are not recycled. Smooth normals are
recomputed from new incident triangles; unchanged flat geometry preserves indexed
render triangle attributes. No schema/cook revision/runtime ABI/dependency changes.
Gallery16 compares polygon source, equivalent triangle source and a triangle cube
with a sculpted/painted corner. All use ordinary selected Mesh/render/export paths.

Root authored Mesh resources expose material slot `default`; built-in primitives
expose `surface`. Inspector labels do not change the authored slot identity. Scene
bindings must use the selected resource's actual slot key. Gallery/render consumer
checks exercise the same material selector as runtime; unknown keys reject.

## Flat and smooth shading authoring

`set_smooth_shading(bool)` changes only the existing optional source `smooth` flag
(default false); current effective policy is a no-op. Flat cooking uses each face
normal. Smooth cooking normalizes the equal-weight sum of incident face normals
for each shared source vertex, including across independent UV seams. There is no
area weighting, angle threshold or smoothing group. It does not alter silhouette,
positions, topology, UVs, colors, identities or extensions. Candidate source
validation and averaged normal definition checks precede replacement; cancelling
incident normals reject enabling smooth atomically. Flat can recover such a prior
smooth source. Existing cooker behavior, source version and runtime ABI stay intact.

Model **Shade flat / Shade smooth** use one SourceDoc Undo step and publication.
Current mode, locked/pending documents, active strokes and geometry previews disable
mutation. Model wireframe previews geometry; Save & Publish changes normal streams
for ordinary Scene/Play/render and selected source-free export. Gallery17 compares
identical original sphere geometry with flat and smooth normals.
