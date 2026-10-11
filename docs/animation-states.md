# Named skeletal animation states

Animation States is a reusable list of named clips. C++ selects an integer ID on
an existing `forge::Animator`; the incoming state's duration controls the fade.
It has no condition language, automatic transitions, layers, masks, retargeting,
rig editor or runtime editor. Existing single-clip and two-clip Animators retain
their behavior when `states` is empty.

## Source, identity and ownership

`Assets/*.states.json` uses `forge.animation-states`, version 1. It stores
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
`forge.animation.states.v1`. Skeleton and every state clip are typed Runtime
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
are diagnosed as unsupported for state sets; single-clip morph playback remains
available.

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
interpolation. Interrupted transitions freeze the current evaluated local pose
and blend from that pose to a new incoming clip. This keeps continuity and
bounds work to two layers, regardless of interruption count. No world-matrix
interpolation or transform decomposition is introduced. Existing player/joint
budgets, copied preview world, model instance bindings and complete-pose
validation remain the owners.

Replacing a set/revision or model revision admits a fresh playback selection and
uses the existing presentation discontinuity mechanism. It does not fade across
asset revisions. Missing/deleted IDs, bad assets or incompatible bindings retain
the last admitted playback and emit a diagnostic. They block a new checkpoint
until the desired configuration is valid.

## Recovery, preview and export

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
compiler and editor are unnecessary at runtime. C++ modules must rebuild with
the matching SDK after the public Animator layout/header change. Gameplay only
needs Animator's typed set reference and stable ID; no Ozz pointer or pose loader
crosses the native ABI.

Gallery scene 10 uses the CC0 Quaternius Idle/Walk/Sprint clips and independently
skinned instances. Provenance is recorded beside the source. See the
[user walkthrough](../manual/editor/animation-states.md).
