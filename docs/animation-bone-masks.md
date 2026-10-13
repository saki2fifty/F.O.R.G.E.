# Reusable animation bone masks

`AnimationMaskAsset` is a reusable typed asset for the second layer of an existing
Animator. `blend_mask` is nullable and defaults to null in older scenes. It applies
to direct Blend and Additive modes; named states exclude direct blending, and the
existing state-set attachment transaction clears the mask. The public Animator
layout changes the exact SDK fingerprint. ABI1 remains; rebuild C++ modules with
the matching SDK.

## Source and identity

Content creates and opens `.mask.json` through the existing source-document and
document-workspace owners. `AnimationMaskSource` format `forge.animation-mask`,
version1, contains `asset_id`, an imported `skeleton` reference, `default_weight`
and `entries`. Each entry has a durable `joint` ModelNodeAsset reference, numeric
`weight` within0–1 and boolean `descendants`. At most1024 entries and256KiB of
source are admitted. The editor permits an incomplete skeleton selection as a
draft; cooking requires a current imported skeleton. Duplicate, missing, foreign
and non-joint references fail before publication. Display names and source array
indices do not bind bones.

For each admitted joint, an explicit entry overrides inherited influence. An
entry with descendants enabled supplies its weight to children unless a more
specific entry overrides it. A joint-only entry changes itself and lets its
children inherit the earlier descendant-scoped ancestor or default. Source row
order does not change the result. A bone's influence is multiplied by the current
global blend weight and by the existing binary Blend subtree filter. Primary and
secondary normalized clip phases remain unchanged.

## Cooking, lifetime and playback

The trusted CPU importer `forge.animation.mask`, revision `forge.animation.mask.v1`,
validates exact selected model metadata and publishes one immutable `mask.json`
through the existing import service, derived cache and AssetPublisher. Its runtime
dependency edges include the skeleton (`mask.skeleton`) and every explicit joint
(`mask.joint:<UUID>`), with that model's exact selected revision. The existing
catalog and dependency graph remain authoritative. Failed source imports retain
the last-good selection. Move/duplicate and reference-impact review have explicit
source adapters; duplication changes only mask identity and retains its bone
references. Unknown source extensions remain uninspected by reference review.

AnimationRuntime owns a typed ResourcePool, bounded to64 selections /4MiB, and
holds immutable leases in each player. Binding verifies the artifact, provenance,
complete typed dependency closure and exact skeleton revision, then resolves
durable joints to a dense float-weight vector. No file reads, JSON parsing or
durable-reference lookup occur during sampling. The existing pinned Ozz blend
jobs consume these weights; no separate animation manager, clock or pose owner
is introduced. Morph-node influence and root-motion contribution use the same
effective joint weights. Existing player/evaluated-joint budgets remain unchanged.

An explicit mask or subtree edit prepares a replacement at the current phase,
including when paused, and validates its complete transform/physics candidate
before adoption. A failed candidate leaves the previous player and complete pose
owned; an invalid desired configuration cannot be checkpointed. A published
replacement selection follows the existing fixed-boundary asset reload policy.
Its first valid application preserves playback and fade progress.

Between ticks, an admitted explicit mask edit commits the joint channels through
the existing complete-pose transaction. Presentation snaps only those changed
joints in its existing interpolation cache. The next paused render therefore uses
the new skin and morph pose without advancing the animation clock, events or root
motion. Physics rejection rolls back the candidate before any pose is adopted.

## Recovery and export

The private blend checkpoint records nullable mask identity and its exact cooked
revision. Cold recovery waits for the typed dependencies and validates identity,
revision, configuration and candidate pose before adopting clocks. Older
checkpoints without mask fields remain readable for null masks. A stale mask
revision cannot be silently restored over a newer selected mask.

Export uses the existing typed reference closure and packaging owners. It admits
the cooked mask and its skeleton/joint dependencies, then ships immutable cooked
data. Source `.mask.json` and glTF files are unnecessary in the relocated game.
Gallery42 compares whole-skeleton, binary subtree and fractional masks using the
original FORGE column and durable imported identities. Native Windows capture
fixtures are separate from local CPU/UI validation and physical acceptance.
