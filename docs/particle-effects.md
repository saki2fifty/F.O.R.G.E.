# CPU and GPU mesh particle effects

ParticleEffect is a reusable logical asset. A native `ParticleEmitter` references
it. Particles are transient render instances, with no Scene entities or saved
particle UUIDs. Flecs owns the component, the fixed clock owns emission age, and
the shared MeshSceneRenderer owns drawing and GPU residency.

## Source and publication

Sources use `.particles.json`, format `forge.particle_effect`. Existing version 1,
`cpu_mesh1` sources remain valid. New sources start with version 2, `cpu_mesh2`;
explicit GPU evaluation uses source3/profile `gpu_mesh1`. Authored particle
graphs use source4 and active values4/profile `gpu_graph1` (see below). Source5
adds explicit CPU values5/profile `cpu_alpha1` for sorted transparent meshes.
Source6 adds CPU values6/`cpu_alpha2` for opacity over life while retaining inactive
opacity and graph settings across evaluation changes. Source7 adds camera-plane
CPU values7/`cpu_billboard1` with retained original Mesh and alignment selection.
Source8 retains separate CPU/GPU alignment selections and projects GPU values8/
`gpu_billboard1` or graph values9/`gpu_graph_billboard1` when GPU alignment is active.
Source9 retains analytic trail settings and selects CPU values10/`cpu_trail1`
when trails are active. Source10 retains a collision plane/enabled flag and selects
CPU values11/`cpu_collision1` for Mesh collision. Source11 retains explicit
`gpu_weighted_enabled` and projects GPU values12–15 for weighted Mesh/Graph and
camera-aligned combinations. Source12 adds `gpu_opacity_enabled` and projects
explicit GPU opacity values16–19. Source13 adds independent `gpu_collision_enabled`
and explicit GPU plane values20–22 sharing the retained `collision` object with CPU.
Source14 adds independent `gpu_trail_enabled` and explicit GPU ribbon values23–25,
sharing the retained trail settings with CPU. Source15 adds `gpu_sorted_enabled`
and explicit ordered GPU values26–37. Source16 adds retained Scene-collision settings
and CPU values38/`cpu_scene_collision1` or39/`cpu_scene_collision_billboard1`.
Source17 retains GPU Material-palette selection with explicit values40–51.
Source18 retains Boolean `scene_collision_2d` and explicitly selects CPU XY values52/
`cpu_scene_collision_2d1` or53/`cpu_scene_collision_2d_billboard1`.
Source19 retains `world_trail_enabled` and selects historical Scene ribbon values54/
`cpu_scene_trail1` or55/`cpu_scene_trail_2d1`.
Source20 retains Boolean `scene_impacts_enabled` and selects copied Scene-impact
values56–61 for 3D/XY Mesh, camera-plane particles and historical World trails.
Sorted and weighted GPU transport selections
are mutually exclusive; inactive shape, opacity, Graph, plane and trail settings
remain retained across transport changes.
Other backends retain inactive settings without
applying them; CPU/GPU activation is independent with shared endpoints/stop IDs.
Values include a Mesh reference, optional Material override, emission
mode, capacity, burst count, rate, lifetime, direction/cone half-angle, speed range,
constant acceleration, start/end size and linear RGB. Version 2 adds spawn shape,
linear drag and optional normalized-lifetime size curves and RGB gradients.
Extension metadata survives
source history but is excluded from cooked execution values. Admission limits are
64 KiB, 4,096 JSON nodes and depth 16. Invalid edits leave the document intact.

AssetSourceDocument owns edit/Undo/Redo/Save. Existing import/publication owners
update immutable cooked selection and catalog/index transactionally. The artifact
contains `effect.json`; exact Mesh/Material dependency revisions and source
generation are verified. Failed publication retains the selected working revision.
Runtime packages include selected cooks and dependencies, requiring neither
editable sources nor a compiler.

New cooking uses `forge.particles.mesh.v3` and a version3 cooked envelope containing
`format`, `version`, `asset_id` and `values`. Values are explicit CPU2, GPU3, graph4
or sorted-alpha CPU5/opacity CPU6/camera-plane CPU7/GPU camera-plane8/graph camera-plane9/
analytic-trail CPU10/plane-collision CPU11/explicit weighted GPU12–15/GPU opacity16–19/
GPU plane20–22/GPU ribbon23–25/ordered GPU26–37/CPU Scene collision38–39/
GPU Material palettes40–51/CPU XY Scene collision52–53/historical Scene trails54–55/
CPU Scene-impact observations56–61/resting CPU Scene response62–73;
legacy CPU1 normalizes to CPU2 without rewriting editable source. Publication
metadata carries envelope `version:3` and the exact `value_version`. Source/stop
UUIDs and extensions remain source-only. The persisted importer identifier remains
`forge.particles.cpu-mesh`: selecting evaluation must not change saved source
settings or sidecar identity.

Existing immutable v1/v2 artifacts retain their exact original recipes and raw
value profiles. Resource admission checks recipe/envelope/metadata/value versions,
source/dependency digests and selected generation together; mixed combinations
reject. Publishing creates a new immutable selection through the same existing
owner. It does not migrate earlier sources or overwrite old artifacts. New
publications require `runtime_particle_effect:3`; GPU values additionally require
`runtime_particle_gpu:1` in the selected runtime kit. The exporter rejects a
missing/mismatched capability before delivery. See [ADR060](decisions/060-gpu-particle-profile.md).

Source lifetime stops contain `id`, `time`, `value` and `interpolation`. Each
nonempty channel has 2–16 stops, strictly ordered distinct times and endpoints
exactly 0/1. UUIDs are nonempty and unique across both channels, scoped to the
effect root; they survive edits/history/save and remain independent after a root
copy. Cooked stops contain only time/value/interpolation. Unknown fields, missing
source IDs, duplicate IDs and unknown interpolation reject atomically. Extensions
are opaque source metadata, with no runtime interpretation or reference guessing.

All profiles admit one static triangle part, one LOD and one material slot with
built-in PBR or Unlit Materials. CPU1/2 and GPU3/4/8/9 require opaque/masked Materials.
CPU5 additionally admits Blend with depth testing and no depth writes; CPU6
requires this Blend profile for opacity. CPU7 also permits opaque/masked Materials
with identity opacity; nonidentity opacity requires the same Blend profile. Skinning,
morphs, point/line topology, transmission and custom Shader surfaces reject with
diagnostics. CPU10 additionally requires double-sided Quad geometry; it permits
opaque/masked Materials with identity opacity or the same sorted Blend profile.
CPU11 permits the same Material alpha/opacity profile as CPU7, using the original
Mesh and its explicit local collision plane. GPU12–19 require Blend with depth
testing/no depth writes and complete-view weighted admission;16–19 apply explicit
per-birth lifetime opacity. GPU20 admits opaque/masked Mesh plane response;21/22
require weighted Blend, and22 also applies lifetime opacity. GPU23–25 describe
double-sided Quad ribbons with opaque/masked, weighted Blend and weighted opacity
variants. GPU26–37 require Blend and complete-view ordered admission, with retained
Mesh/Graph/camera/plane/ribbon choices as listed below. General Scene-collider
response, Graph trails, historical emitter-motion capture and texture-sheet animation
are outside these profiles. The bounded stateless graph profile below extends
position, size and RGB without adding those capabilities.

## Sorted-alpha CPU profile

[ADR062](decisions/062-sorted-alpha-particles.md) introduces explicit values5 and
source5 without changing earlier value meanings, IDs, recipe3/envelope3 or the
ParticleEmitter/gameplay ABI. `particle_uses_gpu` derives GPU selection from the
immutable profile descriptors, including values3/4/8/9/12–37;
CPU5 keeps the CPU random sequence, double clock and lifetime-stop precision.
Source5 retains optional inactive graph settings across evaluation changes.

CPU5 uses existing per-particle poses and the shared Mesh sort queue, including
ordinary Blend Meshes. Blend particles retain individual submissions rather than
opaque instance batching. Sorting is per Mesh part; intersecting triangles retain
ordinary sorted-alpha limits. Transparent particles trigger existing whole-view
weighted fallback. No GPU sorting/readback or second simulation owner is added.

Material owns alpha and textures. The existing seven-row tint packet uses flag2
for CPU5 RGB modulation without the legacy Unlit blockout light. Older profiles
keep flag1. Single, batched and fallback CPU5 draws share one instance projection.
Publication and renderer admission call the same Material profile validator.
Export requires `engine.runtime_particle_alpha:1` for values5, independently of
GPU/graph support. Older kits reject CPU5; old cooks keep their original bytes.
Gallery240 supplies a shared translucent Material and two colored emitters.

