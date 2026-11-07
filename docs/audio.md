# Audio module

`forge.audio` supplies FORGE-owned AudioSource/AudioListener schemas and an optional runtime provider. miniaudio **0.11.25**, commit `9634bedb5b5a2ca38c1ee7108a9358a4e233f14d`, is private static C implementation with the MIT-0 license option. Official source: https://github.com/mackron/miniaudio/tree/9634bedb5b5a2ca38c1ee7108a9358a4e233f14d . Existing dependency pins are unchanged.

## Build and ownership

Windows output explicitly selects WASAPI; Linux selects PulseAudio/ALSA with upstream dynamic backend loading. Null backend is enabled only for explicit internal tests, never an automatic physical-device fallback. WAV, FLAC and MP3 decoding, engine mixing/spatialization and threading are enabled; encoding and waveform generation remain disabled. No external codec framework is linked. The default engine resource manager remains private. AudioRuntime owns the existing typed ResourcePool<AudioClipAsset>; voices retain immutable selected-clip leases and supply resident buffer references or privately owned native streams. No loaded handle enters saved components or the gameplay ABI.

Each active Runtime WorldContext owns one AudioRuntime through EngineModule state. It owns stable-address context, engine, gameplay group, source voices and decoded clip data. Each voice maps a generation-bearing Flecs entity plus EntityRef to one miniaudio sound and its selected resident or stream source. Schema-only Authoring/Validation/default headless composition opens no device. `forge_audio` uses the private miniaudio engine and shared WAV decoder. The isolated asset worker links that decoder; authoring validates cooked PCM without opening a device. Core and authoring do not require an audio backend.

Shutdown stops the device and waits for callback completion, detaches voices, joins loaders and deferred stream/file cleanup, then destroys groups, engine/resource manager and context. Immutable clip resources outlive their attached native sources. Module/code leases follow the existing world-finalization contract. Device notifications only set an atomic bit field; the owner consumes it into ordinary structured diagnostics. The callback never reads Flecs, EngineServices, editor state or gameplay code.

## ECS and assets

AudioSource holds `AssetRef<AudioClipAsset>`, autoplay/loop/spatialized flags, linear gain, pitch multiplier, minimum/maximum distance, distance-falloff choice and strength. AudioListener holds enabled. These use the existing qualified component/property schemas, nullable typed AssetRef and bool metadata, scene formats and prefab override machinery. Known references serialize as UUID or null, not file paths or runtime handles. No scene version or identity changes.

AssetCatalog owns metadata in version1 `forge.assets.json`; ProjectPaths resolves contained project-relative locators. Registration is UI-independent and idempotent for a locator. UI calls require project writer ownership; a detected external index edit prevents publication. Metadata persistence is separate from scene Undo; no cross-document atomicity claim. The shared Phase7 importer/publication and source-watcher workflow below also supports AudioClip. Existing registered-only records remain readable without automatic migration.

Sources are cooked into bounded immutable PCM; playback selects resident storage or bounded streaming as described below. Limits:256 voices including primary, pending and transient sources,16 one-shots per source,1024 queued commands,16MiB source,64MiB decoded clip,256MiB managed resident PCM,2 workers,256 pending requests and4096 selected asset slots. Worker decoding uses bounded temporary memory in addition to resident PCM. Mono/stereo WAV, FLAC and MP3 sources; executable sample checks cover PCM16 at24/48kHz, including resampling. Native invalid configuration and resource failures disable the affected voice with entity/clip/source/module/tick diagnostics. Failed unchanged configurations, including native NaNs, do not retry every frame. Configuration corrections can recover a source; a failed immutable clip request requires restarting Play to retry file admission. Resident cache follows its byte bound until runtime teardown; unused stream snapshots are unloaded through the same pool. New imports select immutable cooked revisions; an active runtime keeps its cached/playing revision until teardown. Restart Play adopts a newer selection. Reimport does not hot-swap a live waveform.

### Loading boundaries

First use requests immutable selected data off-thread. Workers receive copied
project paths and catalog records; they never receive worlds, Scene, voices or
audio device pointers. Identical clip requests coalesce. The owner pumps completed
data and creates voices at audio synchronization or the host's between-tick loading
service. Fixed ticks never wait for clip IO/decoding. Loading remains serviceable
while Play is paused without advancing animation, physics or game time.
For unchanged poses at that same tick, loading service preserves the last fixed
velocity used by existing Doppler voices. Explicit pose synchronization and
teleport/seek boundaries retain their previous velocity-reset behavior.

Runtime status distinguishes pending sources/clips from failed sources. A copied
source observation has `pending:true, available:false` while its request is loading.
Stop/Restart commands retain bounded intent and source/clip/native-generation
identity. Initial autoplay and queued commands are folded before starting a new
voice, so queued Stop cannot leak an initial burst. Pause/Resume preserve their
existing semantics. Removed or switched sources cannot realize a late voice;
unused in-flight requests are cancelled while shared requests remain coalesced.

Scene candidates stay unpublished until required audio is ready. Preparation pumps
without ticking; failure preserves the active world. The ordinary headless replace
command and explicit tests can use the bounded `wait_for_sources` tooling facility;
game synchronization and editor audition do not call that wait. Shutdown stops the
device/callback, retires sounds/buffer references, then cancels/joins loaders and
closes the pool. Cancellation joins bounded admission work; it does not promise
interruptible operating-system file IO.

## Distance falloff

`AudioSource::attenuation` uses FORGE values0 inverse,1 linear,2 exponential;
`rolloff` is a finite strength0–10. The backend mapping is private. Old authored
components omit these fields safely and receive inverse/1 through the existing
builtin admission/default path. Reflection, typed native values, Scene Undo/Redo
and independent prefab override masks use the same component owner.

For distance `d` clamped between minimum `m` and maximum `M`, amplitude is:

- Inverse: `m / (m + r * (d - m))`.
- Linear: `1 - r * (d - m) / (M - m)`.
- Exponential: `(d / m)^(-r)`.

The backend clamps resulting amplitude to0–1. With linear strength1, maximum is
silent; other strengths and curves need not be. Maximum is a clamp distance,
not a universal mute radius. Equal minimum/maximum disables distance loss for
all curves. Strength0 retains positional panning while disabling distance loss.
Nonspatial playback ignores these distance controls. Source gain remains separate.
Values update an existing voice at fixed synchronization boundaries and do not
restart its sample cursor. Backend gain changes have a finite smoothing ramp.

The public AudioSource layout/header fingerprint changes with these appended
fields. Rebuild C++ modules with this exact installed SDK; ABI1 callback tables
and authored scene versions are unchanged. Private recovery still reconstructs
audio according to autoplay from frame0, without a sample-cursor continuity claim.

Original Gallery31 supplies three colored emitters and a one-second synthetic
440Hz WAV. Its camera also carries the listener; only inverse autoplays initially.
The export closes over cooked PCM and authored fields and excludes the source WAV.
Offline tests measure independent power equations, invalid-value rejection,
active-voice continuity and actual relocated source-free Gallery playback.

## Directional emitters

AudioSource adds `cone_enabled` (defaultfalse), `inner_angle`60, `outer_angle`120
and `outer_gain`.25. Openings are full angles in degrees,0–360; outer must be at
least inner. Outside volume is finite linear0–1. Settings remain valid and stored
when disabled or nonspatial; neither mode applies angular loss. The existing
builtin default-admission path keeps older scenes/prefabs omnidirectional.

Emitter forward is the effective WorldTransform affine negative-Z vector,
normalized before backend conversion. Structural/World/Explicit spaces and
parent rotations therefore use the same transform owner as rendering. Scaling
does not enlarge the angular guide or multiply volume. Zero or nonfinite forward
mutes directional spatial output with the existing bounded spatial diagnostic;
the voice remains playing and recovers when the transform is corrected. Native
invalid parameters instead follow existing failed-configuration admission rules.

