# 2026-11-17

## Stopped sequence UI preview and frame output

Stopped Scene sequences render saved UI Documents and animated UI Values in Game
and exported frames through the existing copied sequence world and native UI
presenter. Input and gameplay remain inactive; native CSS presentation holds at
its initial time. Preview/source/Play/project handoffs share the existing lifetime
owner, including retained preview images. Incomplete UI cannot publish a partial
frame. Existing automation/MCP capture operations use the same workflow and
output grants. The user manual includes a native sampled frame and the current
preview, saving and export limits. Standalone clip Scene UI overlays and native
CSS seeking remain outside this profile.

## Navigation crossing collision

Optional Navigation Link Collision checks physical obstructions on straight and
arced crossings through the existing navigation and physics owners. Capsule3D
and circleXY queries share bounded work, initial-overlap/layer/sensor filtering,
held/resumed traversal and atomic failure continuation. Scene/Prefab history,
Inspector, live tuning, selective kept values, automation/MCP and semantic recovery
share the existing subsystem paths. Two editable Gallery Scenes include ready
navigation and normal source-free export. The new public POD requires matching
C++ modules; existing Agent/Link and service-table layouts remain unchanged.

## Camera control animation

Timeline component tracks support Camera Aim and Camera Follow offsets, weights
and discrete controls through the existing reflected admission and sampling owners.
Target references remain Edit-only. Preview, playback, source automation/MCP and
export share this path. An editable Gallery Scene animates following and aiming
weights around a stationary target.

## Camera Follow

Camera Follow derives an eye position from a same-Scene target with weighted
World or Target offsets. It composes before Camera Aim and Camera Shot blending,
preserving native node poses and independent producers. Inspector, Prefab reference
remapping, Scene history, live review, SDK and automation/MCP share the existing
owners. An editable Gallery Scene demonstrates moving-target following, including
source-free export and stopped Timeline frame sampling.

## Camera Aim

Camera Aim follows a same-Scene target through a camera-only view correction,
with target-local offsets and an angular weight. Native transforms, attached
objects and children retain their poses. Camera Shots aim their endpoints before
blending. Inspector, Prefab reference remapping, Scene history, live tuning and
automation/MCP share the existing owners. Read-only `play.cameras` exposes composed
views for a requested extent and output. The matching SDK includes the native
value, and an editable Gallery Scene demonstrates an animated target.

## Camera Shot component isolation

Camera Shot view blends preserve the resolved entity pose used by attached meshes,
lights and children. Camera extraction consumes a separate transient view pose;
runtime presentation and stopped Timeline frame sampling share this behavior.
Malformed view poses report Camera diagnostics without affecting other producers.

## Validation contracts

Scene command discovery checks every supported operation identity and rejects
duplicates. Planar preview checks cover Static/Kinematic directional Map sensors
and retain the Dynamic one-way Map boundary.
Navigation connection pipeline coverage runs straight and arc profiles in separate
timed cases, retaining every assertion and reporting the active profile.

## C++ rename in open files

Code assistance can preview and apply a symbol rename across currently open
project C++ sources. One complete proposal identifies every changed file; source
and compiler-context guards apply before editing. Each file keeps its existing
Undo/Redo, with explicit Save and Build. Native controls, automation and MCP
use the same source task and application path. Unopened files, resource operations
and oversized previews are refused; this remains a current-context workflow.

## Authored navigation arcs

Navigation Link adds an optional world-space arc height. Straight defaults retain
their source and cooked forms. Arced links use the existing Query and Agent owners
with distance-based speed, exact entry/exit, normal holds and retargeting, and
validated private recovery. Inspector, Scene guides and existing automation/MCP
authoring/build operations use the same source contract. The editable arc Gallery
scene includes ready navigation and source-free export support. Matching C++ SDK
modules must be rebuilt for the expanded public component.
