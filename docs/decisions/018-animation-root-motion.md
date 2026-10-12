# 018: Animation root motion has explicit application authority

Date: 2026-10-12. Status: accepted for the root-motion implementation batch.

## Context

Imported animations can translate and rotate a skeleton root. Leaving that
movement entirely in the skeleton makes the visible character move away from its
entity, camera and collision representation. Applying it both to the skeleton
and entity doubles the motion. Physics already owns solver-controlled bodies.

## Decision

AnimationRuntime owns extraction, using its existing fixed playback intervals
and the exact admitted model/skeleton/clip revision. Animator selects Off, Apply
or Extract. Off retains existing behavior. Apply proposes entity movement in the
existing complete pose transaction. Extract publishes a non-consuming fixed-tick
observation for a C++ consumer. Both enabled modes neutralize the selected root's
translation and rotation to its admitted rest pose before LocalToModel; scale
remains animated. Presentation uses this same pose policy without advancing motion.

The root is an imported skeleton root identified by a durable ModelNodeAsset ID.
Null selects a unique root. Names and array indices are never persistent bindings.
Root-motion support does not change the model instance's identity or hierarchy.

Loop movement composes rigid transforms across complete cycles, including turns.
Each layer computes its own displacement; transition motion blends those deltas
at the mean of the interval's previous and current weights. Frozen outgoing poses
contribute identity motion. Rest-root conjugation converts the resulting delta
to model-holder local coordinates before entity scale.

Direct Additive mode extends this interval rule explicitly: retain the full base
delta, compose it with the secondary delta weighted from identity at the same mean
interval weight, including the subtree root mask, then perform rest conjugation.
This treats secondary motion as an additional local displacement rather than a
replacement locomotion source. Blend mode and state fades retain the existing
weighted interval rule. Both modes neutralize root translation/rotation only after
raw per-layer capture, and preserve the existing complete pose/physics transaction.

Apply composes the exact actor local affine with this rigid delta, uses the
existing signed TRS decomposition with its continuity hint, and requires the
actor's scale to remain unchanged. Unrepresentable shear or scale changes reject
the complete candidate. This is an operation-specific restriction, not a blanket
ban on signed, zero or nonuniform visual scale. Existing physics candidate
validation retains body ownership; root motion cannot bypass it.

RuntimeSimulation adds PostAnimation between Animation and Navigation/PrePhysics.
The exact C++ SDK exposes that phase and a size-checked current-tick copied delta.
A consumer can queue physics movement before PrePhysics. There is no new clock,
pose owner, scene database or native Ozz pointer in the SDK. Exact public-header,
component-layout and callback-table fingerprint changes require module rebuilds
under [014](014-extensions.md).

Recovery retains realized scene transforms and existing playback clocks.
Derived extraction samples are rebuilt, and pre-checkpoint movement is not replayed.
Authored scenes never store transient deltas or extraction caches.

## Consequences

Direct visual movement and physics-directed movement have explicit authority.
Legacy animation-only clips remain supported with Off; extraction requires
durable imported-model metadata. Scaled actors that cannot represent a turning
root delta must use Extract and an explicit controller policy.

Implementation and platform acceptance remain distinct. See the batch's tests,
[root-motion contract](../animation-root-motion.md), and delivery evidence.
