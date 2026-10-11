# 2026-10-11

## Slider joints

PhysicsJoint now offers Slider with signed travel limits, body-local frames and
locked rotation. Inspector controls use existing Undo/save/reopen; selected Scene
guides show the travel interval and current signed distance. Original Gallery24
provides vertical and inclined carriages that can be played and exported without
C++ source. Legacy joint documents backfill separate travel defaults. Matching SDK
rebuilds are required; authored formats, ABI1 and private checkpoint4 remain unchanged.
Finite solver iterations can cause rebound with abrupt loads and far-offset anchors;
the manual describes this limit. Windows delivery and physical review remain pending.

## C++ contact observations

Project Code can read copied current-step native contact-cache Begin/End pairs
with canonical UUIDs and ticks. Repeated reads are nonconsuming; owner, fixed-step,
Physics capability and exact output-size checks apply. Retired generations and
prior ticks are unavailable. The existing physics list remains the owner and
transient observations are not replayed after fresh recovery. Sleeping/removal
can produce End; these are not geometric-overlap notifications.

The original editable PhysicsContacts project reacts to a sensor zone by queuing
a velocity reversal, with Inspector speed/reaction settings and runtime counters.
It includes source and a gate for matched compilation, pending-reaction recovery
and source-free export. Exact SDK modules require rebuilding; ABI1, authored
formats and private checkpoint4 remain unchanged. Normal physics and compiled SDK guards/body+character/cache recovery pass. The
original installed-SDK C++ examples pass with ASan/UBSan, independent reversal and
pass-through equations, true pre-contact/pending-reversal recovery and unchanged
authored scene. Four freshly rebuilt core sanitizer suites pass, including signed cooked
collisions/joints and true native recovery. Windows export execution remains
pending; this entry does not claim a new delivered ZIP or physical GPU acceptance.

Windows164 passed core/SDK/editor and source-free C++ loads export/startup but its
generic center-pixel capture assertion failed on the spaced scene. The saved
readback shows all six actors. The observer now checks actual colored actors
across physics scenes and retains its original center check for the center-cube
scene. New final-package execution remains required.

## C++ rigid-body velocity control

Project Code can queue world linear velocity in m/s and angular velocity in
radians/s for Dynamic bodies. Zero stops the selected channel while preserving
the other. Existing fixed-step command order, speed caps, damping, native sleep
policy and motion ownership apply. Kinematic movement keeps its target API.
The matching exact SDK adds a checked velocity-channel callback; rebuild project
modules after updating. ABI1 and authored Scene/Prefab versions remain unchanged.
Private exact-build physics recovery deliberately advances to version4 for the
two pending velocity tags; prior ephemeral recovery envelopes reject.

The original editable `PhysicsVelocity` project compares coasting and timed stops
of movement, rotation, or both. Local normal and compiled exact-SDK checks pass,
including independent full-quaternion motion, nondefault caps/damping, actual
sensor commands, copied input/owner/fixed/capability guards, and fresh-process
pending/timed-stop recovery. Both ordinary C++ examples also pass after their
modules are instrumented with ASan/UBSan. Four core sanitizer suites pass; the
final force/torque-order additions also pass a focused rebuilt policy check.
The combined Windows gate compiles and
packages the matching module and checks source-free export, production startup
and actual graphical motion. Native Windows execution of this batch and physical
GPU acceptance are pending; this entry does not claim a new delivered ZIP.

## Transform timeline local validation

The transform animation bundle is implemented and locally validated. Six focused
runtime/regression suites pass under ASan/UBSan. Mouse/keyboard timeline checks
pass at 65/100/200% UI scale, and all eight Gallery scenes preserve their authored
content through the headless editor API. Held keys and nonseamless loops use the
existing presentation snap mechanism; seamless motion stays interpolated.

The Windows-target Gallery clip is published and included by packaging. The
manual, CI selections, exact SDK header fingerprint and content/export adapters
are reconciled. No new Windows ZIP, Windows execution or physical acceptance is
claimed for this bundle.

## Two-clip skeletal transitions

Animator now has an optional second clip, a blend target and transition duration.
Changing the target from C++ fades from the currently evaluated mix. The two clips
share normalized phase and use pinned Ozz local-transform blending; pause, Step,
resource admission, model-node application and validated recovery retain their
existing owners. Single-clip documents use additive defaults. Rebuild native
modules against the matching SDK after updating.

