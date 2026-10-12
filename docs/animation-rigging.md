# Skeletal reach and aim

See [ownership decision019](decisions/019-skeletal-constraints.md). TwoBoneIK
(`forge.two_bone_ik`) and AimIK (`forge.aim_ik`) are optional reflected C++ components
on an imported model holder with an Animator. They work with direct clips, two-clip
Blend/Additive and named-state poses. They do not create another gameplay language,
entity hierarchy, clock or resource manager.

## Fields and input space

Both components default disabled, weight1, twist0. `target_x/y/z` are model-space
meters, finite within±1000000. `pole_x/y/z` is a model-space direction, not a pole
position. Weight is finite[0,1]; twist degrees is within[-360,360]. Direction
components are finite±10000 and active directions must have squared length≥1e-12.
The adapter normalizes directions before calling the pinned native jobs.

Reach selects `start`, `middle`, `end`, all durable joints in a direct chain in the
same exact skeleton. `axis_x/y/z` is the middle joint's local bend axis; positive
rotation opens the angle. `soften` is finite[0,1], default1 (full extension).
Unreachable targets extend as far as the selected job/softening allows. This is
positional two-bone IK; it does not set a separate endpoint orientation.

Aim selects `joint`, local `forward_x/y/z` and `up_x/y/z`, with nonparallel forward
and up directions. It rotates forward toward the target and aligns up toward the
pole plane. Default forward+X/up+Y. One aim follows one reach, allowing an endpoint
to orient after the limb reaches. It is a single-joint target, not a multi-source
aim constraint or iterative full-body solver.

## Evaluation and failure

No active solve for disabled components or weight0. Nonzero constraints bind exact
joint assets and check effective ancestry before evaluation. Clip union plus
constraint rotation paths samples an owned base; unanimated translation/scale and
other rotations come from ECS. A constrained rotation absent from clip curves uses
admitted rest. Removing a constraint releases its rotation after one base reset.

Native corrections pass through signed local scale, right-multiply the sampled
local rotation, normalize, and recompute model matrices. Active joint bases must
have uniform scale magnitudes and orthogonal axes; mirrored uniform scale is
supported. Nonuniform scale or shear cannot preserve this solver's rotation/scale
contract and rejects the operation. The holder's world scale is independent.
Reach precedes aim. Complete prepared ECS transforms
and downstream physics validate before any write. A selected root-motion joint
cannot be corrected by IK. Missing/foreign/nonchain nodes, singular/ill-conditioned
joint matrices, zero-length bones, a near-zero target direction or target parallel
to pole reject the candidate with the existing animation binding diagnostic.
The previous complete ECS pose is retained; an invalid requested pose is not
advertised as ready. Visual scale remains admitted independently of IK.

Numeric C++ target/weight/softening/twist updates keep animation phase and derived
joint indices. Presentation interpolates those values when layout/axes/pole match;
if the intermediate native sample has no valid inverse or bend plane, the native
pose/morph metadata uses the admitted current fixed sample. Ordinary rendered
joint transforms retain the existing fixed-pose interpolation. No presentation
fallback writes gameplay transforms or advances clocks.
Identity/layout and unanimated base-transform changes snap. Constraints sample from a fresh clip/state expression,
so repeated presentation/preview never accumulates weight. State interruptions
freeze the pre-constraint expression; recovery reconstructs constraints from scene
components alongside existing exact resource/clocks and outgoing/frozen sources.
Cold recovery validates and prepares derived rig slots before their adoption,
so its first presentation includes constraints without requiring an initial
fixed tick or new serialized binding data. It retains existing renderer readiness
until the next successful fixed pose application.

`CodeExamples/SetRigTarget.hpp` demonstrates ordinary typed C++ value updates from
the fixed gameplay phase. The SDK fingerprint changes; rebuild against the
matching SDK. Exact Ozz744eb9d is unchanged; native solver objects/pointers remain
private. Existing baseline samplers retain one LocalToModel; active IK adds work
for merging current ECS locals and each correction. No graph/IK work is scheduled
for unconstrained actors.

Original Gallery40/41 reuse FORGE's own admitted OverlayColumn, showing weights0,
.5,1 for reaching and aiming. They export through the saved-scene runtime closure.
See [walkthrough](../manual/editor/animation-rigging.md). Native Windows visual and
physical acceptance is pending separately from local runtime/export tests.
