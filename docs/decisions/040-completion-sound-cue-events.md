# ADR040 — Completion-aware sound-cue events

## Decision

Extend ADR039's existing producer with default-zero `repeat_mode`: zero retains
periodic behavior; one waits for the current event's requested Source voices,
then its optional gap. The graph remains bounded and acyclic. Ordered nodes can
therefore choose successive sounds after each event finishes. No additional
playlist asset, voice owner, mixer, clock or callback access to ECS is introduced.

Interval mode retains zero(off) or0.25..3600seconds. Completion mode accepts a
finite0..3600second gap, including zero. Startup and independent game-pause intent
remain explicit. The existing32producer/256cue-state and source/command/voice
budgets remain authoritative; each service boundary emits at most one event per
producer. This profile repeats events until its producer is stopped.

## Event provenance and admission

AudioRuntime assigns a transient monotonic ticket when a complete event's commands
and selection state are published. Existing commands carry the requesting cue
and ticket; Source-owned one-shot voices retain only the ticket. Selection counts
cannot identify voice generations because Restart and definition changes reset
them. Tickets remain unique across those transitions within the runtime lifetime.
They are neither durable identities nor a second registry.

CuePlayback tracks the current event's pending/admitted requests. A deferred or
preparing stream voice is not a completed event. Rejected/stale voice admission
stops future production with a diagnostic; it cannot create a zero-gap busy loop.
Already admitted voices remain with their Sources. Unrelated manual one-shots,
other cues and older events after Restart do not hold up the current event.

## Completion, controls and lifetime

The producer observes existing Source voice retirement, including effect tails.
It does not predict completion from clip metadata or inspect a Source's primary
voice. Stream read failures stop the producer while Source controls retain their
resource/retirement responsibility. Source Stop cancels its voices and can finish
the current event; stop the cue's producer to prevent future events as well.

Pause/Resume affect future production and preserve an armed gap. Sources retain
their own pause policy. If voices finish while production is held, the producer
observes completion and begins its gap when it can run again. Source preparation
may postpone that observation. Native transitions follow owner service; no
sample-perfect device chaining or continuity through host stalls is promised.
Offline rendering uses the same source retirement and bounded loading owners.

OneShot remains a single independent request and consumes the shared graph's next
choice. Restart starts fresh choices and a new event without taking ownership of
older voices. Definition/world replacement retires producer state. Ordinary
recovery retains authored mode/gap only and starts fresh; no tickets, queued voices,
completion counts or effect memory are recovered. SDK Play recovery stays within
its existing boundary.

## Compatibility and authoring

Missing JSON/native mode defaults to zero. New completion intent additionally
requires sound_cue_completion:1 and runtime_sound_cue_completion:1, including
retained partial Prefab values. Existing cue/repetition/timing requirements remain.
Old periodic content retains its semantics and kit requirements. C++ modules need
matching component headers/fingerprint; C ABI1 tables and operation values remain
unchanged. No Scene version or durable identity migration is needed.

Inspector, Scene/Prefab/history, C++, automation and MCP use their existing
component and playback owners. Source voices remain authoritative for actual
completion. Windows, full-editor, physical-device and external-client acceptance
remain separate from local CPU/offline validation.
