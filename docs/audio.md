# Audio module

`forge.audio` supplies FORGE-owned AudioSource/AudioListener schemas and an optional runtime provider. miniaudio **0.11.25**, commit `9634bedb5b5a2ca38c1ee7108a9358a4e233f14d`, is private static C implementation with the MIT-0 license option. Official source: https://github.com/mackron/miniaudio/tree/9634bedb5b5a2ca38c1ee7108a9358a4e233f14d . Existing dependency pins are unchanged.

## Build and ownership

Windows output explicitly selects WASAPI; Linux selects PulseAudio/ALSA with upstream dynamic backend loading. Null backend is enabled only for explicit internal tests, never an automatic physical-device fallback. WAV decoding, engine mixing/spatialization and threading remain enabled; MP3, FLAC, encoding and waveform generation are disabled. No external codec framework is linked. The default engine resource manager remains private; FORGE's bounded AssetId cache shares immutable decoded PCM through miniaudio buffer references. A generic AssetHandle remains deferred.

Each active Runtime WorldContext owns one AudioRuntime through EngineModule state. It owns stable-address context, engine, gameplay group, source voices and decoded clip data. Each voice maps a generation-bearing Flecs entity plus EntityRef to one miniaudio sound/buffer reference. Schema-only Authoring/Validation/default headless composition opens no device. `forge_audio` uses the private miniaudio engine and shared WAV decoder. The isolated asset worker links that decoder; authoring validates cooked PCM without opening a device. Core and authoring do not require an audio backend.

Shutdown stops the device and waits for callback completion, then destroys voices, group, engine/resource manager and context. Decoded vectors outlive their voices. Module/code leases follow the existing world-finalization contract. Device notifications only set an atomic bit field; the owner consumes it into ordinary structured diagnostics. The callback never reads Flecs, EngineServices, editor state or gameplay code.

## ECS and assets

AudioSource holds `AssetRef<AudioClipAsset>`, autoplay/loop/spatialized flags, linear gain, pitch multiplier, minimum/maximum distance, distance-falloff choice and strength. AudioListener holds enabled. These use the existing qualified component/property schemas, nullable typed AssetRef and bool metadata, scene formats and prefab override machinery. Known references serialize as UUID or null, not file paths or runtime handles. No scene version or identity changes.

AssetCatalog owns metadata in version1 `forge.assets.json`; ProjectPaths resolves contained project-relative locators. Registration is UI-independent and idempotent for a locator. UI calls require project writer ownership; a detected external index edit prevents publication. Metadata persistence is separate from scene Undo; no cross-document atomicity claim. The shared Phase7 importer/publication and source-watcher workflow below also supports AudioClip. Existing registered-only records remain readable without automatic migration.

Sources are decoded once into bounded immutable PCM. Limits:256 voices,1024 queued commands,16MiB source,64MiB decoded clip,256MiB cache. Mono/stereo WAV only; executable sample checks cover PCM16 at24/48kHz, including resampling. Native invalid configuration and resource failures disable the affected voice with entity/clip/source/module/tick diagnostics. Failed unchanged configurations, including native NaNs, do not retry every frame; changing configuration or restarting Play allows retry. Cache lives until runtime teardown. New imports select immutable cooked revisions; an active runtime keeps its cached/playing revision until teardown. Restart Play adopts a newer selection. Reimport does not hot-swap a live waveform.

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

## Editor clip audition

The AudioClip document offers explicit Play/Restart preview, Stop, Loop and
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
Seeking, waveform display and asynchronous first-use loading remain deferred.
Actual offline PCM and scaled widget tests do not prove physical sound quality.

## Clock and presentation

