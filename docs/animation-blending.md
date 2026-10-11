# Two-clip skeletal blending

The existing `forge.animator` component optionally blends two clips. AnimationRuntime
continues to own clocks, CPU resources, model-node application and recovery. No new
manager, graph asset, dependency or gameplay language is introduced.

## Authored configuration and C++ control

Animator keeps its existing skeleton, primary `clip`, enabled/play-on-start/loop
and speed fields. Additive fields are `blend_clip` (nullable AnimationClipAsset),
`blend_weight` (0–1 target, default 0) and `transition_seconds` (0–10, default .25).
Old documents load with these defaults. Inspector and normal property commands
edit them; scene/prefab persistence, atomic history and asset reference discovery
use the existing reflected schema. Direct C++ component changes use the same
runtime validation. A nonzero target requires a second clip.

At realization, the authored mix is applied immediately. A later target or duration
change records the current evaluated mix as its start. During enabled, playing
fixed updates with nonzero speed, elapsed simulation seconds advance toward the
configured duration. The evaluated mix is start + (target − start) × elapsed/duration.
Zero duration changes the selection immediately and snaps presentation on its
next successful fixed pose application. Until then, the model draw retains its
last valid pose. Interrupted fades start
from the current mix. Pause, disabled playback and speed zero hold both phase and
mix; Step executes one fixed update. Nonloop playback holds the primary end pose
and stops its transition clock. Changing Play on Start does not restart a player.

Both clips share a normalized phase. Primary seconds determine loop/clamp and
phase = primary_time / primary_duration. The second clip samples this same ratio,
so different durations do not cause phase drift. Speed controls phase; transition
duration is measured in simulation seconds and is not multiplied by speed.
Presentation samples phase and evaluated weight between the same fixed boundaries;
reads never advance authoritative state.

## Sampling and binding

Ozz 0.17.0 at commit `744eb9d99f606eda849acb0b1204f7a3dc20bca1` supplies
SamplingJob and BlendingJob. The two local SoA samples use weights (1 − mix, mix),
then one LocalToModelJob composes the result. Native blending linearly mixes
translation/scale and normalizes a shortest-hemisphere quaternion weighted sum.
It is normalized quaternion interpolation, rather than spherical interpolation.
The rest-pose threshold is the pinned default .1; total supplied weight is one.
No matrix decomposition or interpolated world-matrix blending is used.

Both clips must use the same exact skeleton identity/revision. Model clips must
also belong to the same model and publication generation; existing durable joint
AssetIds and skin/inverse-bind correspondence remain authoritative. The union of
both explicit transform-channel sets is prepared once at resource adoption.
Within that union, a missing channel uses its converter-provided rest value. Paths
outside the union retain current authored/local values. Model instance resolution,
complete-pose transform/physics validation and presentation snaps reuse existing
owners. Two-clip morph-weight curves are explicitly rejected in this skeletal
bundle; single-clip morph playback remains unchanged.

Both samplers and resource leases live with their Animator. Second-clip admission
is complete before either resource pair is adopted. Pending or failed replacements
retain the previous admitted playback and diagnose the desired invalid selection;
invalid desired configuration cannot produce a recovery checkpoint. Budgets remain
256 players and 8192 evaluated joints, counting a dual-sampled skeleton twice.
The importer, optimized Ozz format and asset catalog schema are unchanged.

## Recovery and preview

Private animation checkpoint v1 entries add a `blend` object: secondary clip,
exact revision, evaluated weight, start weight, elapsed seconds, target and duration.
Recovery validates bounded finite values, progress consistency, configuration and
all exact revisions, then samples candidate poses before replacing any clocks.
Older entries without `blend` remain valid for a single-clip zero-mix configuration.
The exact-SDK header fingerprint changes with Animator's added fields; native
modules must be rebuilt against the matching SDK. The SDK ABI version is unchanged.

The central Animation Preview document borrows a SkeletalAnimationPreview host
consumer. It owns one copied runtime world with the selected Animator and no
physics/gameplay systems. Resource preparation is polled; scrub uses validated
checkpoint reconstruction, and playback advances the copied AnimationRuntime
using preview frame seconds. Games keep their fixed simulation phase. Both use
the same local-pose evaluator at a given phase and mix.
The copied snapshot includes the ordinary model pose/ready metadata and derived
local/world transforms. No source/history/authoring world is written. Stop, Play,
close, target deletion, scene edits, imported asset publication or project/scene
changes discard preview.
Viewport authoring gestures and competing transform-timeline preview have one owner.
Applying preview settings uses one revision-guarded scene transaction.

## Runtime content and example

The reflected `blend_clip` field makes the second clip reachable through the
normal saved-scene export scanner. Only cooked model/skeleton/clip data ships in
the runtime folder; source glTF, preview tools and compilers are not required.
The original Gallery BlendColumn has Idle (two seconds) and Reach (one second)
clips. Reach also animates joint translation, exercising rest-value mixing where
Idle has no translation channel. Scene 09 places three independent model instances
at mixes 0, .5 and 1.
Their source and admitted `.ozz` artifacts are included in editor ZIP assembly.
See the [user walkthrough](../manual/editor/animation-blending.md).