## CPU opacity over life

[ADR063](decisions/063-particle-opacity.md) extends sorted-alpha evaluation with
explicit values6/`cpu_alpha2`. The existing CPU sampler evaluates normalized
start/end opacity or 2–16 Step/Linear stops at age/lifetime. Material, texture and
vertex alpha multiply this per-birth value; RGB and size remain independent.
Opacity does not consume random numbers or change births, motion or clocks.

Source6 retains `opacity_enabled`, `opacity_start`, `opacity_end`, `opacity_curve`
and local stop UUIDs, including across GPU/Graph switches. The compiled projection
contains opacity fields only for active values6. Disabled CPU alpha projects values5.
Inactive opacity remains validated with double CPU precision. Enabling opacity
selects CPU sorted-alpha through one shared native/MCP source operation; ordinary
validated patch/history operations edit its start/end and curve fields.

The seven-row packet remains unchanged: new tint flag3 admits opacity in the first
lighting-basis row's unused W lane; the vertex path multiplies Color.a. Existing
flags0/1/2, Sprite/wind/temporal profiles and GPU cooked output keep their meaning.
No per-particle Material copy or second queue is introduced. Values6 require the
independent `runtime_particle_opacity:1` capability plus effect3/alpha1. Gallery241
reuses Gallery240's glass Material with a separate editable fading effect.

## Camera-plane CPU profile

[ADR064](decisions/064-camera-plane-particles.md) adds explicit values7 and source7.
The shared CPU sampler remains unchanged; detached per-view transforms align the
fixed Engine/Quad with admitted camera right/up axes. Width/height retain original
world X/Y column lengths, while the complete emitter affine still positions births.
The source Mesh, enabled opacity and graph selection survive backend switches.
Disabled opacity projects identity values without discarding source stops.

Instance and part culling, sorting/parity and actual geometry share one view-local
world matrix. The lifetime envelope pads transformed motion with an isotropic
quad radius, covering reflected/sheared and zero-Z-scale emitters from every view.
Static overrides reject skin/wind consumers. Matrix storage belongs to each view,
counts toward the existing CPU budget and never mutates cached simulation poses.

The native **Camera-aligned quads** control and
`asset.document.particle_billboards` share validation/revision/Undo/Redo ownership.
The active Mesh picker is read-only; toggling alignment restores the original Mesh.
Values7 require effect3/alpha1/opacity1 and `runtime_particle_billboard:1` in the kit.
No dependency, gameplay ABI, importer or earlier immutable artifact changes.

## Camera-plane GPU profiles

[ADR065](decisions/065-gpu-camera-plane-particles.md) adds explicit values8/9.
The original built-in or authored evaluator retains its 47-row constants, RNG and
seven-row device output. A second bounded compute dispatch projects those same
rows in place. Private camera constants and the existing same-UAV barrier order the projection
(the upload also holds appearance under ADR069). Width/height retain original world-column lengths;
camera right/up/minus-forward determine orientation. Device centers and RGB stay
unchanged. No CPU readback, particle transform array or second simulation is used.

The shared renderer supplies per-view placeholder parity and lifetime bounds,
including reflected/sheared and zero-Z emitters. Tint flag2 applies Material RGB.
The published instance count is cleared before preparation and becomes visible
only after both dispatches succeed; existing target restoration covers failures.
Values8/9 remain opaque/masked without lifetime opacity. Explicit weighted and GPU
opacity profiles below admit those capabilities. Cross-emitter/ordinary-Mesh
transparency cannot be replaced with emitter-only sorting.

Source8 retains `gpu_billboard_enabled` independently from CPU `billboard_enabled`,
opacity stops, the original Mesh and graph selection. Reading source7 never enables
GPU alignment implicitly. Enabling GPU alignment preserves an active Graph or
selects built-in GPU evaluation; disabling restores GPU3/graph4 Mesh geometry.
The native checkbox targets the selected backend. Automation's optional
`backend:"gpu"` calls the same source operation; omission preserves the CPU default.
History, publication, grants, revisions and identities retain their existing owners.

Values8/9 require effect3/GPU1 and typed `runtime_particle_gpu_billboard:1` support;
values9 also require graph1. CPU alpha/opacity/billboard capabilities are independent.
Missing, malformed and unsupported kit values reject before distribution replacement.
Gallery243 supplies a built-in effect and shared masked Material; Gallery244 reuses
the Material and Gallery239 program. Their texture remains Gallery242's original
logical asset. The existing Windows package preparers cook the Shader before the
graph Effect. Actual graph9 bytecode realization and export require native Windows
acceptance, separately from generated-program software-device tests.

## Emitter-local analytic trails

[ADR066](decisions/066-analytic-particle-trails.md) defines values10/`cpu_trail1`.
Source9 retains boolean `trail_enabled` and a typed `trail` object with `duration`
(seconds, 0.001–30) and `segments` (integer, 1–16). Enabling selects CPU sorted alpha;
disabling restores the retained CPU projection. Original Mesh, program, CPU/GPU
alignment flags, opacity and local stop IDs remain independent. Optional operation
settings replace duration/segments together; omission retains them. Invalid edits
leave source/history unchanged. Source9 never downgrades during inactive GPU edits.

The shared seeded birth/age/window evaluator supplies detached historical samples.
There is no reverse integration, additional RNG sequence, clock or saved history.
Tails survive until life plus duration while appearance uses original life. A flat
point array and ordered birth ranges belong to the current renderer frame. Each
camera derives connected edges using the current full emitter affine and its X
column for width. This does not record earlier world-space emitter motion. Width,
RGB and opacity are sampled and interpolated per segment; Step discontinuities are
represented by these same bounded samples. V spans the visible tail0 to head1.

Each emitter keeps one existing resource/pose proxy; view-owned segments enter the
ordinary append/cull/sort path with explicit world/bounds. A transient segment tie
follows the genuine birth ordinal. Static Mesh packet flag4 carries explicit
endpoint positions/sides, endpoint RGB/opacity and V coordinates in the existing
seven rows. The shared vertex factory retains identical endpoint arithmetic at
joins and the canonical Quad tangent frame. Materials multiply appearance once.
There is no per-segment asset, Material or persistent pose-history authority.

Continuous capacity includes `ceil(rate*(life+duration))`. Reserve `capacity*segments`
against8192 Scene instances; include samples and generated view records in the
existing8MiB CPU frame budget. Actual corner bounds include camera-relative float
padding, and the whole-life envelope includes an isotropic width allowance.
Double-sided built-in Materials are required; nonidentity opacity requires Blend,
depth testing and no depth writes. These CPU values remain distinct from GPU
ribbon values23–25 below; authored Graph trajectories do not define analytic trails.

Native controls and `asset.document.particle_trails` share the source operation,
revision/history/publication owner, schema discovery and permission checks. Values10
retain recipe/envelope3 and require effect3 plus `runtime_particle_trail:1`, alpha1
and opacity1. No GPU, graph or camera-plane kit capability is implied. Older or
malformed kits reject before distribution replacement. Original source/cook bytes,
importer identity, ParticleEmitter and gameplay C ABI remain unchanged.

## Equations and time

Motion is emitter-local before the complete affine transform. Units are meters
and seconds, with +Y up. Cone half-angle is in degrees; sampling is uniform in
solid angle around the normalized direction. For particle age `a`, initial
velocity `v`, acceleration `g` and lifetime `L`:

```
position = v*a + 0.5*g*a*a
velocity = v + g*a
u = a/L
size = lerp(size_start, size_end, u)
color = lerp(color_start, color_end, u)
world = emitter_affine * translate(position) * uniform_scale(size)
```

Point origins are zero. Box samples its full XYZ dimensions uniformly; Sphere
samples its volume uniformly; Disc samples area in the plane perpendicular to the
normalized Direction. Box dimensions and Sphere/Disc radius are in meters. Shape
draws follow the original velocity draws; Point with default extended values
preserves the v1 random sequence and ballistic equations exactly. Origins receive
the emitter affine once, together with motion and mesh size.

Drag `d >= 0` is linear velocity damping in reciprocal seconds:

```
alpha = (1 - exp(-d*a)) / d
beta = (a - alpha) / d
position = origin + v*alpha + g*beta
velocity = v*exp(-d*a) + g*alpha
```

The zero-drag branch uses the original ballistic math plus origin. `expm1` and a
small `d*a` series for beta prevent cancellation for tiny drag. Drag applies to
the velocity produced by acceleration too; it is not a per-frame multiplier.

