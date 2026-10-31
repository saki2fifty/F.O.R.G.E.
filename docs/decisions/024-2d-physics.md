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

## Moving and trigger Map extension

The Map profile now admits Static or Kinematic PhysicsBody2D, with the existing
Sensor flag selecting trigger rectangles. This supersedes only the original
Static nonsensor restriction above. Every rectangle shares its existing native
body, motion, filter, sensor flag, epoch and command/recovery lifetime. Dynamic
Maps remain excluded; no compound Dynamic mass/inertia policy is introduced.
One-way admits Static nonsensor boxes and painted rectangles as specified below.
The moving one-way extension below also admits Kinematic nonsensor platforms;
sensor one-way combinations remain excluded.

Reuse the checked geometry admission in native realization, Map candidate
preflight and editor preview. Kinematic targets/rotation use the existing body
FIFO and native target transform. Map edits still publish cells only after
physics preflight; empty/refill, independent instances and all shape/pair/event
budgets retain their existing owners. No authored schema, identity, checkpoint
layout, ABI or dependency version changes. Sensor observations retain native
shape-pair multiplicity and discrete sampling; this is not a compound occupancy
manager or a swept-trigger guarantee.

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
Other controllers are excluded. The moving one-way extension below admits
walkable Kinematic one-way support through this same transport owner. Separating intent
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
+Y face. Signed Y scale reverses that face; margins are world metres. The moving
one-way extension below extends motion admission without a new contact owner,
pair database or native world. The Static painted extension below supersedes the original
single-box restriction. Ordinary ray/overlap/sweep queries remain geometric; checked feet
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

## Static painted one-way extension

Reuse PhysicsBody2D.one_way and its world-space margin on a Static nonsensor
TileMapCollider2D. The existing selected layer/rectangle projection and native
body/shape ownership remain authoritative. Each rectangle is solid on its own
transformed local+Y face; signed Y scale reverses the face and Z rotation rotates
it. Image orientation flags remain visual. Other motion/sensor combinations and
per-tile polygons are not admitted by this extension.

The previous inline half-height plane cannot describe offset or stacked Map
rectangles. Both native response hooks and character physical-response queries
now derive that shape's roof from a bounded readonly public native polygon copy.
Existing inline response keeps its half-height fast path. No rectangle scans,
plane cache, changed shape userdata, second contact owner or per-cell entity is
introduced. Current normal/relative-velocity/side-start evidence, geometric queries,
conservative placement, retirement/empty/refill and exact recovery checks remain.
Shared selected-collision arrow projection also covers Map rectangle roofs.

Long falls can legitimately leave a native soft contact deeper than the entry
margin while correction converges. New contact admission still uses side/velocity
evidence. At the unlocked fixed boundary, copy the native touching front-contact
pairs with continuous body history; callbacks use that immutable, bounded witness
to retain native unilateral response until separation. Rebuild it from native
contacts each tick and after recovery. No independent contact lifecycle database
or checkpoint field is introduced, and discontinuities cannot inherit old evidence.
Native pre-solve impulses are not used as a continuity witness.

This deliberately extends only the earlier one-way profile. It changes no authored
field/identity, native body/shape budget, checkpoint format, C callback table or
solver pin. The public value-header contract wording changes its exact SDK hash,
so matching modules are rebuilt. A local portable gate is separate from native
Windows/full editor, physical pixels/input and graphical export acceptance.

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

## Optional 2D surface response

Extend the existing controller/profile with default-preserving floor_stop_on_slope
and slide_on_ceiling flags. Only Grounded locomotion applies these policies:
downward-only idle intent can stop at walkable floors; upward intent can stop at a
mirrored walkable-angle ceiling. Existing active planes identify initial contact;
resolve the zero remaining target through the same native solver and checked
geometry. New contacts retain checked cast approach travel. No synthetic planes,
new solver, service, force, input policy or cross-step cache. Moving-support
transport and geometric queries retain existing behavior. C++ supplies subsequent
movement/gravity. Exact configuration/fingerprint guards carry these flags without
changing private codec9, outer2 or authored formats/identity. Shared reflection owns
Inspector/MCP authoring/history/persistence. The technical contract defines limits.

## Optional checked 2D stair rise

