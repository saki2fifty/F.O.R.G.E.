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

Gallery14 uses original connected/split/mirrored-fitted meshes, the existing checker Material, selected Windows/D3D12 cooks and ordinary Mesh Renderer/export closure. Source-free relocated package checks compare the exact cooked UV, normal, color and topology streams. Automatic unwrap, welding and texture-pixel editing remain outside this slice.

### Pack existing UV islands

`uv_islands()` derives all islands in canonical face/corner order from the same
exact edge/UV adjacency as `uv_island`. `pack_uv_islands(seed_faces, padding,
rotate_tall_islands)` accepts distinct existing face IDs and expands every touched
island. It changes their UVs only. Untouched islands retain their coordinates and
are not obstacles; selected-only packing can overlap those untouched islands.

Packing uses an original bounded next-fit shelf layout of island rectangles,
sorted by decreasing normalized height, width and stable seed face ID. Optional
rotation turns tall rectangles 90 degrees without flipping. A 40-step search retains
an admitted common uniform scale; translations place rectangles within [0,1].
Subtraction/division by the global longest extent handles tiny UV spans without an
overflowing multiplier. Padding is tile distance in [1e-6, .25], applied at borders
and between rectangles; zero padding rejects to keep islands separated. The UI
slider offers .001–.1. Empty/duplicate/missing seeds, zero-width/height rectangles,
impossible padding or a relative scale that collapses an output rectangle reject
the complete candidate. Geometry, colors, corner extensions, allocator high-water,
relative UV area/aspect and shared endpoint equality remain unchanged.

The UV workspace offers selected/all previews, cached by source revision, seed
faces, padding and rotation. A stationary frame does not copy/repack the source.
Preview shares the existing UV derived view and prevents competing numeric/drag
edits. Apply creates one source Undo; Cancel/Escape, close, Model tab, source/history
change, Play/publication or Save discard an unaccepted packing preview. Save still
publishes the applied source. Direct UV drags now reserve a constant-height status
row and cancel on changed canvas geometry, avoiding layout-induced remapping.

This is conservative rectangle packing of existing islands, not a minimum-area
polygon solver, seam creation, unwrap, geometric texel-density normalization,
multiple UV sets, lightmaps or UDIMs. Ordinary source1 UVs cook through the unchanged
MeshData/import recipe, native SDK and resource/package owners. Gallery 44 contains
original six-patch panels comparing overlapping, packed and quarter-turn layouts.
Focused resource and relocated source-offline export checks compare every stream.

### UV selection and arrangement

The UV workspace keeps one transient corner selection. Select all, Clear and
Invert operate on authored face corners, independent of the pick mode. Box select
captures revision, source points/group membership, canvas mapping, original
selection and Corner/Face/Island mode once. Contained corner centers are inclusive;
Face mode expands hits to complete faces and Island mode expands through exact UV
edge adjacency. Plain replaces, Ctrl unions and Shift subtracts (Shift takes
precedence). Release includes its final pointer; the rectangle clamps to the
canvas. Clicks below the drag threshold leave the selection unchanged.

Held frames display a candidate selection without replacing the applied selection
or copying source JSON. Escape, focus loss, source/history changes, Model tab,
close, Save, Play/publication, mode/view/canvas changes or wheel/middle navigation
cancel it. Numeric, packing and move gestures are exclusive. Read-only states
retain ordinary click selection and navigation. No selection is serialized,
published or exported, and selection-only operations create no source Undo.

`align_uv(corners, axis, target)` assigns the selected U or V coordinate to its
minimum, bounding-box center or maximum. The other coordinate remains exact.
`distribute_uv(corners, axis)` groups exact original coordinate levels and spaces
those levels evenly between the original endpoints. Coincident selected levels
stay coincident; one or two levels are a no-op. Invalid axis/target, duplicate,
empty or missing corners reject atomically; unrepresentable distinct spacing also
rejects. Both use complete candidate validation and preserve geometry, color,
IDs, next_id and opaque fields.