An enabled size curve or color gradient evaluates at normalized age `u = a/L`.
Linear blends to the next stop; Step holds until the next stop's exact time.
An empty channel uses its original start/end fields. Enabling starts from those
fields; adding a stop splits the largest gap and retains the interpolated value
and outgoing mode. Endpoints keep their times but their values remain editable.
Interior times must stay between neighbors. Disabling restores the retained
fallback fields; Undo restores the exact previous stop identities and values.

Burst births are at time 0 and expire at `a >= L`. Continuous ordinal
`n = 1,2,...` is born at `n/rate`. Admission compares time and lifetime in the same
emission-phase units, avoiding rounded birth-boundary ambiguity. Only the bounded
active ordinal window is admitted, including at large times. In CPU profiles,
seed and the 64-bit emitter-scoped ordinal initialize a standard-library random engine per birth.
Repeated local evaluations agree; cross-platform lockstep is not a contract.

Fixed ticks advance previous/current age by `fixed_dt * playback_speed`.
Presentation interpolates those ages without advancing simulation. Speed 0
freezes the emitter. Reference/seed changes, disable, removal or native entity
generation changes restart or retire it. Recovery restarts visual emission from
restored native values, without serializing particle arrays. Captures contain
compact time, revision/generation and seed; selection mismatch or seed overflow
rejects before rendering.

## GPU evaluation and renderer lifetime

Source3 uses a separately specified mathematical generator. FNV-1a hashes the
little-endian seed32/ordinal64 bytes; a48271/2147483647 linear congruential sequence
produces birth samples. The independently authored HLSL implements bounded32bit
Schrage arithmetic, checked against a64bit modular reference. It evaluates Point,
Box, Sphere and Disc origins, cone velocity, analytic acceleration/drag, lifetime
Step/Linear size/RGB and complete affine instance transforms. CPU profiles retain
their original standard-library sequence; backend/profile visual patterns differ.
Float GPU evaluation does not provide cross-device lockstep.

CPU double precision determines only the active ordinal window and short relative
ages from the existing presentation clock. No billion-second float clock, second
World, per-particle entity, CPU transform array or independent simulation manager
is introduced. Source3 stop times must remain distinct as floats; malformed or
unrepresentable packets reject before publication or drawing.

The existing renderer's ParticleSource retains strong effect/Mesh/Material leases,
selection epoch/revision/generation and one bounded per-emitter vertex/UAV buffer
realization. Presentation retains shared compute shader/pipeline caches. One proxy
per emitter uses the existing seven-row static instance layout; MeshDraw accepts
an exclusive validated buffer range and preserves the64-instance draw bound.
Each camera gets its own compute evaluation and target/viewport restoration.
Lifetime bounds, including the admitted mesh and float view padding, govern GPU
culling. Compute cannot be merged with CPU batches, temporal meshes or indirect
visibility packets. Particles continue to exclude shadow casting and temporal
Mesh accumulation. Unsupported compute is an explicit diagnostic, without a
silent CPU profile substitution. Failed submissions never queue old rows as new
time; removal, Scene replacement and source epochs retire the same renderer owners.

Native **Evaluation** and `asset.document.particle_evaluation` both call the source
operation under AssetSourceDocument history. Both preserve source/stop IDs,
extensions, curves and normal revision/grant/Undo/publication semantics.

## Per-emitter appearance

`ParticleEmitter::size_multiplier` is a finite factor in [0.01,16], default 1.
`tint_r`, `tint_g`, `tint_b` are finite linear RGB factors in [0,1], default 1.
Size multiplies the evaluated lifetime size; tint multiplies the evaluated
gradient. Neither changes origins, velocity, birth count, age, seed or resource
selection. The emitter affine transforms position and size once. Conservative
bounds include the peak mesh size after multiplication. Several emitters can use
one immutable effect and mesh/material bundle.

The ordinary Inspector edits saved values with normal Undo/prefab override rules.
Live gameplay exposes only these four appearance fields for particle emitters.
Owner-thread commands validate world generation, Scene identity, entity and field
range. Observation adopts edits without advancing emission clocks; Stop/next Play
reconstructs from authored values. Invalid edits retain valid state. Older saved
values backfill neutral defaults on admission without rewriting source files or
changing formats/identities. Unknown extensions remain preserved.

The installed C++ `render_components.hpp` includes the trailing fields. This
changes the exact native SDK fingerprint and value layout: rebuild gameplay
modules against the matching SDK. Mismatched modules remain rejected. The C ABI
version and entry/function table layouts remain unchanged; private sampling and
runtime headers are not installed as gameplay APIs.

Gallery57 demonstrates six differently sized/tinted emitters sharing one effect.
Exports include compiled effect values and saved emitter parameters; no editable
effect source or compiler is required to run the game.

## Limits and ownership

Capacity is 1–1,024; rate is 0.01–256/second; lifetime is 0.01–30 seconds; speed is
0–1,000; size is 0.001–100; cone half-angle is 0–180 degrees. Continuous admission
requires `ceil(rate*lifetime) <= capacity`, or the extended trail window above
for CPU10. Burst count fits capacity when Burst
is selected; the inactive mode's count remains editable. Playback speed is 0–16;
visual time is 0–1e9 seconds.

Shape dimensions/radius are 0.001–1,000 meters; drag is 0–64/second. Curve sizes
use the same 0.001–100 limit; gradient RGB is linear and bounded to [0,1], with
no opacity channel. Inactive values are still admitted and retained. Dynamic
curve storage counts toward the effect ResourcePool byte budget.

A scene admits 64 enabled emitters, 8,192 reserved instances (segments for trails) and 8 MiB retained
particle payload per frame. The existing bounded ResourcePool owns immutable
leases. Requests retire when no emitter needs the reference; late/cancelled
completion cannot revive removed state. Catalog admission is lazy, allowing
asset-free runtimes without a project catalog. Readiness includes queued/loading
requests and completed resources not yet acquired by presentation.

Drawing reuses Mesh/Material/Texture resources, camera-relative uploads,
parity-aware sorting, the shared 64-instance batch limit and fence ownership.
Ordinary meshes and emitters may coexist on an entity. Camera culling, visibility
and layers apply; hidden effects keep aging. This profile does not cast particle
shadows. A conservative outward-rounded lifetime envelope includes maximum speed,
acceleration, origin extent, peak curve size and the full emitter affine for preview framing; actual
CPU instances/segments are culled during drawing; GPU emitters use their lifetime envelope.
Scene-collision profiles instead derive bounds from copied World samples; the current
emitter's lifetime envelope cannot enclose earlier births after emitter movement.
Unsupported profiles and overflow produce
diagnostics instead of successful silent truncation.

The central Particle Effect document owns source controls and analytic isolated playback,
seek and reset. Preview uses the same evaluator/resources with one private draft
override; it does not mutate or tick live Play. Scene-collision profiles require Play;
stopped Scene/asset/Timeline views have no physics simulation or analytic fallback.
Assignment is one Scene Undo
operation. C++ edits the native value through the matching SDK. Public value/header
changes require matching module rebuilds; ABI 1/C function-table layouts remain.

Version2/3 source/evaluator values do not add native ParticleEmitter fields,
installed SDK inputs or ABI changes. Existing matching C++ modules still adjust
the emitter component; they do not need access to source stop arrays.

See [the user guide](../manual/editor/particles.md),
[particle graphs](../manual/editor/particle-graphs.md) and Gallery scenes54,56,238,239.
Native Vulkan readback evidence is separate from Windows/editor physical acceptance.

## Focused validation and capture provenance

`particle_gpu_vulkan` checks1,344parameter configurations and89,376particles against
independent uint64/double reference math, with maximum observed absolute row error
5.34058e-5 on software Vulkan. It also compares two exact CPU/compute-written
MeshDraw pixel pairs and rejects malformed buffer ranges. `particle_gpu_scene_vulkan`
uses actual MeshSceneRenderer and fixed-clock World presentation for source-hidden
Windows-targeted content, without claiming native Windows execution. The Gallery
portion checks576instances in10draws sharing one bundle; authored and source-hidden
package images match exactly. Existing CPU scene and codec tests retain compatibility.
`particle_effect_editor` uses actual widgets/history at regular/compact and scaled
layouts; `mcp_particle_evaluation_stdio` exercises both supported MCP formats.

