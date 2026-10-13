# Audio module

`forge.audio` supplies FORGE-owned AudioSource/AudioListener schemas and an optional runtime provider. miniaudio **0.11.25**, commit `9634bedb5b5a2ca38c1ee7108a9358a4e233f14d`, is private static C implementation with the MIT-0 license option. Official source: https://github.com/mackron/miniaudio/tree/9634bedb5b5a2ca38c1ee7108a9358a4e233f14d . Existing dependency pins are unchanged.

## Build and ownership

Windows output explicitly selects WASAPI; Linux selects PulseAudio/ALSA with upstream dynamic backend loading. Null backend is enabled only for explicit internal tests, never an automatic physical-device fallback. WAV decoding, engine mixing/spatialization and threading remain enabled; MP3, FLAC, encoding and waveform generation are disabled. No external codec framework is linked. The default engine resource manager remains private. AudioRuntime owns the existing typed ResourcePool<AudioClipAsset>; voices retain immutable PCM leases and supply miniaudio buffer references. No loaded handle enters saved components or the gameplay ABI.

Each active Runtime WorldContext owns one AudioRuntime through EngineModule state. It owns stable-address context, engine, gameplay group, source voices and decoded clip data. Each voice maps a generation-bearing Flecs entity plus EntityRef to one miniaudio sound/buffer reference. Schema-only Authoring/Validation/default headless composition opens no device. `forge_audio` uses the private miniaudio engine and shared WAV decoder. The isolated asset worker links that decoder; authoring validates cooked PCM without opening a device. Core and authoring do not require an audio backend.

Shutdown stops the device and waits for callback completion, then destroys voices, group, engine/resource manager and context. Decoded vectors outlive their voices. Module/code leases follow the existing world-finalization contract. Device notifications only set an atomic bit field; the owner consumes it into ordinary structured diagnostics. The callback never reads Flecs, EngineServices, editor state or gameplay code.

## ECS and assets

AudioSource holds `AssetRef<AudioClipAsset>`, autoplay/loop/spatialized flags, linear gain, pitch multiplier, minimum/maximum distance, distance-falloff choice and strength. AudioListener holds enabled. These use the existing qualified component/property schemas, nullable typed AssetRef and bool metadata, scene formats and prefab override machinery. Known references serialize as UUID or null, not file paths or runtime handles. No scene version or identity changes.

AssetCatalog owns metadata in version1 `forge.assets.json`; ProjectPaths resolves contained project-relative locators. Registration is UI-independent and idempotent for a locator. UI calls require project writer ownership; a detected external index edit prevents publication. Metadata persistence is separate from scene Undo; no cross-document atomicity claim. The shared Phase7 importer/publication and source-watcher workflow below also supports AudioClip. Existing registered-only records remain readable without automatic migration.

Sources are decoded once into bounded immutable PCM. Limits:256 voices including pending sources,1024 queued commands,16MiB source,64MiB decoded clip,256MiB managed resident PCM,2 workers,256 pending requests and4096 selected asset slots. Worker decoding uses bounded temporary memory in addition to resident PCM. Mono/stereo WAV only; executable sample checks cover PCM16 at24/48kHz, including resampling. Native invalid configuration and resource failures disable the affected voice with entity/clip/source/module/tick diagnostics. Failed unchanged configurations, including native NaNs, do not retry every frame. Configuration corrections can recover a source; a failed immutable clip request requires restarting Play to retry file admission. Cache lives until runtime teardown. New imports select immutable cooked revisions; an active runtime keeps its cached/playing revision until teardown. Restart Play adopts a newer selection. Reimport does not hot-swap a live waveform.

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
spatial-admitted source gain before the common gameplay Master group. This is
linear volume mixing; no extra graph node or dynamic attachment is required.
Master-route sources bypass category gain, but retain Master output gain.
All channels follow gameplay pause. Channel/master updates validate every value
before applying any gain. Zero volume mutes while source time continues.
Changing route retains the voice, waveform cursor, clip and cache. Source volume
retains the existing spatial-validity gate, so mixer edits cannot unmute an
unresolved source. Runtime `channel_volumes` status is derived presentation data.

Project Settings owns shared defaults; Inspector owns each source route and its
independent prefab mask. Gallery33 uses the original tone at three pitches for
separate audible channels, with relocated source-free PCM proof. SDK component
layout/fingerprint changes require matching C++ rebuild; ABI1 remains unchanged.
This batch supplies four common channels. Custom buses, effects, streaming and
separate unpaused UI/music remain future capabilities.

## Live Play mix and status

Project Settings presents a runtime-only section above the disabled authored
defaults during Play. It reports output mode, pause state, playing/allocated
voices and decoded clip cache count/bytes from the existing runtime snapshot.
Four linear0–1 sliders edit a session-local draft; Apply live mix sends one
request and Reload live values discards that draft. Neither action edits project
settings, scene history or saved player preferences. Dragging a slider sends no
per-frame transport traffic. Pending requests disable further edits.

The private `audio_mix` request admits one finite master and exactly three finite
channel values before changing the existing AudioRuntime. SDK Play also requires
the current active world generation. Disabled audio and invalid/stale requests
retain the previous complete mix. PlaySession uses its existing single correlated
transport worker, keeps audio telemetry from receipts, and queues at most one mix
request. A correlated rejection reports an error and requests a fresh snapshot;
the healthy session remains usable and a later Apply can retry.

