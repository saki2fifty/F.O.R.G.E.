# ADR 021 — Scene-owned animation sequences

Date: 2026-10-14. Implemented and validated locally in phase97. Windows physical
acceptance and a new packaged delivery remain deferred.

## Evidence and decision

Scene/Prefab already own component composition; scene/entity duplication remaps
registered EntityRef fields. Prefab source capture currently converts spatial
member links only; it does not convert component EntityRefs to PrefabMemberRefs.
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
participate through scene-owned sequence bindings. Reusable prefab definitions do
not capture complete arrangements: Create Prefab rejects a subtree containing bound
tracks, rather than retaining an unintended link to the source scene. Portable
definition-member sequence bindings require a distinct, versioned prefab design;
do not disguise prefab member IDs as EntityRefs. Never scan opaque JSON or bind by
display name/path.

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

## Compatibility and completion boundary

Default-null sequence bindings preserve existing standalone clips. Backfill new
known members without rewriting source files; preserve other extensions/overrides.
Scene/Prefab and clip formats/cook recipes remain unchanged. Native component data
and exact SDK fingerprint change deliberately; gameplay modules require a matching
SDK rebuild. C ABI1 function/table layouts remain unchanged. Extend the existing
private animation recovery version explicitly for coordinated controller clocks;
accept old checkpoints only for configurations they can describe faithfully.

No new asset kind, dependency, arbitrary event language, nested sequence asset,
track blending, skeletal editing or in-game editor. Real Timeline input, runtime/
presentation/recovery, persistent remapping, C++, Gallery and source-offline export
must pass before this consumer is marked complete.