Animation Preview opens as a central document with temporary playback, scrubbing,
blend controls and one-step application of settings to Inspector's Animator.
Preview uses a copied world and stops on game Play, scene edits, publication or
close. Feature Gallery scene 09 uses an original skinned column with two clips;
normal export discovers both cooked clip references and needs no source glTF.
Two-clip morph curves are diagnosed as unsupported. This bundle adds no animation
graph, retargeting or new language.

Local validation passed: 11 affected runtime/regression suites under sanitizers,
two shared C++ SDK suites, actual preview controls at 65/100/200% scale, the
existing timeline consumer, and admission/persistence of all nine Gallery scenes.
Source-free relocated playback is included in the numerical blending gate.

Windows execution and physical acceptance remain pending a separately authorized
delivery. No new Windows ZIP or GitHub push is part of this local batch.


## Named skeletal animation states

Reusable state sets now name clips and assign stable numeric IDs, an entry,
loop/speed and incoming fade time. C++ selects IDs on the existing Animator.
Ozz advances independent clip clocks, blends local poses and freezes the current
pose when a fade is interrupted. Short nonlooping clips finish their fade before
stopping. Existing direct/two-clip playback remains available.

The central Animation States document supports source history, Save/Publish,
reordering and atomic scene attachment. Animation Preview adds temporary named
state selection. Typed set dependencies participate in source reference review,
copy/rename and ordinary source-free export. Rebuild native modules with the
matching SDK after updating.

Gallery scene 10 contains actual CC0 Quaternius Idle/Walk/Sprint character clips,
three independent skins, editable state source, cooked Windows-target bundles,
license and immutable provenance. Editor package selection includes its GLB and
license. Windows execution/physical acceptance remains pending; no new ZIP or
GitHub push is part of this local batch.


## C++ animation timing markers

Animation States now author stable named timing markers with numeric payloads.
Incoming states produce fixed-tick observations across exact loop seams and
multiple loops; fades suppress the outgoing source. Pause, Step, disabled/zero
speed, interrupted switches, recovery and source-free export retain their existing
owners. Presentation snaps preserve the semantic marker interval. Copied preview
worlds mute gameplay delivery. Overflow faults explicitly without partial delivery.

The matching C-shaped SDK adds Animation capability and bounded current-tick
broadcast copies in PostPhysics. Rebuild modules with the matching SDK. Removed
actors are rejected after the ordinary Flecs deferred merge. The central editor
supports marker fields, source Undo/Redo and Save/Publish with IDs never recycled.

Gallery scene11 includes real-rig Walk/Run Pulse markers and an optional compiled
C++ System that alternates character height. Source and cooked artifacts, the C++
example and its setup guide are admitted by the single editor ZIP inventory. Prior
90 Gallery asset records are unchanged. No new third-party code or dependency pin.

Local validation: ten affected ASan/UBSan/LSan suites; two existing shared SDK
regressions; actual C++ marker copies/action, Pause/Step, stale-target and second-
process recovery; actual marker editor at65/100/200% UI scale; timeline regression;
ordinary publication/real-rig/source-free export; manual/package/hash/format checks.
Windows delivery of the preceding animation batches is running as build152 after
a CMake target-order correction. This marker batch is not included in build152.
Windows D3D12 execution and physical acceptance of markers are not claimed here.

## Animation playback status and live progress

- C++ reads realized clip/state, fixed time/progress, completion and fade/mix through
  the existing matching Animation service. Queries leave clocks unchanged.
- Live Inspector shows a read-only progress section. The Gallery includes an
  original one-shot return-to-Idle helper; it keeps no separate timer.
- Correct animation UI test registration and link dependencies. Local precise UI
  linkage no longer relies on a superset of cached libraries.
- Local validation: affected sanitizer9/9, ordinary state/real-rig/export2/2,
  matching SDK2/2, actual C++ completion and recovery, ImGui65/100/200% and timeline,
  manual3/3/package1/1. Matching modules require rebuilding. Windows delivery and
  physical acceptance are separate; failed152/cancelled153 produced no new ZIP.


## C++ character animation controls