For source-to-listener direction cosine c, inner/outer half-angle cosines i/o,
the pinned backend applies amplitude1 for c>i; outside amplitude g for c<=o;
between them it applies `g+(1-g)*(c-o)/(i-o)`. This is interpolation in cosine,
not linear interpolation in angle. Equal openings have a hard boundary selecting
outside volume at the cutoff. Inner360 bypasses angular loss. At coincident
positions, backend angular loss is bypassed; stereo mixing remains its own
calculation. Angular amplitude multiplies distance falloff and separate gain.
No new sound manager, sample restart or gameplay clock is introduced. Runtime
status configuration_checks counts actual reflected configuration validations;
it is transient diagnostic data, not persisted or recovered playback state.
The existing failure cache treats repeated NaNs as equivalent invalid values
without changing public component equality or comparing uninitialized padding.

The existing detached SpatialHelpers owner displays a selectable sound marker,
forward arrow and two spherical-cap outlines for selected directional sources.
Guides have bounded227segments, remain finite at0/180/360deg, and show their
presentation radius. They are editor-only aids, with no hidden authored entities
or runtime geometry and no promise that the guide boundary is a mute radius.
Inspector values, Undo/Redo, independent equal-value prefab masks and public C++
fields share the existing schema. Matching SDK modules rebuild after this appended
layout/header fingerprint change; ABI1 tables and audio recovery policy remain.

Original Gallery32 reuses Gallery31's tone, with an Aim parent above a directional
speaker. Source-free relocated playback measures full front volume and1/16power
behind after parent rotation. Offline independent cosine equations, disabled/
nonspatial settings, transformed direction, degenerate-direction recovery and
exact sample-cursor continuity form the focused gate. Device listening is separate.

## Per-source game pause

`AudioSource.pause_with_game` defaults totrue. The appended reflected bool is
optional in old Scene/Prefab documents and participates in the existing
encode/decode/defaults/Inspector/history/override/Save and native value paths.
The exact SDK fingerprint changes; rebuild modules against matching headers and
runtime. C callback table layout/ABI1 and dependency pins are unchanged.

AudioRuntime retains one Master output group for full suspension/gain and one
native child group for pausable sources. Opted-out sources attach to Master;
ordinary sources attach to the child. Native reattachment preserves data sources
and cursors, including live one-shots. No clock, PCM copies or mixer owner is
added. Native graph/device buffering governs transition boundaries; mixed-output
checks distinguish a prior cached mix quantum from steady paused output. Ordinary
scenes retain the existing exact silence/cursor gate by stopping Master during
game Pause when no admitted source needs independent output. Source enumeration
is needed only during active game Pause.

Full suspension stays separate from game pause. Preparation keeps Master stopped;
activation starts only a published world, including independent music in a paused
world. Replacement suspends old output before committing the new world; Stop
retires it. Fixed/control faults suspend output and retain the original diagnostic.
Explicit Audio clip preview Pause still suspends all preview output. Step retains
the game pause state; opted-out sounds continue on the device sample clock.

AudioSourceAccess reports each voice's effective `paused` state and admitted
`pause_with_game` policy. Runtime `paused` remains logical game/full pause;
`suspended` identifies complete output suspension. Controls/mix/status use the
same owner and never tick ECS. Poll loading already reaps completed one-shots
while paused.

The existing `audio_source` SDK callback and `sdk::Client` support opted-out UI
operations during control callbacks. Audio capability discovery is owner-thread
and callable in fixed/control scopes; it no longer advertises FIXED_ONLY. Control
requests recheck live identity, selected clip, output activation and both current/
admitted source policy through the existing queue, then flush without pose sampling
or fixed time. Preparation and ordinary gameplay-only targets reject. Fixed calls
retain deferred synchronization semantics. The internal retained protocol driver
supports source controls but does not run SDK control callbacks; production C++
uses SDK Play. Both hosts retain the source policy and protocol controls.

Original Gallery84 and the installed `unpaused_audio.cpp` sample exercise music
and UI one-shots. Focused PCM/mixed-frequency/lifecycle/Prefab/UI/host/installed-SDK/
MCP checks are attributed separately from native Windows, physical listening,
graphical EXE export and new authentic captures, which remain queued.

## Source filters

AudioSource appends `low_pass_enabled`/`high_pass_enabled` (both false),
`low_pass_hz` (20000) and `high_pass_hz` (20). Reflection admits finite cutoffs
from 20 through 20000 Hz even when disabled. Old Scene/Prefab documents normalize
these defaults through the existing codec; source authoring, equal-value override
intent, Revert, history, live tuning and exact public SDK values share the fields.
No asset format, clip PCM, dependency pin or C callback table is changed. Rebuild
exact modules against the changed value fingerprint.

Each existing stable Voice optionally owns up to two native miniaudio nodes:
second-order Butterworth HPF then LPF, at the engine's output channels/sample rate.
Processing follows that voice's pitch/spatial/gain stage. Primary and one-shot
voices keep independent DSP histories and share source settings. With echo also
bypassed, both filters disabled keeps the old sound-to-group connection without
effect nodes or extra allocation;
disabled cutoff values do not alter the output samples.

The owner builds detached replacement nodes before reconnecting sound output,
then retires the previous chain with the native detach/wait mechanism. It never
calls native filter reinit while device processing may read that state. Unchanged
settings do no filter reconstruction. A game-pause routing change reconnects only
the chain output, preserving DSP and clip state. Changing cutoffs/enabled flags
preserves the sound cursor but resets filter history; native cached quanta and
state reset can produce a short transition or click. There is no sample-accurate
or smoothed automation promise. Native audio callbacks perform no FORGE ECS work,
asset IO, chain construction or authoring mutation.

RuntimeWorld live tuning calls the existing source refresh path without a fixed
tick/new velocity sample. Copied source observations report admitted filter flags
and cutoffs. Full/game/source pause, mute, one-shot limits and retirement retain
the current audio owners. These controls filter individual sources. Shared bus effects have separate
project settings below; a general DSP graph and acoustic occlusion are unavailable.
Source reverb and custom sends have their own controls. Gallery85
compares the same original recording with dry/low-pass/high-pass sources.

## Source echo and effect tails

AudioSource adds `echo_enabled=false`, `echo_seconds=.25f` and `echo_feedback=.35f`.
Reflection admits finite interval .01f..1 seconds and feedback0...75, including
inactive fields. Old Scene/Prefab values normalize additive defaults through the
same codec; source history/Revert, live/Keep values and C++ use these exact fields.
The installed value header changes the exact SDK fingerprint; rebuild modules.
C callback tables/ABI1, dependency pins, asset identities and clip PCM are unchanged.

The existing private Voice chain is SourceEffects, with optional HPF→LPF→echo on
the engine's output channels/rate. The unchanged pinned native `ma_delay_node`
provides immediate dry signal and feedback repeats. FORGE explicitly disables
native delayStart: its constructor otherwise changes mode at zero feedback.
Feedback0 or disabled echo creates no delay node/buffer. Interval rounds to output
frames. Feedback can raise peaks; there is no separate wet/dry mixer or limiter.
Changing effective settings creates a detached replacement chain, preserving clip
cursor but clearing effect history. Dormant parameters do not rebuild a chain.
No attached DSP config is mutated and no callback constructs nodes or accesses ECS.

Each voice retains its own buffer. At the fixed48kHz stereo f32 engine output,
the maximum1second buffer uses384000bytes:93.75MiB for256 stable voices, plus one
detached replacement during a change. `echo_buffer_bytes` in existing global/source
observations counts these allocated buffers; it excludes filter/node overhead,
decoded assets and temporary detached replacements. Source observations include
its one-shots. It is derived from current voice ownership, not another memory ledger.

Short clips retain their echo after EOS. Owner servicing samples atomic native
node-local processed-frame time at observed EOS and retires at the first feedback
repeat whose feedback decay is below1e-4 (-80dB) of the retained signal at EOS,
bounded33seconds at maximum settings from that observation.
Polling latency can delay noticing EOS; this is not a sample-accurate voice expiry.
One-shot tails occupy the existing16-per-source/256-total caps until retirement.
A finished primary chain stops processing when its tail expires, retaining its
bounded buffer for the existing voice lifetime. Component/clip/source retirement
uses the established native detach/wait boundary.

