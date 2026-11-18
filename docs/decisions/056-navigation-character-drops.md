# Physics-owned descending character links

Status: accepted for implementation; extends decisions052–055.

An oriented zero-lift Navigation Link whose exit is below its entry may drive a
bounded physical descent for CharacterController and GroundedCharacterController2D.
Its existing endpoints, direction and Arc height provide the intent; another
authored mode, cooked navigation schema or SDK callback is unnecessary. Existing
ordinary straight traversal and positive-lift character jumps keep their meaning.
Same-height and upward zero-lift physical crossings remain unsupported. Author
one-way drops when reverse physical travel is unavailable.

The existing physical-link state retains intent. Navigation supplies bounded
horizontal one-step steering; native physics owns support, walk-off, automatic
gravity, collision, floor response and descending landing. Navigation never writes
airborne Y, invents an impulse or requires a jump-acceptance receipt for a descent.
Support may continue while walking to the edge, or native floor response may reach
lower ground before the endpoint. Completion requires actual static walkable
support near the admitted exit. A missed or obstructed exit cannot count as arrival.

Admission requires upright World-bound Grounded mechanics, downward automatic
gravity, static walkable launch/landing support, horizontal clearance and bounded
fixed-step resolution. Ordinary physical collision remains authoritative. A wall
before walk-off reports actual blockage; it must not fabricate a flight or freeze
gravity. Pausing freezes simulation; zero Speed releases steering while physics
continues. Disable, clear, changed destinations and manual motion retain the same
ownership and priority as existing physical navigation.

Private navigation recovery12 identifies active physical drop intent. Its native
pose and momentum remain physics-owned. Existing one-step/native physics recovery
is sufficient; no additional physics payload is introduced solely for a drop.
Earlier navigation profiles remain canonical when unused, including mixed3D/XY
positive jumps. Empty, malformed and downgraded extensions reject before candidate
publication; native descent is not constrained to a geometric straight chord.

Acceptance requires actual3D/XY walk-off, falling, collision, supported landing,
blockage, cancellation, cold realization, manual handoff, fresh mixed recovery and
focused malformed-data rejection. Editor/history/automation/MCP, matching installed
SDK, editable/exportable Gallery and manual workflows must share the existing
owners. General platformer reachability graphs, moving-platform routing and
streaming remain separate capabilities.
