# ADR 021 — Scene-owned animation sequences

Date: 2026-10-14. Implemented and validated locally in phase97. Windows physical
acceptance and a new packaged delivery remain deferred.

## Evidence and decision

Scene/Prefab already own component composition; scene/entity duplication remaps
registered EntityRef fields. At the phase97 decision, prefab source capture
converted spatial member links only. [ADR022](022-prefab-component-references.md)
subsequently adds explicit source3 component member references.
TransformAnimationPlayback already owns clip resources, clocks, complete pose/property
candidates, movement-controller conflicts, copied presentation and recovery inside
AnimationRuntime. Reusable clip assets contain no scene identities. A second sequence
asset/loader/manager is unnecessary for the first coordinated-animation consumer.

Store an AnimationSequence controller as a native component in the Scene. Existing
TransformAnimator attachments optionally reference that controller by EntityRef and
store their start offset in seconds. Ordinary Scene history owns the arrangement;
clip source documents keep their separate current history/publication owner. Reuse
the central Timeline workspace with an explicit scene-sequence mode and borrowed
Scene Save/Undo/Redo routing. Existing clip editing remains available there.

Only current known reference fields are remapped. Controller references must resolve
within the target's loaded scene membership; missing/ambiguous/wrong-type references
retain identity and report errors. Scene/subtree duplication uses existing typed
reference machinery, including overrides on prefab instances. Prefab actors can
participate through scene-owned sequence bindings. The initial phase97 boundary
excluded portable definition arrangements and rejected their capture. ADR022
supersedes that deferral with source3 member links, resolved into each Scene
instance's EntityRefs. It retains this decision's clock/candidate ownership.
Prefab member IDs are never disguised as Scene EntityRefs. Never scan opaque JSON
or bind by display name/path.

## Timing and ownership

AnimationRuntime's existing transform provider owns the controller's transient time;
there is no new global clock, scheduler or animation service. One controller governs
all its referenced clips with duration, loop, playback intent and speed0–4. Each
attachment initially had exactly one clip (extended by the two-clip decision below),
so no independent overlapping writers/blend arbitration is
introduced. Bound clips ignore their retained standalone loop/speed/start intent.

Sample local clip time as clamp(sequence time minus start offset,0,clip duration).
The first pose holds before the offset and the last afterward; sequence end can cut
an overlong clip. This follows existing initial_pose semantics and avoids retaining
another authoritative authored-pose cache. Loop/restart/seek boundaries snap existing
presentation endpoints as needed. Game Pause/Step/simulation speed retain their
existing owners. Disabled controllers retire playback, like disabled ordinary players.

Prepare/admit every clip before execution. Build and validate the existing complete
transform and typed property candidates before any pose write; preserve current
physics, navigation, character and skeletal writer-conflict policy. C++ group
controls reuse current AnimationService operations and owner-thread/tick guards.
Per-track controls cannot desynchronize a bound clip from its controller.

Per-object material tracks extend this same complete typed candidate and clock
owner. Their explicit source5/cooked5 format extension is described in
[transform animation](../transform-animation.md). Selected CPU material/mesh
leases come from the existing Resources provider; whole-curve target admission
and complete sampled numeric projection precede component writes. Sequence clips
still contain no Scene identity or separate arrangement/clock resource.

## Reflected component field extension

Editable scalar float/double and held Boolean/integer/enum/bitmask bindings extend
the same clip, complete candidate and
clock owner; see [transform animation](../transform-animation.md). Existing native
AuthoredCodec and reflection admission own component values and lifecycle. Clip
bindings retain stable authoring keys/schema stamps, never native IDs or offsets.
Nested struct leaves extend that identity with explicit member-name arrays under
the root field. Collection leaves add a typed selector/member suffix at the first
collection; root/prefix/suffix remains one stable target identity. Declared local
entry keys use the existing reflection identity helper; unkeyed entries are
explicitly positional. The same reflection admission and complete candidate remain
owners; animation does not create or resize collections.
Presentation carries a complete root struct through the existing field merge.
The private sequence preview may reconstruct inspected metadata in its Runtime
composition without loading project code. This reuses the existing reconstructed
authoring value owner and leaves the gameplay-before-animation phase order intact.

