# Two-clip skeletal blending

The existing `forge.animator` component optionally blends two clips. AnimationRuntime
continues to own clocks, CPU resources, model-node application and recovery. No new
manager, graph asset, dependency or gameplay language is introduced.

## Authored configuration and C++ control

Animator keeps its existing skeleton, primary `clip`, enabled/play-on-start/loop
and speed fields. Additional fields are `blend_clip` (nullable AnimationClipAsset),
`blend_weight` (0–1 target, default 0) and `transition_seconds` (0–10, default .25).
Old documents load with these defaults. Inspector and normal property commands
edit them; scene/prefab persistence, atomic history and asset reference discovery
use the existing reflected schema. Direct C++ component changes use the same
runtime validation. A nonzero target requires a second clip.

`blend_root` is a nullable durable ModelNodeAsset reference displayed as **Blend
subtree**. Null preserves full-skeleton blending. A selected joint restricts the
second clip to that joint and all descendants; other joints retain the primary
clip. It requires a second clip and durable imported skeleton metadata. Foreign,
missing and non-joint nodes fail admission before changing the selected pose.
The existing last-good player remains owned and may advance on later ticks. Names and
source array positions are never bindings. Named-state Animators exclude this
direct blend configuration; attaching a state set clears it in the existing
authoring transaction. Older scenes load null without a scene-format change.

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

For a selected subtree, the existing admitted parent hierarchy produces one
cached binary mask per player/configuration. Complementary native joint weights
are `(1 - mix * mask[j], mix * mask[j])`; their sum is one, preserving the primary
pose outside the subtree even at mix1. The same two sampling jobs and single
LocalToModel pass remain. The existing union of both explicit channel sets retains
ownership, including
secondary-only paths. Outside the mask those paths sample the primary rest value,
so changing a subtree cannot leave the previous secondary pose behind. Paths
absent from both clips retain authored values.
Root motion uses the selected root's effective mean interval weight, so an
upper-only blend cannot replace base locomotion. The same admitted hierarchy filter
applies to animated morph nodes; a mesh outside the selected subtree keeps its
base weights.

Changing a valid subtree preserves phase and fades and snaps presentation at the
next successful pose application. Recovery derives the mask from the saved typed
component and admitted exact skeleton; no serialized mask indices or new checkpoint
version are introduced. C++ can update `Animator::blend_root` through the existing
component path; the changed physical layout/exact fingerprint requires SDK rebuild.

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
owners. Morph curves use the explicit node union and exact admitted per-node defaults
for a missing curve. Two-clip morph sampling shares normalized phase and evaluated
mix with the skeletal pose. Nodes outside the union retain their usual defaults.

## Additive overlays

`blend_mode` is a reflected enum: Blend0 (backward-compatible default), Additive1.
Additive requires a second clip. Named-state configuration excludes this direct
mode along with the other manual mix fields; attaching states clears them in the
existing transaction. Mode changes preserve phase/fade and snap the next complete
pose application. Exact SDK/component layout changes require native module rebuild.

An ordinary absolute second clip is converted relative to the admitted skeleton
rest pose: `deltaT = sourceT - restT`, `deltaQ = inverse(restQ) * sourceQ`, and
`ratioS = sourceS / restS`. The native base layer keeps weight1; Ozz additive_layers
applies weight `mix * subtree[j]`. Translation offsets add in parent local axes;
rotation is `baseQ * nlerp(identity, deltaQ, weight)` with shortest-hemisphere
normalization; scale is `baseS * (1 + weight * (ratioS - 1))`. One LocalToModel pass
remains. Signed scales are valid. A zero reference scale with unchanged zero uses
identity ratio; a selected nonzero offset from zero is undefined and rejects that
candidate before ECS writes. Excluded joints and weight0 have identity offsets.

Additive morph weights are `base + weight * (secondary - modelDefault)`; ordinary
Blend uses `(1-weight)*base + weight*secondary`. Evaluation uses doubles and checks
finite float storage before returning the pose. Weights are not clamped to [0,1],
preserving signed/extrapolated morph semantics. The explicit morph node union is
bounded to the existing 1024-track profile, with at most256 targets per node.

Root motion follows [018](decisions/018-animation-root-motion.md): additive mode
composes the base interval with the secondary interval weighted from identity,
then applies rest-root conjugation. A filtered-out root contributes no secondary
motion. The raw roots are captured before the final pose is neutralized, preventing
double application. This opt-in interval policy retains existing Blend behavior.

Both samplers and resource leases live with their Animator. Second-clip admission
is complete before either resource pair is adopted. Pending or failed replacements
retain the previous admitted playback and diagnose the desired invalid selection;
invalid desired configuration cannot produce a recovery checkpoint. Budgets remain
256 players and 8192 evaluated joints, counting a dual-sampled skeleton twice.
The importer, optimized Ozz format and asset catalog schema are unchanged.

## Recovery and preview

Private animation checkpoint entries retain a `blend` object: secondary clip,
exact revision, evaluated weight, start weight, elapsed seconds, target and duration.
Recovery validates bounded finite values, progress consistency, configuration and
all exact revisions, then samples candidate poses before replacing any clocks.
Older entries without `blend` remain valid for a single-clip zero-mix configuration.
Named-state checkpoint v2 adds optional `frozen_morphs` beside its frozen local pose.
Interrupted transitions freeze the complete evaluated expression. Recovery validates
the exact explicit node union, target counts and finite values before adopting any
clock or source. Old non-morph state checkpoints remain readable; a morph transition
requires its complete frozen expression. Restart and completed fades clear both
frozen sources. Independent outgoing seconds use the same presentation interpolation
as outgoing skeletal sampling. No persistent asset or authored scene version changes.
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
Original Gallery38 OverlayColumn compares base, Blend and Additive, including signed
morph curves. Gallery39 uses the same model with Base/Overlay/Rest automatic states;
Rest has no morph curve and demonstrates model-default fallback. Both export through
ordinary saved-scene dependency closure with no source glTF or compiler requirement.
See the [user walkthrough](../manual/editor/animation-blending.md).
