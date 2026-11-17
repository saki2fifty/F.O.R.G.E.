# ADR052 — Navigation through the existing character controller

Status: accepted.

## Ownership and explicit selection

Extend NavigationAgent with an explicit movement selector. Its absent or zero
value retains ordinary navigation and its physics/controller exclusions. Character
movement requires an enabled upright +Y spatial 3D CharacterController on the same
World-bound entity. Physics alone writes its pose; navigation supplies steering
through the existing physics service and reconciles actual adopted observations.

This deliberately extends ADR031/049/050 for an opt-in ground profile. Ordinary
agents, links, avoidance and behavior destination intent retain their owners.
There is no second controller, pose writer, route manager or physics phase.
Unsupported planes, up axes and explicit link traversal fail before motion is
requested. Simulated jumps and moving-platform routing remain unsupported.

## Request lifetime and observation

Ordinary CharacterService movement stays persistent. Add a host-only one-step
request to the existing PhysicsService/CharacterCommand queue. The character owner
clears its intent after the next positive physics step, preserving actual resulting
velocity/pose observations. Zero-time synchronization is not a simulation step.
Renewing the request each navigation tick supports continuous locomotion; omitting
it stops autonomous intent without a lease registry or late release.

Explicit manual movement, jump or placement in the same pending boundary takes
precedence regardless of queue order. Payloads, references and effective
orientation are preflighted through the existing queue before live changes.
Shape/controller recreation preserves an armed request's one-step lifetime.

Reconcile actual CharacterState after the existing PhysicsAdoptionSystem, before
downstream consumers. Requested points cannot establish corner progress or
arrival. Blocking, partial sliding, manual displacement and unavailable characters
produce actual observations and revalidation rather than a snap to the route.

## Compatibility and recovery

Append the movement selector with zero default, omit zero from canonical source
and keep the authoring default table complete. Old Scene/Prefab identities,
navigation assets, cooked formats and ordinary positive paths retain their
meaning. The public value change requires a matching exact-SDK rebuild; no C
callback table or dependency revision change is intended.

One-step lifetime stays with CharacterRecord and pending CharacterCommand values.
Private physics recovery11 is required only for an armed request or queued
one-step command; unused worlds retain recovery10. Reject downgrade, malformed or
empty extension profiles before publication. Navigation recovery must explicitly
identify character movement and validate authored configuration. Scene/physics
recovery validates and owns the actual controller pose; navigation records no
character translation and never applies its translation setter to a character.
Ground routes replan from that restored pose. There is no duplicate character
configuration/pose database or authoritative observation cache in navigation.

## Acceptance boundary

Completion requires real floor/wall/controller motion, actual pose/arrival,
block/resume, stop/manual handoff in both request orders, rejected modes, controller
recreation and recovery. Existing ordinary-agent/link and character mechanics
remain covered. Authoring, Inspector/live values, Scene/Prefab history, automation/
MCP, matching SDK, an editable exported Gallery and illustrated manual belong to
the same batch. A one-step request alone does not complete character navigation.
Physical Windows/device review remains separately attributable.
