# Authoritative animation playback status

The existing AnimationRuntime/AnimationService derives optional copied playback
status for a canonical EntityRef. This reads existing realized clocks and assets;
it performs no sampling, advancement, catalog IO or authored writes. No second
clock, player registry or persistence format is introduced.

## Values and meaning

Unbound skeletal status describes the selected incoming clip. `time` is its authoritative
fixed time in seconds, wrapped into the current loop. `duration` is the admitted
positive clip length. `progress=clamp(time/duration,0,1)`. Looping players never
report `finished`; a nonloop reports it at the endpoint, even if an incoming fade
continues after that point. Completion is a persistent status, not a consumed event.

Effective `speed` includes named-state speed. For a sequence-bound player or
controller it is the root playback speed multiplied by cumulative placement rate;
its time and duration remain local seconds. `enabled` and `loop` reflect realized
configuration. `playing` means enabled, positive speed and an active player; it
includes a remaining named fade after clip completion. It describes the player's
readiness for fixed ticks, not the global Pause state. Pause changes no clocks;
Step advances one fixed tick. Disabled and zero-speed players retain status/time.
For a bound sequence controller, `loop` reflects its explicit `sequence_loop`
placement flag; the retained standalone `loop` does not govern bound timing.
Child time resets within each admitted cycle and holds its final clipped value at
the parent's end. Bound track status retains its existing nonloop flag and reads
the controller-derived local sample. Active-window status excludes inherited gaps.

Named players carry realized stable state ID/name and states/clip identities.
`transitioning` and `transition_weight` describe an incoming named fade, including
interrupted/frozen outgoing sources. Direct two-clip playback instead reports
`blend_clip` and `blend_weight`; it never claims a named-state transition. Ordinary
host optional asset fields serialize as null; absent SDK asset UUIDs are empty.

Queries return registered live animation players with admitted assets and
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
poses. The live animation Inspector displays state, progress, time, playback flags,
named incoming fade or direct clip mix. Its copied status never enters saved
scene/component data. Separate [playback commands](animation-playback-controls.md)
provide Pause, Resume, Restart and release seeking in the same panel, without
advancing paused game time or editing authored values.

Entity-wide C++/SDK queries preserve the transform player's priority when a holder
has both TransformAnimator and Animator. Private host status adds a `component`
identity and bounded `players` observations for both types on such holders; explicit
player-kind queries use the same existing owners. Each `sequence_clips` object
describes that player's actual primary/next identities, controller, starts, durations
and derived contribution. A bound player's `time` and `duration` describe its placed
span, with controller-derived speed/loop/playback and independently sampled clips.
For a nonempty bounded collection, the existing primary/next fields identify the
active adjacent pair; `clips` describes all placements and zero-based
`active_primary`/`active_next` identify that window. Time/duration describe the
whole track. Empty collections retain legacy primary/next meanings.
The optional `sequence` identifies its controller. These observations are copied
values, never a second player database or advancing clock. The live Inspector reads
bound object/model track timing and selects its controller for playback changes.
Its optional Clip placements tree shows the copied list without sending commands;
independent track controls remain disabled.

Status does not include a cumulative loop counter. [Named states](animation-states.md) own
automatic transitions, and [root motion](animation-root-motion.md) has a separate
current-tick observation. [Retargeted clips](animation-retargeting.md) use the same
playback observations after publication. [Animation Graphs](animation-graphs.md)
report their existing effective inputs through the same owner.

[C++ playback controls](animation-playback-controls.md) use these same realized
clocks for per-player Play, Pause, Restart and Seek before the Animation stage.

A nested controller's copied host observation includes its admitted immediate
parent in sequence. The live panel selects that controller and disables independent
pause/restart/seek, using the same control rejection as native C++ and MCP. Continue
up the parent chain to control the root. Entity-wide C++/SDK playback layouts are
unchanged; their speed already includes the cumulative placement rate.