Source Pause freezes the chain as well as the sound, including post-EOS tails.
Resume restores its state without rewinding a completed clip: native sound start
rewinds EOS, so tail-only Resume must start only the effect. Source Stop silences
the chain and discards its audible history; the next Restart creates fresh history.
Full suspension and game-pause groups freeze their connected chains; routing-only
changes preserve history. Source `echo_tail` identifies its primary echo tail and `echo_tails` counts retained
echoes. `effects_tail(s)` and `reverb_tail(s)` include their respective primary and
one-shot tails, including paused ones. Live sound shows Effect tail and enables
Pause/Resume even when no clip is still playing.
Counts indicate retained output state, not physical device audibility.

Existing property/Play automation and MCP bindings expose the same fields and
copied observations, with normal ownership/permission/revision/generation/history
rules. No extra effect tools/manager or agent-only playback implementation exists.
Gallery90 compares the same original processed recording with dry/quarter-/half-
second echoes and ordinary export dependencies. Shared bus processing is configured
separately below. General DSP graphs, acoustic simulation and smoothed effect
automation remain unavailable.

## Common mix channels

AudioSource `bus` selects the reflected stable FORGE values:0 Master,1 Music,
2 Effects,3 Dialogue. Old scene/prefab sources default to0. Source playback,
spatialization and clip ownership are unchanged. Invalid authored/native route
values use existing reflected admission and bounded failure recovery.

Shared `game.audio` defaults add optional `music_volume`, `effects_volume` and
`dialogue_volume`, finite linear0–1/default1, beside existing `master_volume`.
Resolved legacy settings expose all channels without changing the project file.
Player overrides use the existing settings document/requests and save transaction.
GameSession applies admitted preferences to active/candidate worlds and future
scene configuration. GameHostControls restores the previous complete mix if
settings publication fails. SdkPlay and standalone startup use resolved preferences.
Plain project audio uses declared project defaults.

For this flat gain-only mixer, channel gain multiplies each existing voice's
spatial-admitted source gain at its final existing output bus, after buffered
effects and before the common gameplay Master group. The native source engine
volume stays at one; source and category faders therefore also mute stored tails.
No new group is added. This shared gain owner handles source synchronization,
live mix changes, one-shots, effect replacement/Restart and private preview reset. This is
linear volume mixing; no extra graph node or dynamic attachment is required.
Master-route sources bypass category gain, but retain Master output gain.
Each source follows its own `pause_with_game` policy, independently of its channel. Channel/master updates validate every value
before applying any gain. Zero volume mutes while source time continues.
Changing route retains the voice, waveform cursor, clip and cache. Source volume
retains the existing spatial-validity gate, so mixer edits cannot unmute an
unresolved source. Runtime `channel_volumes` status is derived presentation data.

Project Settings owns shared defaults; Inspector owns each source route and its
independent prefab mask. Gallery33 uses the original tone at three pitches for
separate audible channels, with relocated source-free PCM proof. SDK component
layout/fingerprint changes require matching C++ rebuild; ABI1 remains unchanged.
Four common channels remain available. Project-defined buses extend those
terminals as described below. Streaming retains its separate bounded profile.

## Custom audio routing and sends

Optional `game.audio.routing` is `{version:1,buses:[...]}`. At most16 definitions
have stable lowercase ASCII keys (letter first, up to64 bytes), optional single-line
UTF-8 labels (up to128 bytes), finite volume0..1, one output and up to3 sends
`{target,gain}` with gain0..1. The terminal keys master/music/effects/dialogue are
reserved. All targets must resolve; duplicates and cycles reject even when their
current gains are zero. Preflight potential summed path gain with every custom
fader at1; no bus may exceed16. Values are never normalized to hide amplification.

AudioSource appends `custom_bus`, default empty. Empty uses the unchanged stock
bus0..3; nonempty selects an exact project key while retaining the stock choice.
The existing codec/reflection/backfill, Scene/Prefab/history and native source
validation own it. Labels never bind identities. Unknown custom keys reject
source admission; live tuning checks them before mutating the Scene or voice.
The changed public source layout requires the matching SDK rebuild; ABI1 tables
are unchanged. C++ modules can copy/edit AudioSource.custom_bus through shared
Flecs. Project topology remains authored settings, not an SDK-owned graph.

AudioRoutingProjection compiles bounded deterministic coefficients onto the four
terminals. Main output has weight1 and sends are additional post-custom-fader
paths. Terminal category faders follow their path sums; Master applies once.
Without bus DSP or delay, this is the same scalar output as summing the graph's
paths. Existing per-voice output applies the result after source effects, using
the current engine, cursors, pause groups, one-shots and stream leases. Muting
also covers buffered tails while time continues; rerouting does not restart sound.
No callback reads project maps or performs graph traversal/allocation.

Project Settings edits the existing settings draft; saving/restarting Play admits
the topology. Inspector source selection reads saved project definitions while
the picker is open. Missing selections remain explicit. Live mix adds custom
key faders through the existing AudioMix draft/command owner. User audio
preferences can contain partial `bus_volumes` numeric overrides, resolved against
the project defaults; they cannot override topology. Unknown keys reject.
An explicit live `bus_volumes` map must match every admitted key. Legacy requests
without that map preserve current custom faders. GameSession preflights before
updating active/candidate worlds and future Scene configuration; settings rollback
restores the complete mix through the same owner.

Project topology and retained nonempty source selections require runtime-kit
`runtime_audio_routing:1`, including partial/unbound Prefab values. Empty graph/
selection retains legacy kit admission. See [ADR036](decisions/036-audio-routing.md)
for the ownership and private recovery boundary. Optional shared effects use the
native profile described below; scalar projection alone does not process effects.
Convolution, acoustic simulation and a second audio clock are not provided.

## Shared linear bus effects

Optional `effects` on each `game.audio.routing.buses` row retains the existing
source effect field names and bounds: `high_pass_enabled`, `high_pass_hz`20..20000,
`low_pass_enabled`, `low_pass_hz`20..20000, `echo_enabled`, `echo_seconds`.01..1,
`echo_feedback`0...75, `reverb_enabled`, `reverb_decay`.1..10,
`reverb_damping`0...95 and `reverb_wet`0..1. Booleans are strict, numeric values
must be finite, unknown fields reject and inactive values remain validated.
Default `AudioBusEffects{}` omits the block, preserving legacy serialization.
Any explicit `effects` block requires `runtime_audio_bus_effects:1`, including
empty/default blocks and dormant settings: older strict routing readers reject
the field itself. Typed default serialization omits the block, retaining legacy
kit admission without rewriting raw project settings. The installed SDK value header set, fingerprint,
AudioSource layout and C ABI1 callbacks/operations are unchanged.

Effects-free projects retain the scalar path. With an active bus processor,
AudioRuntime owns a native bus graph with two pause domains per custom bus.
Native input buses sum signals; a continuous passthrough supplies silence for
idle filter histories. Existing EffectsChain implements HPF→LPF→echo→reverb.
A native splitter applies the bus fader and send gains after processing, routing
to the same-domain downstream bus or stock terminal. Terminal faders explicitly
disable unused native pitching to avoid an extra resampler on only some fanout
branches. Category and existing Master faders then apply once. Source/one-shot
cursors, streaming readers, spatialization and voice admission retain their owners.
Callbacks allocate nothing and access no ECS/project maps.

Two histories preserve independent versus game-paused audio. Linear superposition
matches combined processing when both domains run, within floating-point/nonfinite
protection limits. Game pause freezes only its domain; full suspension freezes
both. Independent history keeps draining after its originating voices retire.
Source controls/rerouting affect future input and preserve existing shared bus
history. Cue completion remains Source-owned and excludes shared tails. Bus mute
silences that bus after effects while its time continues; downstream histories can
still sound. The existing parent-group buffered control boundary remains.

Topology/effects are project settings admitted before Play; live commands change
only faders. Recovery preserves exact configuration and mix, with fresh processing
history, through the existing wrapper. No effect memory or source cursor is saved.
The existing exact-SDK Play recovery restriction remains.