The canonical manual image `manual/images/gpu-particles.png` is a lossless P6→PNG
conversion of `particle_vulkan_probe --gpu` Gallery238 at1.5fixed-clock seconds,
960×540, using the production MeshSceneRenderer and pinned Diligent Vulkan backend.
It depicts rendered content, without editor chrome or retouched controls. Capture
provenance: local focused native build31/check32 before source-only documentation
and sample promotion; software Vulkan, DX Shader Compiler1.9 observed in native logs.
The sample's source/cook bytes are exactly those exercised by that candidate gate.
Final build44/check45 uses expanded low-drag coverage and produces a Gallery image
byte-identical to that canonical capture; unchanged capture bytes are retained.
This establishes neither physical hardware performance nor Windows/whole-editor
visual acceptance. Stateful VFX simulation graphs and complete particle-system equivalence
are not claimed.

## Authored particle program profile

[ADR061](decisions/061-particle-graph-programs.md) extends the existing owners.
Shader source4 contains forge.particle.graph1 and compiles with the shared typed
graph Compiler. Root bounds are256nodes/1024edges/16functions; pure-function
expansion is limited to1024nodes. Version/domain validation occurs before
expansion; unsupported versions stay opaque in source/discovery and fail compile.
Material source3 and function format1 are unchanged.

`ParticleGraphProgram` contains one Shader AssetRef, symmetric local XYZ extent
and maximum unmodulated size. `set_graph_program(program, activate)` performs one
validated source/history operation. With activate=false, CPU2 or built-inGPU3
retains source4 settings but projects ordinary values and has no graph runtime
dependency. CPU stop times retain their double precision. Activating Graph admits
GPU stop precision and projects values4/profile gpu_graph1. Root/stop IDs, source
extensions, importer identity, neutral recipe3/envelope3 and native emitter/ABI
layouts retain their meaning.

Context and outputs are described in the user guide. Graph extent<=1e6m peraxis
and maximum size<=1e4m are finite/nonnegative, rejecting nonzero float underflow.
Outputs clamp to those bounds; nonfinite outputs fall back to analytic inputs.
RGB clamps to[0,65504] before per-emitter tint. Culling uses declared bounds and
maximum size with full affine transforms and emitter size. Device-envelope
validation retains numeric headroom and clears rejected submission counts.

Active effects declare the ordinary runtime dependency edge `particles.program`.
Import admission loads the selected immutable Shader through request_shader and
requires particle_graph_profile1: one64x1x1 Compute entry with exactly
ParticleValues, ParticleRows and ParticleGraphLimits and the required typed constant
layout. Native realization re-reflects cooked DXBC. The host's bounded Shader pool
retains leases; existing catalog epochs retire effects/programs and stale selections.
There is no authored-source compiler in the runtime and no alternate Vulkan path
for a D3D12 cook. Kits advertise runtime_particle_graph1 in addition to GPU/effect
profiles. See [Shader assets](shader-assets.md) and [standalone](standalone-host.md).

The editable Gallery239 graph uses an original Orbit offset pure function,
Age/Birth Random, shrink-over-life and a color blend. The two emitters share the
program/effect and keep separate appearance. The Windows package preparer cooks
Shader then Effect through the existing worker/publication services. Runtime
packaging follows exactly Scene→Effect→Shader with source hidden.

Focused checks include source/history/domain/version negatives, retained inactive
settings/CPU precision, exact Shader profile/publication metadata, GPU numeric
limits/culling, real canvas/Effect widgets, both MCP protocols and graceful native
owner exits. `particle_graph_vulkan` verifies generated Compute on software Vulkan;
it is not cooked Windows acceptance. `particle_graph_gallery_export` is the
Windows gate for actual worker cook, relocated source-hidden closure, immutable
bytecode realization,64particle numeric readback and production fixed-clock Game
rendering. Native Windows execution requires its own passing recorded run;
generated-program software-device checks do not establish it.

Canonical `manual/images/particle-graph.png` is the actual MaterialGraphCanvas in
particle mode, displaying Gallery239 unchanged. `particle_graph_canvas` renders
ImGui draw data through the SDL software backend at1440×1000 and asserts display
does not edit the source. Local build21/check21 captured it and it was personally
reviewed. No retouch, complete-editor screenshot, Windows or physical GPU claim.

## Analytic CPU plane collision

[ADR067](decisions/067-particle-plane-collision.md) defines source10, values11/
`cpu_collision1` and typed `runtime_particle_collision:1` with effect3/alpha1/opacity1.
Source retains `collision_enabled` and `collision:{normal,offset,radius,restitution,
friction,bounce_limit}`. Enabling selects CPU Mesh collision; disabling restores
retained trail/camera alignment/opacity choices in CPU sorted-alpha. Source version
never decreases during inactive trail/GPU edits. Old source/value/cook meanings,
importer/stop IDs, recipe3/envelope3 and gameplay component ABI remain unchanged.

One extracted free-flight owner supplies stable acceleration/linear-drag integrals
for ordinary births, analytic paths and collisions. A normalized local plane uses
metric offset and a constant0–1000m radius, independently of visual size. Source
normal components are finite within±1e6 with length at least1e-12; offsets are±1e6m.
Restitution and tangent velocity loss are0–1;0–16rebounds are admitted. The next
incoming contact kills, as do penetrating births and sustained zero-rebound inward
contact. Tangent touches are not impacts. With no inward force, a zero rebound
can slide or release. Incoming roots are bracketed across the single distance
extremum and bisected with a fixed iteration bound. Drag-only asymptotic approach
does not create a finite collision when expm1 rounds its excursion to the limit.

The fixed clock still owns birth age. Each sample reconstructs from the same
seeded birth, independent of seek order, and appearance retains original lifetime
normalization. Dead births are omitted from the existing draw stream. No particle
history, Scene physics queries, collision events or additional entity manager exists.
Whole-life bounds use speed plus acceleration norm to cover oblique rebounds;
ordinary Mesh alpha/depth/culling/sorting and admission budgets remain authoritative.
Native controls and `asset.document.particle_collision` share source validation,
revision/history/publication; partial/invalid settings fail atomically. GPU uses
the separate explicit profiles below. Scene geometry and ribbon/camera-plane
collision combinations remain unsupported.

The canonical `manual/images/particle-plane-collision.png` was captured from
Gallery246 at three seconds in the source-hidden relocated production Game,
640×360 using software Vulkan/llvmpipe. The original PNG was inspected and copied
without retouching. `particle_collision_gallery_game_vulkan` owns the capture
workflow; this records software output, not Windows or physical-device acceptance.

## Analytic GPU plane collision

[ADR070](decisions/070-gpu-particle-plane-motion.md) owns source13 and
values20/`gpu_collision1`,21/`gpu_weighted_collision1`,22/
`gpu_weighted_collision_opacity1`. `set_gpu_collision` commits one validated
candidate with shared plane settings and independent CPU/GPU activation. An absent
disable without replacement settings is a no-op. Enabling selects built-in GPU
Mesh motion and disables GPU camera alignment; Graph selection retains plane
settings inactive. No arbitrary graph-position output is treated as analytic
velocity. Original Mesh, CPU alignment/trails/opacity, graph assignment, extensions
and local IDs remain retained. Native controls and automation/MCP use this owner.

One additional private32-byte upload contains the normalized local plane, metric
offset plus radius, restitution, tangent velocity loss and rebound limit. The
existing renderer-owned emitter selects its plane kernel and uses the same
instance buffer/count, seeded birth window, appearance and optional weighted/
opacity projection. Original47-row input and seven output rows are unchanged;
default free-flight shader bytes are retained by shared source fragments. No
readback, CPU fallback, second clock, retained motion history or physics World is
added. A killed birth reserves its row with finite zero-scale geometry; reported
device row count includes these births and is not a live-survivor count.

Each query reconstructs motion from birth, including incoming contacts hidden
between displayed samples. Stable drag integrals, bounded distance/velocity
bisection and0–16rebounds follow the admitted float motion profile. Tangency and
drag-only asymptotic approaches do not create impacts. The undamped discriminant
is classified before endpoint cancellation can round a near-tangent distance to
zero. Its separate arithmetic is precise to prevent contraction from changing
that classification. GPU float evaluation is distinct from CPU double precision.

Values20 require opaque/masked Materials;21/22 use complete-view weighted Blend.
Require effect3/GPU1/`runtime_particle_gpu_collision:1`, adding weighted1 for21/22
and GPU opacity1 for22. CPU alpha/opacity capabilities are not implied. Bounds use
the existing acceleration-norm rebound envelope. Camera restoration, failed-count
clearing, resource budgets, source publication and recipe3/envelope3 retain their
owners. Gallery251 shares one Effect and the existing glass Material across two
emitters, with weighted alpha and a lifetime fade.

## Explicit weighted GPU alpha

