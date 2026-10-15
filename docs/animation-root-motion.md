# Imported animation root motion

Animator's `root_motion` is `0` Off, `1` Apply, or `2` Extract. Off is the default
for existing documents. `root_joint` is a nullable ModelNodeAsset reference:
null selects the unique imported skeleton root. An explicit reference must match
a root of the exact admitted skeleton. Multiple roots require a selection;
missing instance nodes, foreign nodes and non-root joints reject the binding.
Legacy animation-only assets have no durable model-node binding and require Off.

## Pose and displacement

The existing Ozz sampler captures raw root translation/rotation for each layer,
then replaces those channels with the admitted root rest pose before its one
LocalToModel pass. It preserves sampled root scale. Both fixed ECS node writes
and interpolated render poses use this policy. Reordered presentation samples
do not change authoritative movement or its cached fixed sample.

The existing Animator clock supplies the interval. Per-clip endpoint rigid
transforms are cached with the admitted clip lease and root index. A matching
previous fixed sample is reused; endpoint and reanchoring samples occur only
when the cache is first prepared or the phase/binding changes. Complete loop
transforms compose in order, using bounded binary exponentiation for multiple
cycles. Wrapped endpoint subtraction would discard movement and turning.
Seek, clip replacement and Restart begin from their new phase without applying
the jump from an unrelated previous sample. Paused, completed and zero-speed
players emit identity displacement when admitted and enabled.

Two-clip and state transitions blend per-layer deltas at the arithmetic mean of
the interval's previous/current blend weights. Independent outgoing clocks retain
their own speed and loop policy. A frozen outgoing pose contributes identity
motion. Conjugating the blended rigid delta by the admitted root rest transform
expresses the result in the model holder's local coordinates before actor scale.
Translation is meters; rotation is a unit x/y/z/w quaternion.

Blend Spaces select their pair or triangle before the interval and hold its
normalized weights constant for that interval. In2D, at most three per-clip
deltas are accumulated in stable sample order: weighted translation and the
existing root slerp at each new weight divided by accumulated weight. Each
clip maps the shared cycle phase to its own duration. Changing either coordinate
does not contribute a pose jump as movement, and paused edits emit no interval.

## Application and physics

Apply prepares `actor_local_affine * delta_affine`, then uses the existing signed
TRS decomposition with the previous actor transform as its continuity hint.
The result must preserve actor scale. Shear or a scale-changing representation
rejects the entire pose transaction before an ECS channel changes. Pure
translation follows current actor rotation and scale. Parent/world transforms
continue through the existing spatial evaluator. Signed and zero scales remain
valid visual transforms; particular turning operations may be unrepresentable.

Actor movement and neutralized joint writes form one candidate. Existing physics
validation runs before publication. Extract makes no actor write and publishes
the same rigid delta, allowing a controller to choose how physics should respond.
There is no automatic creation of a PhysicsBody and no second movement manager.
Apply also rejects an enabled Navigation Agent with an active destination on
the same holder. Existing TransformAnimator conflict checks prevent it from
replacing an Animator's holder or imported node pose.

## C++ and recovery

`AnimationService::root_motion(EntityRef)` returns a copied `AnimationRootMotion`
with actor/joint/clip identities, tick, translation, rotation and `applied` flag.
Reads do not consume it. An unavailable, disabled, pending or failed binding
returns no observation. Automatic state exits preserve the delta of the clip
that actually ran during the completed interval.

The exact SDK provides `ForgeSdkAnimationRootMotionV1`, `animation_root_motion`
and `post_animation_phase`. `sdk::Client::animation_root_motion` initializes the
size-checked DTO. Return values are 1 available, 0 unavailable and -1 invalid
argument/context. The host requires matching owner-thread fixed execution,
Animation permission/provider, and observation tick equal to the current input
tick. Valid-sized unavailable outputs are cleared. Gameplay before Animation
cannot read the previous tick as a current delta. PostAnimation runs before
Navigation and PrePhysics, so a controller can queue existing physics commands.

Exact SDK header/table/component changes require C++ rebuilds. No Ozz object,
sampling context or ownership crosses the SDK boundary. Recovery reconstructs
derived caches from existing admitted playback clocks and restored scene poses;
it does not apply movement already represented by restored actor transforms.
Transient observations are not authored, saved as game state, or checkpointed.

See [decision 018](decisions/018-animation-root-motion.md),
[animation](animation.md), [blending](animation-blending.md),
[states](animation-states.md), and [physics](physics.md).
