# 2026-10-26 — Physics and navigation

Local source; Windows verification and numbered delivery remain pending.

## Moving and trigger Tile Maps

- Painted Map collision accepts Kinematic bodies and Static/Kinematic sensors
  through the same geometry, body commands, queries, events and recovery owners.
- Inspector calls the selected layer **Collision layer**, and the Scene guide
  identifies Map sensors. Dynamic and one-way Map collision remain unsupported.
- Gallery81 demonstrates an animated painted course;82 demonstrates painted
  triggers with a solid receiver. Both use ordinary editable scenes and assets.

## Navigation requested destinations

- Inspector and command palette can enter a Scene destination tool, with world
  plane/axis dragging, snapping, detached preview, one Undo, Prefab property overrides
  and normal saving. Shared target callbacks carry their inspected entity.
- SceneTools cancels captured previews when the application loses focus; rig and
  navigation input checks cover the shared guard.

- Navigation overlay shows a selected agent's requested destination and straight
  origin guide, using the saved asset's 3D/XY plane. Disabled guides are gray;
  existing amber runtime routes remain separate.
- Automation/MCP can show or hide that same overlay and read its current visibility,
  with asset-authoring permission and normal target/revision/sequence validation.
- Camera/light/audio and navigation guides share the existing builtin value decoder.

## Canonical physics-query exclusions

- Regression coverage executes the 16384-intersection ray-work boundary through
  real mesh/collision imports and both runtime hosts, including complete overflow
  clearing and early exclusions. XY coverage exercises its full 4096-shape profile.

- Closest/all rays, overlaps and linear sweeps in 3D/XY accept up to 16 canonical
  object exclusions before result selection, combining with existing 2D sweep
  ignore. Layers, sensors, physical collision and query ownership stay independent.
- Eight typed exact-SDK callbacks and C++ span helpers share existing validation,
  result conversion, output clearing and default access. Existing C signatures/POD
  sizes stay unchanged; rebuild against the matching fingerprint.
- 3D closest ray and sweep input snapshots preserve aliases before output clearing;
  the original unfiltered ray now also clears valid outputs on context failure.
- PhysicsVolumes adds original exclusion scenes and a project-owned marker, with
  live toggles through existing runtime property access. RayProbe schema remains1.
- Both runtime hosts share live-property routing in RuntimeWorld; ordinary-runtime
  custom C++ fields use the existing reflected codec, matching SDK Play.

## Complete physics rays

- Copied multi-hit rays in3D/XY return the nearest surface per canonical object,
  sorted along the segment; layer/sensor filters and native geometric rules apply.
- Bounded collectors reject incomplete results, retain existing physics owners and
  leave closest-ray traversal, command state and authored data unchanged.
- Matching size-checked SDK callbacks/C++ clients use complete output-buffer rules;
  rebuild project modules against the new exact fingerprint. ABI1 prefix and
  authored/private checkpoint versions are unchanged.
- PhysicsVolumes includes editable 3D/XY ray observations and usage guidance.
  Shared example preparation discovers unregistered primary and secondary scenes
  through the existing source scanner before stamping their matching C++ schemas.

## Diagnostic text boundaries

- Long valid Unicode diagnostics retain a whole-character prefix at the existing
  8 KiB limit. Strict malformed-text/context rejection, record/ring limits and
  sequence/permission/thread/lifetime rules remain unchanged.
- The core service and compiler text pages share the existing suffix-boundary rule;
  caller/log bytes and page offsets retain their current ownership and semantics.

## Navigation runtime feedback

- Navigation progress includes concrete current failure details and expandable
  valid-only world position/projected goal. Pending/disabled/idle/success states
  do not inherit old failure text; ancestor errors identify authored names/IDs.
- Native display text shares existing JSON replacement behavior with automation;
  source/structured admission remains strict. Resolved agent-limit episodes retire
  their dedup marker so recurring limits emit a fresh timestamped warning.
- Existing runtime/MCP snapshot owners expose the feedback without a new SDK POD,
  component, source/asset/checkpoint format or error database.

## Live navigation-agent controls

- Navigation Agent flags, speed, stopping distance and destination coordinates are
  tunable in Live gameplay through the same runtime owner in ordinary and SDK Play.
  Existing validation/generation checks retain prior values after rejected edits;
  routing applies the new configuration on a fixed tick. NavMesh selection stays authored.
- MCP live tuning and the frozen Keep values after Stop review use the same fields.
  Selected values apply through ordinary Scene history/Prefab overrides/Save, without
  copying runtime movement. Recovery2 remains in its supporting path; SDK Play keeps
  authored restart. No component layout, source format or SDK header change.

## Navigation region Scene tools

- Visible region outlines can be selected through the existing Scene-helper owner;
  selected regions frame their full XY extent, including mixed art/data bounds.
- Centered Width/Height edge handles use detached previews, local snapping and one
  property/Undo operation. Cancellation, edit locks, prefab overrides and Save/reopen
  preserve current ownership. The same projected-axis math serves rig targets.
- Existing automation/MCP properties cover the resulting edits; no source/asset/SDK
  format changes. Portable helper tests also exposed a core digest predicate linked
  through the asset-build layer; the unchanged predicate now belongs to the existing
  byte utility with a lightweight declaration and compatibility reexport.

## Top-down XY navigation

- Navigation Region2D rectangles define independent walkable areas and obstacle
  masks. Navigation builds explicitly select3D orXY through the existing worker,
  asset, runtime and editor owners. XY queries/agents/avoidance preserve visual Z.
