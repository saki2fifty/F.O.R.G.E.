# 2D physics integration contract

**Implementation in progress.** This is the target contract for the current
integration, not a guide to an available editor feature. Build261021-000195 targets
the Tile Map milestone and contains no 2D physics. Local CPU tests cover authored
reflection, scene save/reopen, initial native body simulation, domain separation,
signed/flat parent frames, typed engine controls, queries, copied state/events and
rejected partial steps, and private fresh-candidate recovery. Matching installed
SDK modules also pass local checks through both process hosts. Editor presentation,
Windows and source-free export acceptance remain incomplete.
[ADR024](decisions/024-2d-physics.md)
records the selected ownership, native boundary and recovery direction.

## Native boundary

Box2D 3.2.0 is selected at immutable
`178495ba35695aefe9c321e569d2277f06db3b90`; CMake builds the private backend and
wires its license notices. The initial body adapter is subordinate to PhysicsRuntime;
the full runtime workflow remains incomplete. It is a private static dependency,
with double world positions and ordinary float geometry, rotations and velocity.
The initial native world is single-worker and created only when needed. Existing
3D Jolt behavior, the fixed clock and Physics capability remain authoritative.
There is no collision between the two native domains.

## Authored bodies and shapes

Introduce distinct PhysicsBody2D, BoxCollider2D, CircleCollider2D and
CapsuleCollider2D values in the existing builtin schema family. An enabled body
requires exactly one enabled-domain inline collider and cannot share its entity
with an enabled 3D body or Character Controller. Disabled choices remain authored.

| Setting | Planned meaning |
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

The planned input profile bounds seed/target XY coordinates to ±1000000 meters.
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

## C++ control and observations

Engine-facing typed controls, queries and copied state/events have local CPU tests, including
area-mass/inertia load units, point velocity, owner-thread checks, sleeping reads,
shared command capacity and FIFO target conversion beneath flat/reflected parents.
Matching gameplay SDK callbacks remain in progress; this is not an available
packaged workflow yet.

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
This is local GCC/shared-Flecs evidence, not Windows package acceptance.

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

The development branch implements private checkpoint 5 to preserve native 2D bytes, admitted configuration, private epochs,
next epoch and queued domain-tagged commands inside the existing owner. Native
snapshots scrub hooks and do not provide fresh-world application bindings.
Ten-character native debug tags encode the record epoch, bounded to 60 bits;
supported queries reacquire candidate handles before checked hook rebinding.
Capture requires synchronized adopted state, including completed retirement events. A realized empty world retains its next epoch. Native Kinematic target and actual solver poses are preserved separately because target integration promises a close pose, not exact arrival.
No native ID internals are modified, no authored identity is added and no native
snapshot becomes a game-save format. Transient adopted events are not replayed.

Retain the combined 2 MiB raw solver / 6 MiB physics / 8 MiB outer checkpoint / 16 MiB process
budgets. Query required native size before allocating a byte buffer. All current
world contents must fit the aggregate envelope; maximum component counts alone
do not guarantee recoverability. A rejected candidate preserves the current
published runtime. The first native work/capacity probes do not replace complete
FORGE lifetime and end-to-end acceptance. Local recovery checks cover all inline shapes, initial/paused/sleeping/moving state, mixed-domain FIFO commands, empty-world epoch retention, native image and metadata rejection, the shared remaining-byte limit, and actual runtime process restart. The1024-body sensor profile restores current overlap state without replaying notifications, copies all8160 replacement transitions, and still faults excess pair work after restoration. These CPU checks do not claim installed SDK, Windows, editor or export acceptance.

Matching SDK modules, both real Play process hosts and ordinary source-free
exports must use the same native integration. Package the MIT notice in editor,
runtime and SDK distributions; no Box2D header enters the gameplay SDK. Original
editable Gallery content, Inspector/Scene guides, actual installed-only C++ and
Windows/export gates are required before this feature is complete. Automatic Tile
Map collision, 2D joints/controllers/navigation, durable saves and streaming are
separate work.
