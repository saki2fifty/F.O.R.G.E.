# Named skeletal animation states

Animation States is a reusable list of named clips. C++ selects an integer ID on
an existing `forge::Animator`; the incoming state's fade duration controls the change.
A state can also advance automatically at a normalized exit time.
It has no condition language, layers, masks, retargeting,
rig editor or runtime editor. Existing single-clip and two-clip Animators retain
their behavior when `states` is empty.

## Source, identity and ownership

`Assets/*.states.json` uses `forge.animation-states`, source version 1 or 2.
New documents use 2. Version 1 remains manual, including any opaque `exit` extension. It stores
`asset_id`, `skeleton`, `entry`, `next_id` and `states`. Each state stores `id`,
`name`, `clip`, `loop`, `speed` and `transition_seconds`. IDs are positive uint32
values, unique within this asset. Names contain 1–64 printable bytes and are
unique. There are at most 32 states; speed is 0–4 and fade is 0–10 seconds.
`next_id` exceeds every existing ID and advances on addition. Reordering and
renaming preserve IDs; removing a state never makes its ID available again.
Source Undo preserves the allocation high-water mark, including after Save; Redo
restores the original row identity. Ordinary edits reject previously allocated IDs.
Duplicating the asset changes its asset identity and preserves these local IDs.
An empty draft is editable but cannot publish. A nonempty set needs a skeleton
and a valid entry. Animator state zero selects that entry.

The central document uses `AssetSourceDocument`, its source writer ownership,
revision guards and history. Existing `AssetImportService` publishes one bounded
`states.json` artifact and catalog metadata `forge.animation_states` through
`forge.animation.states.v4`. Cooked version 2 wraps the original source
document as `document`; source version and cooked version are independent.
Legacy raw cooked version 1 remains readable. Source remains bounded to 256 KiB;
the cooked envelope permits 128 additional bytes. Skeleton and every state clip are typed Runtime
dependencies with selected recipe revisions. They must belong to one imported
model owner and exact publication generation. Source discovery, copy/rename,
reference-impact review and runtime export use their existing owners. Unknown
source extensions stay opaque and are marked uninspected during reference review.
The runtime resource pool validates the selected recipe, artifact hash, size, version
and asset identity before adoption. It retires unused set requests.

This exercises F37's stable identity/document/evaluator boundary with a list
selector. It does not claim the broader graph editor. Existing exact joint,
model and transform contracts remain authoritative.

## Selection and evaluation

Animator adds optional typed `states` and uint32 `state` fields. Its playback
speed multiplies the state's speed. The set owns clip, loop and incoming fade;
manual second-clip blending cannot also be selected. An explicitly assigned
Animator skeleton must match the set. All clips are admitted before adoption,
with exact rig/model checks and the union of explicit transform channels prepared
once. Paths absent from that union retain ordinary authored values. Within the
union, a missing clip channel uses its converter rest value. Morph-weight curves
mix through the admitted node union/defaults, including interrupted fades; see
[blending](animation-blending.md). Optional [reach/aim](animation-rigging.md)
constraints apply after the mixed skeletal expression.

A valid change takes effect on the next positive fixed tick. The incoming clip
starts at zero. The outgoing clip retains its phase and advances with its own
loop and state speed, multiplied by the current Animator speed. Fade progress
uses fixed seconds independently of clip speed. A zero fade switches immediately.
Selecting the same effective ID preserves playback time. Disabled Animators,
Pause and zero playback speed hold clocks and fade; Step uses the same fixed
pipeline. If a nonlooping incoming clip ends before the fade, its endpoint is held
until the fade finishes, then playback stops.