RuntimeSimulation invokes audio synchronization only after a successful completed fixed tick and final transform evaluation; scene candidate realization can synchronize at the initial/recovered boundary. It publishes current effective world positions and listener direction/up. Affine directions are normalized/orthogonalized without changing authored transforms or imposing physics shear restrictions. Source scale does not scale range/gain. Spatial positions must be finite and within ±1e9 meters before conversion to backend float; this is a safety bound, not a large-world precision claim. Distance falloff defaults to inverse with strength1 and clamped min/max distances; the selectable curves are described below. No Doppler, cone filtering or occlusion semantics.

The device callback mixes continuously and never advances gameplay time. Pause stops the gameplay group while keeping the device alive. Step updates intent/transforms once with the group still stopped; it does not request an audio timeslice. Resume starts the group. Play commands restart the source at frame0; Stop stops and resets it. Basic playback counts are diagnostic presentation state, not authoritative simulation clocks.

Recovery reconstructs autoplay from recovered authored source components, from frame0; other playback is stopped. No sample cursor or audio blob enters the private physics checkpoint or scene. Candidates start with their group paused; probes use no-device composition. The physics/private recovery envelope remains unchanged. The movement-only ABI1 contract mentioned in the original phase record has since been retired.

## Composition and SDK

`forge_runtime --project PATH --audio device` explicitly enables optional real output. Plain `--project` supplies the asset root only; protocol hello keeps the existing clock/input/gravity configuration authority, including recovery from the active play session. `--audio offline` selects no-device logic tests; omitting `--audio` remains headless. The experimental profile uses `--sdk-project PATH` with the same audio option. A consumer requiring Audio must declare dependency `forge.audio`; unavailable required services prevent consumer startup. Optional output failure emits a diagnostic and continues without Audio capability.

Exact SDK appends a size/fingerprint-checked `audio_source` callback taking scene UUID, entity UUID and Play/Stop operation. It is callable on the fixed owner thread; generation is captured at enqueue and revalidated before execution. Gameplay can register/query/edit FORGE AudioSource values through shared Flecs. No miniaudio types or headers enter the SDK. AssetId is preserved by copying typed component references; out-of-line host identity/catalog helper implementations are not exported as a general SDK library.

## Validation and deferred work

Audio tests use offline PCM and explicit null-device callback teardown. Process tests cover schema-only mode, pause/step/resume, private recovery reconstruction and rejected candidate preservation; shared SDK fixture checks typed components, Play/Stop and missing capability. Existing physics/runtime/render tests remain required. These tests cannot establish subjective spatial quality or physical-device playback.

Later work includes asynchronous runtime first-use loading, streaming music, one-shots/voice stealing, custom buses and unpaused UI/music separation, cue graphs/effects, multiple listeners, sample-accurate recovery continuity, waveform/seek previews and device selection.


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
use the same bounded native WAV decoder. The existing runtime cache and voice
ownership remain unchanged; first-use loading still occurs on the runtime owner
thread. There is no claim of generic asynchronous AudioClip resource leases.

Invalid, cancelled or stale imports keep the previous catalog, sidecar and
selected clip. Reimport retains AssetId. Content moves retain identity and the
selection; duplicates allocate a new identity, clear admission/selection metadata
and require their own import. Source control includes the WAV, sidecar and
catalog; DDC remains disposable. Selected cooked audio must be included in runtime
packaging. Scene Undo does not reverse asset imports.

The central Audio clip document owns import/reimport/close guards. Content
Inspector shows duration/channels/sample rate/format. Typed AudioSource picking
and drop reuse the existing shared field. There is no audition player: adding
one requires an editor-owned audio output lifecycle, which the runtime provider
does not supply. This optional preview remains deferred; use Play for playback.

Validation covers direct and actual worker decode, deterministic output, cache
hit, stable legacy ID, corrupt/truncated/nonfinite data, successful/stale/failed
reimport, corrupt selected output, source-absent runtime playback, Content
move/duplicate and public CLI publication. Sanitizers cover the decoder and
in-process admission path. Windows and physical-speaker acceptance are recorded
separately from these Linux/offline checks.