Both legacy and SDK Play update gains without restarting voices, reopening output
or advancing a paused simulation. Retained runtime configuration preserves the
mix through world reconstruction within the same process. Stop, a new process or
a new Play session reloads saved defaults. The editor clears its draft and queued
request on teardown. No installed SDK API/layout, authored schema, backend, thread
or exported editor is added. Gallery33 demonstrates category balancing; Gallery34
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
WAV; opening its document retains the existing import-editor contract. No private codec or runtime
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

Preview pause uses the existing gameplay sound group. Seeking commits on timeline
release or waveform click and preserves pause. Private AudioPreviewAccess admits
only the preview's single nonspatial pitch1/bus0 source. It detaches/uninitializes
that sound node before seeking its buffer, then recreates the node to discard old
resampler/processing cache. Exact miniaudio sound uninit waits graph readers; the
decoded PCM and output device remain retained. It never reads the live mutable
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

### Selected runtime source controls

Inspector's Live sound panel watches only the selected AudioSource in the active
Play world. Play/Restart, Pause/Resume and Stop use the existing AudioService command queue at
an owner boundary; they do not resample transforms or advance paused ticks.
Identity and shared-SDK generation checks reject stale targets before mutation.
The existing correlated Play transport carries one bounded audio command at a
time and reports nonfatal rejection for retry. A changed selection requests one
fresh observation, including in nonperiodic probe mode.

Source Pause stops its native node without seeking; Resume starts only an
explicitly paused source. An unpaused/stopped/completed source's Resume is a no-op.
Repeated Pause is idempotent. Restart and Stop clear source pause. Whole-game
pause remains independent: source Resume cannot make a paused game audible, and
game Resume does not resume a source explicitly paused by gameplay or Inspector.
The paused-source flag is ephemeral voice state; recovery follows authored
autoplay and does not restore that flag or a sample cursor.

The private protocol accepts one `operation` string (`play`, `stop`, `pause`,
`resume`) or the existing strict `play` boolean. Conflicting fields, unknown or
mistyped operations reject before mutation. Existing boolean consumers retain
Restart/Stop behavior.

Status exposes availability, native atomic playing flag, source pause, game pause, loop and clip
duration. It does not read the mutable data-source cursor or claim physical output
latency. Already submitted audio buffers may finish after Stop. Saved scenes and
mix defaults remain unchanged; Stop Play/new Play restores authored autoplay.
The private access header is not installed SDK API. The appended public component
member changes the matching SDK fingerprint/layout and requires a C++ rebuild;
ABI1 tables, authored formats and private recovery envelopes remain unchanged.

The device callback mixes continuously and never advances gameplay time. Pause stops the gameplay group while keeping the device alive. Step updates intent/transforms once with the group still stopped; it does not request an audio timeslice. Resume starts the group. Play commands restart the source at frame0; Stop stops and resets it. Basic playback counts are diagnostic presentation state, not authoritative simulation clocks.

Recovery reconstructs autoplay from recovered authored source components, from frame0; other playback is stopped. No sample cursor or audio blob enters the private physics checkpoint or scene. Candidates start with their group paused; probes use no-device composition. The physics/private recovery envelope remains unchanged. The movement-only ABI1 contract mentioned in the original phase record has since been retired.

## Composition and SDK

`forge_runtime --project PATH --audio device` explicitly enables optional real output. Plain `--project` supplies the asset root only; protocol hello keeps the existing clock/input/gravity configuration authority, including recovery from the active play session. `--audio offline` selects no-device logic tests; omitting `--audio` remains headless. The experimental profile uses `--sdk-project PATH` with the same audio option. A consumer requiring Audio must declare dependency `forge.audio`; unavailable required services prevent consumer startup. Optional output failure emits a diagnostic and continues without Audio capability.

Exact SDK supplies a size/fingerprint-checked `audio_source` callback taking scene UUID, entity UUID and operation: `FORGE_SDK_AUDIO_STOP`0, `FORGE_SDK_AUDIO_RESTART`1, `FORGE_SDK_AUDIO_PAUSE`2, `FORGE_SDK_AUDIO_RESUME`3. The first two retain their previous semantics; unknown values reject. It is callable on the fixed owner thread; generation is captured at enqueue and revalidated before execution. Source-engine AudioService exposes matching `play`, `stop_source`, `pause_source`, `resume_source` owner-thread commands. The changed installed header fingerprint requires rebuilding exact SDK consumers; callback table size and ABI number remain unchanged. Gameplay can register/query/edit FORGE AudioSource values through shared Flecs. No miniaudio types or headers enter the SDK. AssetId is preserved by copying typed component references; out-of-line host identity/catalog helper implementations are not exported as a general SDK library.

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

Later work includes streaming music, one-shots/voice stealing, custom buses and unpaused UI/music separation, cue graphs/effects, multiple listeners, sample-accurate recovery continuity and device selection.


## Phase7 AudioClip import and selected revisions

Verified2026-09-21 against the unchanged exact miniaudio pin. `forge.audio.wav`
uses the common importer registry, supervised `forge_asset_build` process,
revision key/DDC, `AssetPublisher`, source/settings sidecar, source watcher,
Content import document and public `forge_tools --assets import` operation.
No second audio engine, device, simulation clock or catalog is introduced.

The worker uses `ma_decoder_config_init(ma_format_f32, 0, 0)`, explicitly selects
`ma_encoding_format_wav`, then calls the pinned memory-init, frame-length,
read-frames and uninit APIs. It checks whole-frame reads, mono/stereo format,
finite samples,16MiB source and64MiB decoded limits. The pinned WAV decoder's
384000Hz upper bound is retained; format recognition is not a claim that every
WAV encoding has an acceptance fixture. Fixtures cover PCM16 at24/48/96kHz.
The worker limit is512MiB/30seconds with bounded input/output and cancellation.

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