## Camera-shot extension — 2026-10-17

The F49 camera-cut consumer extends this same Scene/controller design. Each
ordinary shot entity carries CameraShot with explicit controller/camera EntityRefs
and a bounded interval. Reuse ADR022's typed member-reference traversal for Scene,
subtree and portable Prefab copies. There is no dynamic arrangement asset or
additional identity registry. Up to 256 enabled/bound shots share the provider's
existing clocks; each loaded Scene membership has one enabled shot director and
nonoverlapping intervals. Camera composition is overridden in copied presentation
only, preserving native/authored enabled flags, with normal composition in gaps.
The exact timing, invalid-input policy and recovery4 boundary are specified in
[transform animation](../transform-animation.md#sequence-camera-shots).

## Repeated child controller placements — accepted additive design

2026-11-07. Append default-empty `AnimationSequence.sequence_extra_placements`:
at most seven entries with `start_seconds`, `rate` and `loop`, following the existing
primary placement. One child still has one parent and one complete pose candidate.
These are positional values, not independent identities, controller instances or
additional clocks. Independent simultaneous instances remain Scene/Prefab copies.
An unbound controller ignores the retained placement configuration.

Starts must increase strictly after the primary start. Each rate is within0.25–4;
every inherited span's cumulative rate remains within1/64–64. A finite placement
must finish before the next starts; its final value holds across a gap. A looping
placement fills through the next start or the parent end. The final placement may
be clipped by the parent end, just as the original primary placement was. At an
explicit next start the incoming placement resets local time to zero. Ancestor
resets clear descendants even before their first active placement. End/entry cue
coincidence retains the existing once-per-cue/root-point convention. Root motion
integrates each placement independently and never extracts the reset as movement.

Extend the existing immutable derived mapping with a rate per span; no global
affine-rate assumption may survive in status, cue inverse, camera activity,
skeletal/root-motion intervals or copied presentation. Preserve the256-span budget,
mathematical preflight, inline ordinary span, full candidate admission and last-good
publication. Empty extra lists preserve all previous timing and recovery meanings.

The native value and exact SDK fingerprint change deliberately; C ABI1 callback
tables remain unchanged. Runtime document/kit capability `sequence_placement:1` /
`runtime_sequence_placement:1` is required for a stored nonempty list, including
partial Prefab overrides. Private recovery14 wraps its preceding profile and
records exact primary/extra configurations for every controller before adoption.
Existing source-extension, typed reference, Prefab and history owners remain.
Timeline, read-only live observations, generic automation/MCP, matching installed
SDK, recovery, Gallery and source-hidden export checks are required before
completion. Concurrent/multi-parent placements and unrestricted blends are outside
this bounded extension.

## Direct skeletal sequence tracks — accepted additive design

2026-11-07. Append optional `sequence`, `sequence_start_seconds`,
`sequence_next_clip` and `sequence_next_start_seconds` values to Animator. Null
bindings preserve standalone meaning. The bound profile uses direct clips and is
exclusive from States, Blend Spaces, Graphs, manual blending, masks and additive
selection. Retained standalone speed/loop/start intent does not control a bound
track. A matching SDK rebuild is required; C callback table layouts stay unchanged.

TransformAnimationPlayback retains the only controller and nested clocks.
AnimationRuntime derives the skeletal track's clamped local sample coordinates
from those clocks. The existing skeletal resource, Sampler, morph, constraints,
writer checks and root-motion owners remain authoritative. Independent per-clip
phases use native weighted sampling; the standalone normalized-phase blend API
keeps its current meaning. Both selected clips must match the exact skeleton and
model revision. Their transform-channel union and compatible morph defaults are
admitted together, including the currently inactive clip.

The existing two-clip span supplies ordered starts/ends, overlap weight, gap cuts
and endpoint holds. Sequence duration still clips the arrangement. Fixed, copied
presentation, preview and seeks use the same mapping. A private prepare/publish
boundary composes skeletal candidates with the sequence owner's complete candidate
before native writes. Seeking preflights the prospective controller coordinates
before changing clocks. No second writer, scheduler, pose database or clock is added.

Root motion follows ADR018's per-layer interval policy. Clip-local intervals clamp
at the placement boundaries; gaps and held endpoints contribute identity movement.
Overlaps blend interval deltas at their mean endpoint influence. Controller wraps
compose the completed sequence interval and the new interval without replaying a
seek or extracting motion in presentation. Recovery must record exact bindings,
selected revisions and the existing controller clocks in an explicit private
profile; older shapes remain valid only for configurations they can describe.
Private recovery10 wraps profiles1–9 with `skeletal_sequence_profile`, retaining
the existing Graph and camera/nested wrappers. Scene/Prefab runtime feature metadata
records `skeletal_sequence: 1` for a non-null admitted sequence binding, including
portable member references and variant property overrides. A matching kit advertises
`runtime_skeletal_sequence: 1`. Unbound documents retain their earlier metadata and
compatibility. Existing document-feature discovery and kit admission own these tags.

This decision defines the implementation contract. Completion requires the native
Timeline/Inspector, copied preview, automation/MCP, installed SDK, recovery and
source-hidden export checks specified by the affected subsystem documentation.

## Ordered multiway clip overlaps — accepted additive design

2026-11-07. Append default-false `sequence_multiway` to TransformAnimator and
Animator. It opts a bound clip collection into up to eight simultaneous contributors;
the existing maximum of eight clips, ordered starts/ends and complete resource
admission remain. False preserves the prior adjacent-pair profile, including triple
overlap rejection and observed identities. Unbound values are retained and ignored.
This extends one player-owned candidate; it adds no clock, placement identity,
animation asset kind, independent pose writer or arbitration manager.

Each adjacent pair keeps its current natural-overlap linear fade, or a cut after a
gap. In clip order, an incoming fade overlays the earlier accumulated contribution.
Thus the first influence is the product of every later one-minus-fade; each later
influence is its own fade times those later factors. Nonnegative influences sum to
one. Every clip samples at its independently clamped local phase. Holds, cuts and
reset snapping retain their current timing owner. Admit all selected resources and
bindings even when their current influence is zero.

Continuous scalar/property/Material/reflected values use the normalized influences;
rotation and skeletal poses use their established component-specific interpolation
owners. Held discrete values choose the greatest influence, with the later clip
winning a tie, preserving exact typed integer/Boolean values. Extend the existing
native weighted sampler's FORGE-owned three-layer bound to eight through the same
pinned public blending API and existing per-placement sampling contexts.

Root-motion integration keeps the existing reset/root-loop and per-layer interval
owners. Split at every clip boundary and average each influence analytically over
the interval's product of linear fades. This reduces to the previous endpoint mean
for a pair; no reset motion or presentation extraction is added. Use bounded inline
polynomial storage and preflight limits before iteration or integer conversion.

The complete private editor/MCP watch exposes ordered clip identities, local times
and influences. Existing two-slot native/SDK fields remain exact for the old pair
profile. With more than two contributors they provide only a clearly documented
dominant/runner-up summary and relative pair influence, not a complete pose recipe.
The complete watch marks that summary and exposes every contributor. Do not change
C ABI1 records or callback tables to disguise this observation limit. Matching SDK
modules can author/control the opt-in values and use the existing motion controls.

Private recovery15 wraps its prior3–14 profile and validates exact opt-in flags and
all selected revisions before adoption. Runtime document/kit capability
`sequence_mixing:1` / `runtime_sequence_mixing:1` guards stored true flags, including
partial Prefab overrides and unbound retained settings. Source/cook formats stay
unchanged; native value changes require the matching rebuilt SDK. Timeline pending
fields, Scene/Prefab/history, read-only live status, automation/MCP, native/SDK,
recovery and an original source-hidden Gallery must pass together before completion.
This bounded ordered profile does not claim arbitrary unbounded arrangements or
concurrent/multi-parent controller writers.

The existing Game viewport also consumes stopped sequence preview. Its explicit
preview label and absent gameplay/runtime-UI execution preserve the authoring/Play
boundary. The camera-shot extension adds cuts; the preview-frame consumer below
subsequently adds bounded offline image sampling. Shot blending and nested
sequences remain separate work.
The new native component changes the exact installed SDK fingerprint. Existing
controller/player layouts and ABI1 function tables remain unchanged.

## Point audio-cue extension — 2026-10-17

AudioCue extends the same Scene/controller arrangement with a source EntityRef and
one time in seconds. It requests the existing AudioService OneShot after the
complete animation candidate commits on a forward fixed tick. AudioSource and
AudioRuntime retain clip, gain, pitch, routing, asynchronous loading, command and
voice ownership. There is no second clock, native audio pointer, event language or
audio track resource in the animation provider. Seeking and stopped preview make
no new requests; previously requested effects retain ordinary audio lifetime.

Reuse typed Scene/Prefab member links and existing Timeline/Scene history. Bound
sources must belong to the same loaded Scene membership. A private entry flag
distinguishes an unplayed sequence start from seeking or wrapping to zero; visual
presentation resets cannot rearm it. Checkpoint5 adds exact cue configuration and
this entry intent. Versions1–4 remain valid only without cue configuration.

Collection and dispatch are bounded before command submission. Per-cue binding or
audio admission failures produce diagnostics without undoing a committed pose or
stopping unrelated cues. The existing audio owner keeps its final voice/command
limits. This adds point events at fixed-tick boundaries, with no continuous clips,
trimming, sample-accurate synchronization or sound rollback. The native component
changes the matching SDK fingerprint; ABI1 function tables remain unchanged.
See [sequence audio cues](../transform-animation.md#sequence-audio-cues).

## Sequence preview-frame consumer — 2026-10-18

Timeline retains its existing isolated preview world, cursor and play state. A
private exclusive generation-checked lease holds elapsed playback, seeks explicit
finite sequence times through AnimationService and restores only into the same
surviving context. Scene/project/revision/catalog changes and close/reopen retire
the lease; no authored snapshot is written back as a restored pose.

The existing image task sequentially consumes the normal Game renderer's display
target. Current-view dimensions are the default; an optional fixed extent asks
that same renderer to draw at bounded export dimensions while the preview lease
is held. Game fits the resulting image into its panel, preserving aspect ratio;
the request ends with the task and ordinary rendering resumes at panel size.
This creates no second rendering or target-retirement owner. Its existing frame
fence and one asynchronous file worker own readback, PNG publication and the final
detached receipt. No sequence renderer,
new animation clock, gameplay execution, sound dispatch, persistent asset kind or
public SDK boundary is added. Samples are end-exclusive because existing looping
seek intentionally maps an exact duration to zero. See
[editor image export](../editor-ui.md#sequence-preview-frames) for finite bounds,
new-folder ownership, cancellation and per-file publication semantics.

## Compatibility and completion boundary

Default-null sequence bindings preserve existing standalone clips. Backfill new
known members without rewriting source files; preserve other extensions/overrides.
Sequence bindings themselves leave Scene/Prefab and clip formats/cook recipes unchanged. Native component data
and exact SDK fingerprint change deliberately; gameplay modules require a matching
SDK rebuild. C ABI1 function/table layouts remain unchanged. Extend the existing
private animation recovery version explicitly for coordinated controller clocks;
accept old checkpoints only for configurations they can describe faithfully.

No new asset kind, dependency, arbitrary event language, nested sequence asset,
track blending, skeletal editing or in-game editor. Real Timeline input, runtime/
presentation/recovery, persistent remapping, C++, Gallery and source-offline export
must pass before this consumer is marked complete.


## Two-clip object sequence spans — accepted additive design

2026-11-03. Extend the existing TransformAnimator with an optional next transform-clip
reference and start time on its existing sequence. Empty next slots preserve
single-clip and standalone behavior. These are two stable component property keys,
not new placement identities, sequence assets, clocks or pose writers. The provider
already owns one complete candidate per actor, so a separate arrangement-entity
writer is unnecessary for this bounded profile.

Admit both clip resources together. The initial next-clip profile requires two
transform-only clips with identical nonzero channel masks and ordered starts/ends.
Blend linearly through their natural overlap, reusing the existing clip-owned
quaternion interpolation. Gaps hold the primary endpoint until the next clip starts;
the shared clock then cuts to the next clip. Hold the first/last outside the whole
span, subject to the existing controller duration. Preserve complete transform /
writer validation, held/cut discontinuities, preview, seek, loop and exact recovery.
Property/material/component curves and skeletal animation retain their existing
paths; they are not mixed by this profile.

Append installed component values deliberately and require a matching exact SDK.
C callback table layouts and reusable clip source/cook formats stay unchanged.
Recovery must record both selected revisions and reject incompatible state before
publication; older no-next configurations retain their supported recovery readers.
Editor/automation/MCP and runtime/export use the same fields and provider owners.

## Built-in property pair extension

2026-11-06. Extend the existing two-clip owner with matching built-in property
tracks. Both clips retain equal transform-channel masks and equal property masks;
at least one channel or property is required. This admits property-only pairs as
well as combined tracks. Material and reflected component binding arrays remain
outside this profile; skeletal clips retain their separate owner.

Each clip samples its own curves at its clamped local time. The existing span's
linear next weight combines Camera FOV/height, Light intensity/RGB, Audio gain/
pitch and Particle size/RGB through `std::lerp`. Discrete Sprite frame values hold
the first sample below half weight and select the next at or above half. Gap cuts
select the next sample at its start. No fractional frame index is constructed.
Both complete frame curves must pass the existing target-dependent admission when
either selected revision/configuration or the target frame capacity changes.

One private property sampler serves fixed complete candidates and presentation;
copied sequence preview uses that same provider. Complete native component and
transform validation precedes writes. Existing clock, discontinuity, resource
retirement, last-good failure and nested-controller rules remain authoritative.

No component field, source/cook format or callback-table change is needed. Existing
pair recovery already records both exact resource revisions and configurations;
the properties are derived from those resources and the restored clock. Older
transform-only/single-clip meanings are unchanged. Earlier runtimes reject newly
permitted property pairs at admission rather than restore an incompatible meaning.

## Compatible Material and reflected binding pairs

2026-11-06. Extend the same two-clip owner to equal sets of Material and reflected
component bindings. Material equality includes slot, key and exact semantic type.
Reflected equality includes component, root/member/collection path, schema admission
and value category. Source order may differ: derive bounded ordinal correspondence
from complete binding equality when either selected clip/configuration changes.
It is disposable resource-derived state, never persistent identity or authority.
Transform/property masks still match, and at least one track of any kind is required.

Each clip retains its own local-time/key sampling. Blend numeric Material lanes and
continuous reflected scalars with the existing span's linear weight. Boolean and
integer/enum/flags leaves choose the first sample below half and the next at or above
half, preserving their exact JSON/native signed or unsigned integer representation.
Neither implicit numeric conversion nor fractional discrete values are permitted.

Both complete clips require existing target/domain/schema admission, including an
incoming revision not yet contributing at the playhead. Material admission includes
both selected resource revisions and current draw selection/unanimated intent.
Project mixed sampled values through the existing detached Material and reflected
candidate owners before any native write. Fixed and copied presentation use the
same private samplers. Source/history, last-good failure, timing and recovery retain
their existing owners. No new public field, source/cook or recovery layout is needed;
derived correspondence is rebuilt from the exact selected revisions on recovery.
Skeletal clips and general multi-clip arrangements remain separate.

## Nested Scene controller placements — accepted bounded design

2026-11-03. This additive profile uses the existing sequence ownership boundaries.
An AnimationSequence may retain a typed parent sequence reference and start in that
parent's seconds. Empty bindings preserve standalone behavior. The existing native
component, Scene/Prefab reference/history owner and transform animation provider
remain authoritative; no sequence asset, graph database, global clock or manager
is added. Installed value additions require matching SDK rebuilds.

A bound controller is a finite placement. Its time is derived from the root clock,
summed placement offsets and ancestor-clipped span. Standalone speed, loop and
play-on-start intent stay stored but do not create an independent bound clock.
Child controls reject; root seek/restart stages descendant poses and clocks in one
complete candidate. Validate same-Scene references and cycles within the current
64-controller budget. Disabled chains retire through current activation ownership.

Translate sound cue points to the root's unwrapped interval and reuse the existing
periodic-range/batch owner, preserving exact offset entry, end and root-wrap events.
Sample root presentation time before local clamping; camera shots direct only
within the placement's active span. Keep the current one camera-shot director per
loaded Scene rather than introducing nested precedence. Recovery must validate
parent bindings and all derived clocks after resource admission, before adoption.

The Timeline borrows Scene history. Local copied preview includes descendants and
isolates the selected controller from its parent only after the existing provider
validates its original active binding, so preview cannot hide an active cycle.
Automation/MCP must expose the same native fields/operations and ownership.
Multiple placements of one controller, independent child rate/loop overrides,
reflected/material/skeletal crossfades and movie encoding are outside this controller-runtime
profile. [Silent movie export](../sequence-movie-export.md) belongs to the existing
preview capture owner and does not change controller clocks or simulation authority.

### Independent placement rates

2026-11-07. A dedicated `AnimationSequence.sequence_rate`, default 1, scales local
seconds per parent second for a bound controller. The retained standalone
`playback_speed`, `loop` and `play_on_start` values keep their original meaning.
Rates are positive, within 0.25–4; the cumulative rate from a root must remain
within 1/64–64. These bounds keep deep finite placements numerically useful within
the existing duration and controller budgets. A root ignores its stored placement
rate. No child clock, reverse playback, child looping or repeated placement is added.

The existing provider derives a cumulative rate, root offset and ancestor-clipped
end. All local sampling uses that mapping, including camera shots and independent
skeletal/morph clip phases. Sound cue points and skeletal root-motion boundaries
map back to root time through its inverse. Root seek/restart, complete candidate
publication, copied local preview and last-good resource admission retain their
owners. Observed speed includes the cumulative rate; child direct controls reject.

Append the installed value deliberately and rebuild exact matching SDK modules;
C callback tables remain unchanged. Runtime document/kit profile 1 is required
when a nondefault rate is stored, including a partial Prefab override. Private recovery 11 wraps its existing
profile and records all controller placement rates, rejecting changed rates or a
missing wrapper before adoption. Existing default-rate checkpoints retain their
previous profiles and meanings. Generic property authoring/history and MCP use the
same admitted field. The Timeline exposes local placement start and rate together
and displays the parent-time length as local duration divided by placement rate.

### Child placement loop override

2026-11-07. Add a dedicated default-false `AnimationSequence.sequence_loop`.
A bound true value repeats the child's local duration through the remaining
parent-local active span. An ancestor's reset also resets descendants. Root
playback controls remain authoritative; stored standalone `loop`, speed and start
intent retain their meanings, and a root ignores its stored placement-loop flag.

The existing sequence provider derives a piecewise root-to-local mapping during
admission. Each active span is affine with the existing cumulative rate. Compose
the parent's spans, placement start/rate and local cycle boundaries; reject more
than 256 spans per controller before enumerating them. There is no serialized span
list, independent placement identity, second clock or additional manager. Ordinary
nonloop placements retain one inline affine span and allocation-free sampling.
Invalid replacement geometry preserves the last admitted complete configuration.

Select the incoming span at an internal reset. An ancestor reset clears a
descendant to its entry value, including the inactive gap before its placement;
other inactive gaps hold local endpoints. Retain the final clipped local value
after the last span. A placement
starting exactly at the parent's endpoint has a zero-length endpoint span; it
does not become playing. Presentation snaps when a reset is crossed. Camera
activity uses the selected active span rather than the bounding interval.

Sound cue points invert through each span. Entry and local endpoint cues retain
their existing inclusive forward-event convention; the same cue/root point is
emitted once if coincident spans produce it. Root-loop periodic delivery and the
128-event all-or-none tick budget retain their owners. Skeletal root motion
integrates each active span separately, splitting at existing clip boundaries and
sampling both interval endpoints in the same span/window. A clock reset never
creates motion between an outgoing endpoint and the incoming start. Repeated
root cycles retain the existing whole-cycle composition/power path.

Source/Prefab/history, Timeline placement controls, copied preview/live views,
generic automation/MCP, matching SDK values, exact recovery and runtime kit guards
belong to this additive profile. Local preview isolates the selected controller
through the existing validated preview owner. Repeated independently authored
controller placements and unrestricted arrangements remain separate capabilities.

## Bounded clip collections — accepted design

2026-11-07. Extend the existing player-owned arrangement to at most eight clips.
Keep the primary/next fields and their default behavior; append a default-empty
typed `sequence_extra_clips` collection of clip references and controller-local
start seconds. It admits at most six extra entries and requires the initial pair.
Use the existing registered vector, reflected reference and collection-limit
owners. No clip-placement entities, persistent placement identities, clocks or
independent pose writers are introduced.

Starts and ends must be ordered, with at most two adjacent clips contributing.
Reject triple overlaps; equal endpoints are valid. Reuse the existing pair span
for each adjacent window. Gaps hold the outgoing endpoint until the next start,
then cut. Outside the whole arrangement, hold its first/last values. The common
derived timing owner selects a window without allocation during sampling.

All referenced resources and revisions must be admitted together. Every clip must
match the established channel/binding/skeleton/model contract. Preserve complete
candidate publication, future-step preflight and last-good retry. Reuse native
sampler state and existing root-motion/rate interval ownership. Do not allocate
temporary sampling contexts every tick or change existing standalone/pair costs.

The new opt-in profile reports the selected adjacent pair through existing copied
and C++/SDK clip identities, while time/duration describe the full placed span.
Existing one/two-clip configurations retain their current observed identities and
semantics. Installed native values require matching SDK rebuilds; callback tables
remain unchanged. A new runtime document/kit capability and private recovery
profile must guard the complete collection and every selected revision.

Implement native source/Prefab/history, Timeline add/remove/timing/clip selection,
copied preview/live observations, generic automation/MCP and export in the same
batch. A collection or timing helper alone is not a completed feature. Repeated
controller placements, child looping, arbitrary multiway blends and unrestricted
arrangements remain outside this bounded design.

## Scene-owned UI values through reflected builtin admission

2026-11-03. Optional UiValues joins the existing component-field candidate owner.
Builtins must explicitly opt in with a five-field forge.builtin-component stamp
derived from their reflected value schema; UiValues is the initial admitted type.
User-authored component stamps remain forge.authored-component and their native
values still require matching $forge admission. Existing clip/cook versions retain
their current keyed collection identities; no Scene migration or new clock is used.
Native builtin codecs stage complete detached candidates before shared publication.
The same reflection-extension owner preserves opaque keyed collection siblings.

UiRuntime projects the committed Scene values into its existing copied model;
UiPresenter retains native expression/style/class ownership. Explicit C++ name
collisions reject. CSS animations retain their independent presentation clock and
transient progress. Sequence seeking means sampling the ECS values, never seeking
a native CSS player. Copied Edit previews do not execute RML or add an overlay.
Public installed value/header semantics require a matching exact SDK rebuild.

## Smooth camera-view extension — 2026-11-03

CameraShot optionally carries a zero-default incoming blend duration. Its existing
provider/director and root clock own selection and smoothstep transition timing.
Only adjacent shots blend; first shots, gaps and cycle starts retain cuts. The
shared camera helper validates a matching composition profile and interpolates
rigid world pose and lens parameters into the incoming Camera's copied row. Actual
Camera identity, authored/native transforms and offscreen producer composition
retain their existing owners. No virtual Camera, secondary director, image crossfade
or native pose writer is introduced.

RuntimeSimulation applies this projection after copied world-pose interpolation.
The stopped Timeline and its frame renderer call the same animation projection
after Scene resolves the copied hierarchy. Timeline fields retain one Scene history
transaction; source/Prefab references retain ADR022 traversal. Live tuning exposes
only the duration on an admitted shot through the animation owner and existing
Inspector/MCP/review path. Private recovery8 records blend durations and preserves
the preexisting nested sequence profile explicitly. Zero-duration cuts retain their
older recovery shapes. Exact compatibility and migration behavior live in
[transform animation](../transform-animation.md#sequence-camera-shots).
