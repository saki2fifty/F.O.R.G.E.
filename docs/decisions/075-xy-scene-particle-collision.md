# ADR075 — CPU particle response in the XY physics world

Status: accepted; implementation and coherent local integration gate passed.
Native Windows and physical-device acceptance remain separate.

## Ownership and compatibility

Extend the existing world-owned CPU Scene response from [ADR073](073-scene-particle-collision.md)
with explicit XY query profiles. ParticleRuntime::Entry retains birth histories,
fixed-update timing, detached candidate admission and contact traces. PhysicsService
owns circle casts against the adopted native 2D world. Rendering consumes the same
world samples, birth basis and ordinary CPU material/geometry paths. No second
World, collider registry, particle entities, clock or presentation-time query is added.

Append source18 and immutable values52/53 for CPU XY Mesh/camera-plane geometry.
The source retains an explicit `scene_collision_2d` selection while inactive.
Only explicit selection introduces this source version; existing sources, profiles,
importer/recipe/envelope meanings and identities retain their prior behavior.
The existing Scene-collision source operation keeps its signature and settings;
an additive space setter selects the retained query world without enabling it.
No component/POD layout or PhysicsService virtual slot changes. These authoring
headers are outside the installed C++ SDK boundary defined by
`cmake/native_sdk.cmake`; the gameplay SDK fingerprint remains unchanged. Active
XY profiles require their own runtime-kit capability checks.

## Contact and motion semantics

Use the existing native circle query with its 0.005–500 m radius and finite XY
endpoint domain of ±1,000,000 m. Layer filters, sensors, geometry readiness,
binding/generation validation and native work limits stay with that owner.
The selected native API uses double world origins and float local geometry;
precision decisions must follow actual native queries, not global-float assumptions.

XY queries ignore visual depth. Birth affine, velocity, acceleration and drag
remain three-dimensional. Contact normals are XY; normal restitution and tangent
loss affect XY only. Z follows the shared analytic motion owner at contact endpoints
and between presentation knots. Derived force/drag in the existing trace supports
that sampling without forcing depth-only motion into additional collision chords.
XY curvature retains the existing bounded-chord policy and shared query budgets.

The existing native query explicitly reports initial overlap, including touching,
as fraction0 with a zero normal. For these new profiles, that sentinel ends the
birth at the current contact time. Do not fabricate a normal or weaken the original
3D profile's normal validation. A native cast's contact slop can report its center slightly inside the true
circle-radius boundary while still returning the target surface witness. Before
XY response, conservatively clip that fraction to the witness supporting plane
at the actual radius, preserving chord/time correspondence. This is particle
response to the existing query contract; it does not change public query semantics
or increase a global departure bias. XY response uses a minimum5 mm departure
skin, because native casts can classify a smaller positive gap as initial contact
at the minimum circle radius. The original3D departure policy remains unchanged.
Other contacts retain the existing rebound limit,
remaining-interval consumption, departure bias and exact contact-death rules.
There is no resting solver, two-way impulse, moving-surface continuous collision,
GPU Scene query, collision callback or historical world-space ribbon simulation.

## Complete workflow

Native controls and shared document automation/MCP must use the same source
candidate, history, revision and publication owners. Stopped views still explain
that Scene response requires Play; they do not start a preview physics World.
New active profiles require an explicit matching-kit XY capability. Inactive
retained source settings do not require that runtime feature.

Acceptance requires actual native circle/geometry/depth and mixed-world lifecycle
tests, existing profile compatibility, shared UI/MCP and source history, unchanged
installed SDK compatibility, an editable Gallery, source-hidden exported content and authentic captures.
Implementation, local verification, native Windows and physical-device acceptance
remain distinct. Local acceptance does not establish native Windows or physical
acceptance.
