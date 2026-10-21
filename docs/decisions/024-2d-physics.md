# ADR 024 — Typed 2D physics within the existing physics owner

Date: 2026-10-21. Accepted integration boundary.
Implementation and delivery validation are described in the linked technical
contract; this decision does not announce a packaged feature.

## Decision

Extend the existing `forge.physics` schema module, PhysicsRuntime/PhysicsService,
fixed phases and canonical EntityRefs. Jolt continues to own 3D simulation; a
private, lazily created Box2D world supplies the distinct 2D domain. Each entity
has at most one enabled body/controller writer. Both domains participate in the
same detached transform/configuration/command preflight before publication.
There is no second ECS hierarchy, physics service, clock or identity registry.

Select Box2D 3.2.0, immutable revision
`178495ba35695aefe9c321e569d2277f06db3b90`, for the implementation. Use private
static C17 linkage, double world positions, normal float geometry/velocities,
one native worker, native Debug validation and unchanged default length units. Preserve
its MIT notice. The private body adapter, controls, events and recovery remain
subordinate to the existing physics owner. Vendor headers and handles do not
enter the gameplay SDK. Typed authoring, Inspector/guides, Gallery content and
source-free export use the existing subsystem owners and acceptance gates.

## Coordinates and authority

The domain uses world XY meters and positive Z rotation. Preserve authored Z and
signed presentation scale. Dynamic bodies require World spatial binding; effective
ancestors of other bodies cannot contain either domain's Dynamic body or a
movement controller. Reject tilted, sheared or unsupported shape transforms.
The existing transform owner remains the only author of WorldTransform.

A restricted XY point/vector mapping may operate beneath a zero-depth parent
when its XY map is invertible. It uses the existing reciprocal-condition threshold
and changes no general 3D inverse, decomposition, parenting or Scene-edit policy.
Planar geometry conversion alone never admits a body orientation or collider.

Use distinct typed 2D state/query/control values through the current service and
matching SDK. Do not reinterpret existing 3D methods. New optional Scene/Prefab
components retain existing identity, ownership, defaults and opaque data rules.
Exact SDK fingerprints change and modules require rebuilding; native ABI 1 remains.

## Native lifetime, events and recovery

Private realized-body records own a monotonic simulation epoch. Copied 2D events
include each canonical reference and epoch, distinguishing an End for a retired
body from a replacement using the same UUID. A bounded retired-shape mapping lives
only until native events are copied. Observations expire at the next admitted
synchronization; recovery never replays the transient adopted event list.
These are simulation observations, not durable identities or an event bus.

Native snapshots clear application pointers, and native IDs are opaque. Reacquire
fresh candidate handles through supported shape queries and native debug names;
do not edit native ID fields. Encode the existing record epoch in ten printable
base64 characters, bounded to 60 bits, matching the pin's default name capacity.
Validate unique bindings, canonical identities, geometry/configuration and all
limits before rebinding private hooks and publishing a candidate. This private
checkpoint binding introduces no authored ID or general lookup owner.

Private physics checkpoint 5 introduced the typed 2D block, epoch mapping and
tagged commands. The connected-body extension deliberately advances it to 6.
Joint metadata contains complete canonical references/configuration/scaled anchors
and the native joint enumeration order visited by canonical body reference.
Reacquire fresh joint handles using supported body-joint/body endpoint APIs;
validate complete native configuration and count before candidate publication.
Duplicate configurations remain distinct through that recorded order. No opaque
ID editing, durable identity or additional ownership registry is introduced.
Retain the combined 2 MiB raw solver, 6 MiB physics, 8 MiB checkpoint and
16 MiB process budgets. Older exact-build checkpoints reject; no durable source
migration is involved. In-place restoration of the published native world is not
an acceptable substitute for an unpublished candidate.

## Connected-body extension

PhysicsJoint2D is distinct from the existing 3D PhysicsJoint. Distance, Hinge and
Slider use body-local XY anchors scaled by each body's signed physical scale;
rigid frame angles remain independent of scale. Joint entity transforms have no
solver authority. Only enabled nonsensor 2D bodies participate; at least one is
Dynamic. Existing transform/animation admission still applies.

The subordinate Native2D owns realized joints, retiring them before affected body
replacement. Unchanged geometry preserves solver state; effective control changes
wake connected bodies at the existing synchronization boundary. Any active
collision opt-out wins for duplicate links. Native retirement/contact transitions
share existing event adoption, capacity and checkpoint guards. The combined
enabled-joint ceiling remains4096 across both domains. See the technical contract
for the deliberately bounded scalar and motor profile.

## Capacity and failed native steps

The first profile targets 1024 2D bodies within the existing combined 8192-body
limit, 4096 admitted pair checks including retained native contacts, and 16384
copied event slots. The existing 4096-command FIFO is shared across body domains.
Native reserve capacities are not hard caps. A private callback counts pair work
before native contact/sensor allocation. Overflow rejects the step and prevents
adoption or continuation; filtering excess pairs must never become a successful
partial simulation. Callbacks neither mutate the native world nor access Flecs.
CCD and all event-allocation paths require their own proof before completion.

These are admission/work limits, not a guarantee that all combinations fit the
unchanged shared recovery envelope or meet a frame-time target. Native allocation
or solver faults are runtime faults, not transactional rollback promises.

## Evidence and completion boundary

Exact-pin native tests and original probes establish double-position geometry,
sensor retirement, pointer scrubbing, fresh native candidates and pair filtering.
The 8192-body dense probes require approximately 5.8 MB of native snapshot data;
1024 bodies with 4096 guarded solid contacts require 1906629 bytes in the tested
configuration. This supports the smaller initial profile without enlarging the
existing recovery budgets. It does not prove complete runtime integration.

The private derived XY mapping and native dependency/profile fixtures have local
CPU/CMake tests; package-notice retention checks also pass. Initial native body
simulation, detached admission, controls, queries and copied current/retired
events have local CPU tests, including existing navigation/animation exclusions.
Private fresh-candidate recovery has local CPU and actual process restart checks.
Build261021-000199 delivered the body-only integration after automated Windows
editor, installed SDK and standalone relocation gates. Physical acceptance remains
separate. The connected-body extension has local runtime, authoring, actual
three-scale Inspector, moved cooked-content and installed SDK checks through both
process hosts; its native images/Windows/export milestone remains pending. See the
[2D integration contract](../physics-2d.md).
