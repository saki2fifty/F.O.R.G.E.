# 2D physics integration contract

2D bodies use the world's XY plane through the existing physics owner, with
typed authoring, C++ controls, collision outlines and standalone export.
Build261021-000195 predates this feature and contains no 2D physics.
CPU/process checks cover authored reflection, scene save/reopen, native simulation,
domain separation, signed/flat parent frames, controls, queries, copied events,
rejected partial steps and private fresh-candidate recovery. Matching installed
SDK modules run through both process hosts. The focused Windows audit passes
Game rendering and Scene/Inspector captures at100/150/200%; the shared-game gate
also runs exported games after source/SDK removal and fresh-runner relocation.
These scoped results do not replace the complete milestone gate or physical
Windows/GPU acceptance. The editor's **Help → User Manual → 2D physics** explains
the authoring workflow.
[ADR024](decisions/024-2d-physics.md)
records the selected ownership, native boundary and recovery direction.

## Native boundary

Box2D 3.2.0 is selected at immutable
`178495ba35695aefe9c321e569d2277f06db3b90`; CMake builds the private backend and
wires its license notices. The body adapter is subordinate to PhysicsRuntime.
It is a private static dependency,
with double world positions and ordinary float geometry, rotations and velocity.
The initial native world is single-worker and created only when needed. Existing
3D Jolt behavior, the fixed clock and Physics capability remain authoritative.
There is no collision between the two native domains.

## Authored bodies and shapes

Distinct PhysicsBody2D, BoxCollider2D, CircleCollider2D and
CapsuleCollider2D values belong to the existing builtin schema family. An enabled body
requires exactly one enabled-domain collider and cannot share its entity
with an enabled 3D body or Character Controller. Disabled choices remain authored.

| Setting | Meaning |
| --- | --- |
| Motion | Static, Kinematic or Dynamic; default Static |
| Density / mass | Default 1 kg/m²; mass 0 uses area-density, positive kg overrides mass with proportionally scaled native inertia |
| Surface | Friction default 0.6, restitution default 0; stored on the existing body value |
| Gravity / drag | Existing Project gravity XY times default multiplier 1; native linear/angular damping default 0 per second |
| Layers | Existing named slots 0..31 and uint32 collision mask; native 64-bit filter adapted explicitly |
| Flags | Enabled, sensor, sleep permission, Dynamic world X/Y/rotation locks and bullet mode; inactive choices preserved |
| Box | Full XY dimensions centered at entity origin |
| Circle | Radius centered at entity origin |
| Capsule | Radius and straight segment height along local Y; total height is segment plus diameter |

The supported input profile bounds seed/target XY coordinates to ±1000000 meters.
Effective full box dimensions are 0.01..1000 m; circle radius 0.005..500 m. Capsules
require effective straight height at least 0.01 m and total height at most 1000 m.
The pin rejects a capsule segment at or below its native slop threshold; no
implicit conversion to a circle is promised. Density is 0.001..10000 kg/m² and
mass override 0..1000000 kg. Friction is 0..10, restitution 0..1, gravity factor -10..10,
and damping 0..10000/s. These are input guards, not universal stability guarantees.
A negative gravity factor reverses Project gravity. Authored collider scalar
dimensions allow 0.000001..10000 m; the tighter effective physical dimensions are
checked after world scale is applied, so source geometry can use different units.
An omitted Body2D source field takes its current schema default; unknown source
fields retain the existing opaque-extension behavior.

The original native range probe checks 90 box/circle/capsule cases at 0/±1000000m,
including dimensions 0.01..1000 m, densities 0.001..10000 kg/m², positive finite
area-density mass/inertia, free motion and closest rays. It does not establish
contact quality, CCD, large-world performance or all intermediate combinations.

### High-speed collision and sensor limits

The selected native world enables continuous collision. A Dynamic body's Bullet
flag additionally tests against Kinematic and non-bullet Dynamic bodies. It does
not provide bullet-versus-bullet CCD or exact projectile timing for two moving
targets. Local engine tests drive all three inline shapes at100m/s through a
0.05m receiver's location at60Hz, checking Static, Kinematic and massive Dynamic
receivers independently. These examples do not establish every trajectory,
initial overlap, timestep or relative-speed combination.