Extend the existing Grounded controller/profile with default-off world-space
step_height0..1m. Following an ordinary horizontally blocked move, require current
walkable support and non-upward intent. Existing physical casts check up, forward
and down; a one-radius forward probe proves a walkable tread but is not adopted.
Independently check actual requested-X clearance and bounded rise. Keep horizontal
travel within the request; correction does not become jump velocity. The actual
capsule may briefly lack support over its leading edge. Conservative headroom/
tread failures retain ordinary movement. Native failure uses existing fail-before-
adoption/query-work policy. Effective stance and physical one-way/filter context
stay shared. No new solver, cross-step cache, queue or pose authority. Configuration
and matching SDK fingerprint extend; private9/outer2 and authored identity/formats
remain unchanged. This extends the earlier no-stairs profile deliberately; detailed
limits and completion evidence belong in the2D integration contract.

## Per-tile convex collision profiles

Extend the existing Tile Set definition with a default full-cell profile, explicit
no-collision profile or one strictly convex contour of three through eight
counterclockwise vertices in unit-cell XY/up coordinates. Source admission rejects
nonfinite, out-of-range, repeated, collinear, concave and self-intersecting contours;
a native convex hull must not silently repair authored ordering. Existing full
cells retain merged rectangles. Custom contours use the inverse of the renderer's
cell flip/transpose mapping before cell size and signed physical scale. Validate
the resulting float contour again and construct it through the selected public
native hull/polygon APIs. No native implementation changes are required.

The current RuntimeTileMapSnapshot already retains matching Map and definition
values/leases. Pass that same candidate through existing physics admission and
retain the same immutable snapshot as geometry provenance. Existing compound body
records own both merged boxes and convex pieces, sharing the total4096 shape
budget, filters, epochs, queries, events and checked candidate publication. No
per-cell entity, durable shape identity, parallel collision database or second
physics/resource owner is introduced. None definitions omit their shapes; an
all-None layer has no native body. Dynamic Maps and custom polygon one-way response
remain outside this profile. The simple-polygon extension below adds concave
decomposition. Static/Kinematic ordinary
polygons and sensors use existing policies; full-cell Static and, through the
moving one-way extension below, Kinematic one-way roofs remain.

Tile Set source2 and cooked2 are backward-readable additions. Source1 opaque
collision fields cannot gain physical meaning through implicit promotion: explicit
promotion rejects existing reserved-field collisions across all rows. Ordinary
edits cannot downgrade; Undo/Redo retain exact source restoration and allocator
high-water. Cooked1 stays full-cell; new cooks publish2 through recipe2. Source
schema version and publication metadata API1 remain distinct. No asset, tile,
Map, layer or entity identities change. Existing ownership selects and verifies
actual cooked output versions and dependency revisions.

The editor copies the matching immutable pair from the render host's existing
published Map/Set pools, and uses the same checked physical conversion for worker
outlines. Keep the last usable pair together when preparation fails. Native
fresh-candidate recovery checks the complete polygon geometry multiset through
public getters before rebinding existing body epochs. Advance the private physics
block9 to10 for the extended configuration/geometry contract. Outer Play2 remains;
older private checkpoints reject under existing exact-build/configuration guards.
This is a private recovery boundary, not a durable authored-source migration or
C callback-table change. Completion and acceptance evidence remain in the affected
technical contracts and validation records.

Copied Play definitions originally carry their explicit nested cooked-profile version2 and
collision values through the existing projection/receiver. Legacy definition
headers remain readable; known-revision reuse compares normalized admitted data.
The existing scope, frozen-transfer, acknowledgement and byte budgets remain
authoritative. This does not introduce a second asset/physics channel.

## Simple concave tile outlines

Add a separate `polygon` collision kind for one simple, counterclockwise contour
of3..8 finite unit-cell vertices. Preserve strict `convex` admission. Reject repeated
vertices, adjacent collinearity, intersections and touching nonadjacent edges;
do not infer a convex hull or repair an invalid outline. An original deterministic
first-ear decomposition returns triangles indexed into the admitted contour.
Validation and Map geometry use the same bounded helper. Holes, multiple contours
and an optimal or seamless decomposition are not promised.

Source3 and cooked3 admit this additive kind. Reads of1/2 retain their prior
meaning; old profiles cannot admit the new kind. Explicit collision edits promote
to the required source version, ordinary edits never downgrade, and existing
source1 opaque-field conflict checks and exact history remain authoritative.
Recipe3 publishes cooked3; copied Play definitions write nested version3 through
the existing projection/receiver. AssetCatalog and runtime packaging accept source3
only for Tile Sets. No durable identity or public SDK layout changes are required.
The existing exact SDK fingerprint includes the new decomposition helper and its
typed value header as value-support semantics; matching modules must rebuild.