[ADR068](decisions/068-weighted-gpu-particles.md) adds source11 and explicit
values12/`gpu_weighted1`,13/`gpu_graph_weighted1`,14/`gpu_weighted_billboard1`,
15/`gpu_graph_weighted_billboard1`. `enable_gpu_weighted_alpha` commits one validated
source candidate, retaining Mesh/program/CPU choices/extensions/local stop IDs.
Enabling selects built-in GPU or retains an active Graph/alignment. Disabling an
absent flag is a no-op. Source toggles never downgrade source11. Inactive CPU
opacity stays at CPU precision; these device profiles do not apply lifetime opacity.

Require built-in Blend Materials with depth testing and no depth writes. Material,
texture and vertex alpha share the existing weighted Mesh path. The Scene must
opt in and its complete view must pass weighted admission. Any incompatible layer,
resource/target/budget failure or disabled setting omits these emitters for that
view with scoped diagnostics, preserving ordinary sorted fallback for other work.
There is no whole-emitter sorted substitute or mixed weighted/sorted device alpha.
Approximate weighted color is not exact layered ordering or refraction. GPU
lifetime opacity, plane motion and analytic ribbons use the explicit profiles below.

The original47-row evaluator/graph input remains unchanged. Reuse the existing
supplemental projection and same-UAV barrier to choose Material tint flag2,
with either the camera-plane basis or unchanged Mesh transforms. The seven output
rows, device instance buffer, per-emitter count publication, view restoration,
resource budgets and fixed clock retain their existing owners. No readback or
second queue/clock/cache is added. GPU3/4/8/9 retain original flags and Material limits.

Recipe3/envelope3/importer/IDs/component/gameplay ABI remain stable. Values12–15
require effect3/GPU1 and typed `runtime_particle_gpu_weighted:1`, plus graph1 for
13/15 and GPU billboard1 for14/15. Earlier kits reject the new values. Gallery247
and248 reuse the existing soft Texture/Material with separate built-in and authored
program Effects. Native Graph publication remains a Windows Shader-worker workflow.

## Explicit GPU lifetime opacity

[ADR069](decisions/069-gpu-particle-opacity.md) defines source12 and values16/
`gpu_weighted_opacity1`,17/`gpu_graph_weighted_opacity1`,18/
`gpu_weighted_opacity_billboard1`,19/`gpu_graph_weighted_opacity_billboard1`.
`enable_gpu_opacity` validates and commits one source candidate. Enabling retains
an active sorted GPU transport; otherwise it calls the existing weighted activation.
Graph/alignment remain selected. Disabling retains the selected alpha transport,
endpoints, stops and local IDs. CPU and GPU enable flags are
independent. Shared opacity endpoints/stops retain double source precision; only
active GPU curves require distinct representable float times. The existing lifetime
curve operation preserves an active GPU fade; its ordinary default still selects CPU.

The original47-row analytic/program input and RNG remain unchanged. One existing
supplemental projection uses a deliberately expanded private352-byte upload with
short float age/lifetime, endpoints and16stops. It writes the existing flag3/normal-W
instance alpha after optional camera projection, preserving other geometry/normal/
RGB lanes. This private struct is not installed, serialized or a gameplay interface.
One upload/binding/pipeline/UAV barrier, original emitter buffer/count owner and clock
remain. Invalid input clears the count; runtime performs no readback or CPU sampling.
Float age at Step/lifetime boundaries may differ from CPU double evaluation.

Materials, scoped omission and complete-view weighted admission follow ADR068.
Values16–19 require effect3/GPU1/weighted1 plus typed `runtime_particle_gpu_opacity:1`;
17/19 add graph1,18/19 add GPU billboard1. CPU alpha/opacity capabilities are not
requirements for GPU-only profiles. Runtime kit manifests admit explicit supported
values without reinterpreting older cooks. Recipe3/envelope3/importer/asset/stop IDs,
component and gameplay ABI remain unchanged. Resource accounting includes retained
opacity-curve capacity. Gallery249/250 reuse the soft Material/Texture/Orbit program.

## GPU analytic ribbon trails

[ADR071](decisions/071-gpu-analytic-particle-trails.md) extends the existing renderer
realization with built-in analytic ribbons. Source14 retains `gpu_trail_enabled`
independently of CPU `trail_enabled`, sharing the typed `trail` settings. The
source operation `set_gpu_trails` selects built-in GPU evaluation and disables
GPU plane/camera alignment, retaining Graph selection inactive. CPU settings,
original Mesh, extensions and stop identities survive. Inactive edits never
reduce the source version. Native controls and optional `backend=cpu` (default)
or `gpu` on `asset.document.particle_trails` use the same validated source and
history/publication owner; MCP discovery exposes the same schema and operation.

Values23/`gpu_trail1` admit double-sided built-in opaque/masked Quad Materials.
Values24/`gpu_weighted_trail1` use weighted Blend, and25/
`gpu_weighted_trail_opacity1` add GPU lifetime opacity. The existing complete-view
weighted admission/refusal owns transparency; per-emitter sorting is not an
alternative transport. Graph and plane trajectories cannot combine with these
profiles. Current emitter transforms move the entire local path, without historical
world-space motion capture.

The double-precision clock/window owner admits births through life plus duration,
then supplies short float ages. Original lifetime still clips trajectory and
normalizes appearance. Each birth computes up to17 endpoint samples once, joins
camera-forward sides with sign continuity, and writes the existing seven-row flag4
packet. Tangents use transformed motion displacement before adding birth origin
or camera-relative emitter translation, preserving tiny intervals near large
origins. Shared endpoints retain identical bits across adjacent segments. Size,
RGB/opacity and path-V use the same endpoint sampling contract as CPU ribbons,
with explicit device float precision. A quantized interval can collapse to finite
degenerate geometry; it never triggers CPU sampling or readback.

The existing view-local matrix storage retains an identity-linear draw proxy and
counts it against the CPU payload budget. Geometry already includes the emitter
transform; applying its parity again would invert PBR face shading. Packed
endpoints that become identical after translation retain zero-area slots.

The existing private352-byte view/appearance upload feeds this kernel directly.
Its segment-count lane and unused fourth timing lane carry segments and duration;
camera and opacity lanes retain their meanings. Ordinary affine/billboard projection
must not overwrite ribbon rows afterward. Shared birth/RNG, motion and appearance
fragments retain the exact older free-flight, plane and projection shader strings.
The immutable47-row input, source importer, recipe/envelope3, asset/stop IDs,
component and installed gameplay ABI remain unchanged.

Continuous capacity covers `ceil(rate*(life+duration))`; reserve `capacity*segments`
against8192 Scene instances, with1–16segments and0.001–30seconds duration. The
renderer allocates the ordinary bounded vertex/UAV buffer and draws64-instance
slices. Published count means reserved segment slots, including degenerate slots.
Only the existing CPU resource/pose proxy remains on the host. Per-view evaluation,
whole-life bounds, target restoration, count clearing/publication and resource
retirement retain their established owners.

Require effect3/GPU1 and typed `runtime_particle_gpu_trail:1`. Values24/25 also
require weighted1, and25 requires GPU opacity1; CPU trail/alpha/opacity and Graph
capabilities are independent. Missing, malformed or unsupported kit fields reject
before replacing a distribution. Gallery252 shares one editable GPU trail Effect
and the existing ribbon Material between two emitters, with its own saved Scene
weighted-transparency setting. Native Windows and physical-device acceptance
remain separate from source-hidden software rendering.

## Explicit ordered GPU alpha

[ADR072](decisions/072-ordered-gpu-particle-transport.md) defines a bounded sorted
transport through the existing Mesh renderer. Source15 adds `gpu_sorted_enabled`.
`enable_gpu_sorted_alpha` and native **Sorted alpha (GPU)** select GPU evaluation,
preserving an active Graph, geometry, shared appearance and inactive CPU settings.
Enabling clears `gpu_weighted_enabled`; enabling weighted alpha clears sorted
activation. GPU opacity preserves an active sorted transport. Disabling sorted
activation retains its settings and restores the selected opaque/masked GPU
profile. Material authoring remains independent of the evaluation choice.

The source-document operation `asset.document.particle_gpu_sorted` takes the same
editor/session/revision guards as the other particle operations and a required
boolean `enabled`. Native controls, automation and MCP use the existing source
validation, Undo/Redo and publication owner. Source versions never decrease when
geometry or evaluation changes. Existing source formats and values1–25 retain
their exact names, version numbers and behavior; recipe/cooked envelope3, asset
and stop identities, components and the installed native ABI remain unchanged.