Box2D3.2.0 also records continuous sensor candidates for a fast solid body. The
visitor can pass completely through a thin sensor during a tick and still produce
a Begin, followed by End on a later tick. The native implementation retains at
most eight continuous sensor candidates per fast body per step; solid hits can
discard later crossings. This is a bounded best-effort native observation, not a
promise to detect every thin trigger along an arbitrary high-speed path. Use
explicit queries when gameplay requires its own path checks. Sensors remain
non-solid. The shared pair-work guard also runs in native continuous and sensor
queries before their admitted pairs allocate event storage.

The existing authoring commands own component addition, typed property changes,
Undo/Redo and save/reopen. Prefab property intent stays separate from a complete
component override: an equal-valued mass edit remains explicit when the source
changes, untouched density still follows the source, and Revert restores
inheritance. Source publication uses the existing history boundary. Snapshot
reconstruction preserves canonical references and opaque future fields, including
full unsigned64-bit values. Local checks also realize the saved authored body in
an actual runtime while confirming that authoring worlds create no solver.

## Painted Tile Map collision

`TileMapCollider2D.layer` selects one exact uint64 layer key from the same entity's
`TileMapRenderer` instance. Zero is unassigned. The Inspector and Prefab source
editor show published layer names, including hidden layers, and preserve missing
keys. Duplicate names never substitute a row index for the key. Source publication
must finish before a new layer choice is offered.

This profile requires a Static nonsensor PhysicsBody2D. The Map collider is mutually
exclusive with inline colliders. Every occupied selected-layer cell is solid;
renderer visibility, layer visibility and image orientation flags do not change
collision. Decorative cells belong in another layer. Empty valid layers have no
native body or placeholder shape; refilling creates a new private realization epoch.
An enabled joint cannot retain an endpoint whose last solid cells are removed.

One pure projection merges identical horizontal occupied runs through consecutive
rows within each32×32 chunk. It produces nonoverlapping boxes, with no per-cell
entities, identities, second Map database or native world. This is not a globally
minimal rectangle cover or a promise of seamless character movement across joins.
For a rectangle origin `(x,y)`, cell counts `(w,h)`, cell size `(cx,cy)` and admitted
signed XY scale `(sx,sy)`, body-local metres are:

```text
center = ((x + w/2) * cx * sx, (y + h/2) * cy * sy)
full size = (w * cx * abs(sx), h * cy * abs(sy))
```

The same checked conversion builds native shapes and copied Scene outlines.
Each full dimension must be0.01..1000metres; each local box corner remains within
±10000metres. The existing planar ancestry/pose admission applies, including signed
and flat-depth transforms. Scene outlines show published geometry and explicitly
exclude runtime cell edits. Geometry preparation runs on immutable copied input.

The existing runtime Map owner retains cells and publishes fixed edits. Its one
private admission callback delegates to the existing PhysicsRuntime before adopting
candidate cells. Unsupported extent, aggregate native capacity or connected-empty
geometry rejects the complete request. An incompatible source revision retains
the instance's last admitted cells and collision with a diagnostic. Independent
instances of one asset remain independent. Unchanged snapshots/layer/physical scale
reuse immutable geometry inside the existing native-body record.

There are at most4096 total native2D shapes across Map rectangles and inline bodies,
within existing body, joint, pair and event limits. Empty Map actors do not consume
native body/shape slots. This profile excludes Dynamic/Kinematic/sensor Maps,
slopes, per-tile polygons, one-way platforms, navigation and character controllers.

## Connected bodies

The connected-body extension adds distinct `PhysicsJoint2D` values through the
same schema/authoring/Physics owners. It does not reinterpret existing 3D joint
kinds. The joint entity's transform has no native authority. Enabled joints need
two distinct enabled nonsensor 2D bodies and at least one Dynamic body. Disabled
references may be empty/unavailable. Body/shape/transform admission remains intact.