Invert the existing Sprite cell transform once per contour, then emit checked
triangles through the existing Map polygon conversion. All pieces count against
the shared4096 native-shape budget. Full-cell rectangle merging remains unchanged.
Native bodies, last-good candidate publication, outlines, queries, contacts and
recovery continue to own the same convex-piece data. Private physics10 and Map
checkpoint1 need no new fields: fresh recovery validates the same realized pieces.
Static/Kinematic solid and sensor policies apply; Dynamic Maps and custom-polygon
one-way response remain excluded.

## Tile collision contours and holes

Add the separate `contours` kind in Tile Set Source/cooked/recipe6, retaining the
older1..5 meanings and strict `convex`/single-loop `polygon` admission. Each of
1..4 filled contours has3..8 counterclockwise unit-cell points and0..4 holes of
3..8 counterclockwise points each. Validate every simple loop through the existing
owner. Require strict hole containment and reject touching/crossing boundaries,
overlapping filled regions and nested holes. Separate filled islands inside a
void are permitted. Do not repair source ordering or infer an outer boundary.

The existing collision profile holds an optional immutable shared value containing
source loops, a flat original-vertex pool (at most160 vertices), and prepared uint16
triangle indices (at most184 triangles). An original deterministic visibility
bridge and ear decomposition reverses holes only in its derived traversal. Share
segment predicates with the existing simple-loop validator. Prepare once per
definition admission; repeated cells transform the flat vertices once and emit
existing convex pieces. The derived cache is never serialized as another source
authority, and its retained allocations count through the existing Set estimator.

Explicit6 promotion rejects prior opaque `collision.contours` fields across every
row. Source1..5 retain those fields as extensions. Typed edits preserve unknown
profile and retained contour-position siblings through the same indexed-row merge
used by animation and neighbor profiles. Extensions belong to retained array
positions, including when geometry changes position; truncation discards removed
positions, and changing kind removes unused known contour data. Ordinary edits never downgrade.
Asset/local keys, allocator history, document revisions, Undo and publication keep
their existing owners. Inspector and the existing collision automation/MCP operation
use that source transaction; there is no parallel authoring implementation.

All pieces use existing physical float admission, Static/Kinematic solid/sensor
policies, the combined4096-shape limit, candidate/last-good publication, debug,
native queries and recovery. Dynamic Maps and custom-polygon one-way remain
excluded. Private recovery already stores complete convex pieces, so this addition
does not require another private recovery field/version or public C-table change.
Copied definitions carry nested version6 through the existing bounded channel;
the helper and typed semantics participate in the exact SDK fingerprint.

## Moving one-way platform extension

Extend the existing one-way policy to Kinematic nonsensor Boxes and full-cell
Tile Maps. This supersedes the Static-only motion limits in the one-way and
painted one-way sections, and the transport exclusion above. Dynamic, sensor
and custom-polygon one-way collision remain excluded. Ordinary body controls,
transform animation, Map admission and opt-in Character Controller 2D transport
retain their existing owners and budgets.

Solid-side evidence must compare actor and platform poses from the same boundary.
Native rigid callbacks use both bodies' admitted fixed starts; character physical
queries use the candidate actor pose and the platform's actual current pose.
Previous evidence pairs their preceding fixed starts. Reuse the existing
body-owned samples, angular-aware relative point velocity and bounded native
touching-contact snapshot. A previous actor pose evaluated against a moving
platform's current plane is not sufficient evidence. No pair-history cache,
second contact lifecycle, native world or pose authority is introduced.

Continuous Kinematic targets retain continuity. Creation, replacement and
discontinuous placement retain their existing resets. Geometric queries remain
geometric and checked placement remains conservative. Opt-in support transport
uses the same walkability, bounded move/slide, jump detachment and crush policy;
it does not add implicit departure momentum or an angular swept-volume claim.

No authored value, identity, field layout, callback table or checkpoint field is
added. The updated public contract header changes the exact SDK fingerprint;
modules must use the matching SDK. Local native/source/UI/MCP/installed-SDK and
source-free content checks are distinct from Windows, physical input/GPU and
graphical executable acceptance.