These are deliberate corner edits, not rigid island operations or unwrap. Partial
selections can change seams and distortion. Alignment can intentionally collapse
UVs onto a line, which remains valid source but cannot be island-packed until it
has extent on both axes. Arrange controls use the existing source mutation/history
owner; ordinary UV streams use the unchanged cook/resource/export consumers.

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

## Connected-surface facing

`connected_faces(seed)` validates the existing source and returns faces joined by
shared undirected geometric edges in authored order. A vertex-only contact does
not join surfaces. `reverse_faces(span)` validates a bounded unique existing
selection and reverses complete corner objects after the existing first fan anchor.
It preserves vertex/face IDs, allocator high-water, geometry, colors, UV/extension
association, shading flag and root/face metadata. Whole-surface reversal reverses
the existing fan triangles without changing their covered geometry; two reversals
restore exact JSON.

Candidate source validation continues to reject nonmanifold/inconsistent directed
edge uses. Reversing an arbitrary connected face subset can fail this contract;
Model deliberately offers **Reverse connected surface** and **Reverse Mesh facing**
instead of silently detaching vertices or accepting inconsistent winding. Smooth
candidates additionally validate averaged incident normals before replacing source;
undefined averages leave source/history untouched. Source/cook versions and the
normal weighting policy remain unchanged. No new service/owner or dependency.

Model actions use existing SourceDoc mutation/history, locks, pending-publication
and competing-preview guards. A successful revision resets UV corner scopes/cache
and reconciles durable selection. Wireframe establishes geometry only; the published
Scene/runtime Mesh normal and triangle winding establish facing. Native module
compatibility uses the explicit installed header/bridge set; editable-Mesh
authoring APIs are outside that set, so this change alone does not require a
C++ gameplay module rebuild.

Gallery19 publishes two original open-front room sources with identical geometry
and opposite facing. Tests verify signed normal direction relative to the interior,
complete corner association, actual typed resources/material resolution/camera
bounds and selected relocated source-free export. Native captures139–141 and final
Windows package Gallery19 exported EXE startup are authored checks; native/physical
acceptance is reported separately when executed.

## Model vertex selection

Model Vertex mode provides all/clear/invert, ordinary click/Ctrl toggle and explicit
box selection. The rectangle contains projected vertex centers inclusively,
including vertices behind other surfaces. Plain rectangles replace the captured
selection; Ctrl rectangles union with it. The rectangle is clamped to the canvas,
and the existing mouse drag threshold distinguishes a gesture from a no-move click.
Release includes its final coordinates even without an intermediate held frame.

The editor captures source asset/revision, original selected IDs, projected points,
canvas origin/size, orbit and pointer once. Held frames draw temporary feedback;
selection release changes editor state only. Escape, focus loss, collapsed/closed
window, UV/source/project switch, source revision/history change, changed canvas or
orbit, Play lock, publication or incompatible previews cancel it. Save requests
cancel immediately before a worker can finish. Selected IDs still feed the existing
validated source transform and one AssetSourceDocument history entry. Invalid edits
retain geometry/history; planar/convex/manifold/count/byte rules are unchanged.

Pinned ImGuiListClipper submits only visible Vertex rows, including the first and
last IDs through ordinary list scrolling. Selection closes/reopens independently
of authored geometry and the Scene owner. No selection is serialized or exported;
published geometry uses the established resource/package route. Shared native SDK
headers/fingerprint inputs are unchanged by these private editor controls.

## Model face groups

The existing Face domain now holds a transient stable face-ID set in the Mesh
editor, alongside its active face context. Plain list/canvas clicks replace it;
Ctrl toggles exactly one nearest visible face. Canvas picking resolves depth before
changing selection, so overlapping polygons cannot toggle several faces in one
click. All/clear/invert operate on source faces and connected selection follows
the existing shared-geometric-edge adjacency. Selection-only changes create no
source revision, history entry or persisted selection database.