| Kind | Frame and control |
| --- | --- |
| Distance (0) | Anchors remain the authored world distance apart; optional frequency and damping provide a spring |
| Hinge (1) | Anchors coincide; rigid local frame angles define rest orientation; optional angle limits and bounded velocity motor |
| Slider (2) | Body B moves along Body A's joint-frame X; rigid relative frame orientation and perpendicular position constrained; optional travel limits and bounded velocity motor |

Anchor X/Y are body-local metres, including each body's effective signed XY scale.
Frame angles are rigid body-local Z degrees independent of scale. Positive motor
motion means Body B relative to Body A's frame. Distances/travel are world metres,
Hinge motor speed degrees/s, Slider speed m/s, torque N*m and force N. Copied SDK
body angles/angular velocities continue to use radians/radians per second.

Defaults are enabled Distance, one metre, empty references, zero anchors/frame
angles, rigid frequency0/damping1, enabled limits [-90,90]deg or [-1,1]m, disabled
motor with90deg/s or1m/s targets and10N*m/100N budgets, and connected collision
enabled. Initial constraint violations are corrected iteratively. A joint is not
a promise of zero numerical error or unlimited actuator force.

The supported scalar profile is:

- Distance0.005f..10000m, reflecting the native five-millimetre stable minimum;
  local anchor coordinates within +/-10000m.
- Frame angles +/-180deg, ordered Hinge limits within +/-178deg and ordered
  Slider travel limits within +/-10000m. Limits may exclude the initial pose.
- Spring frequency0 (rigid) or0.01..100Hz, damping ratio0..10.
- Hinge velocity within +/-36000deg/s, Slider velocity within +/-1000m/s, and
  torque/force budgets0..1000000 in their corresponding units.

All stored scalar settings must be finite and valid, including inactive settings;
unused values are preserved. Complete detached admission runs before either
domain mutates. Runtime C++ edits are applied at the same Physics synchronization
boundary as other typed values; no second command FIFO or clock is introduced.
Geometry changes rebuild the affected joint, not its unchanged bodies. Body
replacement retires/rebinds connected joints before native body destruction.
Unchanged joints retain solver state; only effective control changes wake them.

Connected collision also requires normal layers/masks. Any enabled joint opting
out suppresses that pair even when another link opts in. Retiring the final
opt-out restores the ordinary pair policy; resulting End/Begin transitions use
the existing bounded copied-event/adoption window. A checkpoint rejects pending,
unadopted retirement events, including those caused by joint edits.

The Inspector uses the existing typed property history and 2D-body reference
picker. Relevant settings follow the selected kind. Scene guides derive frames
from copied authored spatial data, with signed/flat ancestry and XY-only physical
measurements; they do not instantiate a solver, change source or measure live
forces. Normal subtree/Prefab reference adapters preserve local binding and
explicit property intent. Gallery76 demonstrates all three kinds, with a separate
rigid tether and elastic Distance example. The installed
`sample/Physics2DJoints.hpp` and `physics_2d_joint_gameplay` fixture exercise the
matching shared Client without vendor physics headers/linkage.

The combined enabled-joint ceiling remains4096 across 2D/3D. Body, pair, command
and checkpoint budgets below remain unchanged. Local checks cover equations,
signed frames, moving anchors/locked bodies, motors/sleep/reversal, collision
retirement, mixed joint capacity, malformed/fresh recovery, authored/Prefab
ownership, actual three-scale Inspector controls, source-free moved content and
installed-only both-host C++. The complete automated Linux/Windows core/SDK matrix
passes, along with Windows installed-SDK, source-free export and independent
checkout-free relocation checks. Native Scene/Inspector captures for all three
kinds at100/150/200% and Game poses at640/1280 have been reviewed. The complete
editor source gate passes321 checks and its same-runner benchmark comparison.
One earlier audio-mixer walkthrough hit the real5s runtime hello guard; the
unchanged full repeat passed, with no deadline or assertion changes. That failure
is retained as validation evidence, not presented as a fixed engine bug.
[Editor source gate](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/39678469846)
and [shared export gate](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/39658737670)
record those scopes. Numbered delivery and physical acceptance remain separate.
Build261021-000199 contains bodies, not these joints.
No other native joint types, position drives, breaking or character controllers
are included in this extension.

