# ADR063 — CPU particle opacity over life

Status: implemented with passing local source, native controls, both MCP protocols,
publication/relocation, software graphics and scoped sanitizer checks. Native
Windows acceptance remains pending. This deliberately extends ADR062 with a
separate value profile while retaining older meanings.

## Sampling and source ownership

Values6/`cpu_alpha2` multiply ordinary Material, texture and vertex alpha by a
normalized per-birth opacity. The existing CPU sampler evaluates start/end values
or 2–16 Step/Linear stops at age/lifetime. Opacity does not change birth membership,
motion, RGB, size, seed consumption or clock ownership. Values1–5 keep opacity1.
GPU3/graph4 do not evaluate opacity; their patterns and compute output stay intact.

Source6 retains opacity start/end, curve, local stop UUIDs and an explicit enabled
state across backend changes. Enabling opacity selects CPU sorted-alpha. Disabling
it retains settings and projects values5 when that backend remains selected.
CPU/GPU/Graph selections project only their existing admitted values. Inactive
opacity is still validated with CPU double precision; GPU float-stop restrictions
never apply to an inactive opacity curve. Native controls and the typed
`asset.document.particle_opacity` operation use the same source/history owner.
Generic source patching edits curve values through existing revision and validation
rules. No second curve database, simulation registry or per-particle entity exists.

## Material and instance packet

The opacity profile requires built-in Blend, depth testing and no depth writes.
It reuses ordinary per-particle Mesh sorting and complete-view weighted fallback.
Material owns blending and its base alpha; the particle contributes a multiplier.
No per-particle Material allocation or new pipeline family is required.

Keep the seven-row static instance layout. The lighting basis uses only XYZ;
under new tint flag3, its first row's spare W contains normalized opacity. The
vertex path multiplies Color.a by that lane only for flag3. Flags0/1/2 and their
bytes retain their meanings. Sprite uses its existing distinct packet/profile;
wind/temporal and immutable GPU cooks retain their current interpretation. The
shared CPU instance projection carries opacity for single and fallback draws.

## Compatibility and acceptance

Recipe3/envelope3 retain explicit value-version admission. Values6 require effect3,
alpha1 and the new `runtime_particle_opacity:1` capability. Older runtime kits reject
the new profile before export; CPU opacity needs neither GPU nor graph capability.
No asset/importer identity, earlier source/stop ID, immutable cook, ParticleEmitter
layout or gameplay C ABI changes. No migration is required.

Acceptance covers scalar/Step/Linear math and boundaries, malformed and inactive
source settings, native controls and both MCP protocols, exact kit refusal,
analytical rendered alpha multiplication, independent ages, sorting/cameras/depth,
zero/full opacity and old-profile recovery. Gallery241 reuses Gallery240's glass
Material and must publish/relocate an exact source-hidden three-asset closure and
run its fixed-clock World and production Game renderer. Windows and physical
acceptance remain separate from local software-graphics proof.
