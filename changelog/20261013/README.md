# 2026-10-13

## Model duplication and grid placement

- Duplicate selected face groups into detached geometry with a Mesh-local offset.
  Shared vertices copy once; UVs, colors and metadata remain, and copies are selected.
- Snap Vertex, Edge or Face selections to a local-origin grid with enabled XYZ
  axes. Invalid geometry rejects atomically; aligned selections add no history.
- Existing source Undo reserves new IDs across branches. Publication and
  source-free exports use the same Mesh pipeline; no format or gameplay SDK change.

## Model topology cleanup

- Model face deletion keeps surviving records and unused vertices; explicit
  cleanup removes only unreferenced vertices. At least one face must remain.
- Join two adjacent triangles into a planar convex quad from their selected edge.
  Keep the lower face ID and diagonal; reject UV seams and conflicting metadata.
- Source Undo/Redo, selection reconciliation, publication and source-free export
  use the existing Mesh owners. No source/cooked format or gameplay SDK change.

## Model face groups

- Model Face mode supports Ctrl selection across the list and wireframe, with
  all/clear/invert/connected controls and shared-vertex group transforms.
- Selected-face triangulation selects generated triangles; selected facing keeps
  existing shared-edge winding checks. Inset/extrusion keep their active-face scope.
- Groups remain temporary editor selection. Applied geometry uses existing source
  Undo, Save & Publish, Scene/Play resources and source-free export.

## UV selection and arrangement

- UV box selection supports corners, full faces and connected islands, with
  replace/add/remove previews and cancellation. Select all, Clear and Invert
  change the temporary selection without dirtying the Mesh or adding Undo steps.
- U/V alignment to minimum, bounds center or maximum and even coordinate spacing
  use the existing Mesh source history. Coincident selected points remain together
  during spacing; partial corner edits can deliberately change seams or distortion.
- Applied UVs use the existing Save & Publish, Scene, Play and export path.
  Source/cooked formats, import recipe and installed SDK layout remain unchanged.

## UV island packing

- Editable Mesh UV adds selected/all-island packing previews, normalized padding,
  optional quarter turns and one-step Apply/Undo. Existing shapes, colors, seams
  and identities stay with the Mesh source.
- Packing previews reuse the UV view and cancel safely through its document
  lifecycle. Direct UV drags cancel when the canvas moves or resizes; a fixed
  status-row height prevents scrollbar changes from shifting a held drag.
- Gallery 44 compares three original checker panels. Published Mesh resources and
  relocated source-free export retain the packed UV streams. Source/cooked formats,
  importer recipe and native SDK layout remain unchanged.
- Source, maximum-profile and actual UI checks pass locally with sanitizers;
  Windows/physical acceptance remains pending.

## Interactive transform curves

- Transform Animation Timeline adds draggable position/scale tangent handles,
  selected-key Flat/Linear/Smooth presets and fitting with visible handles.
- A released drag creates one source Undo step. Canceled gestures restore prior
  fields; numeric curve drafts participate in Save, Undo and dirty-close handling.
  Pose/curve updates preserve unknown key and tangent extensions.
- Preview, published playback and relocated source-free export use the same
  existing evaluator. Source/cooked formats and native SDK layout remain unchanged.
- Focused source, publication/export and actual ImGui checks at three scales pass
  locally, including sanitizer execution. Windows/physical acceptance remains pending.

## Animation state graph authoring

- Animation States shows connected cards for existing named clips and timed exits.
  Select, connect, rewire, remove exits and choose entry through the existing source
  inspector and history. Clip choices stay scoped to the imported skeleton's model.
- Card positions survive Save/Publish and reopen, with one Undo step per drag or
  arrangement. Pan, zoom and framing change the view; dirty-close guards preserve drafts.
- Layout stays in editor source. Cooked poses, fades, marker events, recovery and
  source-free export retain the existing runtime semantics. No public SDK layout changes.
- Local source and actual ImGui interaction checks are recorded separately from
  Windows execution and physical acceptance, which remain pending.

## One-dimensional animation blend spaces

- Content adds reusable clip samples on a numeric axis, with model-scoped clip
  selection, cycle duration, source history, publication and atomic Scene attachment.
- Animator selects neighboring clips with Blend position at a shared cycle phase;
  positions clamp at endpoints. Existing morph, root-motion, IK and physics consumers
  retain their complete-pose ownership. Preview changes remain temporary until applied.
- Exact recovery and source-free export include cooked space/model dependencies.
  Gallery43 compares three positions and includes an ordinary C++ control example.
- Matching SDK modules need rebuilding after the public Animator layout change.
  ABI1 remains; native Windows and physical acceptance remain pending.

## Reusable animation bone masks

- Content adds an Animation Mask document with skeleton-scoped bone selection,
  fractional influence, descendant inheritance, source Undo/Redo and Save/Publish.
- Animator's Bone mask combines with Blend subtree for Blend/Additive animation,
  root motion and morph curves. Runtime binding and replacement share existing
  resource, clock and complete-pose transaction owners.
- Recovery validates exact mask revisions; export ships cooked masks and their
  typed dependency closure. Gallery42 compares whole, subtree and weighted masks.
- The Animator header changes the matching SDK fingerprint; C++ modules need a
  rebuild. ABI1 remains. Native Windows and physical acceptance remain pending.

## Rig target viewport controls

- Reach and aim targets have labeled Scene markers and model-axis handles,
  available from their Inspector, Scene View menu and command palette.
- Dragging uses parented model space, including mirrored/nonuniform placement.
  A temporary draft commits one Undo step on release; cancellation preserves the
  scene. Prefab targets retain ordinary property override and Undo behavior.
- Live markers observe the running scene in Forge's Scene/Game views; the tinted
  Inspector owns runtime tuning. Standalone games contain no editor tools.
- Unrelated normal entities no longer advertise an empty Live Inspector.
- Native Windows captures and physical acceptance remain pending for this local
  source bundle. Local source/UI evidence is tracked separately from delivery.
