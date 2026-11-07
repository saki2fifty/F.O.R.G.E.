# ADR042 — Finite sound cue runs

Accepted locally. Extends ADR039/040.

SoundCue adds default-zero `repeat_event_limit`, bounded to 0..65535 scheduled events
including the first. Zero preserves unlimited producers; a dormant limit does not
enable interval-zero repetition. Manual OneShot requests retain shared choice
selection but do not consume the scheduled run's count. Skipped late intervals
do not consume it either. Both timing modes retain existing service boundaries.

After the final scheduled event, the producer stops issuing requests and observes
all of this run's Source-owned voices, deferred admission and source effect tails.
It reports finished only after those requests retire. Interval events can overlap;
waiting only for the last event would falsely finish an earlier long sound.
Completion mode adds no final gap. Shared bus tails remain separate.

Existing CuePlayback/Command/Voice owners retain one transient run ticket alongside
event tickets. Both come from the runtime-local monotonic provenance allocator,
preventing older Restart/definition generations from aliasing a run. No second
voice registry, timer, playlist asset or callback access to ECS is introduced.
Rejected/stale admission and stream failures stop production with diagnostics.
Manual/other-cue/older-run voices do not delay this run's finish.

Pause/Resume retain an in-progress or finishing run. Source controls retain their
own pause/Stop behavior. Cue Stop remains future-only and does not report natural
completion; Restart starts fresh choices/counts without taking older voices.
Definition/world replacement retires producer state. Ordinary recovery retains
authored intent only and starts fresh; the exact-SDK host restriction remains.

Codec/default evolution, actual Inspector/Live UI, Scene/Prefab/history, SDK,
automation/MCP, both hosts and additive kit guards belong to the same batch.
The component header/fingerprint requires matching SDK rebuilds; C ABI1 callback
tables and operation values remain unchanged. No identity/Scene-version migration
or dependency change is needed. This bounded component run does not claim complete
standalone playlist-document or cyclic graph support.
