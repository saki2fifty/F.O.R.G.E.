# ADR 024 — Typed 2D physics within the existing physics owner

Date: 2026-10-21. Accepted integration boundary; implementation is in progress.
This record does not announce available 2D bodies or a packaged feature.

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
its MIT notice. Private dependency/build integration and the initial body adapter
are present, with engine controls/events and private recovery implemented locally. The matching SDK/editor/export workflow remains pending.
Vendor headers and handles do not enter the gameplay SDK.

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

Private physics checkpoint 5 adds the typed 2D block, epoch mapping and tagged
commands. Retain the combined 2 MiB raw solver, 6 MiB physics, 8 MiB checkpoint and
16 MiB process budgets. Older exact-build checkpoints reject; no durable source
migration is involved. In-place restoration of the published native world is not
an acceptable substitute for an unpublished candidate.

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
Private fresh-candidate recovery has local CPU and actual process restart checks. Editor, installed SDK and Windows/export acceptance remain incomplete. See the
[2D integration contract](../physics-2d.md).