Each AudioRuntime admits at most sixteen buses with two native domains each.
Its effects remain resident for that runtime and require no tail registry or
extra voice slots. Candidate/active worlds retain their existing separate owners.
At fixed48kHz stereo,32maximum echo buffers use12288000payload bytes and32reverb
chains use1904640payload bytes with the existing implementation. Global delay
memory includes source and bus ownership; `bus_echo_buffer_bytes`,
`bus_reverb_buffer_bytes`, `bus_effect_chains` and `bus_processing` distinguish
shared processing. Counts exclude native/filter overhead and decoded/stream data.
These bounds do not establish physical device deadline or performance acceptance.
See [ADR041](decisions/041-audio-bus-effects.md). Nonlinear processors, convolution,
cyclic audio routing and richer acoustics remain unavailable.

## Per-source reverberation

AudioSource appends `reverb_enabled` (false), `reverb_decay` (1.5 seconds,0.1–10),
`reverb_damping` (0.5,0–0.95) and `reverb_wet` (0.25,0–1). Existing builtin default
admission keeps older Scenes/Prefabs disabled. Reflection, Inspector, ordinary
Scene history, generic automation/MCP and native SDK values share these fields.
Native component layout/fingerprint changes require matching C++ modules; C callback
tables and clip/source formats are unchanged. Recovery retains component intent,
not native DSP buffers or one-shots; restored autoplay begins with fresh history.

The FORGE-owned private AudioReverb uses four unequal delays per channel, an
orthogonal feedback mix, duration-scaled losses and one-pole feedback damping.
Channels have different delays and independent state. Decay is nominal; no physical
room or exact universal reverberation-time equivalence is claimed. Existing pinned
miniaudio provides graph routing and callback lifetime only. The existing
EffectsChain owns a stable-address custom node after HPF→LPF→echo; sources and
shared bus domains reuse this implementation. No second mixer is introduced. Construction allocates on the owner, processing accesses only
preallocated samples, and node uninitialization detaches/waits before DSP storage
is destroyed. Continuous callbacks consume native padded input counts, including
null silence. Callbacks never access ECS, files, services or project ownership.

Disabled or zero-wet reverb creates no node/buffer. Effective parameter changes
replace the existing effect chain and clear its history without resetting the
clip. Dormant parameter changes do not rebuild it. Source/category faders apply
after effects, so mute covers retained output while source/DSP clocks continue.
Routing and pause preserve history. The existing native group can retain two
already buffered stereo frames at a leaf control boundary in offline tests;
controls are not sample-accurate cuts. Do not reset the shared group to control
one source. Source Stop disables output; Restart creates and admits fresh history
and restores its gain. Full suspension and per-source/game pause freeze tails.

Each reverb voice owns eight float delay buffers at the current48kHz stereo
output. The local implementation allocates59520 payload bytes per voice; actual
`reverb_buffer_bytes` derives from vector capacities and includes one-shots. It
excludes native node/filter overhead and temporary replacement chains. Existing
`echo_buffer_bytes` retains its separate meaning. Existing256-total/16-one-shot
caps apply; there is no independent unbounded effect population. These memory and
CPU/offline checks do not establish physical audio deadline or device performance.

A reverb tail is retired after `ceil(rate*(2*decay+0.5))` output frames from
owner-observed EOS. With echo upstream, its existing tail bound is added. This is
a bounded retirement policy, not a universal absolute-silence threshold for every
possible waveform. Paused tails retain their existing voice slot. Gallery148
reuses the original tone and sequence/mesh/camera assets for dry0s/bright3s/damped6s
comparisons. Actual offline cue/tail PCM is byte-identical after source-hidden
relocated export; no source WAV or project C++ is required at playback.

## Live Play mix and status

Project Settings presents a runtime-only section above the disabled authored
defaults during Play. It reports output mode, pause state, playing/allocated
voices and decoded clip cache count/bytes from the existing runtime snapshot.
Four stock linear0–1 sliders and any custom-key faders edit a session-local draft; Apply live mix sends one
request and Reload live values discards that draft. Neither action edits project
settings, scene history or saved player preferences. Dragging a slider sends no
per-frame transport traffic. Pending requests disable further edits.

The private `audio_mix` request admits one finite master and exactly three finite
channel values plus an optional complete custom `bus_volumes` map before changing the existing AudioRuntime. SDK Play also requires
the current active world generation. Disabled audio and invalid/stale requests
retain the previous complete mix. PlaySession uses its existing single correlated
transport worker, keeps audio telemetry from receipts, and queues at most one mix
request. A correlated rejection reports an error and requests a fresh snapshot;
the healthy session remains usable and a later Apply can retry.

Both legacy and SDK Play update gains without restarting voices, reopening output
or advancing a paused simulation. Retained runtime configuration preserves the
mix through world reconstruction within the same process. Stop or a new Play
session reloads saved defaults; a new process does too unless an explicit custom
routing recovery envelope restores its complete host mix. The editor clears its draft and queued
request on teardown. The mix command adds no C ABI table, backend, thread
or exported editor. Gallery33 demonstrates category balancing; Gallery34
also exercises the live controls with processed cooked PCM.

## Editor clip audition

The AudioClip document offers explicit Play/Restart preview, Stop, Pause/Resume, Loop and
temporary Preview volume0–1/default.5. Selecting a clip never autoplays it.
These controls change no authored source, shared settings, asset index or history.
Preview volume is independent of gameplay mix channels.

The editor-only AudioClipPreview consumer owns one temporary Runtime-role world
with one nonspatial AudioSource, using the normal audio provider and selected
cooked revision admission. Authoring worlds still expose no audio service.
Selected identity/type/schema/source/metadata must match the current catalog;
the existing loader independently admits artifact digest/PCM. The preview provider reads published cooked bytes independently of the original
recording; opening its document retains the existing import-editor contract. No private codec or runtime
backend is duplicated, no new asset ownership or gameplay clock is introduced.

Explicit audition performs the existing bounded clip load (source16MiB/PCM64MiB).
The device callback plays the retained voice; poll does no decode. Loop edits
update that voice; preview volume updates its existing Master gain. Unchanged
controls do no synchronization. Natural completion, Stop, close, selection or
revision change, project change, import draft/job/lock and game Play release the
voice, device and decoded cache. Failed initialization reports the actual audio
error without a null-device fallback. Explicit offline/null outputs are test-only.
Show waveform explicitly loads through the same bounded selected-clip admission,
without an audio device. A bounded editor-only resource job admits the selected PCM
and reduces it to at most4096 min/max buckets per channel off-thread, then releases
PCM. The retained result is only an envelope. Playing uses the normal runtime clip
pool; if an envelope is missing, an independent envelope job uses the same admission.
No cook revision, catalog array or second PCM cache is introduced. A bucket spans
`[floor(i*frames/N), floor((i+1)*frames/N))`, including the final partial range.
Mono/stereo channel extrema remain separate; values above unit amplitude are
displayed using a common scale. Stop/natural completion retain only the envelope;
close, selection/revision/project change and Play/import locks release it.

Preview pause suspends the existing Master sound group. Seeking commits on timeline
release or waveform click and preserves pause. Private AudioPreviewAccess admits
only the preview's single nonspatial pitch1/bus0 source. It detaches/uninitializes
that sound node before seeking its buffer, then recreates the node to discard old
resampler/processing cache. Exact miniaudio sound uninit waits graph readers; the
resident PCM or immutable stream backing and the output device remain retained. Stream seeking prepares a replacement native source asynchronously. It never reads the live mutable
data-source cursor from the editor. End seeking clamps to the final source frame.
The UI time is source seek offset plus the atomically read sound output-node time
divided by engine sample rate, modulo duration when looping. It measures mixed
playback position, not physical device latency or sample-accurate audible time.
Nonfinite/out-of-range positions reject before changing the admitted preview.
The private friend declaration is not an installed SDK header or ABI callback.
Audition and waveform requests report loading and adopt results in preview poll.
Stop remains available during audition loading; transport position/pause controls
require readiness. Errors from asynchronous admission are retained in the clip tab.
Actual offline PCM and scaled widget tests do not prove physical sound quality.

