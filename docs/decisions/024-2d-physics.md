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

## Painted collision extension

Use the existing TileMapRenderer runtime instance as cell authority and one
TileMapCollider2D stable layer key on the same Static nonsensor body. Derive bounded
nonoverlapping chunk rectangles through one pure projection and one shared checked
native/preview conversion. There are no per-cell entities or second Map database.
All native shape lifetime, queries, event binding, connected-body exclusions and
fresh-candidate geometry matching remain subordinate to the existing native body.
An empty selected layer has no native body; refilling creates a new private epoch.

The existing Map queue calls the current PhysicsRuntime through one private weak
admission callback before publishing candidate cells. Failed physical admission
preserves both previous cells and collision. The existing Map owner's private
checkpoint codec restores edited values before Physics on an unpublished candidate.
Advance the ordinary host's private whole checkpoint to2; the physics block stays6.
This is exact-build transient recovery, not a new durable format or arbitrary C++
state restoration. Authored identity/formats and ABI1 are unchanged. Bound total
native2D shapes to4096 within existing shared body/joint/pair/event/byte budgets.

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

## Character-controller extension

Extend the existing CharacterService and shared4096 CharacterCommand FIFO with
distinct full XY intent, checked feet placement and copied state. Native2D's
existing canonical body records own a solid Kinematic inner capsule plus intent,
clipped velocity and placement result. There is no hidden authored PhysicsBody2D,
second queue, solver owner or durable identity. Game C++ supplies gravity/jump.
Use upright World binding and shared native/outline capsule conversion.

Bounded plane response, independent penetration admission and read-only support
observation compose existing exact-pin public APIs. Native callback work is bounded;
a failed composed movement or placement batch must fault before adoption or
continuation. Floor following is vertical; stairs remain outside this profile and
platform transport requires the option below. Both real hosts use one copied character-debug projection; the editor owns
only inspection/Scene guides. C SDK tail additions retain ABI1 and change the exact
fingerprint so modules rebuild.

Advance the private physics block from6 to7 for domain-tagged character commands
and copied placement status. The outer ordinary-Play checkpoint stays2. Exact
build/source/fingerprint guards and combined byte limits remain unchanged; older
checkpoints reject. No authored identity/Scene migration or arbitrary C++ recovery
is introduced. The integration contract owns numerical details and limits.

### Optional moving-support transport

Extend the same controller with default-off platform_carry. Existing Native2D body
records hold only a transient pre-step reference/rigid transform of currently
walkable ordinary Kinematic/Dynamic support. Derive transport from actual rigid
motion of the lower capsule cap center and run existing bounded move/slide before
relative locomotion. Exclude only the carrier during transport. The following
ordinary locomotion owns shallow recovery and endpoint checks against all solids.
Crush follows the existing fail-before-adoption deep-overlap policy. No new
parenting, pose authority, service, native world or durable contact cache.
Other controllers and moving one-way surfaces are excluded. Separating intent
detaches without departure momentum. The capsule stays upright; each step casts
linear transport displacement, not an angular swept volume.

Detached admission may exempt a continuous unchanged Kinematic target only after
proving current walkable support of an unchanged opt-in controller. Share that
proof with the existing candidate-work bound; retain edited/new geometry admission.
No carried state survives the fixed step, so private codec8/outer2 do not change.
The additive flag and matching SDK fingerprint preserve authored identities and
old default behavior. Technical/manual contracts define numerical limits.

## One-way surface extension

Use an optional default-off response setting on existing PhysicsBody2D. The
selected profile is a Static nonsensor single Box, solid on its transformed local
+Y face. Signed Y scale reverses that face; margins are world metres. No moving or
painted one-way platform profile, new contact owner, pair database or native world
is introduced. Ordinary ray/overlap/sweep queries remain geometric; checked feet
placement remains conservative. Native CharacterController2D physical response
uses explicit context through its existing plane/cast/penetration/support path.

The existing native body record keeps the preceding admitted fixed-step start.
It supplies solid-side evidence when native contact creation lags a crossing;
zero-dt inspection never rolls history. Creation, replacement and discontinuous
placement invalidate it. Native response also checks front-facing contact normals
and angular-aware relative point velocity. Both public exact-pin pre-solve and
pre-continuous hooks are required; pre-solve alone can clamp allowed fast travel.
Their work shares the existing16384 fixed controller/query work envelope, and
failure prevents partial-step adoption. Hooks allocate nothing and access no ECS.
Default-off worlds do not sample side history or request pre-solve events.

Advance the private physics block from7 to8 to preserve the required body-owned
side-start sample through fresh-candidate recovery. The ordinary outer checkpoint
stays2. Configuration, sample values, native pre-solve flags and fresh bindings
are checked before hooks are installed on the unpublished candidate. SDK public
layout changes require an exact matching rebuilt kit; ABI1 and authored Scene /
Prefab identity/formats stay unchanged. This decision selects the implementation
boundary; the technical contract and validation gates determine feature completion.

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
process hosts. Its complete Linux/Windows matrix, native editor/source acceptance
and same-runner benchmark, reviewed images and shared source-free/fresh-runner
exports pass. Physical acceptance and numbered delivery remain separate. See the
[2D integration contract](../physics-2d.md).

## Checked 2D stance extension

Extend existing CharacterService/FIFO with planar kind2 and reuse C command2D
operation2. Existing native body records own stance/blocked flags; standing
configuration remains authoritative. Crouch height0 selects bounded automatic
half-height and avoids invalidating older short characters. Checked geometry
updates use the same Kinematic shape/body binding and preserve feet/motion/epoch.
Reserve native contact destruction Ends before the public shape setter; retain
existing query/work/event/fault budgets and conservative placement-style clearance.
No second queue, native world, source override or pose authority is introduced.

All physical/query/debug consumers use effective stance. Replacement preserves it;
fresh recovery separately guards standing configuration and checks effective
clearance/native shape before rebinding hooks. Advance private physics8 to9 for
stance/blocked state and queued requests; outer2 and authored identity/formats
remain unchanged. Exact matching SDK fingerprint changes; ABI1 table version stays.
Copied C state appends posture/blocked flags, with exact-size validation. Existing
combined debug blockage remains supported alongside precise new flags.