| Evaluation geometry | Sorted values | With GPU lifetime opacity |
| --- | --- | --- |
| Built-in Mesh | 26 / `gpu_sorted1` | 32 / `gpu_sorted_opacity1` |
| Graph Mesh | 27 / `gpu_graph_sorted1` | 33 / `gpu_graph_sorted_opacity1` |
| Built-in camera Quad | 28 / `gpu_sorted_billboard1` | 34 / `gpu_sorted_opacity_billboard1` |
| Graph camera Quad | 29 / `gpu_graph_sorted_billboard1` | 35 / `gpu_graph_sorted_opacity_billboard1` |
| Built-in plane motion | 30 / `gpu_sorted_collision1` | 36 / `gpu_sorted_collision_opacity1` |
| Built-in ribbon | 31 / `gpu_sorted_trail1` | 37 / `gpu_sorted_trail_opacity1` |

All require a built-in Blend Material, depth testing without depth writes, and
Scene weighted transparency off. For values26–37, the complete visible alpha group must share
one physical Mesh/Material/texture bundle, part, raster parity, layer mask,
shadow-reception choice, projected-decal selection and Material parameter values.
Physical identity includes immutable resource publications and generations.
Matching logical asset names or equal-looking Material values is insufficient.
Matching lighting, projected light images, local irradiance and fog follow the
existing layer/view selection; per-particle RGB/opacity stays in the packets.

An ordinary transparent Mesh, CPU alpha particle, incompatible GPU alpha emitter,
transmission, visible Sprite/Tile layer, pending/invalid resource or rendering
diagnostic refuses the complete sorted GPU group for that view. Diagnostics name
the reason. Other existing paths keep their own behavior. Opaque/masked geometry
continues through the ordinary depth path. Refusal, an empty view and another
camera never draw an earlier ordered packet count. This is an explicit admission
boundary, not unrestricted mixed-Material transparency or a fallback profile.

The renderer gathers existing seven-row device packets without particle readback
or another simulation clock. It generates center-depth/index keys, sorts indices
back to front and gathers the final packet permutation. Static Mesh depth uses
the transformed Mesh-part bounds center; ribbons use their segment midpoint.
Equal depths, including signed zero, retain canonical emitter identity followed
by birth/segment order. Padding remains finite degenerate geometry. Ordering is
per object or segment; intersecting geometry is not sorted per fragment.

The derived view resource reserves at most8192 packets and64 sources and retains
the existing64-instance draw slices. GPU barriers order key generation, compare
passes, final gathering and later reuse. Count clears before validation and is
published only after successful gathering. The camera owner restores targets on
success and failure. Source assets, immutable publications, draw bundles and
simulation state keep their established owners.

Require effect3/GPU1 and `runtime_particle_gpu_sorted:1`, plus graph1, GPU
billboard1, GPU collision1, GPU trail1 and GPU opacity1 only where selected.
CPU alpha/opacity capabilities and weighted1 are not requirements of a GPU-only
sorted profile. Missing, malformed or unsupported kit fields reject export before
replacing a distribution. Native Windows execution and physical-device review
remain separate from local software-device correctness evidence.

## Bounded ordered Material palettes

[ADR074](decisions/074-ordered-particle-material-palettes.md) adds source17's
`gpu_material_palette_enabled` and append-only values40–51. Values40–45 mirror
26–31's geometry/program combinations;46–51 add GPU opacity in the same order.
The policy is inactive on retained CPU/weighted sources. Enabling it enables
sorted GPU alpha and preserves the active Particle Graph. Values1–39, source and
local-stop IDs, envelope3, recipe3 and the gameplay ABI keep their meanings.

The complete visible group must opt in. The existing MeshSceneRenderer admits at
most four exact physical bundle/parameter selections sharing the same static
triangle geometry, part, shading model, canonical lighting/geometry shader body,
Material helper interface, raster parity, layers and shadow reception. Material
factors, texture publications, UV transforms and sampler choices may differ. The
union has at most eight distinct sampler states and 32 texture bindings. Custom
Material surfaces, transmission, projected decals/light images, skin, morph and
wind do not participate. Refusal omits the whole group for that camera and retains
the ordinary diagnostic/recovery path.

MeshDrawBundle projects per-object parameters through the canonical Material
owner. The renderer retains all source bundles until its derived native draw is
destroyed, charges it and copied parameters to existing budgets, and adopts a
fully prepared candidate atomically. ParticleGpuOrder carries an opaque per-source
uint tag in a separate device buffer; canonical source/birth depth ordering and
the seven float4 instance rows remain unchanged. No CPU particle readback, second
clock, registry or authoritative Material cache is added. MaterialShader owns
packing, UVs and sampler aliases for both ordinary and composed helper generation.

Export requires exact integer `runtime_particle_gpu_material_palette:1` in addition
to the profile's existing typed GPU/sorted/geometry/opacity capabilities. An older
kit cannot silently accept a new profile. The editable Gallery scene is
`Scenes/255-compatible-material-particles.scene.json`.

### Device image validation

The lower renderer fixture compares independently sorted ordinary Material draws
with the composed palette across48 cases/145 frames. Native shader capture shows
identical vertex arithmetic apart from the extra integer Material-index output.
The pixel programs have different precision contexts: the ordinary path includes
precise projected-surface calculations, while the bounded palette excludes those
features. Native optimized code therefore uses different precision annotations and
multiply-add sequences. HLSL permits these rounding differences; see Microsoft's
[precise modifier](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/precise)
and [multiply-add contract](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/mad).
The same first failing metallic-roughness case becomes exactly equal when only
graphics optimization is disabled. Production optimization remains enabled.

The regression compares metallic-roughness RGB using
`abs(actual-expected) <= max(3e-5, 4e-5*max(abs(actual),abs(expected)))`.
This retains an absolute floor while scaling with HDR radiance. Alpha, Unlit and
specular-glossiness retain the original3e-5 absolute limit. Both inputs must be
finite. The measured native48-case baseline has maximum scaled error3.3964e-5
and maximum absolute error0.0013466 at HDR intensity; an absolute-only limit
incorrectly rejects14 metallic-roughness cases. The budget applies to this fixture,
not to every authored lighting configuration or physical adapter.

The incorrect-Material render must still differ by more than0.001 in over100
channels and fail the same HDR-aware comparison in over100 channels. Visible
coverage, exact out-of-palette discard, full-bit tags, draw/slice counts, resource
refusal/recovery and the independent published-Scene checks remain separate gates.
The passing image report includes maximum absolute/scaled errors. Changing this
comparison changes no shader source, Material value, ordering, ABI or runtime policy.

## CPU Scene geometry response

[ADR073](decisions/073-scene-particle-collision.md) assigns transient histories to
ParticleRuntime::Entry, which retains its existing emission clock and immutable
resource selection. Source16 projects explicit values38/39; profiles1–37, recipe3,
envelope3, logical/local-stop identities and gameplay component/ABI layouts retain
their meanings. Scene activation deactivates local plane/ribbons while retaining
their settings. Explicit local-plane/ribbon activation deactivates Scene response.
CPU camera alignment and lifetime appearance compose with Scene response; GPU
selection retains inactive Scene settings without projecting them.

`scene_collision` has exactly radius, restitution, friction, bounce_limit and mask.
Radius is0.001–1000 world meters, coefficients0–1, rebounds0–16 and mask a uint32.
PhysicsService owns sphere sweeps against realized3D geometry after physics adoption;
sensors are ignored. Particle radius and response do not inherit visual size or
mix Scene-body contact materials. The original profiles have no resting response
or ribbon history; explicit extensions below add those options and bounded linear
surface queries. There are no two-way impulses, rigid-body resting solver,
unrestricted moving-surface dynamics, GPU Scene queries or synchronous callbacks.
Explicit source18/values52/53 extend the same owner to native XY geometry as
described below; they do not change the original3D profiles.
At birth the adopted fixed-update affine transforms position, velocity and
acceleration; its linear basis is retained. Existing births do not follow later
emitter/parent transforms. This does not reconstruct historical sub-tick poses.

The shared analytic force/drag owner supplies endpoints and velocities. Curved
motion uses chords with h<=1/120 and deviation bound min(radius*.01,.001); constant
velocity needs no subdivision. Query/precision failures reject explicitly. Contact
traces retain both sides of an impact and consume remaining travel. Initial
penetration, exhausted rebound allowance and sustained zero-normal-speed inward
acceleration end births. A metric/coordinate-aware bias handles native departing
touch without excluding other surfaces of the same body. Initial zero-duration
births receive a contact probe; frozen existing births do not query physics.