## Clock and presentation

RuntimeSimulation's fixed phase invokes audio synchronization after a successful completed tick and final transform evaluation; scene candidate realization can synchronize at the initial/recovered boundary. The host's separate loading service adopts completed clips between ticks, including while paused, without advancing the simulation; unchanged poses retain their last fixed velocity. Synchronization publishes current effective world positions and listener direction/up. Affine directions are normalized/orthogonalized without changing authored transforms or imposing physics shear restrictions. Source scale does not scale range/gain. Spatial positions must be finite and within ±1e9 meters before conversion to backend float; this is a safety bound, not a large-world precision claim. Distance falloff defaults to inverse with strength1 and clamped min/max distances. Directional gain and optional Doppler use the existing native spatializer. Occlusion and frequency-dependent cone filtering are deferred.

### Moving sources and listeners

AudioSource appends reflected `doppler`, strength0–5/default0. Spatialized sources
with positive strength derive source and listener velocity from consecutive final
world positions and the actual completed fixed delta. Render time and device
callbacks never supply motion. Default0 and nonspatial playback preserve nominal
pitch. Changing strength retains the existing voice and decoded clip.

Previous positions are private audio state. Initial or repeated/skipped boundaries,
invalid poses, disabled tracking, listener changes and world reconstruction reset
motion. Existing animation seek/loop and physics teleport discontinuities also
reset affected emitters/listeners through the effective spatial parent graph;
ordinary animated and physics movement retains velocity. No second hierarchy,
simulation clock or persistent movement component is introduced.

The selected native Doppler calculation uses343.3m/s. FORGE bounds finite velocity
magnitude to100000m/s and limits the effective strength so each scaled speed is at
most half that sound speed. This prevents singular native denominators and bounds
the pitch ratio to[1/3,3]. Ordinary subsonic motion retains authored strength;
extreme motion is a safe bounded effect, not supersonic acoustic simulation.

### Overlapping source effects

`AudioService::play_one_shot` queues an independent, nonlooping playback of the
current AudioSource clip. It shares that source's immutable selected-clip lease and retains
its own cursor; it does not restart the primary sound. Source looping applies to
the primary only. Each transient follows the source's validated current transform,
gain, pitch, attenuation, cone, Doppler and mix bus. Master/channel updates and the
Master gain and the source pause policy apply to both kinds of voice.

Source Stop retires all its effects and stops/resets the primary. Pause/Resume also
operate on effects, including beside a stopped primary, preserving their positions.
Restart resets only the primary; existing effects continue with their own source
pause state. A new effect inherits the primary's explicit source pause, if present.
Pending clip commands retain identity/clip/native generation and fold in order;
new transients start after the entire owner command batch, so Stop or Pause during
loading cannot leak an initial audio burst. One-shot, Stop, one-shot keeps only the
last effect. These commands never change authored values or advance simulation.

Limits are16 active effects per source and256 total primary/pending/effect voices.
The existing1024 command bound also applies; pending queued one-shots are bounded
at16 per source since the last Stop. Excess commands or native voice requests reject
with a diagnostic; healthy primary voices are not stolen or failed. Completed
transients retire on the owner host/fixed boundary (or explicit offline read),
releasing only their leases. Deleted/removed/changed-clip sources retire their effects;
a recreated source cannot receive a queued command for its old native generation.
No callback accesses ECS or files. Recovery restores authored autoplay, without
transient voices or sample cursors. Export uses the existing AudioSource clip closure.

### Selected runtime source controls

Inspector's Live sound panel watches only the selected AudioSource in the active
Play world. Play/Restart, Play one-shot, Pause/Resume and Stop use the existing AudioService command queue at
an owner boundary; they do not resample transforms or advance paused ticks.
Identity and shared-SDK generation checks reject stale targets before mutation.
The existing correlated Play transport carries one bounded audio command at a
time and reports nonfatal rejection for retry. A changed selection requests one
fresh observation, including in nonperiodic probe mode.

Source Pause stops its native node without seeking; Resume starts only an
explicitly paused source. An unpaused/stopped/completed source's Resume is a no-op.
Repeated Pause is idempotent. Restart and Stop clear source pause. Whole-game
pause remains independent: ordinary sources stay silent until game Resume;
opted-out sources can resume during game Pause. Game Resume does not resume a
source explicitly paused by gameplay or Inspector.
The paused-source flag is ephemeral voice state; recovery follows authored
autoplay and does not restore that flag or a sample cursor.

The private protocol accepts one `operation` string (`play`, `stop`, `pause`,
`resume`, `one_shot`) or the existing strict `play` boolean. Conflicting fields, unknown or
mistyped operations reject before mutation. Existing boolean consumers retain
Restart/Stop behavior.

Status exposes availability, primary native atomic playing flag, source pause, game pause, loop, clip
duration and total/playing/paused one-shot counts. Global voices counts primary plus transients;
source_voices and one_shots report those counts separately. Status does not reap/adopt resources. It does not read the mutable data-source cursor or claim physical output
latency. Already submitted audio buffers may finish after Stop. Saved scenes and
mix defaults remain unchanged; Stop Play/new Play restores authored autoplay.
The private access header is not installed SDK API. The appended public component
member changes the matching SDK fingerprint/layout and requires a C++ rebuild;
ABI1 tables, authored formats and private recovery envelopes remain unchanged.

The device callback mixes continuously and never advances gameplay time. Pause stops the gameplay group while keeping the device alive. Step updates intent/transforms once with the group still stopped; it does not request an audio timeslice. Resume starts the group. Play commands restart the source at frame0; Stop stops and resets it. Basic playback counts are diagnostic presentation state, not authoritative simulation clocks.

Recovery reconstructs autoplay from recovered source components at frame0; other
playback is stopped. No sample cursor, effect memory or audio blob enters the
physics checkpoint or Scene. Candidates start suspended. Custom routing uses
private root checkpoint3/audio block1 with exact project graph and complete host
faders, admitted before candidate publication. Invalid graph/faders preserve the
active world and host mix. Legacy projects retain root2; it cannot restore a
required custom profile. Exact-SDK Play retains its explicit recovery restriction.

## Composition and SDK

`forge_runtime --project PATH --audio device` explicitly enables optional real output. Plain `--project` supplies the asset root only; protocol hello keeps the existing clock/input/gravity configuration authority, including recovery from the active play session. `--audio offline` selects explicit PCM-read tests; private `--audio null-test` uses the existing null device to exercise host/output-clock progression without physical audio acceptance; omitting `--audio` remains headless. The experimental profile uses `--sdk-project PATH` with the same audio option. A consumer requiring Audio must declare dependency `forge.audio`; unavailable required services prevent consumer startup. Optional output failure emits a diagnostic and continues without Audio capability.
Runtime Scene preparation requires an available audio provider for nondefinition
AudioSource entities; headless scenes without that requirement still load normally.
Schema-only authoring and validation retain data-only component support.

Exact SDK supplies a size/fingerprint-checked `audio_source` callback taking scene UUID, entity UUID and operation: `FORGE_SDK_AUDIO_STOP`0, `FORGE_SDK_AUDIO_RESTART`1, `FORGE_SDK_AUDIO_PAUSE`2, `FORGE_SDK_AUDIO_RESUME`3, `FORGE_SDK_AUDIO_ONE_SHOT`4. The first two retain their previous semantics; unknown values reject. Fixed callbacks retain queued owner-thread operations. Control callbacks can immediately command only active sources with `pause_with_game=false`; ordinary sources and suspended/unpublished output reject. Generation and selected clip are captured at enqueue and revalidated before execution. Source-engine AudioService exposes matching `play`, `play_one_shot`, `stop_source`, `pause_source`, `resume_source` owner-thread commands. The changed installed header fingerprint requires rebuilding exact SDK consumers; callback table size and ABI number remain unchanged. Gameplay can register/query/edit FORGE AudioSource values through shared Flecs. No miniaudio types or headers enter the SDK. AssetId is preserved by copying typed component references; out-of-line host identity/catalog helper implementations are not exported as a general SDK library.