## Transform admission and fixed ownership

Use the existing effective spatial graph, evaluated double affines and signed
TRS decomposition. The supported body/ancestor orientation is pure Z rotation;
reject tilt, parent-induced shear and invalid XY geometry. Reflected XY box scale
is supported; circle/capsule require equal absolute XY scale. Depth scale may be
signed or zero. Physical geometry ignores depth, while authored Z and all local
scale channels remain preserved.

Dynamic bodies are World bound and publish only XY translation and Z rotation.
Static/Kinematic ancestry must not reach either domain's Dynamic body or an
enabled Character Controller. Nonphysics visual descendants remain ordinary
transform children. Animation admission must enforce the same simulation writer
and shape rules, including before the first native realization.

The checked private PlanarMapping derives local XY points/vectors from an
untilted parent affine, including zero Z scale. It retains the existing inverse
condition policy and rejects nonfinite/overflowed conversions. It is neither a
general inverse nor body/shape admission. CPU tests use 240 actual signed, flat
and far parent transforms, with forward evaluation, existing 3D inverse checks,
shear/tilt distinctions and unchanged input nodes.

Both domains must join the same detached preflight and FIFO movement conversion.
Validate final configurations/geometry before authored writes or native retirement.
Then use the existing PrePhysics, Physics, Adoption and PostPhysics phases. Explicit
teleports reset the existing presentation history. Native capacity failures stop
the affected simulation; do not silently adopt a filtered or partially stepped world.

Kinematic targets must fit the selected native speed and rotation bounds for that
fixed tick: default world linear speed 400 m/s and at most 45 degrees of rotation
per tick. The native target API promises a close result, not exact arrival; an
oversized move clamps internally. FORGE rejects such a target during detached
admission so it cannot silently display an unreachable pose. Teleports use a
separate discontinuous placement path. Quaternion/native rotations convert
directly from their unit bases; native approximate angle helpers do not define
FORGE's authored transform values.

The Inspector reuses the existing **Static / Kinematic / Dynamic** motion selector
and Project Physics named-layer/mask controls. Layer names keep their stable slots
across both domains. Component picker labels use **Physics Body 2D**, **Box Collider
2D**, **Circle Collider 2D** and **Capsule Collider 2D**. Actual ImGui CPU input checks
select motion and named mask bits and verify typed Undo/Redo at100/150/200% scale.
Mass and density units come from the registered Flecs member metadata: kilograms
and kilograms per square meter, distinct from 3D volume density. Native Windows
captures exercise the same controls and guides at all three scales.

## Editor collision outlines

The existing Scene **Selected collision** control includes 2D boxes, circles and
capsules. These use copied closed outline segments in the same overlay as 3D
collision geometry. The preview reports sensor/disabled status, physical scale
errors and bounded geometry preparation. Disabled 3D asset choices do not block
an active 2D preview or start unused asset loading. Removing the body or clearing
the selection clears its ready state.

Runtime admission and preview share effective shape conversion and native mass
checks. The preview uses the existing affine decomposition and a derived planar
sign preference to preserve contours beneath reflected or zero-depth parents;
translation and rotation are applied after physical shape scaling. Outlines show
authored geometry; runtime admission additionally checks World binding and body
ancestry. No simulation body, hidden entity or second spatial owner is created.

Local analytic checks compare144 shape/parent combinations against full source
affines. Actual unmodified ImGui CPU contexts exercise100/150/200% scale, async
preparation, clipping, read-only source and correction/removal. Native Windows
WARP captures cover the Scene outline and Inspector; software-rendered images
do not establish physical GPU performance or acceptance.

## C++ control and observations

