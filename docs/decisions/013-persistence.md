# ADR 013 — Persistence domains

Date: 2026-09-19. Decision frozen for review in Build260919-000063; automated package
validation is complete. Future implementation requires its own authorized scope.

## Decision

Authored Scene/Prefab/asset documents preserve UUIDs, unknown payloads, component
ownership and explicit override intent. Recovery checkpoints are ephemeral, exact
runtime-compatible state, not save games. A future save-game schema and network
replication schema each need explicit version/authority/remapping semantics.

## Current evidence and implementation boundary

scene-format.md, prefabs.md, transforms.md and runtime-timing.md describe current
formats. Phase3 TRS is separate Flecs components; WorldTransform is derived. Structured
prefab revisions are immutable and reconciled before publication.

## Consequences

No automatic legacy prefab migration. Revert removes intent and is scene Undo;
direct source publication is a separate history boundary. Future Apply requires
coordinated asset+scene publication, recovery and honest Undo semantics. Nested prefabs
and variants must define cycle/identity/override composition before enabling them.
Runtime-generated entities have no authored UUID unless explicitly promoted.

## Deferred work and exact trigger

Scene composition adds transient instance scope at simultaneous instantiation,
not persistent DocumentId. Save games begin when real resumable-game requirements
exist; networking begins with replication ownership/time/ID contracts. Neither may
serialize raw Flecs IDs or reuse checkpoint bytes as a durable public format.

## 2026-10-11 — physics joint recovery

Authored PhysicsJoint settings use existing optional reflected scene/prefab
containers and persistent EntityRefs. Shared admitted-reference traversal remaps
builtin and custom component fields during scene/entity duplication, preserving
opaque extension payloads. Native constraint indices remain private: physics
checkpoint version 2 records joint identities/settings in actual Jolt manager order
and reconstructs that order before native RestoreState. Old ephemeral checkpoints
are rejected; no authored schema migration or persisted solver handle is introduced.
Exact SDK fingerprint changes through the public physics settings header; ABI1 is
unchanged. See [physics](../physics.md).

## 2026-10-11 — continuous collision setting

PhysicsBody adds a trailing `continuous_collision=false` field through the existing
codec and reflected authoring schema. Missing saved fields are backfilled on
admission; prior source documents require no rewrite or format revision. The
stored choice survives inactive Static/Kinematic/sensor modes. Native selection
and recovery remain owned by PhysicsRuntime. The public C++ layout and exact SDK
fingerprint change, so modules require a matching SDK rebuild; native ABI1 remains
unchanged. Recovery bytes remain private and exact-build compatible.

## 2026-10-11 — queued body loads

Physics checkpoint version3 deliberately supersedes version2 in the private,
exact-build recovery domain. The existing ordered body-command queue now records
tagged teleport, kinematic, force, impulse, torque and angular-impulse commands,
including bounded copied vectors and optional world points. Restoration validates
all command policies before reconstructing native bodies/constraints; older
ephemeral envelopes reject. No authored Scene/Prefab/project schema or durable
save format changes. The exact SDK callback table and PhysicsService header
change; native ABI1 remains unchanged and matching module rebuilds are required.

## 2026-10-11 — body motion tuning

Five trailing PhysicsBody values expose existing native damping, speed limits and
sleep permission through the existing reflected authoring codec. Missing fields
backfill current native defaults; ordinary Scene/Prefab formats and identity are
unchanged. Runtime configuration equality and private exact-build recovery mapping
include them. Public SDK layout/fingerprint changes mandate a matched rebuild;
ABI1 and private checkpoint version3 remain unchanged. No second motion owner or
durable save-game format is introduced. Angular authoring is degrees/s, converted
once by the native owner. See [physics](../physics.md).

## 2026-10-11 — queued body velocity

Private exact-build physics checkpoint4 deliberately supersedes3 to add two
strict existing-FIFO tags:6 linear velocity and7 angular velocity. Zero vectors
retain command intent and stop the selected channel; they cannot be discarded
using the load no-op rule. The owner validates the full bounded queue before
native reconstruction and execution. Older ephemeral envelopes reject, while
authored Scene/Prefab/project versions and durable save formats remain unchanged.
The checked exact-SDK callback is a tail addition with explicit channel values;
existing load-kind meanings and native ABI1 are unchanged. Matching SDK module
rebuilds are required. PhysicsRuntime remains the only motion/queue/recovery owner.