The C++ convenience wrapper retains `sdk::Client::audio(scene,entity,bool)` and
adds `audio_operation(scene,entity,uint32_t)` for all four constants. Separate
names preserve existing boolean and integer-to-boolean calls without introducing
ambiguous overloads. Both paths use the checked host callback.

## Animated source volume and pitch

The existing Transform Animator/Timeline can bind AudioSource gain and pitch using
the same authored keys and fixed animation clock. The existing post-tick audio
synchronization applies these values to the retained voice, preserving playback
position. Clip, looping, spatialization and mix channel remain unbound configuration.
Shared AudioSource scalar/domain validation serves both audio and animation; missing
or invalid targets reject before animation commits any player's candidate.
Master/channel volume still multiplies the sampled source gain. Timeline preview
does not play sound or advance audio. See [typed property animation](transform-animation.md)
and Gallery53. This is fixed-tick automation, not sample-accurate scheduling or
pitch-preserving time stretch; ephemeral recovery retains the existing restart policy.

## Validation and deferred work

Audio tests use offline PCM and explicit null-device callback teardown. Process tests cover schema-only mode, pause/step/resume, private recovery reconstruction and rejected candidate preservation; shared SDK fixture checks typed components, Play/Stop and missing capability. Existing physics/runtime/render tests remain required. These tests cannot establish subjective spatial quality or physical-device playback.