The World reserves1024 Scene births,2048 interval birth traces,8192casts per particle
update and256casts per birth. All next histories/ages commit only after every
candidate succeeds; prior gameplay/physics/resource adoption is not rolled back.
Continuous phases use the shared exact membership rules, including births/deaths
inside one fixed interval and large clocks. Burst initialization does not replay
killed births. Disable/state inactivation, seed/effect replacement and existing
visual reset discard corresponding histories. Recovery restarts visual emission.

The existing playback object adds bounded `world_samples` only for Scene profiles.
Each contains birth, age, position, velocity, size, RGB, opacity and a row-major3x3
birth basis. Scalar/type/order/count guards, immutable revision/generation/seed
binding and live-phase checks protect extraction/rendering. Effective Scene
projection removes opaque playback fields before the runtime attaches native data;
authored unknown-field round trips retain their input. ParticleRuntime.sample
returns world positions for these profiles, local positions for earlier analytic
profiles, and applies emitter modulation once. Presentation carries raw appearance;
the renderer applies that same modulation once and reuses ordinary instancing,
per-view camera alignment and cross-object CPU sorting. Bounds use actual samples.

Export requires effect3/alpha1/opacity1 and `runtime_particle_scene_collision:1`,
plus CPU billboard1 for values39. Unsupported or malformed kit fields reject
before replacing output. Native controls and
`asset.document.particle_scene_collision` share source candidates, sessions,
revisions, history and publication; the corresponding MCP tool is
`forge_asset_document_particle_scene_collision`. Stopped views display the
`render.particles.play_required` information diagnostic, without free-flight
substitution or a second preview World. Implementation/local/native/physical
acceptance are recorded separately.


### XY Scene query space

[ADR075](decisions/075-xy-scene-particle-collision.md) extends the existing history,
clock, work admission, capture and rendering paths with explicit values52/53.
Source18 requires a Boolean `scene_collision_2d`; an inactive selection retains
settings and identities without requiring an XY runtime capability. Earlier source
versions and all existing profile meanings remain unchanged. Scene settings remain
the same five-field object; XY radius must be0.005–500m even when retained inactive.
The existing source setter and symbol remain available; an additive explicit-space
overload validates settings and space in one final candidate. A space-only setter
preserves the active evaluation/activation. Source history retains opaque extensions
and local stop IDs; cooking excludes the source-only flag.

ParticleRuntime calls PhysicsService::shape_cast_2d with a circle, existing layer
mask and sensors excluded. No second physics World or query implementation is added.
Native query endpoints must be finite within±1e6 XY. Visual Z is independent of
query geometry. Birth force/drag stays three-dimensional; XY curvature bounds only
XY motion and contact restitution/tangent loss affects XY. Presentation reconstructs
Z analytically between the existing contact knots, without extra depth-only casts.
Mixed3D/XY emitters share the existing1024birth/2048trace/8192query World limits and
256queries-per-birth limit, with all-or-nothing particle candidate admission.

The native initial-overlap/touch sentinel (fraction0, zero normal) ends an XY birth
at that time. Native casts return a target surface witness and may report a center
inside the geometric radius due to contact tolerance. The particle response clips
the incoming chord fraction to that witness supporting plane at the actual radius
and leaves at least0.005m departure clearance. Original3D contact/bias semantics are
unchanged. Explicit resting, World-ribbon and linear Scene-motion profiles are
described below. These initial XY profiles have no moving-surface continuous
response. There is no rigid-body solver, synchronous callback, body impulse or
GPU Scene query.

Native **Scene query space** selects **World 3D** or **World XY**. Shared
`asset.document.particle_scene_collision` and MCP
`forge_asset_document_particle_scene_collision` accept optional
`space: world3d|worldxy`. Omission preserves retained intent. Optional complete
collision settings and explicit space are staged together before final validation,
so a valid combined change never fails on an intermediate radius. Existing grants,
document sessions/revisions, Undo/Redo and publication remain authoritative.
Active52/53 require `runtime_particle_scene_collision_2d:1` in addition to the base
Scene/effect/alpha/opacity capabilities;53 also needs CPU billboard1. Kit installation includes the new profile capability. The source-editing headers
are not part of the explicitly installed C++ SDK boundary in
`cmake/native_sdk.cmake`. Installed headers, bridge/value support and SDK fingerprint
remain unchanged; no component/POD layout or PhysicsService slot changes.


## Historical world-space CPU trails

[ADR076](decisions/076-historical-world-particle-trails.md) extends the existing
SceneHistory owner with explicit source19/values54/55. Earlier immutable head-only
and local analytic profiles retain their meanings. Native **World ribbon trails
(CPU)** and optional `space=world` on `asset.document.particle_trails` call
`ParticleEffectSource::set_world_trails`; omitted space remains local. CPU only;
existing Scene query-space/settings, document sessions/revisions, grants, history
and publication remain authoritative. Local ribbon/plane activation deactivates
World geometry while preserving inactive settings.

Previous/current flat snapshots preserve bounded birth-age grid points and exact
response corners; the existing interval trace supplies any current presentation
alpha. At most `2*segments+4` retained points per birth per snapshot. Generate only
the visible grid window after long steps. At presentation, retain clipped tail/head
and interior contact corners, then split longest temporal gaps within1–16segments.
Before adopting a fixed update, reject any intermediate window that cannot preserve
its mandatory geometry, even when the entire tail expires before the final clock.
No silent truncation, presentation-time query or extra World/clock/registry is added.

World positions and birth X-column width magnitudes remain frozen. Appearance uses
original birth age/lifetime; live emitter size/tint applies once at the rendering
boundary. Lifetime/contact-dead heads retain shrinking tails until end+duration.
Disable, reset and seed/effect replacement discard history under existing policy.
This remains a sampled polyline approximation, not continuous collision geometry.

Derived `world_paths` uses exact-key flat tuples: ranges `[birth,first,count,width]`
and points `[age,x,y,z,size,r,g,b,opacity]`. Strict shape/type/finite/order/time/phase
and immutable binding checks precede extraction/rendering. Zero path velocities
mean unspecified ribbon velocity, while head samples preserve actual motion.
Shared head/path ordinals count once against1024World births. Existing8192instance
and8MiBframe limits include reserved segments, detached samples and per-view
geometry. Reuse actual ribbon joins, world bounds, camera sides and ordinary CPU
Material/sort/draw owners. Tail-only bounds retain selection/framing.

Export requires effect3/alpha1/opacity1/trail1/Scene collision1 plus typed
`runtime_particle_scene_trail:1`; XYvalues55 additionally require the XY capability.
Missing/malformed/unsupported capabilities reject before output replacement. No
installed SDK/POD/vtable/fingerprint change. Gallery259/260 share an original
translation clip and existing Ribbon Material. Local/rendered/native Windows and
physical acceptance remain separate evidence claims.


## Scene impact observations

[ADR077](decisions/077-scene-particle-impact-notifications.md) defines opt-in copied
CPU impact observations. Source20 requires Boolean `scene_impacts_enabled`;
values56/57 are 3D Mesh/camera-plane,58/59 XY Mesh/camera-plane,60/61 historical
World ribbons in3D/XY. Enable selects CPU sorted alpha and Scene collision through
`ParticleEffectSource::set_scene_impacts`; inactive intent and other settings
survive backend changes. False on an older absent field is an exact no-op.

Native **Scene impact notifications (CPU)** and
`asset.document.particle_scene_impacts` / `forge_asset_document_particle_scene_impacts`
share this source operation. Their existing asset.write grant, document session,
revision, Undo/Redo, publication and save/reopen contracts apply. MCP may author
and build project code, run Play and read existing runtime snapshots/diagnostics;
there is no additional MCP tool claiming a separate particle-event database.

The motion step records only opted-in native outcomes: Touch0, Bounce1 and End2.
Position is the contact centre before departure bias; normal and incoming/outgoing
velocity are copied world values. XY initial overlap has zero normal and End.
Time is relative to the fixed visual step; birth age includes emitter playback
speed. Times use particle playback seconds, and copied velocities are World-space
vectors per particle playback second. Convert clocks when comparing them with
real fixed-step rigid-body velocities. Scene query masks and sensor exclusion
retain their existing meanings.

ParticleRuntime publishes at most8192 detached records after whole-candidate
adoption. Its actual fixed tick, emitter/collider UUIDs, effect, seed, generation
and birth identify the observation. Reset/shutdown clear it. Empty next intervals
publish no observations. Failed updates do not partially replace the broadcast.
Reads filter removed references or a changed emitter binding and preserve sparse
slot indexes; repeated reads do not consume them. This state is transient and is
not part of Scene persistence, export content or runtime recovery.