- Matching C++ Gameplay Systems can Play, Pause and Restart admitted skeletal
  playback without editing saved Animator fields or keeping another timer.
- Runtime entity handles can produce a copied registered canonical UUID reference;
  stale, unregistered and ambiguous scene instances reject instead of guessing.
- Restart resets sampling history and ends a named fade; existing recovery retains
  pause/restart intent. Commands after Animation reject without mutation.
- The Gallery includes an original per-entity control helper and input-action setup
  guide. Rebuild C++ modules with the matching SDK. Windows acceptance is pending.

## Face inset and recessed panels

Editable Mesh now offsets a selected face inward by a uniform edge distance,
retains its inner face selection and creates a border. Signed extrusion raises
or recesses that face. Preview, Cancel, Escape and one-step source Undo retain
the existing editor document owner. UVs and colors interpolate through the
original face fan; invalid/collapsed operations leave source/history unchanged.
Mesh IDs remain reserved through Undo branches and save/reopen; this corrects
the existing documented no-reuse contract without changing the source schema.

Gallery scene12 contains three original framed/raised/recessed panel sources,
Windows-target cooked Meshes and checker Material bindings. Normal standalone
content uses the cooked Meshes without modeling source or tools. Windows
execution and physical acceptance remain subject to the delivery gate.

## Authored vector curves

Transform animation keys now support cubic position/scale curves with incoming and
outgoing slopes in units per second. The Timeline shows the selected-axis curve
and tangent guides; Apply creates one source Undo step. Preview, publication,
playback, recovery, copied assets and source-free export retain their existing
owners. Rotation keeps shortest-path quaternion interpolation; held keys retain
their curves while overriding outgoing motion.

Curve source/cooked version2 and importer recipe2 are explicit. Current readers
also admit selected version1 clips without rewriting them; older readers reject
new curve sources. Existing C++ modules need a matching SDK rebuild. Whole-segment
extrema validation rejects invalid overshoot atomically while preserving signed/
zero visual scale. Gallery scene13 compares the unchanged linear clip with original
eased and arc clips. Actual Timeline editing passes at65/100/200% interface scale;
legacy/new published Gallery playback and relocated source-free export pass locally.
Native Windows curve captures and physical acceptance remain pending.

## UV island authoring

The existing Editable Mesh UV workspace now shows whole-Mesh context and supports Corner/Face/Island canvas picking, multi-face selection, direct move preview/release, pointer zoom, pan and framing. Numeric signed nonuniform scale, Mirror U/V and padded aspect-preserving fit use one shared source operation. Exact authored seams stay separate. Drag cancel, source history, read-only locks and publication retain existing owners. Original Gallery14 demonstrates connected, split and fitted checker mappings. This slice adds no source schema, runtime code editor, automatic unwrap or atlas packing.

Local and native evidence is recorded separately; the next Windows delivery includes these changes after its full gate. Build155 contains the earlier completed batches through face inset, not the curve/UV changes.

## UV drag responsiveness

UV dragging now evaluates selected coordinates without repeatedly copying and
validating the whole source. Full source validation and one Undo step still happen
on release. On the same optimized Linux test host, actual pointer-movement frames
for the 4,096-vertex sample decreased from115ms to16.5ms; release remains about200ms
at that limit. This measures editor CPU work, not physical GPU/game FPS. Drawn
preview, invalid movement, cancellation, source parity and sanitizer checks pass.

## Sculpt and vertex-paint symmetry

Model brushes now offer local Mirror X/Y/Z controls and reflected hover rings.
Overlapping reflections retain the same strength. Parameters are captured once
per stroke, cancellation preserves the source, and release remains one Undo step.
The existing normal field guides sculpting; no topology mirroring or welding is
introduced. Gallery15 supplies original one-sided/mirrored sculpt and mirrored
paint examples through the existing published Mesh/render/export path.

Source, actual editor workflow, publication, resource and relocated export checks
are distinct from pending native Windows/physical acceptance. Build156 uses the
prior curve/UV source; the next combined delivery will include responsiveness and
brush symmetry after its validation gate.

## Authored triangulation

