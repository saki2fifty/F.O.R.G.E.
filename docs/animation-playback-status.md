# Authoritative animation playback status

The existing AnimationRuntime/AnimationService derives optional copied playback
status for a canonical EntityRef. This reads existing realized clocks and assets;
it performs no sampling, advancement, catalog IO or authored writes. No second
clock, player registry or persistence format is introduced.

## Values and meaning

Status describes the selected incoming skeletal clip. `time` is its authoritative
fixed time in seconds, wrapped into the current loop. `duration` is the admitted
positive clip length. `progress=clamp(time/duration,0,1)`. Looping players never
report `finished`; a nonloop reports it at the endpoint, even if an incoming fade
continues after that point. Completion is a persistent status, not a consumed event.

Effective `speed` includes named-state speed. `enabled` and `loop` reflect realized
configuration. `playing` means enabled, positive speed and an active player; it
includes a remaining named fade after clip completion. It describes the player's
readiness for fixed ticks, not the global Pause state. Pause changes no clocks;
Step advances one fixed tick. Disabled and zero-speed players retain status/time.

Named players carry realized stable state ID/name and states/clip identities.
`transitioning` and `transition_weight` describe an incoming named fade, including
interrupted/frozen outgoing sources. Direct two-clip playback instead reports
`blend_clip` and `blend_weight`; it never claims a named-state transition. Ordinary
host optional asset fields serialize as null; absent SDK asset UUIDs are empty.

Queries return only registered live Animator players with admitted assets and
valid bindings. Missing/removed targets or pending/invalid bindings return no
status. A failed configuration that retains last-good playback reports that
realized state, not the failed request. Gameplay precedes this tick's animation
realization and observes the last sample; PostPhysics sees this tick. A selection
made in PostPhysics is realized at the next existing synchronization boundary.
Recovery uses the existing clocks/configuration and derives the same status without
advancement. Stop destroys the owner and leaves no status.

## Matching native SDK

Existing fixed-only Animation capability provides `animation_playback` and
`forge::sdk::Client::animation_playback`. Result is1 copied status,0 unavailable,
-1 invalid arguments/output size or SDK context (including the wrong thread). Valid-size output is cleared before lookup and
assigned only after the entire value is prepared. Access is owner-thread/runtime/fixed-only and subject
to descriptor permission. UUID arguments must be valid canonical identities.
The fixed-size C struct contains no host allocation or STL ownership.

`native_entity` is a borrowed handle in this matching world and fixed callback;
use the callback's Flecs stage for ordinary writes and recheck the target. Do not
store it for later ticks or across worlds/recovery. Public headers changed;
rebuild C++ modules with the exact matching SDK. The experimental ABI/version
number remains1 under the existing whole-header fingerprint/size contract.

The optional original Gallery `CodeExamples/OneShotReturn.hpp` helper checks
`finished` and selects an existing return state using ordinary Animator writes.
It requires an explicitly selected named state, respects a newer state request
(including zero/entry selection), and keeps no private timer. The caller
chooses stable scene/entity IDs and declares Animator write access in PostPhysics.
This is ordinary compiled C++, not a new condition graph or automatic editor rule.

## Editor and transport

Effective runtime scene output includes read-only `animation_playback` beside
`animation_pose`. The former uses fixed status; the latter retains render-time
interpolation. Consumers must not infer authoritative completion from interpolated
poses. The live gameplay Inspector displays state, progress, time, playback flags,
named incoming fade or direct clip mix. This section has no editing controls and
never enters saved scene/component data. Runtime/GPU transforms are unchanged.

No transform-animation status, cumulative loop count, automatic transitions,
retargeting, root-motion movement or complete animation graph is claimed.