Face transforms derive the canonical union of referenced vertex IDs and call the
existing transform kernel once. Shared vertices therefore move once. Original
planar/convex/manifold admission remains; invalid group deformations reject without
replacing the good source. Inset/extrusion/active-face triangulation remain explicit
single-face tools and require that the active face belongs to the Model selection.
Extrusion selects its generated cap; inset retains its original inner face.

Selected triangulation calls the existing span kernel, retains selected original
IDs and selects newly allocated source face IDs after acceptance. The source
exclusively owns their creation and allocator. Existing topology source Undo/Redo
prunes IDs that disappear and keeps surviving IDs; transient selections are not
history payloads. If a previously nonempty selection loses every face, it falls
back to the available active face, preserving the established single-face workflow.
An explicitly cleared selection stays empty. Close/reopen selects one active face.

Selected facing uses existing shared-edge winding validation; a partial closed
surface reversal rejects, while connected/all selection reverses the complete
surface. Accepted edits use one existing source Undo, reset invalid UV scopes and
reconcile Model selection. Source/preview revision keys retain derived-cache
invalidation; highlight groups stay outside cached geometry. Source/cook formats,
installed native SDK headers, importer recipe and immutable resource/export owners
remain unchanged. No batch inset/extrusion or region extrusion is claimed.

## Model topology cleanup

`delete_faces` accepts 1–4096 distinct existing face IDs and rejects deleting every
face. It removes only the selected complete face records; surviving faces/corners,
all vertex records (including unused ones), source extensions and `next_id` remain.
`remove_unused_vertices` explicitly removes only vertices not referenced by any
remaining corner. It preserves the used records and identity high-water mark;
an already clean source is a no-op with no history entry. Cleanup does not update
external deformation/rig authoring data; those assets retain their own owners.

`join_triangles` accepts two distinct geometric edge vertex IDs. Exactly two
incident triangles are required. Nonidentity face fields and complete shared-
vertex corner records must agree exactly, including UVs and opaque fields. Thus
a UV seam or unknown-data conflict rejects atomically rather than being dropped.
The oriented boundary must form one planar strictly convex four-corner polygon.
The lower face ID survives in its original source position; the other face is
removed. All vertices, remaining face/corner fields and `next_id` are retained.
The first shared-edge endpoint in the retained triangle's original corner order
anchors the quad, preserving the original shared edge as its cooked fan diagonal.
Smooth normals are recalculated from the new unit-face-weighted topology; global
cooked normal equality is not promised. No general polygon dissolve or automatic
triangle pairing is provided.

Each changed candidate is admitted through the full existing source/cook
validators before replacing source, including undefined averaged-normal checks.
Rejected operations leave source, revision, history and selection unchanged.
Model **Delete selected faces**, **Remove unused vertices** and Edge-mode **Join
triangles** use the existing source/document history and publication pipeline.
Deletion prunes selection using the Model face-group policy; joining switches to
the retained quad in Face mode. Removed edge/corner scopes are reconciled, and
each accepted action is one source Undo step. Play, publication and competing
Model previews disable these actions. No source/cooked schema, importer recipe,
runtime ownership or installed gameplay SDK layout changes.

## Derived Model view cache

The private editor cache scopes projected points, original positions, depths and
ordered face drawing loops by source asset/revision, valid numeric/topology preview
identity, orbit and canvas origin/size. Idle or selection-only frames reuse the
geometry. Highlighting and selections stay outside the cache. Source/view/preview
changes rebuild a candidate before replacement; close/load/project switch release
it. Invalid previews use the original source view. The cache preserves the existing
projection math, face draw order and click/sculpt/paint coordinates; source history,
publication, runtime resources, format and native SDK ownership remain unchanged.

Independent axis-coordinate/depth/order and invalidation tests, actual scaled
selection and Model/UV/brush/facing/shading/triangulation gates pass locally with
full sanitizer coverage for cache and selection. Same optimized widget fixture
shows roughly11.4ms warmed idle CPU at4096vertices/3969faces, compared with15.8–17.1ms
before caching. This is fixture CPU evidence, not a native GPU or physical FPS
measurement. Native delivery and physical review remain separate acceptance gates.
