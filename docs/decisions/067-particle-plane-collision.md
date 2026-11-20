# ADR067 — CPU analytic particle plane collision

Status: implemented with passing local model, native-widget, both MCP protocols,
source-hidden export, software-rendering and scoped native sanitizer checks.
Native Windows collision and physical-device acceptance remain separate.

## Ownership and motion

Extend the existing CPU birth sampler with an explicit emitter-local infinite
plane. The free-flight integrals have one owner shared by ordinary particles,
analytic paths and collision queries. Seeded births, the fixed emission clock,
appearance ages, current full emitter affine and renderer/resource ownership
remain unchanged. Collision does not create particle entities, persistent body
histories, another physics world or a presentation-owned clock.

The admitted plane has a finite nonzero normal, a signed metric offset along its
normalized normal, a constant center radius, restitution and tangential velocity
loss in0–1, and0–16 permitted rebounds. On the next incoming contact the particle
ends. Zero rebounds means kill on first contact. Births initially inside the
blocked half-space end at birth. A nonpenetrating tangency is not an impact.
Zero rebound under inward acceleration ends the particle; with no inward force
the particle can slide or depart. This is a visual particle response, without
rigid-body resting contacts or impulses on Scene objects.

For constant acceleration and nonnegative linear drag, normal velocity is
monotonic and distance has at most one extremum. A bounded root search finds the
first incoming contact, including two crossings between displayed samples.
The renderer does not approximate collision by clamping frame endpoints. Every
query reconstructs the same path from its original birth; seeking requires no
checkpointed particle history. The rebound budget bounds work even at vanishing
contact intervals. Original free-flight IEEE output is separately checked.

## Source, values and consumers

Source10 retains collision settings and an independent enabled flag alongside
the existing Mesh, graph, opacity, camera-plane and trail settings. Explicit
CPU sorted-alpha collision values11 use `cpu_collision1`. Collision selects Mesh
geometry; turning it off restores retained ribbon/camera-plane choices. GPU or
ordinary CPU selection retains inactive settings. Native controls and automation/
MCP must use the same candidate operation, history, validation and publication.

Keep recipe3/envelope3 and the existing importer/asset/local-stop identities.
Earlier sources and value profiles keep their meanings and encoded bytes. The
new values require effect3, alpha1, opacity1 and typed
`runtime_particle_collision:1`; older kits reject them. ParticleEmitter and the
installed gameplay component/ABI contracts do not change for an asset extension.

Restitution/friction never increase speed at a contact. The whole-life bounds use
the initial speed plus the acceleration vector norm, covering redirected motion
in every axis. Ordinary Mesh culling, Material alpha/depth and cross-object sort
remain authoritative. Constant collision radius is independent of visual size.

GPU collision, arbitrary Scene collider response, historical world-space emitter
motion, collision event callbacks and collision combined with ribbons/camera-plane
geometry remain explicit gaps. They must not be inferred from this plane profile.
