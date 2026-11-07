# ADR039 — Repeating sound-cue events

## Decision

Extend ADR037/038 with optional periodic events inside the existing AudioRuntime
and CuePlayback owner. SoundCue retains a default-disabled interval, explicit
startup intent and game-pause policy. The native engine remains the sole output
clock. Scene Timeline loops keep their separate game-time crossing ownership.
No additional mixer, resource registry, voice owner or callback access to ECS is
introduced. Graphs remain bounded and acyclic; clip-completion playlists are not
part of this profile.

## Scheduling and ownership

Restart resets event selection and begins production when all declared sources
are prepared and its output domain is active. Pause/Resume retain the remaining
interval; Stop prevents future requests. AudioSource continues to own every
already-requested voice and its loading, streams, offsets, pause, effects, budget
and retirement. OneShot remains one independent event.

Intervals are0(off) or0.25..3600seconds. At most32producers share the existing256
selection-state bound. Owner service emits at most one event per producer per
boundary; overdue periods are skipped and counted while the next deadline keeps
its phase. Device dispatch follows owner service and cannot promise callback
accuracy through a host stall. Offline reads split at due frames through the
existing PCM read owner. Both runtime hosts service outside game ticks.

Full suspension holds all producers. Independent gameplay-pause production
requires every declared Source to ignore gameplay pause. Invalid source policy,
retirement or unavailable graph references stop the producer with a diagnostic.
Manual Stop suppresses startup rediscovery for the current definition. Definition
changes and world retirement reset transient production and selection. Ordinary
recovery retains authored intent only; explicit startup begins a fresh lifecycle.

## Compatibility and authoring

JSON/native defaults are interval0, startupfalse and pause-with-gametrue. Existing
event-only content retains its controls and kit requirements. Nondefault intent,
including retained partial Prefab values, additionally requires sound_cue_repeat:1
and runtime_sound_cue_repeat:1. Component headers/fingerprint must match installed
C++ modules; C ABI1 callback tables and operation values are unchanged. No durable
identity or document-version migration is needed.

Inspector drafts, Scene/Prefab/history, C++, automation and MCP use the existing
component/admission/playback owners. Controls explicitly distinguish future event
production from Source-owned voices. [Audio](../audio.md) specifies the current
behavior and limits. Native Windows, full-editor, device and external-client
acceptance remain separate from local CPU/offline validation.

[ADR040](040-completion-sound-cue-events.md) extends the same producer with
optional completion-aware timing while retaining Source voice ownership.
