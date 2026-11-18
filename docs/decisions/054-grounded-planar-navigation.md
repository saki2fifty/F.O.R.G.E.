# ADR054 — Grounded XY steering and opt-in controller gravity

Status: Accepted.

## Existing ownership and compatibility

[ADR052](052-navigation-characters.md) retains its Floating-only movement2 and
zero-default game-supplied XY gravity contract. Add opt-in native automatic
vertical gravity to CharacterController2D, and a separate movement3 for grounded
XY steering. Old source defaults, profiles and identities retain their meaning.
This is an extension, not a replacement character or navigation implementation.

Append `gravity_factor` with default0 and finite range0..10. Zero preserves full
persistent XY velocity and explicit game gravity. A positive factor adds Project
Y gravity at each positive native step; horizontal Project gravity is unsupported
for this upright automatic profile and must reject before either domain advances.
The Native2D Body retains vertical momentum in its existing mover intent. Collision
clipping supplies the next vertical value. There is no second velocity cache,
solver, gravity setting, physics phase or pose authority.

A manual full-velocity command replaces both components before the next gravity
increment. Gameplay may initiate a jump with an upward velocity once, or steer
using the current observed Y velocity. Repeating an explicit Y value replaces that
momentum again. One-step steering expires after a completed positive step while
automatic falling continues. Zero-time synchronization does not integrate gravity;
placement, shape replacement, stance, FIFO/manual priority and recovery retain
their existing owners and explicit clear semantics.

The same effective velocity is preflighted for finite speed, displacement and
endpoints before advancing either native domain, then used for native movement.
Zero-gravity native configurations keep their prior serialized representation.
The appended authored value participates in reflected defaults, source/history,
Prefab ownership and exact installed-SDK identity; no C callback table is added.

## Grounded navigation boundary

Movement3 requires an enabled World-bound Grounded CharacterController2D, positive
automatic gravity, downward vertical XY Project gravity, and a PlanarXY NavMesh
without explicit links. Movement0/1/2 retain their established admission.
Navigation supplies horizontal steering through the existing one-step FIFO and
preserves current vertical momentum. Physics alone owns falling, support, collision,
floor following and stairs. Actual post-adoption feet and static walkable support
establish progress and arrival, never a projected point or requested displacement.
Keep the existing0.01m minimum stopping distance and1mm arrival tolerance.
Grounded corner advancement reuses the native1cm support contact band; it does
not enlarge the chosen arrival radius.

Bound this profile to ordinary static supported ground. Read-only filtered physics
queries guard upcoming support with explicit work bounds. Unsupported ground or
an unreachable vertical goal cannot produce a fabricated arrival or hovering;
manual handoff, pause and cancellation cannot freeze gravity. Existing routing,
behavior, avoidance, observations, authoring, automation and MCP retain their owners.
No second platform graph, physics scene, identity registry or pose setter is added.

Private navigation recovery10 identifies grounded XY selection and can retain
existing physical3D jump markers in a mixed world. Character poses remain solely
physics-owned. Earlier profiles retain their prior recovery meanings; downgrades
and empty or malformed extensions reject before publication.

Grounded XY jump/drop link routing, moving-platform route planning and navigation
streaming remain separate unfinished capabilities. This bounded ground profile
does not establish general platformer reachability or physical device acceptance.

## Acceptance

Require actual fall, landing, jump/ceiling, one-step expiry, manual priority,
recreation, placement, recovery and rejected mixed-domain pre-step atomicity.
Ground routing must prove physical arrival, blockage/resume, support/gap/vertical
limits, cancellation during a fall and independent visual depth. Shared reflected
history/Inspector, both MCP envelopes and actual Play, matching installed SDK,
editable source-hidden relocated Gallery, illustrated manual and scoped graceful
native sanitizer checks belong to the same coherent acceptance gate.
