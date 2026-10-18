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
attachment still has exactly one clip, so no overlapping writes/blend arbitration is
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

The existing image task sequentially consumes the normal Game renderer's current
display target. Its existing frame fence and one asynchronous file worker own
readback, PNG publication and the final detached receipt. No sequence renderer,
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
