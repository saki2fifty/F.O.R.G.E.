# ADR052 — Navigation through the existing character controller

Status: accepted.

The positive-lift 3D link extension is defined by
[ADR053](053-navigation-character-jumps.md). The ground and Floating XY ownership
and default profiles below remain in force.

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

## Floating XY refinement

Movement2 selects the existing World-bound Floating CharacterController2D and a
PlanarXY NavMesh. It uses the same route, behavior, avoidance and post-adoption
feedback owners. The existing Native2D Body owns the one-step intent flag, carries
it through capsule replacement and clears it after a completed positive mover
step. Actual clipped velocity and support observations remain available. Manual
XY movement or placement takes precedence in either queue order, including an
already armed request. Grounded platformer gravity and jumps remain game policy;
this selection does not supply them. Explicit links remain unsupported.

Route projection admits a surface; actual planar coordinates establish steering,
corner progress, remaining distance and arrival. A physical centre outside an
eroded mesh edge must approach the admitted goal within the speed budget. Spatial
movement uses projected surface Y with actual XZ; planar movement uses actual XY
and preserves independent visual Z. No navigation path or observation owns a
second physical pose.

Private physics recovery12 is required only for armed or queued XY one-step
intent. It can include existing3D one-step state; otherwise recovery11/10 retains
its prior meaning. Native2D stores an optional true flag alongside its existing
mover intent, under the outer version admission. Navigation recovery8 requires
movement2 and retains recovery7's absence of character translations. Mixed direct
agents and their link/collision profiles retain their own recovery data. Old
Scene/Prefab/NavMesh formats and asset identities require no migration. The
host-only PhysicsService extension requires matching SDK modules; the C callback
table and pinned dependencies do not change.
