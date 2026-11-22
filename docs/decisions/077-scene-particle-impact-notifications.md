# ADR077 — Copied Scene-particle impact observations

Status: accepted; implementation and the coherent local integration gate passed.
Windows execution and physical acceptance remain separate.

## Ownership and publication

Extend the existing ParticleSceneStep and ParticleRuntime from
[ADR073](073-scene-particle-collision.md),
[ADR075](075-xy-scene-particle-collision.md) and
[ADR076](076-historical-world-particle-trails.md). Opted-in steps copy native
contact identity, centre before departure bias, normal, time and velocities.
The existing bounded query work also bounds observation production. No particle
entities, second World, event manager, registry, clock or presentation query is
introduced. Legacy integration does not collect these records.

RuntimeSimulation advances particle visuals after fixed ECS execution and module
end_tick. Preserve that order. Adopt the detached, at-most8192-record broadcast
only after the complete particle candidate succeeds. Failure leaves the earlier
publication intact. Reset/shutdown clear it. An interval with no new contacts
publishes an empty frame; observations are never replayed as a later interval.

Each record includes canonical emitter/collider references, effect, seed, source
generation, birth index and age. Reads validate those references and the current
emitter binding. Retired slots become unavailable without shifting indexes or
consuming other systems' observations.

## Exact SDK and subsystem boundary

Project code reads the previous completed visual interval during the next fixed
tick, on the Runtime owner thread with Physics permission. The record carries the
completed tick, not the current callback's tick. There is no synchronous callback
into project code while collision queries or candidate histories are incomplete.

The SDK bridge is in forge_core; particles already depend on core. Expose a narrow
read-only private interface through the existing weak ParticleBridge. This keeps
one module-owned runtime and avoids a reverse library dependency. The private
virtual interface is neither installed nor an external ABI.

Append ForgeSdkParticleImpactV1 and two C-shaped tail callbacks to the exact SDK.
Preserve native ABI1 and every prior callback offset. The table size and exact
fingerprint deliberately change; matching module/SDK rebuilds are mandatory.
Borrowed host context, copied POD and output-size/clear/error rules follow the
existing SDK contract. No allocation ownership crosses that boundary.

## Authoring and delivery

Source20 retains Boolean scene_impacts_enabled. Values56–61 explicitly select
3D/XY Mesh, camera-plane or historical World-ribbon observations. Older profiles
retain their meanings. UI and automation/MCP call one source operation and retain
the source document's identity, history, revision and publication owner.

Typed runtime_particle_scene_impacts:1 admission accompanies the selected effect,
alpha, opacity, Scene-query and geometry capabilities. Old or incorrectly typed
kits reject before replacing an accepted export. The editable Gallery System uses
the public SDK and modifies only runtime ParticleEmitter tint after observations.

Resting response is a separate opt-in extension in
[ADR078](078-resting-scene-particles.md), using the same copied records.
Moving-surface continuous collision, two-way impulses, device readback and
synchronous collision callbacks remain outside this contract.