Scene sequence [audio cues](transform-animation.md#sequence-audio-cues) request the
same one-shot operation on forward fixed playback. The animation provider owns
only crossing/entry intent; this audio owner retains decoding, commands, source
generations, routing and voices. Seek and stopped Timeline preview request no
sound. Existing effects retain their ordinary audio lifetime. Private animation
recovery preserves cue intent without recovering or replaying audio cursors.

## Event sound-cue composition

`forge.sound_cue` is an optional Scene/Prefab-owned SoundCue component. Its root,
seed and ordered node/edge values are authoritative. SoundCueProgram is a bounded
immutable derivation; AudioRuntime keeps only transient selection state indexed by
the existing native entity generation and durable reference. It adds no clip
registry, sample clock, mixer, source voice or backend node.

The profile admits32nodes, eight children per branch and16emitted source requests
per event. Node kinds are native reflected enum values0 Source,1 Layer,2 Weighted
and3 Ordered. Source nodes contain a typed EntityRef and no children; branches
contain no source and one to eight edges. Edge targets are zero-based indices in
this component's node list. Weighted edges accept integer weights1..65535; other
edges require1. Null Source leaves and an empty graph are editable drafts, but
cannot be triggered until every declared leaf is complete. Validation inspects
all nodes, including disabled/unselected branches, for shape, cycles, targets and
potential Layer fanout. No unbounded or recursive cue-entity references are allowed.

Source-only `delay_seconds` defaults to zero and accepts finite values0..10.
Branches require zero. Selection returns offsets alongside source references;
commands retain both in the existing transactional admission. Each prepared
one-shot schedules against the native engine output-frame clock, rounding to the
nearest output frame independently of pitch. Preparation/loading can postpone the
start of the offset. The existing voice owns only remaining-frame/deadline intent,
not a second clock, scheduler or resource cache.

Full suspension, explicit source pause and applicable game pause preserve an
unexpired interval. The owner stops and rebases a waiting voice before resuming;
already-started sounds retain normal cursor/effect semantics. Source pause policy
changes reuse the existing parent group routing. Stop and source/world replacement
retire waiting voices through their existing owner. Waiting voices count toward
the existing one-shot and total budgets. See [ADR038](decisions/038-timed-sound-cue-offsets.md).

Each visited node has a transient64bit visit count. Layer visits children in stored
order; Ordered uses its previous visit count modulo its child count. Weighted uses
a local standard MT19937 seeded by `{seed,node_index,visit_low,visit_high}` through
standard seed_seq. The first32bit output maps to the bounded cumulative weight
range using a64bit product and upper32bits. Rounding differs by at most one ticket
per2^32values; this is reproducible event selection, not a cryptographic or acoustic
quality claim. Shared nodes advance per visit. Counter exhaustion rejects without
advancement; the caller's input state is never mutated by selection.

The existing AudioService one-shot operation and ABI1 SDK callback accept either
an AudioSource or a SoundCue entity. A cue entity must not also have AudioSource;
event-only cues accept only OneShot. Configured repeating cues also accept the lifecycle operations described below. The callback table and operation values are unchanged.
The public installed component header/fingerprint requires matching C++ modules.
Timeline AudioCue events can target a SoundCue through the same AudioService.

Before enqueue, the runtime compiles the entire definition and resolves every
leaf in the cue's loaded Scene. It validates active application-state conditions,
source values/clip assignment and custom bus keys. Control-domain requests also
require every declared source to ignore gameplay pause and active output. The
event expands into existing generation/clip-bound one-shot commands. A rejected
append rolls back every command from that event; selection advances only after
the complete enqueue succeeds. Subsequent loading, voice budgets, stale source
changes and backend errors retain ordinary one-shot semantics/diagnostics. An
accepted event is not an atomic guarantee of audible voices.

Graph changes, source-world retirement or fresh/recovered runtime replacement
reset selection state. Source cursors, effect memory, one-shots and cue selection
counters are not recovered. Cue status reports its kind, maximum fanout, accepted
trigger count, node visits and most recently selected source references/offsets; it does
not claim ownership of those sources' other voices. Scene values remain unchanged
by triggers. Nested native vector/reference metadata uses the existing admission,
copy, Prefab member remapping and history owners. Inspector graph drafts publish
complete candidates using property.set_many and the captured Scene revision.

Automation/MCP uses existing entity recipes/property editing and scoped Play
watch/audio controls. Discovery exposes the component's nested enum, typed refs
and collection bounds; `one_shot` consumes selection state and is not safe to retry
as an idempotent operation. Runtime document capability `sound_cues:1` requires
kit `runtime_sound_cues:1`, including retained partial component/property values.
Scenes without that component do not require this capability.

Nonzero offsets additionally emit `sound_cue_timing:1`, requiring
`runtime_sound_cue_timing:1`. Earlier zero-delay content retains its previous kit
requirements. Retained/partial values participate in feature discovery. Earlier
nodes default missing offsets to zero through the JSON codec and native member
evolution annotation. This changes installed component headers/fingerprints and
requires matching C++ modules; C ABI1 callback tables/op values remain unchanged.
Source status includes `delayed_one_shots`; Live sound admits Pause/Resume/Stop
while a voice is waiting, including through the existing scoped MCP controls.

Later work includes unbounded music streaming, voice stealing, cyclic cue
graphs, nonlinear bus processors, impulse responses, acoustic room simulation, multiple
listeners, sample-accurate recovery continuity and device selection.


## AudioClip import and selected revisions

The pinned miniaudio0.11.25 (`9634bedb5b5a2ca38c1ee7108a9358a4e233f14d`)
provides WAV, FLAC and MP3 decoding. Explicit recipes `forge.audio.wav`,
`forge.audio.flac` and `forge.audio.mp3` use one shared importer/settings/processing
implementation, the supervised `forge_asset_build` worker, source/settings sidecar,
watcher, revision key/DDC, AssetPublisher, Content document and public asset tools.
Each selects `ma_encoding_format_wav`, `_flac` or `_mp3`; a mismatched extension
never authorizes fallback format detection. Published source-format metadata and the Audio clip details identify the actual
recording; importer, publication, cache and runtime reject cross-format metadata.
Old WAV recipe IDs/settings, legacy
registered-only WAV, asset identities and cooked PCM format1 remain supported.
MP3/FLAC require importing; the legacy registration method still accepts WAV only.

The worker uses `ma_decoder_config_init(ma_format_f32, 0, 0)`, memory init,
frame-length/read and uninit. It admits finite complete mono/stereo data within
16MiB source/64MiB decoded and the existing384000Hz ceiling, in a512MiB/30second
worker. Encoding/generation remain disabled; the existing miniaudio build now
includes its bundled FLAC/MP3 decoders. No native codec type enters the SDK.
FLAC matches independent original PCM exactly in the tested profile; MP3 is lossy.
Encoder delay/padding metadata affects MP3 length; absent metadata can leave
padding. Decoder admission is not universal bitstream validation: a shorter stream
with no declared missing frames can be valid shortened audio. Ogg/Vorbis,
arbitrary multichannel audio and source encoding remain unsupported. Bounded
cooked-PCM streaming is described below.

Original mono48kHz and stereo24kHz fixtures cover native decoding, cross-format
rejection, processed shared recipes, actual supervised workers, stable publication,
preview/waveform/seek, failed reimport retention and source-free playback. The
shared import/export and MCP owners retain normal scope/revision/history rules.
Development-only fixture encoding tools are not shipped or engine dependencies.
Native Windows/MSVC, physical listening, external-client use and new authentic
codec/gallery captures remain separate queued acceptance.

The version1 settings schema adds optional `force_mono`, `normalize_peak` and
`trim_silence` booleans, all false by default, and finite `trim_threshold_db`
in[-96,-6], default-60. Existing empty overrides preserve decoded samples,
channels and sample rate. Settings enter the existing canonical cook key and
sidecar; the exact recipe fingerprint changes with the processing implementation.
The worker processes the decoded buffer once, without adding runtime DSP:

- Force mono averages stereo `(L+R)/2` with a double intermediate. Mono is unchanged;
  opposite-phase channels can cancel.
- Normalize peak divides every channel by the shared absolute peak, using double
  arithmetic. Zero silence is unchanged. This is peak normalization, not loudness
  normalization or compression.
- Trim silence retains frames where any channel exceeds `10^(threshold_dB/20)`,
  removes only quiet leading/trailing frames, and preserves channel alignment.
  Entirely quiet input becomes one silent frame. Each edge actually removed gets
  a5ms linear ramp, capped at half the kept frames; ramps shorter than2 frames
  are skipped. The first/last ramp sample is zero, and the inward endpoint is1.

The order is mono, normalization, then trimming/ramps. Final peak can therefore
fall below1. Cancellation is checked within long passes. Source WAV bytes and
runtime source/mixer volumes are unchanged. Metadata and preview describe the
new selected PCM after successful reimport; dirty import settings stop/release
the previous audition/envelope. Failed admission/publication keeps the prior
selected revision. Source-free export includes the processed immutable PCM.

Editor preview metadata comes from the existing import service's published catalog,
independently of asynchronous Content discovery. A stale browser row cannot replace
that current selection or revive a removed clip. Read-only projects retain their
existing discovery snapshot fallback. Waveform/PCM admission still checks the
captured selection against the on-disk published catalog on an explicit request.


Original Gallery34 compares untouched stereo with normalized, trimmed mono.
Direct and isolated worker fixtures cover exact defaults, unequal/antiphase
channels, finite extreme/subnormal samples, silence, short clips and24/48/96kHz
ramps. Generated controls use real input at65/100/150/200%; the editor workflow
covers audition teardown, publication, waveform replacement and reset/reload.
Relocated Gallery export validates actual processed PCM/playback after source
removal. Native import images and physical listening have separate acceptance.

Cooked `forge.audio-clip` version1 contains two immutable files: `audio.fpcm`
and `audio.json`. PCM has an explicit32-byte little-endian header: eight-byte
`FORGEAUD` magic, u32 version1, u32 channels, u32 sample rate, u32 zero reserved,
u64 frame count, followed by exact-length little-endian IEEE754 f32 samples.
Count/length/numeric checks precede allocation. JSON records source digest,
WAV/PCM format, channels, sample rate, frames and duration; it must agree with
PCM. No miniaudio/native struct or process handle is persisted. Artifacts and
logical references are graphics-backend-neutral.

Runtime selects the catalog's DDC key, validates hashes and metadata, and loads
cooked PCM without requiring the development WAV. Registered-only legacy records
use the same bounded native WAV decoder. AudioRuntime retains voice ownership and
uses its typed CPU pool to perform admission off-thread. The catalog snapshot and
selected cook remain authoritative; asynchronous loading changes no audio format,
importer revision, persistent identity or installed gameplay callback table.

Invalid, cancelled or stale imports keep the previous catalog, sidecar and
selected clip. Reimport retains AssetId. Content moves retain identity and the
selection; duplicates allocate a new identity, clear admission/selection metadata
and require their own import. Source control includes the WAV, sidecar and
catalog; DDC remains disposable. Selected cooked audio must be included in runtime
packaging. Scene Undo does not reverse asset imports.

The central Audio clip document owns import/reimport/close guards. Content
Inspector shows duration/channels/sample rate/format. Typed AudioSource picking
and drop reuse the existing shared field. The editor clip audition described above
uses a separate temporary Runtime-role world and the existing provider; it does
not enable audio on the authored scene.

Validation covers direct and actual worker decode, deterministic output, cache
hit, stable legacy ID, corrupt/truncated/nonfinite data, successful/stale/failed
reimport, corrupt selected output, source-absent runtime playback, Content
move/duplicate and public CLI publication. Sanitizers cover the decoder and
in-process admission path. Windows and physical-speaker acceptance are recorded
separately from these Linux/offline checks.


## Bounded cooked-PCM streaming

The Audio clip import schema has `playback_storage`, an explicit string choice
`resident` (default) or `stream`. Resident metadata omits the field, preserving
existing selected bundles. Stream metadata adds `playback_storage:"stream"` to
format1 and requires at least48000frames at48000Hz after processing. The pinned
native resampler converts other admitted rates during cooking, with cancellation,
finite-value validation and the existing64MiB decoded bound. Exact48000Hz input
is retained. Normalization is a cook operation, not a live mixer conversion.

`AudioClipResource` is the private ResourcePool realization: immutable resident
PCM or an owned `AudioStreamFile`. The existing catalog, selected artifact digest,
metadata validation and revision identity remain authoritative. Stream loads copy
validated cooked bytes directly into a proper float WAVEFORMATEX/fact backing,
without constructing a second full PCM vector. Source recordings and DDC files
may disappear after admission. `WorkerStaging` and `WorkerStageLease` own locked
`audio-stream` stages beneath the operating-system temporary root; recognized
orphans use the existing maintenance rules. Live and unknown stages are retained.
This is not a project/cache locator borrowed by the mixer or another asset registry.

The existing engine-owned miniaudio resource manager prepares `STREAM|ASYNC`
sources without `WAIT_INIT`. Each private native source has two one-second pages.
Eight source slots include preparation and retirement. Mono/stereo page reservation
is384000/768000bytes per voice; eight stereo reservations are6144000bytes. These
are page budgets, not total process-memory measurements. Temporary immutable
backing has separate64-file/256MiB quotas. Unused streamed selections are unloaded
through the same ResourcePool rather than retained as zero-resident-byte cache
entries. Resident cache policy remains byte-bounded at256MiB.

Voices and one-shots use the existing map with a pending phase. Their headers,
format and initial pages must be prepared before sound attachment/start. Commands
retain intent through preparation, including Stop suppressing autoplay. Pause/
Resume keeps the same native position. Restart, Stop/reset and preview seeks detach
the sound, retire its old source and request a new source with an explicit initial
frame. Readiness is acknowledged through normal asynchronous preparation; available
frames alone do not acknowledge a native seek. Resident preview seek retains its
existing pause-group routing. Preview reports the target position while loading
and retains paused transport. Waveform generation is independent and may transiently
decode the full clip before retaining only its bounded envelope.

An adapter composes the public native data-source APIs. Mixer reads only record
atomic starvation/read-error observations. Owner servicing emits at most one
starvation and one read-error diagnostic per source incarnation; no callback
fileIO, logging, World access, FORGE work allocation or worker wait is introduced.
The explicit offline consumer alone gates page readiness and reads bounded256frame
quanta outside the callback. The page gate covers authored pitch and the existing
bounded native Doppler profile. Offline checks do not establish physical-device
latency, audio quality or acceptance.

Native stream uninitialization can join IO. `AudioRetirement` therefore owns one
bounded joined cleanup thread per runtime with stream-capable catalog content.
Only detached native sources and copied immutable backing cross this boundary;
World, ResourceLease, ma_engine and device graph ownership remain on the provider.
The fixed72-job queue follows the8source/64backing limits. Shutdown stops callbacks,
detaches sounds, joins ResourcePool loaders, drains native/file cleanup, then
uninitializes groups and the engine/resource manager. No live source is destroyed
on the fixed synchronization or mixer path.

Status separates `decoded_bytes` (resident payload), `stream_page_bytes` (attached
or preparing page budget), `stream_reserved_page_bytes` (including retirement),
`stream_backing_bytes/files`, preparing/reserved/retiring counts and current-voice
`stream_underruns`. Backing counters describe disk, not RAM. Live audio mix exposes
these categories. Existing source-editor, automation and MCP settings tools borrow
the same schema, draft revision checks, publication owner and current catalog.

Generated kits advertise `engine.runtime_audio_streaming:1` from
`audio_stream_profile.hpp`. Export requires this exact integer profile for a
reachable streamed AudioClip before promoting a destination. Older resident
projects remain compatible with existing kits. Packaged content includes immutable
cooked PCM, never the development recording or temporary backing. Gallery172
contains an original twelve-second stereo44100Hz source cooked for streamed48k
playback and an ordinary rotating cube, camera and light. The supplied runtime
kit is required for export.

Focused checks compare actual mixed samples with resident playback across pages,
mono/stereo, pitch, looping, initial offsets, pending commands, live loop/pitch/
filters, spatial/Doppler/effects, source/cache removal and preview seeks. Controlled
native worker starvation/failure checks cover counters, resumed output, IO thread
ownership, slot limits and joined cleanup. MCP exercises both framings through
actual settings and worker publication. Platform and physical review remain
separate from these no-device checks.


## Repeating sound-cue events

SoundCue root values `repeat_interval_seconds` (default 0), `repeat_on_start`
(default false) and `repeat_pause_with_game` (default true) configure optional periodic
events. In Interval mode, zero disables repetition; otherwise the interval must be 0.25..3600
output seconds, rounded to native output frames. Every event still evaluates the
bounded acyclic graph. This is periodic event production, not cyclic node graphs
or clip-completion playlists. See [ADR039](decisions/039-repeating-sound-cue-events.md).

The existing Restart operation (`play` in automation/private protocol) resets
selection and starts a producer. Its first event waits for every declared source
voice to prepare and the applicable output domain to run. Later deadlines use the
same native engine clock. Pause/Resume preserve the remaining interval; Stop
cancels future events. Already-requested sounds, including delayed voices, remain
owned by their Audio Sources. OneShot always requests one event, even when the
producer is stopped. A manually stopped startup producer stays stopped until
Restart, a definition change or a new runtime world. Observed definition changes
retire previous selection/production state.

Full output suspension always holds production. Gameplay pause holds producers
with `repeat_pause_with_game=true`; independent producers require every declared
source to disable its own `pause_with_game`, including unused branches. Existing
source gain, pitch, routing, streams, effects and budgets apply to each requested
voice. Source removal or an invalid source policy stops production with a
transient diagnostic; fix the source and explicitly Restart. No callback reads
Scene or graph state.

At most 32 producers run within the existing 256 cue-state limit. Each owner service
boundary produces at most one event per active cue; missed periods are skipped
and counted while the next deadline retains the original phase. A stalled host
therefore cannot create an unbounded catch-up burst. Native device requests are
owner-boundary dispatch, not a promise of callback-accurate starts across stalls.
Offline PCM reads split at event deadlines and use the existing bounded loading
owner to prepare newly created stream voices before advancing output time.
Unrelated failed sources retain their diagnostics. Device playback remains
asynchronous. Both runtime
hosts service independent production outside fixed game ticks.

Status exposes availability, running/explicitly-paused/domain-suspended state,
source preparation, next remaining interval, skipped events and retained producer
errors. These are transient runtime observations. Recovery retains authored
configuration and starts fresh selection and production; active deadlines,
producer controls, queued voices and effect state are not recovered. Startup
intent may start a fresh producer in the recovered world.

Scene/Prefab/history, Inspector, C++, automation and MCP use the same component
values and existing owners. Missing native/JSON members default to 0/false/true.
Any nondefault repeating intent, including retained partial Prefab values, requires
`sound_cue_repeat:1` and kit `runtime_sound_cue_repeat:1`. C ABI1 callbacks and
operation values are unchanged; C++ modules need matching installed component
headers and fingerprint.


## Completion-aware cue production

`SoundCue.repeat_mode` defaults to0(Interval). Mode1(AfterCompletion) permits a
finite0..3600second gap in `repeat_interval_seconds`, including zero; mode0 keeps
its existing0off or0.25..3600second interval. The same producer/control and Source
owners apply. See [ADR040](decisions/040-completion-sound-cue-events.md).

Accepted events publish a monotonic runtime-local ticket with their commands.
Source-owned one-shots retain that provenance; CuePlayback records pending and
admitted requests for its current event. Source retirement, including effect
tails, determines completion. Manual one-shots, other cues and older events after
Restart do not delay the current event. Rejected/stale admission stops future
production with a diagnostic and cannot create a zero-gap busy loop. Source
controls retain already-requested voices. No durable ticket, clip registry or
callback access to ECS is added.

The gap starts when the owner can observe completion and current Source readiness.
A held producer begins that gap after it resumes. Source pause holds voice
completion; Source Stop can finish the current event and permit another, while
cue Stop cancels future events. Current Source/domain validation remains required
while waiting, including a Source changing its gameplay-pause policy. Definitions
and recovery retain their existing fresh-state behavior. Without an event limit,
the producer repeats. Arbitrary cyclic graphs and a standalone playlist document
format remain separate.

Status additionally reports mode, waiting-for-completion, completed events,
pending requests and current event voice counts. These counts are transient and
exclude unrelated/older voices. Native chaining follows service boundaries, not a
promise of sample-perfect continuity across host stalls. Mode1 adds runtime feature
`sound_cue_completion:1` and kit `runtime_sound_cue_completion:1`, including retained
partial values. Missing mode0 retains earlier requirements. Matching C++ component
headers/fingerprint are required; C ABI1 callback tables/op values are unchanged.

## Finite cue runs

`SoundCue.repeat_event_limit` defaults to 0 (unlimited). Values 1..65535 limit the
scheduled events in one run, including the first, in either timing mode. A limit
alone does not enable Interval repetition when its interval is 0. Skipped late
intervals and manual OneShot requests do not consume this count; manual requests
still advance the graph's shared choice state.

After the final request, production enters finishing and waits for all Source-owned
voices and source effect tails requested by this run. Earlier overlapping Interval
events count too. Manual sounds, other cues, older runs after Restart and shared
bus tails do not hold up completion. No final completion-mode gap is added.
Source cancellation retires its voices through the same owner; rejected admission
or a failed stream stops production with an error rather than successful completion.

Pause/Resume hold production and completion observation; Source and output pause
policies still apply. Cue Stop cancels future requests and clears natural-finish
state while retaining Source voices. Restart resets choices/counts and starts a
new run. Run provenance is runtime-local, shares the existing event allocator and
is not persisted. Recovery starts fresh from authored intent.

Status adds `repeat_event_limit`, `repeat_issued_events`, `repeat_finishing`,
`repeat_finished`, `repeat_run_pending_requests` and `repeat_run_voices`.
The existing producer slot remains occupied until its run finishes or stops.
Inspector, Live sound, Scene/Prefab/history, automation and MCP share these owners.
Nonzero limits, including retained partial Prefab values, require
`sound_cue_finite:1` and kit `runtime_sound_cue_finite:1` in addition to applicable
cue/timing/repetition/completion profiles. Missing or zero limits retain earlier
requirements. Rebuild C++ with matching component headers/fingerprint; C ABI1
tables and operations are unchanged. See [ADR042](decisions/042-finite-sound-cue-runs.md).
Finite component runs do not introduce a standalone playlist document format.