Exact-SDK `particle_impact_count` returns0..8192 slots or-1 for invalid context.
`particle_impact_event` returns1 for a copied record,0 for a missing/index/retired
slot, and-1 for invalid context or arguments. Null or incorrectly sized output
rejects without writing; valid-sized output clears on miss/failure. C++ Client
helpers initialize output and preserve these statuses. Reads require the Runtime
owner thread, an active fixed tick and Physics permission. The published tick
must be exactly current fixed tick minus one: visual integration occurs after
fixed ECS execution, so project systems observe the previous completed interval.
Neither a control callback nor an arbitrary worker can read the broadcast.

ForgeSdkParticleImpactV1 and two appended host callbacks deliberately change the
exact SDK fingerprint/table size. Native ABI1 and old callback offsets remain;
rebuild against the matching SDK and runtime kit. The private read-only interface
uses the existing weak particle bridge, is not installed, and owns no runtime.
Export additionally requires typed `runtime_particle_scene_impacts:1`, plus all
capabilities selected by the Scene-query space and geometry. Old kits reject.

Gallery261 demonstrates Mesh impacts; Gallery262 combines XY impacts and World
ribbons. `CodeExamples/ParticleImpactTint.cpp` is the original shared managed
System. Installed sample `particle_impact_gallery_gameplay` compiles that same
source with its standalone matching-SDK descriptor. It changes runtime emitter
colour on a copied contact; unmodified Gallery scenes still simulate without
optional code. No native contact causes a synchronous project callback, collider
impulse or device-particle readback. Resting response is an independent opt-in below.

## Resting CPU Scene response

[ADR078](decisions/078-resting-scene-particles.md) extends the existing history
owner with a copied supporting collider and unit normal per birth. Source21
requires Boolean `scene_resting_enabled`. Explicit values62/63 select 3D
Mesh/camera-plane,64/65 XY Mesh/camera-plane,66/67 3D/XY World ribbons. Values68–73
select the same six geometries with independent copied impact notifications.
Older profile meanings, source identities and saved inactive settings remain.
`ParticleEffectSource::set_scene_resting` normalizes through the existing source
owner and canonical profile table. Enabling selects CPU sorted alpha and Scene
collision; disabling an absent older field is an exact no-op.

Native **Rest on Scene surfaces (CPU)** and
`asset.document.particle_scene_resting` / `forge_asset_document_particle_scene_resting`
call that same setter. Existing asset.write grants, sessions, exact revisions,
Undo/Redo, publication and save/reopen apply. Stopped Scene, Timeline and isolated
Effect previews still require Play; they do not create a second physics context.

Zero restitution and inward acceleration can keep an incoming birth alive after
an allowed rebound. At least one rebound must be permitted. Existing contact
friction removes tangent velocity once; tangent acceleration and drag continue.
A valid support probe constrains the inward normal acceleration only. Initial
penetration, zero-normal XY overlap and exhausted rebound limits still end the
birth. Positive restitution retains ordinary bounce response. This is not a
rigid-body or static/Coulomb-friction solver. These profiles do not transfer surface
velocity; the separate linear Scene-motion profiles below provide relative response.

Revalidate support through the existing filtered native query. A stationary held
birth needs one probe per advancing interval. Sliding steps are bounded by
1/120 s and radius/4 travel, retaining ordinary motion casts and a final endpoint
probe. Missing or changed support releases ordinary motion; replacement geometry
must establish a new contact identity. These profiles use discrete current-snapshot
support; linear Scene-motion queries are a separate opt-in. Every support probe shares the existing
256-query birth and8192-query World limits. Failure rejects the detached candidate
before adoption. Existing chord-knot interpolation and independent analytic XY
depth remain authoritative.

Copied notifications remain optional and use unchanged existing SDK shapes.
Landing reports Bounce; continuing support reports at most one Touch per birth in
an advancing interval. The existing weak bridge publishes only after whole-candidate
adoption and exposes the prior completed interval during the next fixed tick.
No callback, additional store or installed SDK slot is introduced. Lifecycle reset,
disabling and seed/effect replacement clear support with the ordinary histories.
World ribbons retain earlier motion while a supported head remains alive; paths
whose points all coincide have zero ribbon area. Existing bounds/render/capture
owners consume actual world samples without reapplying emitter transforms.

Exports require typed `runtime_particle_scene_resting:1` as well as the selected
effect/alpha/opacity/query/geometry/notification capabilities. Missing, malformed
or unsupported kit fields reject before replacing accepted output. The source
and private motion headers add no installed SDK ABI; matching-boundary evidence
and local/native/physical acceptance are recorded separately.

## Linear Scene-motion intervals

[ADR079](decisions/079-linear-scene-motion-queries.md) extends the existing physics
query and particle history owners with bounded linear surface translation.
Source22 requires Boolean `scene_motion_enabled`, retaining all earlier required
source fields. `ParticleEffectSource::set_scene_motion` selects CPU Scene response
when enabled, preserves inactive intent and keeps earlier immutable profiles exact
when disabled. Values74–97 append four six-profile groups:

| Values | Resting | Notifications |
| --- | --- | --- |
| 74–79 | Off | Off |
| 80–85 | Off | On |
| 86–91 | On | Off |
| 92–97 | On | On |

Each group orders 3D Mesh, 3D camera-plane, XY Mesh, XY camera-plane, 3D World
ribbon and XY World ribbon. The canonical value-profile table owns selection.
No earlier profile, persisted identity, importer or cooked envelope is redefined.
The native **Linear Scene motion (CPU)** control and
`asset.document.particle_scene_motion` / `forge_asset_document_particle_scene_motion`
use the same source mutation, exact session/revision, Undo/Redo and publication
owners. Export requires typed `runtime_particle_scene_motion:1` plus the selected
Scene, geometry, resting and notification capabilities.

PhysicsRuntime owns opt-in transient pre/post-step native geometry observations.
Native2D remains subordinate to that owner. Ordinary Worlds do not record motion
until requested. An observation becomes ready only after successful physics
adoption and expires at synchronization. Teleports, static placement, admission,
retirement and rebuilt geometry do not invent a pre-admission sweep. Queries use
native broad phases, existing canonical bindings/filtering and native shape casts;
there is no particle-owned physics World, geometry registry, clock or solver.

For unchanged shape/orientation, each query casts particle displacement relative
to body translation against the native shape at the slice start. Contact witnesses
are remapped to physical contact time. The copied surface velocity is body
translation divided by real fixed duration. A potentially intersecting rotating
or changing target rejects the interval. Unchanged rotated poses remain valid.
This is a linear approximation between adopted poses, not an angular sweep or
unrestricted moving-surface continuous collision implementation.

XY interval queries retain an actual-radius native initial-distance check and
compensate the native encroachment skin inside the first-contact cast. A positive
actual-distance witness distinguishes a padded contact band from genuine initial
overlap; only the latter keeps the zero-normal End sentinel. One float step of
endpoint allowance is clamped back to the requested interval. Short accelerated
chords therefore retain their first contact instead of entering the next chord
as a penetrating start. Older current-snapshot query profiles keep their meaning.

SceneHistory adds each birth's existing offset to query-local seconds. Runtime
maps these seconds to a normalized physics slice using the actual visual age
advance, including playback scaling and representable clock precision. Native
surface velocity is multiplied by real duration / visual advance before relative
normal/tangent response. Paused playback keeps existing freeze semantics. Runtime
opts into observation during preparation without waiting for a step; a motion
emitter admitted too late defers its first visual advance until an observed step.
Other emitters continue. Replayed or mismatched adopted intervals reject before
candidate adoption. Visual reset clears consumed-frame provenance with history.

All native casts and XY initial-distance checks, including misses and support
probes, count toward the existing256-birth/8192-World query limits. Empty queries
still cost one operation. Native candidate collection is bounded; exhaustion
rejects the complete detached interval. Resting, historical World ribbons,
independent XY depth and optional copied impact notifications retain their
existing owners. Response affects particles only; no two-way rigid-body impulse,
historical emitter-pose reconstruction or stopped-preview physics is introduced.

The canonical `manual/images/linear-scene-particles.png` and
`manual/images/xy-linear-scene-particles.png` show the shipped Gallery265/266 at
three seconds through the actual source-hidden GameFrameRenderer. The local
`particle_scene_motion_gallery_game_vulkan` and
`particle_xy_scene_motion_gallery_game_vulkan` checks publish and relocate the
content, exercise the native translating surface, require supported living heads
and compare the particle-visible image against the removed-particle view. Original
640×360 captures use software Vulkan/lavapipe and are copied without retouching.
They establish that rendering path, not physical GPU or Windows acceptance. The
corresponding Windows `_gallery_game` paths use the existing native viewport host.