Model now offers Triangulate face and Triangulate Mesh. Both use the existing
source/history owner, preserve positions/colors/corner UVs, reserve new face IDs
without recycling and support one Undo step. Smooth normals follow the resulting
topology. Original Gallery16 compares polygons, equivalent triangles and a sculpted
triangle corner. Source and actual UI/publication/relocated export checks are
separate from pending Windows execution and physical acceptance.

- Corrected authored Mesh material slot bindings in Feature Gallery05/06/12/14 so
  their exported games resolve the intended materials. Added actual render material
  selector checks; runtime validation remains unchanged.

- Added flat/smooth shading controls for authored Meshes, with ordinary Undo and
  publication. Geometry and UVs remain unchanged. Feature Gallery17 compares the
  two policies on identical sphere geometry.

### Timeline key batches

- Ctrl-select animation keys, select all or clear the selection; selected diamonds
  use a distinct color. Move, duplicate or delete the selection as one source Undo.
- Drag selected keys together while retaining their spacing and complete pose,
  curve and held settings. Final release is sampled; Escape, focus, Play, close
  and document changes cancel the capture. Newly authored keys become selected.
- FeatureGallery18 compares original eased travel, a delayed group and duplicated
  motion. The normal runtime and relocated source-free export use the cooked clips.

### Connected Mesh facing

- Reverse an edge-connected surface or every Mesh surface without changing
  geometry, complete corner UV/extension association, colors or identities.
  Shared-edge winding and smooth normal validity remain enforced atomically.
- Model actions use one source Undo, clear changed UV corner scopes, obey Play/
  publication/preview locks and save/reopen through the existing Mesh source owner.
  Original Gallery19 compares outward and inward facing open-front rooms and their
  source-free runtime export.

## Windows validation corrections

Mesh and animation UI fixtures release all project writer leases before removing
scratch files. Native brush captures navigate the Model tab through real UI
input after UV review. Gallery brush reference comparisons allow only bounded
double-precision arithmetic variation; topology, identity and cooked-resource
checks remain exact. Windows execution is pending the replacement delivery gate.

## Model vertex selection

Vertex mode adds rectangle selection, Select All, Clear, Invert and a visible count.
Ctrl adds rectangle hits; Escape and view/document/Play/publication changes cancel
unfinished gestures. The clipped Vertex list remains usable at the4096vertex bound.
Existing source transforms/history/publication retain their correctness rules.
Clean Mesh window close releases its document before subsequent drawing. Recent
key-group, facing and selection suites are included explicitly in native CI.
Windows capture/physical acceptance is pending a later numbered delivery.

## Model view reuse

The Model workspace caches derived projections and face loops between source/view
changes. Same optimized4096vertex fixture idle CPU falls from15.8–17.1ms to about11.4ms;
selection/previews/brush/history/publication and independent geometry/invalidation
checks pass. No source schema/runtime/SDK ownership changes. Native delivery and
physical performance acceptance remain pending.

- Corrected automated native capture mouse transport so brush and vertex-selection
  setup clicks survive the platform input update; added a real-widget regression.

## Authored physics joints

Physics Joint connects two rigid bodies with a bounded distance tether or hinge.
The Inspector uses typed body pickers and ordinary Undo/Redo and save ownership.
Signed-scale and offset-center-of-mass anchors, body replacement, duplicate remapping,
prefab inheritance and native constraint-order recovery have focused coverage.
Gallery scene20 contains original swinging blue and amber examples and uses the
same source-free standalone export route as other scenes. Rebuild gameplay modules
against the matching SDK; private physics checkpoints now use version2. Durable
scene/prefab formats and native ABI1 remain unchanged.

Local runtime, actual scaled Inspector, export and SDK validation are recorded in
the batch report. New native joint captures and Windows standalone acceptance are
registered for the next numbered build; physical desktop testing remains pending.
Automated brush captures now preserve tool-menu keyboard focus while it is open.

## Point/fixed joints and Scene authoring guides

Physics Joint adds Point for freely rotating attachments and Fixed for explicit
body-local rigid frames, using the existing body references, signed-scale anchors,
solver lifetime and native-order recovery. Gallery21 demonstrates a point pendulum
and a fixed dynamic pair. The full Inspector shows each kind's relevant fields,
preserves inactive settings and opens enum navigation at the current selection.
Selected joint Scene guides show both anchors, axes and gap with invalid-reference
feedback; the visibility preference is independent of collision wireframes. The
core schema fixture is reconciled with the 26 builtin component types.

