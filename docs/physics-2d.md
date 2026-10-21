# 2D physics integration contract

**Implementation in progress.** This is the target contract for the current
integration, not a guide to an available editor feature. The Build261021-000195 pipeline targets the
Tile Map milestone and contains no 2D physics. Private XY conversion and native backend/profile fixtures have executed FORGE
tests; body realization is not implemented. [ADR024](decisions/024-2d-physics.md)
records the selected ownership, native boundary and recovery direction.

## Native boundary

Box2D 3.2.0 is selected at immutable
`178495ba35695aefe9c321e569d2277f06db3b90`; CMake builds the private backend and wires its license notices. Runtime integration
is pending. It will remain a private static dependency of the current PhysicsRuntime,
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
| Flags | Enabled, sensor, sleep permission, rotation lock and bullet mode; inactive choices preserved |
| Box | Full XY dimensions centered at entity origin |
| Circle | Radius centered at entity origin |
| Capsule | Radius and straight segment height along local Y; total height is segment plus diameter |

The planned input profile bounds seed/target XY coordinates to ±1000000 meters.
Effective full box dimensions are 0.01..1000 m; circle radius 0.005..500 m. Capsules
require effective straight height at least 0.01 m and total height at most 1000 m.
The pin rejects a capsule segment at or below its native slop threshold; no
implicit conversion to a circle is promised. Density is 0.001..10000 kg/m² and
mass override 0..1000000 kg. Friction is 0..10, restitution 0..1, gravity factor 0..10,
and damping 0..10000/s. These are input guards, not universal stability guarantees.

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

## C++ control and observations

Add explicitly typed 2D methods through the current service/SDK: copied body
state, world XY velocity and angular velocity, forces/impulses with optional
world points, torque/angular impulse, teleports, kinematic targets, closest rays
and bounded shape overlaps. World positions use doubles; angular controls/state
use radians and radians/s. No native pointer, handle or borrowed array crosses
the boundary. Owner-thread, fixed-tick, capability and exact-size checks remain.
Queries support the existing collision mask and sensor inclusion policy.

Commands share the existing 4096-entry body FIFO with explicit domain tags.
PostPhysics commands apply at the following admitted boundary. Limits reject
without partial SDK output. Overlap results are sorted/unique, limited to 256
bodies and 16384 callbacks. Reads neither wake bodies nor flush queued commands.

Copy solid-contact and sensor Begin/End observations before native buffers expire.
Keep stable slots for the tick and expire them at admitted synchronization. Each
pair includes canonical EntityRefs and simulation epochs. An End can identify a
retired body with current availability false; it must never resolve through a
replacement epoch with the same UUID. Native solver/sensor events are not a
durable geometric-overlap history. No generic event manager is introduced.

## Capacity, recovery and packaging

The first profile targets 1024 2D bodies within the existing 8192 combined rigid/
character limit. Account retained contacts and new pair work against 4096 admitted
checks. Native reserve hints do not bound allocation. A counting custom filter
can reject excess pairs before native contact/sensor insertion; its overflow flag
must fault the step before adoption. CCD and retirement paths remain validation
requirements. The copied 2D event list is bounded to 16384 slots.

Checkpoint 5 will preserve native 2D bytes, admitted configuration, private epochs,
next epoch and queued domain-tagged commands inside the existing owner. Native
snapshots scrub hooks and do not provide fresh-world application bindings.
Ten-character native debug tags encode the record epoch, bounded to 60 bits;
supported queries reacquire candidate handles before checked hook rebinding.
No native ID internals are modified, no authored identity is added and no native
snapshot becomes a game-save format. Transient adopted events are not replayed.

Retain the combined 2 MiB raw solver / 6 MiB physics / 8 MiB outer checkpoint / 16 MiB process
budgets. Query required native size before allocating a byte buffer. All current
world contents must fit the aggregate envelope; maximum component counts alone
do not guarantee recoverability. A rejected candidate preserves the current
published runtime. The first native work/capacity probes do not replace complete
FORGE recovery, lifetime or maximum-profile tests.

Matching SDK modules, both real Play process hosts and ordinary source-free
exports must use the same native integration. Package the MIT notice in editor,
runtime and SDK distributions; no Box2D header enters the gameplay SDK. Original
editable Gallery content, Inspector/Scene guides, actual installed-only C++ and
Windows/export gates are required before this feature is complete. Automatic Tile
Map collision, 2D joints/controllers/navigation, durable saves and streaming are
separate work.