Engine-facing typed controls, queries and copied state/events have local CPU tests, including
area-mass/inertia load units, point velocity, owner-thread checks, sleeping reads,
shared command capacity and FIFO target conversion beneath flat/reflected parents.
Matching gameplay SDK callbacks are implemented and exercised through both
process hosts. Use a matching SDK and runtime containing this integration.

Add explicitly typed 2D methods through the current service/SDK: copied body
state, world XY velocity and angular velocity, forces/impulses with optional
world points, torque/angular impulse, teleports, kinematic targets, closest rays
and bounded shape overlaps. World positions use doubles; angular controls/state
use radians and radians/s. No native pointer, handle or borrowed array crosses
the boundary. Owner-thread, fixed-tick, capability and exact-size checks remain.
Queries support the existing collision mask and sensor inclusion policy.
Ray origins/endpoints and overlap origins use the supported XY coordinate range.
The adapter reserves native category bit63 for queries and includes it in native
shape masks, while body categories remain in the existing lower32 project slots.
Queries therefore select project layers independently of a body's pair-collision
mask. This does not add an authored layer or change body-pair collision selection.

Commands share the existing 4096-entry body FIFO with explicit domain tags.
PostPhysics commands apply at the following admitted boundary. Limits reject
without partial SDK output. Overlap results are sorted/unique, limited to 256
bodies and 16384 callbacks. Reads neither wake bodies nor flush queued commands.
A teleport's clear-velocity option clears linear and angular velocity; it does
not silently discard earlier one-step loads from the command queue.

Copy solid-contact and sensor Begin/End observations before native buffers expire.
Keep stable slots for the tick and expire them at admitted synchronization. Each
pair includes canonical EntityRefs and simulation epochs. An End can identify a
retired body with current availability false; it must never resolve through a
replacement epoch with the same UUID. Native solver/sensor events are not a
durable geometric-overlap history. No generic event manager is introduced.
Adoption must complete before the next synchronization or step. This preserves
native event-buffer lifetime, including Ends generated between simulation ticks.

### Swept 2D shapes

`PhysicsService::shape_cast_2d(PhysicsSweep2D, filter)` returns the closest copied
`PhysicsHit2D` from the synchronized native world. It uses the same centered
geometry as overlaps: Box full XY dimensions0.01..1000m; Circle radius0.005..500m
and unused height0; Capsule radius0.005..500m, straight height0.01..1000m and total
height at most1000m. Angle is a finite +Z rotation in radians, bounded to +/-1e6.
Origin and endpoint center coordinates must each be within +/-1e6m. Invalid
requests reject even with no realized world or a zero layer mask.

For a local shape point `p`, the cast tests `origin + Rz(angle)*p + t*displacement`,
where `0 <= t <= 1`. Rotation stays fixed. Displacement may be zero. Native float
shape/skin tolerances apply; the returned fraction is not an exact unslopped
Euclidean distance. For example, a downward displacement `(0,-4)` and fraction0.4
place the cast center1.6m below its origin; the returned position is the native
contact point, not that center. This is a query, not a move, angular sweep or
penetration-recovery operation.

The existing layer mask and sensor policy apply. Optional `ignore` excludes all
shapes of one complete canonical body reference, including a painted compound
floor. A reference with no realized body simply matches nothing. Native initial
overlaps report fraction0 and zero normal; their output does not describe a
depenetration vector. `ignore_initial_overlap` skips all native fraction0 hits,
including touching, so an initial side-wall contact need not hide a later floor
hit. Equal-fraction selection follows native traversal and has no stable tie-order
guarantee. The query shares the existing16384-callback bound and checked body
bindings. It neither flushes queued commands, wakes bodies nor advances the clock.
Authored body changes/removal adopt at the normal synchronization boundary.

The original **Movement2D** project (see `Examples/Movement2D/README.md` in the
combined editor package) supplies
an editable C++ walk/jump consumer with painted and ordinary box floors, project
input actions and Inspector settings. Its Dynamic capsule remains the sole pose
writer. The support sweep does not introduce a native CharacterController2D,
automatic stair climbing, navigation or a second physics owner. Core and matching
installed-SDK both-host checks cover the consumer.
[Build261022-000204](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/39761476803)
passes the complete Windows/Linux source matrix, native editor, combined-package
and final editor C++ workflow gates. The source-free relocated game checks native
SDL movement, a 1.822136 m jump, painted support and held-jump suppression; its three
rest/jump/landed WARP captures were reviewed. Physical GPU acceptance is separate.

