# Physics-owned positive-lift XY character links

Status: accepted for implementation; extends decisions052,053and054.

The GroundedCharacterController2D navigation profile may traverse positive-lift
PlanarXY links between static physical supports. The Floating selection, ordinary
direct movement, zero-default controller gravity and persistent full-XY manual
velocity retain their meanings. General drop graphs, moving-platform routing and
streaming remain outside this bounded extension.

Native2D owns grounded jump acceptance, vertical momentum, collision and actual
landing. Navigation retains the admitted link and supplies bounded horizontal
steering; it never writes an airborne pose or requires the physical trajectory to
match the displayed intent arc. A copied host observation reports actual takeoff
acceptance. It is not another velocity, body or pose authority.

The existing character FIFO's combined one-step jump kind uses its existing
planar domain tag for XY. Native pending jump speed belongs to the existing mover
record, alongside its one-step movement origin. Manual movement or placement wins
in either queue order, including after zero-time arming and capsule replacement.
Ordinary manual upward velocity continues to implement game-defined jump policy.
No additional SDK C callback or authored component field is required.

Read-only preflight bounds the effective pending jump velocity before either
native domain advances. Static-support acceptance uses the same native support
query/filter owner, with bounded admission work. Native2D consumes the pending
jump against current support before its rigid-body step, so moving XY geometry
cannot change the decision between preflight and application. Its existing mover
step then integrates automatic gravity and applies collision. Zero-time
synchronization does not consume takeoff. A rejected jump cannot manufacture an
acceptance receipt; release or cancellation never suspends native gravity.

Admission requires upright World-bound Grounded2D mechanics, downward vertical
Project gravity and a positive automatic gravity factor. Link lift, available
horizontal speed, fixed-step resolution and static takeoff/landing support must
be checked. Actual descending support and proximity to the exit establish a
successful crossing. Missing or obstructed landings fail explicitly. Pausing
simulation freezes time; setting Speed to zero releases steering while physics
continues. A changed destination is replanned after an admitted flight finishes.

Private physics recovery14 identifies XY automatic jump requests or their native
takeoff receipt. Earlier10/11/12/13profiles remain canonical when unused. Private
navigation recovery11 identifies an active XY physical jump, preserving existing
physical3D jumps in mixed worlds. Recovery retains intent and physics-owned state,
never a navigation flight pose. Empty, malformed or downgraded extensions reject
before native publication. Installed SDK identity follows the established inputs;
host-only additions must not introduce a second compatibility identity.

Implementation acceptance requires actual native takeoff/flight/landing and
obstruction, manual priority/expiry/recreation, cancellation, zero-time arming,
negative admission and fresh-runtime recovery, including mixed3D/XY profiles.
Editor/history, automation/MCP with actual Play, matching installed SDK, preserved
editable/exportable Gallery content and the illustrated manual use the same
owners. Scaffolded or planned paths do not establish completed navigation.