The pinned Ozz sampler evaluates local TRS. A transition supplies outgoing weight
`1-w` and incoming weight `w` to Ozz `BlendingJob`, followed by one LocalToModel
job. Native quaternion keys and mixed rotations use normalized linear
interpolation. Interrupted transitions freeze the current evaluated local/morph
expression before constraints and blend from it to a new incoming clip. This keeps continuity and
bounds work to two layers, regardless of interruption count. No world-matrix
interpolation or transform decomposition is introduced. Existing player/joint
budgets, copied preview world, model instance bindings and complete-pose
validation remain the owners.

Replacing a set/revision or model revision admits a fresh playback selection and
uses the existing presentation discontinuity mechanism. It does not fade across
asset revisions. Missing/deleted IDs, bad assets or incompatible bindings retain
the last admitted playback and emit a diagnostic. They block a new checkpoint
until the desired configuration is valid.

## Timed exits

Source version 2 optionally adds `exit: {"state": 2, "phase": 0.75}` to a state.
Missing/null disables it. Target is another existing stable ID; phase is finite
and .001–1. Cycles are allowed. Invalid/deleted/self targets reject publication.
The source editor converts a version-1 document through ordinary Undo history;
a non-null opaque legacy `exit` must be renamed before conversion.

At a positive playing fixed tick, unwrapped clip advance crossing
`phase * duration` selects the target once after publishing source markers.
Incoming time is zero, using the destination's fade. No residual time or multiple
hops are consumed. Loop wraps and a nonlooping endpoint can trigger an exit.
Pause, disabled Animator, zero speed, pending assets and seek without advancing
cannot trigger it. A manually resumed player preserves playback even when its
original `play_on_start` was false. Manual C++ selections use the same admission path.

Only a fully admitted candidate updates the runtime Animator state. The editor's
authored world stays unchanged. Failed targets retain the old player/component,
latch the rejected configuration and block checkpoint readiness; they do not retry
on every tick. An explicit manual selection or catalog refresh can retry. Final
source markers retain their source state; target entry markers arrive on its next
advancing tick. Repeatedly forcing a different ID from C++ overrides the automatic
selection in the ordinary next gameplay phase.

## Recovery, preview and export

Private animation checkpoint 2 additionally records the admitted raw
`requested_state`, including zero for entry. A partial prefab field override must
not become an authored state override merely to recover an automatic selection.
Detached existing playback candidates, aggregate evaluated-joint budgets and the
complete pose are validated before adoption; rejected restore retains the previous
players, clocks, typed selection and transforms. Legacy private checkpoint 1
retains its original strict scene-selection semantics. These checkpoints are
ephemeral recovery, not a public save-game format.

Recovery preserves the set identity/revision, selected ID, incoming time, live
outgoing ID/time/speed or frozen local pose, and fade progress. All candidates,
finite clocks, source poses, exact revisions and complete model bindings are
validated before adopting clocks or transforms. Dependency admission shares a
single bounded recovery deadline. Existing direct-clip recovery remains readable.

Animation Preview selects names temporarily in a copied world. Its explicit
state command can change the copied selection without ticking gameplay or
advancing time. Scrub and Play operate on the selected clip; a scrub during a
fade preserves the transition source/progress. Apply writes state and Animator
speed as one scene Undo step. The source editor attaches the set and entry in one
scene transaction. Play, source publication, scene edits and close release preview.

Export follows the set's typed dependencies, including clips not selected in the
startup scene. It ships admitted cooked data; the original GLB, state source,
compiler and editor are unnecessary at runtime. C++ modules use the matching SDK. Timed exits do not change the installed SDK
function table, Animator layout or fingerprint. Gameplay only
needs Animator's typed set reference and stable ID; no Ozz pointer or pose loader
crosses the native ABI.

Gallery scene 10 uses the CC0 Quaternius Idle/Walk/Sprint clips and independently
skinned instances. Provenance is recorded beside the source. See the
[user walkthrough](../manual/editor/animation-states.md).

Gallery scene 30 cycles Idle → Walk → Run automatically through the same clips.

Optional [timing markers](animation-markers.md) attach named observations to these clips.