## Matching C++ SDK

The existing Physics capability now includes distinct 2D callbacks at the tail of
`ForgeSdkWorldV1`, with header-only `sdk::Client` helpers. Rebuild modules against
the matching SDK fingerprint; native ABI 1 and authored identity formats remain
unchanged. No Box2D header, native ID or application pointer enters this boundary.

- `move_2d` queues an XY/radian teleport or Kinematic target. It preserves source
  depth and signed scale; clear-velocity applies only to a teleport.
- `apply_2d` takes `ForgeSdkPhysicsLoad2DV1`. Force/Impulse use the two linear
  components (N/N*s); Torque/AngularImpulse use the angular scalar (N*m/N*m*s).
  Unused channels and unused point values must be zero. An optional absolute XY
  point is supported for linear loads only.
- Separate linear/angular velocity helpers use m/s and radians/s. State and point
  velocity reads copy current realized values without flushing commands or waking
  sleeping bodies. State includes the private simulation epoch.
- Ray and box/circle/capsule overlap queries copy results with existing project
  layer masks and optional sensor inclusion. Overlaps return complete sorted,
  unique canonical references, at most 256. Insufficient caller capacity returns `-2`
  with cleared valid output slots. Invalid size/capacity buffers are not written.
- `shape_cast_2d` takes a copied exact-sized `ForgeSdkSweep2DV1` and returns1 hit,
  0 miss or-1 invalid/context. Shape numbers match2D overlaps; dimensions, angle,
  origin and displacement follow the sweep contract above. Both ignore UUID
  strings empty means no exclusion; otherwise both must be complete valid UUIDs.
  `include_sensors` and `ignore_initial_overlap` accept0/1. Valid-size hit outputs
  clear on miss/failure; invalid-size output is untouched. The request is copied
  before clearing output. Owner-thread fixed execution and Physics permission
  apply. This appended callback changes the exact SDK fingerprint/table size;
  rebuild matching modules. Native ABI1 and authored formats stay unchanged.
- PostPhysics collision reads expose the current tick's stable, nonconsuming
  slots. Sensor is first, visitor second; solid pairs are canonical. Retired End
  events retain their UUIDs/epochs and report availability separately. No old
  fixed-tick notification is replayed in Gameplay.

Calls require the fixed owner thread and Physics permission. Commands return `1`
when queued and `0` when rejected. Sized reads return `1` available, `0` unavailable and `-1`
invalid/context. Valid-size outputs clear on a miss or failure; point and ray
inputs may share their output storage. All body commands retain one shared FIFO.

The original `sdk/sample/physics_2d_probe.cpp` is a boundary acceptance fixture.
It checks load equations, output clearing, aliases, stable event slots, replacement
epochs and context/permission rejection through both actual process hosts. Its
headless SDK Play fixture uses the normal candidate fetch/acknowledgement and a
simulated editor release observation; it is not physical cursor/GPU acceptance.
The ordinary native-process host retains known reflected/solver recovery. Exact
SDK Play still rejects arbitrary native-state recovery and publishes no recovery
payload; this integration does not add that feature.

An installed-only external CMake project compiles both fixture variants through
`ForgeNativeSdk::Client` and runs the same checks against the matching runtime.
The SDK install retains the exact Box2D MIT notice and contains no Box2D headers.
Local GCC/shared-Flecs checks and the Windows shared-game gate exercise these
modules. Neither alone establishes acceptance of the final combined editor ZIP.

## Capacity, recovery and packaging

