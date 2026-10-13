# 2026-10-13

## Navigation capture performance

- Capture marked surfaces and required ancestors from native values, without serializing unrelated scene data every navigation update.
- Preserve canonical bake hashes, static checks, scene-instance isolation, prefab inheritance and invalidation for removed members with owned surface intent.
- Add NavSourceCapture to the existing CPU profiler. Keep read-only gameplay queries on the last writable capture; bake/export formats and the SDK fingerprint are unchanged.
- Validate the same CPU workload before/after plus normal and sanitizer runtime checks. Windows/physical acceptance remains pending.

## Static Mesh navigation

- Bake marked editable and imported static triangle Mesh leaf entities through existing cooked selection/resource ownership; reject unsupported deformation and oversized input with diagnostics.
- Record explicit Nav2 source/mesh revision provenance and typed Build dependencies. Retain selected Nav1 assets and primitive-only headless compositions.
- Hold navigation Pending while selected CPU data loads; stale or failed replacement stops movement even when last-good bytes remain available. Reuse Mesh leases without per-frame cooked-file decoding.
- Add original Gallery51 with an editable floor, baked route, obstacle and agent; verify editable/imported runtime routes and relocated source-offline exports.
- C table/component layouts stay unchanged; the existing SDK fingerprint changes and matching modules require rebuild. Windows/physical acceptance remains pending.

## Observable navigation agents

- Report Moving, Arrived, Paused, Blocked and Failed separately from route-query success; show readonly progress in Live gameplay Inspector.
- Let C++ systems inspect copied agent observations and respond to arrival. Gameplay reads the last completed navigation tick; PostPhysics reads this tick.
- Stop according to remaining waypoint-route distance, so a close goal across an obstacle does not cause premature stopping.
- Add an original exportable navigation Gallery50 with a static obstacle, moving agent and ready-built NavMesh. Keep prior Gallery identities/artifacts.
- Exact SDK fingerprint changes; rebuild C++ modules with the matching SDK. Windows/physical acceptance remains pending.

## Animated camera and light properties

- Extend the current animation clip and Timeline with camera field of view/orthographic height and light intensity/linear RGB keys, including property-only clips.
- Reuse current document history, publication, fixed playback/recovery and copied preview; between-tick presentation updates camera/light fields without live ECS writes.
- Publish explicit cooked version3 while selected older clips remain readable. Keep the native component layouts and installed C++ SDK interface unchanged.
- Add Gallery49 with original camera zoom and colored sunlight clips, plus runtime/render/export and actual Timeline interaction checks. Windows/physical acceptance remains separate.

## Spatial HDR bloom

- Add an original color-only bloom pass before tone mapping in Scene and Game views, with explicit camera rectangles and alpha preservation.
- Reuse bounded texture sets across camera sizes; bypass GPU work when disabled or at zero intensity.
- Author threshold, intensity and spread in Scene lighting through existing Save/Undo and export ownership.
- Gallery48 supplies original warm/cool emissive materials and a reference cube; temporal effects remain separate work.
- Local shader/Vulkan, CPU/UI and content checks do not claim Windows/GPU or physical acceptance.

## CPU profiler

- Inspect separate Editor and Play CPU scopes with start/stop/clear, filtering and recent count/average/max/latest timings.
- Reuse existing editor frame measurements; keep collection off by default and compile-out supported.
- Invalidate unfinished measurements across collection changes, and protect runtime controls with live world generations.
- Reconcile runtime timing documentation with current exact-SDK phases and project frequency.
- Installed C++ SDK values and ABI1 remain unchanged. Windows/GPU and physical acceptance remain pending.

## Animation key value transforms

- Multiply and offset selected local positions and multiply selected scales as one Undo step, with matching slope changes.
- Keep key times, rotations, IDs, held modes and channel masks; reject invalid candidates atomically.
- Gallery47 compares original, repositioned and resized motion through the existing Play/export path.
- Keep the preview status row stable so the first timeline key drag does not cancel itself when preview starts.
- Windows/GPU and physical acceptance remain pending.

## Model topology refinement

- Split selected faces around new center points, or subdivide the whole Mesh into smaller triangles with shared midpoints.
- Preserve the shape, interpolate UV/color data and use one SourceDoc Undo step; invalid candidates leave the source unchanged.
- Gallery46 provides original, center-split and subdivided cubes through the existing publication/export pipeline.
- Windows/GPU and physical acceptance remain pending.

## Material vector geometry

- Length and Distance support typed scalar/vector/color inputs and scalar outputs.
- Reflect and Refract calculate Vector3 ray directions; Refract uses a scalar index ratio and returns zero for total internal reflection.
- Existing graph history, copy, functions and editor controls handle the new nodes.
- Native Windows/FXC5.1 and physical GPU acceptance remain pending.

## Animation timeline navigation

- Zoom, pan, fit the whole clip or fit selected keys without changing source timing.
- Shift-drag rectangle selection supports adding/removing groups and safe cancellation.
- Scrubbing and key dragging follow the visible time range; off-screen keys are clipped.

## Transform animation timing

- Scale whole-clip duration and key times together while adjusting curve slopes.
- Snap selected keys to a whole1–240 FPS grid; preserve poses, identities and slopes.
- Existing source history and publication feed preview, Play and source-free export.
- Gallery45 compares original, slower retimed and frame-snapped object motion.
- Windows delivery and physical acceptance remain pending.

## Material graph selection and layout

- Graph rectangle selection, All/Clear/Invert, rectangle alignment, even center
  spacing and graph-grid placement share existing source/history controls.
- Position changes and their Undo/Redo preserve automatic shader previews;
  source saving/publication still tracks layout. Value and connection edits
  continue to request preview compilation.
- Pending gestures cancel safely on Escape, document changes or locking.

## Material thresholds and rounding

- Step, Smoothstep, Ceiling and Round support scalar, vector and color graph
  values, existing functions, source history and editor type controls.
- Smoothstep handles reversed and equal endpoints safely. Round uses halfway
  ties to even; Ceiling rounds toward positive infinity.
- Local source, sanitizer, scaled editor and actual shader compilation checks
  pass. Windows/FXC5.1 and physical GPU acceptance remain pending.

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
