# ADR038 — Timed sound-cue source offsets

## Decision

Extend ADR037 event composition with a default-zero delay on each Source leaf.
The supported interval is zero to ten output seconds, rounded to the nearest
native output frame. Layer and choice nodes retain their event semantics; branches
cannot carry a delay. This does not introduce cycles, playlists or a new clock.

SoundCueProgram selects the source references and their delays together. The
existing transactional command admission publishes both before advancing selection.
AudioRuntime carries the offset into its existing one-shot voice. Once prepared,
that voice schedules its start against the native engine's output-frame clock.
Source pitch affects the clip; it does not scale the start offset. Source loading,
stream preparation and later voice admission keep their existing semantics.

## Pause and lifetime

An absolute native deadline alone is insufficient: the engine clock can advance
while a source's parent group is paused. The voice therefore retains only its
remaining-frame/deadline intent. Before a paused delayed voice can resume, its
remaining interval is rebased against the existing native clock. Already-started
voices retain the ordinary source cursor and effect-tail behavior.

Full suspension, game pause for sources that follow it, and explicit source pause
preserve the remaining interval. Independent sources continue during game pause.
Stop and source/world retirement release delayed one-shots through the existing
voice owner. Waiting voices consume the existing one-shot and total voice budgets.
No Scene, Flecs object, graph or selection state enters the native callback.

Recovery retains authored offsets with the graph and starts fresh selection.
Audio cursors, scheduled voices, queued one-shots and effect memory remain outside
the recovery contract. Accepted enqueue does not guarantee audible voice admission.

## Compatibility and authoring

Earlier nodes omit delay_seconds and default to zero through the JSON codec and
native member evolution annotation. The existing sound_cues:1 capability remains
unchanged. Nonzero authored offsets additionally require sound_cue_timing:1 and
kit runtime_sound_cue_timing:1, including retained or partial Prefab values. Old
zero-delay content retains its previous kit requirements. Matching installed
component headers/fingerprint are required; C ABI1 callback tables and operation
values stay unchanged. No durable identity or document-version migration is needed.

The detached graph Inspector, native discovery, Scene/Prefab/history, Timeline,
automation/MCP and C++ use the same typed values and source/voice owners. No second
authoring implementation, scheduler, mixer or resource cache is introduced. See
[Audio](../audio.md) for implemented behavior and validation limits.
