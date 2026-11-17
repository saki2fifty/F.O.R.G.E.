# ADR050 — Collision queries in navigation link traversal

Status: accepted. Implemented through the existing navigation, physics, authoring
and live-value owners. Portable native tests, SDK/MCP, Inspector controls and
source-hidden relocated Gallery export are verified. Native Windows and physical
device acceptance remain separately attributable.

## Ownership and compatibility

Extend ADR031/049 with an optional NavigationLinkCollision on an Agent. The
existing navigation owner remains the only movement writer. Its PhysicsService
queries test the currently realized collision world at the existing gameplay,
navigation, physics and final-transform boundary. No body, character controller,
route manager or progress clock is created. Existing physics/character writer
conflicts remain enforced. Surface movement retains its current NavMesh and
avoidance policy; the optional queries guard explicit link crossings.

Disabled or absent configuration, and a zero collision mask, preserve unchecked
traversal. Existing NavigationAgent/NavigationLink/observation and service-table
layouts remain unchanged. New component data uses the current Scene/Prefab/value
owners, not the baked descriptor list. Old source and cooked navigation formats,
IDs and recovery profiles remain valid. The additional public POD requires a
matching exact-SDK rebuild and runtime kit. No dependency revision is changed.

Semantic recovery6 records collision values and ownership using the existing
navigation checkpoint owner, covering temporary native Prefab overrides. It also
carries existing straight/arc continuation. Downgrades, invalid values and changed
component ownership reject. Worlds without this component retain recovery1–5
representations. Collision query results themselves remain derived each tick.

## Query volume and bounded curves

Spatial3D uses an upright world-sized capsule: radius, positive straight height
excluding the rounded ends, and positive clearance above the navigation foot.
PlanarXY uses a world-sized circle at the XY position, retaining visual depth.
Node scale does not scale these query dimensions. The current query owner requires
positive dimensions of at least0.001m. Navigation limits radius/straight height
to100m and clearance to1m. Layer masks are exact unsigned32 values and sensor
inclusion is explicit. Initial overlaps block; there is no depenetration, sliding,
gravity, physical response or moving-platform attachment in this profile.

Straight crossings use linear casts. For an arc interval [a,b], the difference
between the parabola and its parameter-matched chord is purely world Y and has
maximum magnitude h*(b-a)^2. The existing NavigationLinkCurve owns this bound
and bounded subdivision. Inflate the query radius conservatively, rounding
outward at native float precision. Keep padding within half the clearance so
the expanded 3D capsule retains positive floor clearance. Bound casts per
Agent/fixed tick; exceeding the bound reports failure without publishing a
partially checked pose. Internal resource and query validation remain in force.

A hit holds the last admitted on-curve sample, rather than moving to a chord
contact that could invalidate recovery. The existing distance budget, route
continuation, cancellation and retarget rules remain authoritative. Recheck on
the next tick so a removed blocker permits continuation. Blocking observations
and diagnostics use existing owners; collision state does not become another
persisted progress authority. Missing physics cannot silently bypass checks.

## Acceptance boundary

Independent curve-envelope checks and metadata are foundations. Completion also
requires actual3D/XY native casts, straight/arcs, initial overlap, layers/sensors,
clearance, block/resume, lifecycle/recovery and bounded-work refusal. Native
Inspector/history/Prefab/live review, automation/MCP, matching SDK and editable
source/export examples belong to the same engineering batch. Physical Windows
and device review remain separately attributable.
