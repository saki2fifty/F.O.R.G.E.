# 2026-11-03

## Smooth sequence camera views

Camera Shot adds an optional zero-default incoming blend duration. Compatible
adjacent views smoothly interpolate current copied world poses and lenses while
retaining the incoming Camera identity. The existing controller clock, hierarchy,
preview, frame sampling and exported runtime supply the same behavior. First shots,
gaps and loop starts retain cuts; render-texture producers remain independent.

Timeline applies interval/camera/blend edits as one Undo step. Live Inspector and
MCP tuning expose only an admitted shot's duration; Keep runtime values can review
it for the Scene. Scene copy, Prefab references, exact SDK matching and versioned
recovery preserve existing ownership. Original Gallery153 demonstrates animated
camera rigs, incoming/return blends and an overview gap.

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


## Foliage projection onto heightfields

**Tools > Foliage placement...** can project seeded ordinary objects onto a
published upright heightfield. Separate template/surface choices and a bounded
height offset resolve every origin against the rendered terrain revision.
Missing coverage or mismatched on-disk source rejects the complete layout.
Existing Scene history, copied renderer values, seeded horizontal spacing and
ordinary export remain responsible for the result. Placements do not follow
later surface edits or align to slopes.

Read-only automation/MCP terrain preparation returns ordinary `mesh.scatter`
arguments, Scene revision and source provenance. The shared command admits an
optional exact-count finite world-height array. Runtime needs no procedural
placement manager or source files. Gallery147 demonstrates 128 projected blades
using existing original content; the manual includes controls and Game captures.
An additive immutable-cache opening factory preserves existing constructor/layout
semantics. Dependency pins and native gameplay component/callback layouts are
unchanged. Windows/full-editor/physical acceptance remains pending; no numbered
delivery is implied.


## Source reverberation and processed-voice gain

Audio Source adds opt-in bounded reverb with decay, damping and wet mix controls.
The independent per-voice DSP uses the existing runtime effect graph, tail budgets,
source/one-shot lifecycle and project resource owners. Existing scenes default to
disabled; parameter edits, pause, Stop/Restart and recovery retain the documented
clip/history boundaries. Native component values require a matching rebuilt SDK;
C callback tables and clip formats are unchanged. Generic authoring/Inspector,
Play tuning and automation/MCP expose the same registered fields.

Source and category faders now apply at each voice's final existing output, so
mute includes buffered echo/reverb while source/DSP time continues. No new mixer
or bus graph is introduced. Live sound identifies retained Effect tails and keeps
Pause/Resume available after clip EOS. Gallery148 reuses original sound/sequence
content for dry/bright/damped comparisons and source-hidden exported PCM. The
illustrated manual covers authoring, controls, saving/export and current limits.
Full Main/Windows/physical acceptance remains pending; no numbered delivery is implied.

### Sixteen independent controller slots

- Extended the existing controller routes to sixteen numbered players. Multi-digit control IDs, rebinding, assignment, rumble and copied motion use the same validated slot bound; earlier bindings and the active alias retain their behavior.
- The Gamepads popup offers all sixteen slots and scrolls within the viewport. Automation/MCP discovery and assignment share the same owner and revision/permission guards.
- Gallery149 adds sixteen markers and twelve actions while preserving the earlier Gallery records, actions and four-player scene. The shared optional C++ example and installed SDK sample support both scenes.
- Updated controller instructions and authentic assignment/motion screenshots. Windows, physical devices, full-editor and graphical-export acceptance remain pending.

### Lossless WebP view-image output

- Scene/Game image export accepts PNG and lossless WebP. Native encoding preserves transparent RGB and uses the existing bounded IO/atomic replacement owner; PNG remains the default.
- File type/suffix checks and overwrite confirmation share the editor capture task. MCP admits the same formats and reports full-file MIME, while image content and sequence frames remain PNG.
- Updated image-export instructions and native codec/ownership contracts. Platform picker, physical GPU and full Windows acceptance remain pending.

### Action-triggered typed component messages

- Added optional Input Message authoring with fixed digital action and attached
  payload selectors, shared property/history/Prefab ownership and runtime diagnostics.
- A native fixed phase copies bounded builtin or opted-in project values and sends
  them through the existing ECS observers before gameplay; there is no parallel bus
  or persistent queue. Existing Play input injection/observations expose the workflow
  to MCP.
- Added original Gallery150 and a shared optional C++ receiver/installed SDK sample.
  The matching SDK fingerprint changes; rebuild native modules. ABI1 and existing
  scene/prefab formats remain unchanged. Windows and physical acceptance are pending.

- C++ System creation can omit the component template through Content or MCP,
  creating an empty managed startup function for project queries/observers. Existing
  component-bound rotation templates and registration/build ownership are retained.

### Sequence-driven UI values

- Added optional UI Values: up to16 unique named finite numbers on a UI Document
  entity. Existing RML expressions consume these Scene-owned values for text,
  styles and classes, with explicit C++ publication collision checks and shared
  copied-model limits.
- The existing component-track owner now admits explicitly stamped reflected
  builtins, starting with keyed UI Values. Timeline/source/cook, complete candidate,
  nested sequence clocks, seek, ordinary recovery and opaque keyed extensions use
  their existing owners. Native CSS animations retain presentation time.
- Inspector, live tuning/review, property/history/Prefab, automation/MCP and the
  matching installed SDK share the same component. Collection-entry drafts have
  room for their fields, and startup animator failures include their cause.
- Added original Gallery151, source-hidden relocated content coverage and an
  illustrated user guide. Scene/sequence Edit previews and frame export still omit
  Runtime UI; arbitrary DOM paths and native CSS seeking are unavailable. Rebuild
  project modules against the matching SDK. Windows/physical acceptance is pending.
- Structured Gallery admission explicitly recognizes current additive sequence,
  navigation-sharing and audio-reverb defaults while keeping existing authored
  files and identities unchanged.

## Scene camera output images

- Saved Render Texture configurations and Camera output references feed typed
  Material2D Color bindings. Multiple Cameras compose into one output, and
  dependent outputs render before their consumers during the same frame.
- Existing pools, native leases, real completion fences, FrameRenderer and Material
  ownership supply bounded view-local images. Missing writers and feedback cycles
  cannot authorize stale pixels. Scene and Game images remain independent.
- Content source controls, Inspector/Prefab/history, source and assignment MCP tools,
  matching C++ SDK values, original Gallery152 and the task manual cover the workflow.
  Resize requires republishing dependent Materials before export. Source/bundle3
  preserves older extension semantics with explicit collision-safe promotion.
- The profile is opaque linear HDR2D, four outputs/sixteen Cameras/four million
  configured pixels. Unsupported static-only consumers retain their current types.
  Local software rendering is separate from pending Windows/physical acceptance.
