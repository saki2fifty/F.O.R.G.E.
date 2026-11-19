# ADR061 — Bounded particle graph programs

Status: implemented in the working feature branch; native cooked Windows
acceptance and the final batch gate are pending. This extends ADR060 deliberately;
it does not change the meaning of CPU1/2 or built-in GPU3 values.

## Single source and compilation owners

Reuse the typed graph Compiler, UUIDs, pure functions, clipboard, layout and canvas
with an explicit particle domain. Shader source4 contains forge.particle.graph1;
MaterialGraphSource/Document retain source/history/publication authority. The
existing Shader worker generates the compute entry, compiles under the Windows
FXC5.1 trust boundary, reflects immutable bytecode and publishes through the current
asset importer. Material source3 and function format1 retain their meanings.
No new graph database, clock, Shader registry or authored runtime compiler exists.

Particle context is local analytic Position/Velocity/Origin, Age/Age01, unmodulated
Size/Color and one seeded birth Random sample after shape sampling. The only
outputs are local Position, Size and linear RGB. Unsupported domain nodes and
future schema versions preserve source data but reject compilation. Pure functions
receive context through typed inputs; their bodies cannot access hidden context.

## Bounds and execution

ParticleEffectSource4 stores a typed Shader reference and immutable local position
extents/maximum size. Its shared operation optionally activates graph evaluation;
CPU2 and built-in GPU3 retain inactive settings without acquiring a runtime Shader
dependency. Active graph projects values4/gpu_graph1 through the existing neutral
recipe3/envelope3. Persisted importer, asset/stop IDs and ParticleEmitter/ABI layouts
remain unchanged. The exact selected graph Shader is a normal runtime dependency.

A required particle_graph_profile1 admits only one64x1x1 Compute entry and its
exact three-resource/constant layout. The ordinary Shader owner verifies source,
compiler identity, cooked metadata and native re-reflection. Ordinary Compute is
insufficient. Runtime selection uses the host's bounded Shader ResourcePool and
leases, with existing catalog epoch, retirement and device cache rules.

Compute writes the existing seven-row instance layout. Graph position/size clamp
to declared finite bounds; nonfinite outputs fall back to analytic inputs first.
RGB clamps to[0,65504], then emitter tint applies. Culling uses these bounds and
full affine/size headroom. Invalid envelopes clear the current submission count;
old buffers cannot become a successful new sample. Existing64draw/1024effect/
8192Scene bounds remain. No feedback, mutable simulation storage or collision is
implied by this stateless graph profile.

## Compatibility and access

Bounds permit XYZ<=1e6m and maximum size<=1e4m, including zero but rejecting nonzero
values that collapse to float zero. CPU curves keep double precision while graph
settings are inactive; activation enforces existing GPU float-stop spacing.
Runtime kits require runtime_particle_graph1 in addition to current GPU/effect
profiles. Native UI and automation/MCP call the same typed settings/evaluation
operations with existing permissions, revisions, Undo and publication semantics.
Source-hidden execution must load cooked bytecode, not reconstruct graph source.
