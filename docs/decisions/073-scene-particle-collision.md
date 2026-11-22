# ADR073 — World-owned Scene particle response

Status: accepted. Local implementation checks, native platform execution and
physical acceptance are recorded separately by validation and delivery owners.

## Ownership and compatibility

Add explicit CPU Scene-collision profiles to the existing Particle Effect family.
ParticleRuntime retains transient birth states and the current fixed interval's
contact traces. RuntimeSimulation supplies its existing fixed update after native
physics adoption. PhysicsService owns filtered sphere queries against realized
3D Scene geometry. Rendering consumes copied observations and retains its existing
Mesh, Material, resource, culling and transparency owners. No particle entities,
second physics world, renderer queries, second emission clock or asset registry
are introduced.

Earlier local analytic, plane, ribbon and GPU profiles retain their exact meanings.
Append explicit source/value profiles and a typed runtime-kit capability; do not
reinterpret earlier versions, recipes, IDs, component layouts or the module ABI.
Scene response is separate from the analytic infinite-plane setting and device
evaluation. Initial Scene profiles support CPU Mesh and camera-plane geometry,
lifetime appearance and ordinary CPU alpha ordering. They do not imply stateful
ribbon history, GPU Scene queries, 2D collision or arbitrary graph motion.

## Births and motion

Emission age, ordinals, seeded births and appearance remain with their existing
owners. At birth, use the emitter's adopted fixed-update affine to transform
position, velocity and acceleration into world space. Retain its linear basis for
visual orientation/scale. Later emitter/parent movement affects new births; it
does not drag existing particles. This samples the adopted pose, without claiming
continuous historical emitter motion between fixed updates. The collision radius
is a positive world-space metric independent of visual size or emitter scale.

Reuse the existing constant-acceleration/linear-drag integration. Native sphere
queries sweep bounded chords of the curved trajectory. Subdivide using its
acceleration bound and a maximum curved interval; this is an approximation, not
an exact curved native sweep. The Scene geometry is the current adopted snapshot;
moving-surface continuous dynamics and two-way rigid-body impulses are excluded.
Layer masks select geometry; sensor shapes are ignored.

Consume the remainder after each impact. Apply normal restitution and tangential
velocity loss independently. Retain contact-time knots for presentation on both
sides of an impact; remove a birth exactly at contact death or lifetime end.
Initially penetrating births end. Zero permitted rebounds means kill at the first
incoming contact; exhausting the limit ends at the next. Sustained zero-normal-
speed contact under inward acceleration ends instead of creating a resting-body
solver. Explicit resting profiles extend this rule in
[ADR078](078-resting-scene-particles.md); older profiles retain it.
A small metric/coordinate-aware departure bias resolves native touching
hits without excluding a whole compound or concave body from later queries.

## Budgets, lifecycle and presentation

Bound Scene-response reservations, births, per-birth casts, World casts and trace
storage explicitly. Reject overflow and failed queries before committing any
particle entry's next age/state; never silently skip geometry or truncate births.
This atomicity applies to the particle update, not to prior gameplay/physics work.
An ordinary pause does not advance time or query physics from presentation reads.
Disable, state inactivation, seed/effect replacement and the existing visual reset
discard corresponding transient histories. Recovery restarts the visual simulation
under the existing particle reset policy; transient particles are not saved bodies.

Presentation carries bounded world-space samples, birth bases and immutable
effect/seed/generation evidence through the existing extraction transport.
Effective Scene projection discards opaque particle-playback fields before the
runtime attaches its native observation. Authored round trips retain unknown
source data; those fields never supply simulation state.
Derive render bounds from those actual samples, since redirected particles and moving
emitters invalidate a current-emitter analytic envelope. Reuse ordinary per-view
camera alignment and cross-object CPU alpha sorting.

Stopped Scene, asset and Timeline previews have no live physics World. They must
explain that Scene response requires Play, without substituting free flight or
starting an independent preview simulation. A complete feature also requires the
native controls, shared automation/MCP operations, editable Gallery, source-hidden
runtime/export checks, authentic capture and reconciled user instructions. Record
implementation, local verification, native Windows and physical acceptance
separately.