- New XY assets use guarded envelope/catalog/geometry3. Existing3D settings and
  envelope1/2 retain their semantics; rebuilds preserve logical AssetId. Exact SDK
  modules rebuild for the added public source type. Recovery2 remains unchanged.
- Plane selection, selected-region outlines, reflected properties/history/Save,
  automation/MCP and cooked relocation use shared owners. Original Gallery80
  includes an obstacle route, avoiding Sprite pair and orthographic Camera.
- Native/actual ImGui/MCP/process/installed-SDK and relocated cook checks are
  separate from pending Windows rendering/input/export and authentic screenshots.
  This finite profile does not extract colliders/TileMaps or plan platformer links.

## Navigation movement ownership

- Navigation rejects enabled Character Controllers in both domains on agents,
  static bake sources and their resolved spatial ancestry, including inherited
  values and before native realization. World binding still ends traversal;
  disabled controllers retain ordinary eligibility. Pure path queries remain
  available for C++ controller policy.
- Native and authored source capture share the same validation. Failed builds
  preserve prior catalog/artifact selection. Existing identities, formats, SDK
  tables and checkpoints are unchanged. A focused MCP navigation route exercises
  actual publication/rejection without requiring a Windows runtime kit.

## Checked 2D stair rise

- Character Controller2D adds default-off world-space **Step Height**, with bounded
  headroom/tread/actual-endpoint clearance through existing native casts and work
  limits. Stepping preserves requested horizontal distance and separates vertical
  correction from velocity. Floating, air and upward intent keep ordinary movement.
- Character2D includes an original green/amber three-tread comparison and a taller
  jump barrier. Reflected Inspector/MCP settings, history/Save and all registered
  sample Scenes use existing owners. Matching SDK modules must rebuild; source
  formats/identity and private checkpoint9/outer2 remain unchanged.

## 2D slope and ceiling response

- Character Controller2D adds **Stop on slopes** and **Slide on ceilings**, with
  defaults preserving existing sources. Grounded locomotion can hold on walkable
  ramps or stop upward requests at ceilings through the existing bounded solver.
  Platform transport remains separate; no implicit gravity is added.
- Character2D includes an original green/amber surface-response comparison Scene.
  Its C++ gravity policy distinguishes grounded contact from airborne velocity.
  Example preparation stamps every registered Scene through the existing schema.
- Inspector and MCP expose the same typed settings, history and saved values.
  Matching SDK modules must rebuild; authored identities/formats and private
  physics checkpoint9/outer2 remain unchanged.

## Checked 2D crouch and stand

- Character Controller2D adds Crouch height, with0 for automatic bounded half-height.
  Checked stance requests share the existing character FIFO and C++ SDK operation
  table. Blocked changes preserve geometry, feet and motion; copied stance and
  shape-change status are separate from placement status.
- Native2D updates its existing Kinematic shape, preserving binding/epoch. Movement,
  support, queries, candidate checks, replacement and Scene guides use effective
  stance. Private physics checkpoint9 retains stance and pending requests; outer2,
  authored identities and native ABI1 table version remain unchanged. Matching SDK
  modules must rebuild.
- Character2D includes a C-key input action and a pink low-clearance obstacle. Its
  C++ visual follows observed posture and retries standing when the path clears.
  Inspector and MCP use the same reflected authoring fields, history and Save.
- The root SDK example fixture now compiles its moving-platform source and declares
  the JSON header dependency required by its Sprite Renderer consumer.

## Moving-platform character transport

- Character Controller2D adds default-off **Follow moving platforms**. Grounded
  characters can ride ordinary moving Kinematic/Dynamic support while retaining
  relative locomotion velocity and game-owned gravity/jump policy.
- Existing native records, bounded movement queries and endpoint admission handle
  translation, rotation, wall clipping and fail-before-adoption crush. No parenting,
  second pose authority, dependency or persisted carry cache was added.
- Shared Inspector/automation/MCP fields retain validation, Undo/Redo and Save.
  The component header requires a matching SDK/module rebuild; Scene/Prefab identity
  formats and private recovery versions remain unchanged.
- Character2D includes an original editable Moving Platform2D component/system
  and a cyan platform. The ordinary example preparation tool admits multiple
  inspected authored types in the same module.
- Focused controller, fresh recovery, actual Inspector/MCP and relocated installed
  SDK example checks accompany the workflow. Native Windows verification remains
  separately pending.

## One-way platforms

- Physics Body 2D has default-off One-way collision and a world-space One-way
  margin. The selected profile uses Static nonsensor Box platforms and their
  transformed local +Y face, including signed Y scale and supported Z rotation.
- Rigid bodies use both discrete and continuous response filtering. The existing
  native character controller applies the same side rule through movement,
  penetration recovery, support and floor following. Geometric queries and checked
  placement retain complete shape observations.
- Inspector and automation/MCP share the reflected fields, validation, history
  and persistence. Selected collision draws an arrow toward the solid face.
- Gallery79 compares landing and upward passage; Character2D includes an
  accessible one-way platform using its existing editable C++ behavior.
- Private physics checkpoint8 retains the required body-owned side history.
  Existing Scene/Prefab identities and formats remain compatible; public layout
  changes require matching rebuilt SDKs and modules.
- Focused gates cover response/CCD, mirrored and rotated shapes, extreme supported
  origins, configuration changes, recovery, actual UI controls, MCP edits,
  packaged content and installed SDK examples. Native Windows gates retain
  separate acceptance requirements.
