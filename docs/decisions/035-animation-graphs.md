# ADR 035 — Authored graphs share the animation owner

Date: 2026-11-06. Status: accepted.

## Ownership and identity

An Animation Graph is an ordinary root asset. Its `.animgraph.json` source and
cooked wrapper use `forge.animation-graph`, version 1. Existing source documents,
history, import/publication, immutable selections and typed resource pools retain
their responsibilities. AnimationRuntime owns clocks, complete pose transactions,
constraints, root motion and recovery. There is no additional animation service,
scheduler, skeleton registry or world mutation path.

Node UUIDs are scoped to one graph. Input slots 1–4 are stable local keys; names
are labels. Nodes, ports and the output are not catalog AssetIds, even if their
text equals an asset UUID. Copying changes only the root identity. Typed reference
inspection visits skeleton, clip and mask fields; it never interprets opaque
metadata or local links as external references.

Drafts may have disconnected nodes and null bindings. Present links must resolve,
IDs must be unique and every subgraph must be acyclic. Publication requires all
nodes to contribute to one completely bound output. Dependency order comes from
links, independently of source row order. Cooking strips source-only metadata.

## Evaluation

Profile 1 bounds graphs to 32 nodes, eight clip occurrences, four input slots and
256 skeleton joints. Source limits are 256 KiB, 12,000 parser events and depth 12.
Cycle duration is finite within 0.0001–3600 seconds. Inputs, scale and bias are
finite within ±1,000,000; resulting influences clamp to [0,1]. Constants are in [0,1].

Clip, Blend and Additive nodes share one normalized cycle. Graph mode is exclusive
from direct Clip, States, Blend Space and manual blending. Four separate override
checkboxes preserve source defaults when unchecked; enabled undeclared slots reject.
Input changes preserve leases and phase, validate a complete pose and snap
presentation at adoption. No implicit input smoothing is introduced.

The existing clip selector admits imported and target-bound baked clips against
one exact target skeleton/model. Clip and skeleton source generations are separate;
resource slot generations are not source generations. Existing native local-space
blending, rest-relative additive math and mask weights remain authoritative. Shared
leaves sample once; owned bounded node values feed one final local-to-model pass.
Morph defaults and transform-channel unions include every leaf. Existing complete
pose, writer-conflict and rig transactions precede world publication.

The existing world work gate charges graph nodes multiplied by skeleton joints
against 8,192 evaluated joints, with at most 256 players. Admitted morph tracks
belong to that same joint set and have at most 256 components each. These are work
limits, not measured process-memory or physical-hardware performance claims.

## Root motion

[ADR 018](018-animation-root-motion.md) remains authoritative. Each distinct clip
computes its scaled interval and loop displacement through the existing layer
motion owner. Blend nodes mix those deltas; Additive nodes retain the base delta
and compose the secondary delta weighted from identity. The selected root's mask
influence multiplies the node weight. Current weights remain constant for the
interval; changing an input does not convert a pose difference into locomotion.

Raw leaf roots are copied from native local values before final neutralization
and constraints. Shared clip intervals evaluate once. Rest-root conjugation applies
once to the output delta. Apply/Extract retain existing entity, physics, tick and
publication authority. Absolute graph-root pose differences are not a motion policy.

## Compatibility and recovery

Content requires `runtime_animation_graph` profile 1 in a matching runtime kit.
Export validates the complete graph/clip/mask/target-model closure and omits authored
source files. Animator's native layout and reflection expand; matching SDK
fingerprints and rebuilt modules are required. ABI1 does not preserve old layouts.

Private animation recovery 9 wraps the previous profile 1–8 in `animation_profile`;
an underlying profile 8 retains its `sequence_profile`. Graph, clip and mask
revisions, input values/default selection and clocks validate before adoption.
Older readers reject 9. Derived caches are reconstructed, never persisted as identity.

State/Blend Space graph nodes, independent node clocks, arbitrary callbacks and
morph-only graphs without an admitted skeleton are outside this profile. Native
editor, automation/MCP, matching SDK, manual and software-rendered export integration
are covered by their focused checks. Windows/full-editor and physical acceptance
remain separate from this architectural decision.