Normal/sanitizer solver, real relocated Gallery export and full Inspector evidence
are recorded in the batch review. Native Windows capture/EXE and physical acceptance
remain separate delivery gates; these notes do not claim a new verified ZIP.

## Optional continuous collision

Physics Body exposes Continuous collision for fast Dynamic solid bodies. It uses
the pinned native linear sweep, preserves the default discrete behavior and
retains the saved choice in inactive Static/Kinematic/sensor modes. Ordinary
Inspector editing, Undo/Redo, save, recovery and standalone export use existing
owners. Gallery22 compares a blue continuous cube and amber discrete cube above
thin platforms under ordinary gravity. This does not sweep all angular motion;
incompatible native collision-asset radii reject before physics mutation. Rebuild
native modules with the matching SDK because PhysicsBody's public layout changes.

Automated local and Windows delivery evidence is tracked separately from physical
desktop acceptance. These notes do not claim a newly verified Windows ZIP.
## C++ body forces and impulses

PhysicsService and the exact SDK add one-step force, impulse, torque and angular
impulse commands through the existing ordered body queue. Optional world points
use actual native center of mass and inertia. Shared bounds, Dynamic-role checks,
zero-vector sleep preservation, preflight, movement ordering and queued private
recovery are included. Private physics recovery deliberately advances to version3;
ABI1 and authored formats remain unchanged. Exact SDK modules require rebuilds.

The ordinary PhysicsLoads example uses reflected C++ Components and a fixed
System, with six cubes for mass response, one-shot kicks, spin and off-center
pushes. Packaging builds the matching module; exported games use compiled code.
Numerical, compiled module, sanitizer, Windows capture/export and physical
acceptance evidence remain distinct. This entry does not claim a new verified ZIP.

## Body damping, speed limits and sleep

Physics Body exposes linear/angular damping, linear speed limits, angular speed
limits in degrees per second, and sleep permission. Missing saved fields retain
the existing native defaults. The ordinary Inspector, Undo/Redo, prefab overrides,
recovery and source-free export use the existing owners. Disabling sleep wakes a
stationary body without applying motion; lowering limits clamps retained velocity
at replacement. Matching C++ SDK module rebuilds are required; ABI1 and authored
formats remain unchanged.

Compiler-free Gallery23 compares free fall, damping, speed limits and two resting
sleep policies. Local numerical, sanitizer, compiled SDK and native Windows/visual
acceptance evidence remain separate; this entry does not claim a new verified ZIP.

## C++ rigid-body motion observations

C++ can read copied native body COM, linear/angular velocity, motion kind and
activity, or velocity at an absolute world point. Reads preserve sleeping state
and pending commands, and unavailable/retired identities return no state. Angular
observations use radians/s. The existing Physics owner, fixed/owner/capability
boundaries and exact SDK fingerprint admission remain authoritative; rebuild
project modules with the matching SDK. Authored formats and ABI1 are unchanged.

The original PhysicsLoads example logs six actual first-step speeds once. Its
compiled consumer checks those speeds independently under defaults and nondefault
tuning, plus existing fresh-worker recovery. Normal body/point math and mirrored
cooked COM, compiled SDK guards and capability denial are locally validated.
Native Windows acceptance of this source remains pending the combined delivery.

Actual Windows163 package acceptance exposed CLI export selecting an adjacent
static schema runtime for C++ projects. The CLI now accepts the existing exporter's
inspection runtime explicitly; combined C++ export selects the matching SDK
worker. Fingerprint checks and isolated loading remain intact. The correction
requires the next real Windows export/source-removal/render gate; no163 ZIP was
delivered.


C++ physics volume queries now return copied sorted unique object identities for
Box, Sphere, Capsule and Cylinder. Existing layer/sensor filters, owner/fixed
capability guards and bounded complete-result admission apply. Sleeping bodies
are observed without waking; query results add no persistent overlap manager or
checkpoint format. The original editable PhysicsVolumes project demonstrates
current object counts independently of transient contact-cache events. Matching
SDK module rebuilds are required. Local validation and native Windows delivery
are tracked separately; Windows acceptance for this source is pending.
