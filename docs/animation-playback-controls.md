# C++ skeletal playback controls

The existing AnimationRuntime owns Play, Pause and Restart for a currently admitted
skeletal Animator. `AnimationService::control(EntityRef, AnimationControl, tick)`
uses the existing private player clocks and leases. It creates no command queue,
second timer, player registry or new saved component.

## Fixed boundary and commands

Call from Gameplay before Animation in the current fixed tick. The matching native
callback obtains that tick from RuntimeSimulation's existing input snapshot. The
owner records the last completed/started animation tick solely as a stage guard;
PostPhysics calls return unavailable without mutation. Queries remain permitted
in PostPhysics. A valid restore clears the transient guard, not the clock.

- **Play** resumes the current time and fade, including a player initially held by
  Play on Start=false. A finished nonloop with no remaining named fade returns
  unavailable; use Restart to play it again.
- **Pause** holds private playback intent, incoming/outgoing clocks and fade weight.
  This affects this player, independently of the global game Pause.
- **Restart** begins the realized incoming clip at zero, resets its existing sampler
  and interpolation history, starts playback and ends a named incoming fade. It
  releases outgoing/frozen samples. A direct two-clip mix keeps its target weight.
  The next Animation tick applies the pose; the callback never writes model joints.

Enabled and Speed remain independent authored settings. Commands do not enable a
player or change its speed. Disabled/zero-speed players can retain a pending restart;
phase-zero markers fire once on the first actual advancing tick. Controls emit no
immediate observations. Repeated restart requests deliberately restart repeatedly.

Commands target realized playback. A pending component clip/state selection takes
precedence at ordinary synchronization; controls cannot select or restart a new
unadmitted request. Missing, removed, unadmitted or invalid-bound players reject.
Last-good admitted playback remains controllable after a failed request.

Existing recovery fields preserve time, playing, fade and entry-marker intent,
including a checkpoint taken after Restart and before advancement. No scene/schema
or checkpoint format change is required. Stop discards runtime intent. There is no
seek, reverse play, transform-animation control or automatic transition graph here.

## Matching native SDK

`animation_control` / `forge::sdk::Client::animation_control` take canonical scene
and entity UUIDs and `FORGE_SDK_ANIMATION_PLAY`, `..._PAUSE` or `..._RESTART`.
They return 1 applied, 0 unavailable/late/completed Play, or -1 invalid UUID,
operation or context. Raw calls from a foreign thread return -1. Animation is a
fixed-only descriptor permission; include it in allowed_capabilities. Rebuild
modules with the exact matching SDK after these public-header changes. Existing
experimental ABI version1 is still governed by whole-header fingerprint/size checks.

`entity_reference` / `Client::entity_reference` copy a fixed-size
ForgeSdkEntityRefV1 for a current matching-world handle. This uses intrinsic
RuntimeEntities availability and the existing WorldContext reference/resolve maps.
It returns 1 only for a registered live handle whose unscoped canonical reference
resolves back to that same handle. Stale, unregistered or ambiguous loaded-scene
instances return 0; invalid handle/size/context returns -1. StableId alone does not
establish membership. No names, paths or persistent scene-instance ID are invented.
Valid-size output is cleared before lookup; no host allocation crosses the boundary.
Handles stay transient, matching-world and callback-scoped. Copied UUID identity can
be retained but must be resolved again before use.

The original Gallery `CodeExamples/ClipControl.hpp` combines this lookup and control
for ordinary C++ entity Systems without hardcoded scene UUIDs. Its setup guide uses
existing fixed action pressed edges, not a separate timer. Normal Gallery playback
needs no compiler. Optional behavior ships as compiled project code on export.
