# Animation timing markers

Named Animation States can carry optional markers. A marker describes a clip
phase, printable name and numeric value. Compiled C++ observes crossed markers
in the existing PostPhysics fixed phase. AnimationRuntime owns the observations;
there is no method-name execution or separate gameplay event manager.

## Source and publication

The existing version1 state source accepts root `next_marker_id` and each state's
optional `markers` array. Each marker has `id` (positive uint32), `name` (1–64
printable bytes), `phase` (finite normalized [0,1]) and `value` (finite [-1e6,1e6]).
IDs are unique across the whole state asset. There are at most64 markers/state and
512/set. The allocator exceeds every ID, never decreases through ordinary edits,
and survives source Undo/Redo and Save. Removal does not recycle an ID.
Sets without markers or the allocator retain their version1 compatibility.

The central Animation States document owns edits and history. Save / Publish uses
AssetImportService with own recipe `forge.animation.states.v2`. The additive
compiled format remains version1. Existing typed skeleton/clip dependencies and
source-free export closure are unchanged. Marker strings do not name functions,
files or assets; unknown marker extensions stay explicitly uninspected during
reference-impact review.

## Fixed timing

The selected incoming state emits; an outgoing or frozen fade source does not.
Occurrences cross the interval `(previous, previous + advance]`. Phase0 additionally
fires on the first actual advancing tick of a state entry. At a loop seam, phase1
and phase0 each occur once. Multiple loops in one tick are supported. `offset` is
seconds from the beginning of that fixed tick, divided by the effective clip speed.
Nonlooping playback clamps to the endpoint. Zero speed, disabled playback and
paused simulation emit nothing. Step performs one ordinary fixed tick.

Candidates are collected after model pose application. Unadmitted/invalid bindings
emit nothing. Observations sort by offset, stable EntityRef and marker ID. A tick
that exceeds4096 occurrences faults with an explicit diagnostic and publishes no
partial observation list. This faults the tick; it does not roll back earlier
simulation changes. No seek, preparation, presentation or restore operation emits.
Copied editor preview worlds explicitly mute delivery, including Play preview.

Recovery persists only `entry_pending` with the existing state clocks/revision.
Observations are transient. A valid restore clears them; the next tick crosses only
new phases. An older checkpoint without the flag defaults to no pending entry.

## C++ and lifetime

`AnimationService` is published in the established weak world-service slots.
`Capability::Animation` / `FORGE_SDK_ANIMATION` is4096. The matching SDK offers
`animation_read_event` and `forge::sdk::Client::animation_event`: 1 copied occurrence,
0 end/unavailable, -1 invalid argument or a removed target. Reads are broadcast;
reading an index twice does not consume it, and removed indexes are not compacted.
Availability is live, versioned, owner-thread and fixed-only. Read in PostPhysics;
Gameplay precedes this tick's animation and does not receive previous-tick data.

The fixed-size C value carries state/marker IDs, current tick, offset, phase/value,
scene/entity/state-asset/clip UUIDs and name. Its native_entity is a borrowed Flecs
handle in this matching world and tick only. The callback validates EntityRef and
Animator before copying. Recheck the entity before acting if another system can
remove it. No STL, Ozz object or allocation ownership crosses this callback.
Public headers changed: rebuild modules with the exact matching SDK.

## Gallery

Scene11 uses the real Quaternius rig with Pulse markers at .25/.75 of Walk/Run.
These are demonstration timing points, not measured foot contacts. Values1/0
raise/reset the character's vertical scale when the optional compiled
CodeExamples/MarkerPulseSystem.cpp consumer is registered. Normal Gallery playback
shows the clips without needing a compiler. The example uses PostPhysics and
ordinary LocalScale writes; it has no private timer or replacement animation owner.
See the Gallery CodeExamples README and user manual for setup/export.

[Playback status](animation-playback-status.md) uses the same owner/service for
read-only incoming clip progress and completion.