The first profile targets 1024 2D bodies within the existing 8192 combined rigid/
character limit. Account retained contacts and new pair work against 4096 admitted
checks. Retained solid contacts reserve work; sensor overlaps are charged by the
native query that rebuilds them each step, without counting retained sensors a
second time. Native reserve hints do not bound allocation. A counting custom filter
can reject excess pairs before native contact/sensor insertion; its overflow flag
must fault the step before adoption. CCD and retirement paths remain validation
requirements. The copied 2D event list is bounded to 16384 slots.
Preflight also bounds retained retired-shape records to16384 and pending native
destruction-End estimates to8192, reserving the remaining8192 for old sensor Ends
and new admitted transitions. A local1024-body test maintains4092 sensor overlaps
across ticks and copies all8160 End/Begin observations when1020 visitors are
replaced. A separate16384 zero-dt retirement check rejects further replacement
before native writes, then permits it after adoption releases that window.

Private checkpoint6 preserves native 2D bytes, admitted configuration, private epochs,
next epoch and queued domain-tagged commands inside the existing owner. Native
snapshots scrub hooks and do not provide fresh-world application bindings.
Ten-character native debug tags encode the record epoch, bounded to 60 bits;
supported queries reacquire candidate handles before checked hook rebinding.
Capture requires synchronized adopted state, including completed retirement events. A realized empty world retains its next epoch. Native Kinematic target and actual solver poses are preserved separately because target integration promises a close pose, not exact arrival.
No native ID internals are modified, no authored identity is added and no native
snapshot becomes a game-save format. Transient adopted events are not replayed.

For compound Maps, fresh supported body-shape enumeration must match the complete
geometry multiset before rebinding every shape hook to its canonical body/epoch.
Shape enumeration order and native shape IDs are not authored identities.
The outer private ordinary-Play checkpoint is version2 and includes the existing
Map owner's version1 block. Restore exact source/definition revisions and edited
instance values on the unpublished candidate before restoring Physics. The admitted
Map projection codec is reused; the block has an8MiB JSON budget and64MiB copied-cell
budget within the unchanged whole-checkpoint limit. Capture counts edited values
as independent reconstructions, even when their live chunks share storage;
unedited source values retain resource sharing. Pending cell requests make
capture unavailable; observations/tokens and arbitrary C++ globals are not restored.
Authored Scene/Prefab/Map formats and ABI1 are unchanged; matching SDKs/modules
must be rebuilt. This does not add arbitrary native recovery to the SDK Play host.

Joint metadata records complete identity/configuration/scaled anchors and native
enumeration order visited by canonical body reference. Fresh supported body-joint
APIs reacquire candidate handles; exact complete settings/endpoints/counts and
default native profile are checked before publication. Duplicate same-pair,
identical-configuration joints retain distinct authored bindings through the
recorded order, including after deletion/recreation. Native body hooks are rebound
only on the candidate; joint hooks stay null. Older exact-build checkpoint versions
reject without changing the published world; no source-format migration is needed.

Retain the combined 2 MiB raw solver / 6 MiB physics / 8 MiB outer checkpoint / 16 MiB process
budgets. Query required native size before allocating a byte buffer. All current
world contents must fit the aggregate envelope; maximum component counts alone
do not guarantee recoverability. A rejected candidate preserves the current
published runtime. The first native work/capacity probes do not replace complete
FORGE lifetime and end-to-end acceptance. Local recovery checks cover all inline shapes, initial/paused/sleeping/moving state, mixed-domain FIFO commands, empty-world epoch retention, native image and metadata rejection, the shared remaining-byte limit, and actual runtime process restart. The1024-body sensor profile restores current overlap state without replaying notifications, copies all8160 replacement transitions, and still faults excess pair work after restoration. These recovery checks remain distinct from installed-SDK, editor and export gates.

Matching SDK modules, both real Play process hosts and ordinary source-free
exports must use the same native integration. Package the MIT notice in editor,
runtime and SDK distributions; no Box2D header enters the gameplay SDK. Original
editable Gallery content, Inspector/Scene guides, actual installed-only C++ and
Windows/export gates are required before this feature is complete. Automatic Tile
Map collision, 2D controllers/navigation, durable saves and streaming are
separate work.
