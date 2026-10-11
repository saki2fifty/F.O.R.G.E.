# 2026-10-11

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
