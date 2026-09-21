# Audio module

`forge.audio` supplies FORGE-owned AudioSource/AudioListener schemas and an optional runtime provider. miniaudio **0.11.25**, commit `9634bedb5b5a2ca38c1ee7108a9358a4e233f14d`, is private static C implementation with the MIT-0 license option. Official source: https://github.com/mackron/miniaudio/tree/9634bedb5b5a2ca38c1ee7108a9358a4e233f14d . Existing dependency pins are unchanged.

## Build and ownership

Windows output explicitly selects WASAPI; Linux selects PulseAudio/ALSA with upstream dynamic backend loading. Null backend is enabled only for explicit internal tests, never an automatic physical-device fallback. WAV decoding, engine mixing/spatialization and threading remain enabled; MP3, FLAC, encoding and waveform generation are disabled. No external codec framework is linked. The default engine resource manager remains private; FORGE's bounded AssetId cache shares immutable decoded PCM through miniaudio buffer references. A generic AssetHandle remains deferred.

Each active Runtime WorldContext owns one AudioRuntime through EngineModule state. It owns stable-address context, engine, gameplay group, source voices and decoded clip data. Each voice maps a generation-bearing Flecs entity plus EntityRef to one miniaudio sound/buffer reference. Schema-only Authoring/Validation/default headless composition opens no device. `forge_audio` uses the private miniaudio engine and shared WAV decoder. The isolated asset worker links that decoder; authoring validates cooked PCM without opening a device. Core and authoring do not require an audio backend.

Shutdown stops the device and waits for callback completion, then destroys voices, group, engine/resource manager and context. Decoded vectors outlive their voices. Module/code leases follow the existing world-finalization contract. Device notifications only set an atomic bit field; the owner consumes it into ordinary structured diagnostics. The callback never reads Flecs, EngineServices, editor state or gameplay code.

## ECS and assets

AudioSource holds `AssetRef<AudioClipAsset>`, autoplay/loop/spatialized flags, linear gain, pitch multiplier and minimum/maximum distance. AudioListener holds enabled. These use the existing qualified component/property schemas, nullable typed AssetRef and bool metadata, scene formats and prefab override machinery. Known references serialize as UUID or null, not file paths or runtime handles. No scene version or identity changes.

AssetCatalog owns metadata in version1 `forge.assets.json`; ProjectPaths resolves contained project-relative locators. Registration is UI-independent and idempotent for a locator. UI calls require project writer ownership; a detected external index edit prevents publication. Metadata persistence is separate from scene Undo; no cross-document atomicity claim. The shared Phase7 importer/publication and source-watcher workflow below also supports AudioClip. Existing registered-only records remain readable without automatic migration.

Sources are decoded once into bounded immutable PCM. Limits:256 voices,1024 queued commands,16MiB source,64MiB decoded clip,256MiB cache. Mono/stereo WAV only; executable sample checks cover PCM16 at24/48kHz, including resampling. Native invalid configuration and resource failures disable the affected voice with entity/clip/source/module/tick diagnostics. Failed unchanged configurations do not retry every frame; changing configuration or restarting Play allows retry. Cache lives until runtime teardown. New imports select immutable cooked revisions; an active runtime keeps its cached/playing revision until teardown. Restart Play adopts a newer selection. Reimport does not hot-swap a live waveform.

## Clock and presentation

RuntimeSimulation invokes audio synchronization only after a successful completed fixed tick and final transform evaluation; scene candidate realization can synchronize at the initial/recovered boundary. It publishes current effective world positions and listener direction/up. Affine directions are normalized/orthogonalized without changing authored transforms or imposing physics shear restrictions. Source scale does not scale range/gain. Spatial positions must be finite and within ±1e9 meters before conversion to backend float; this is a safety bound, not a large-world precision claim. Initial attenuation uses miniaudio inverse distance with rolloff1 and clamped min/max distances; no Doppler/cone semantics.

The device callback mixes continuously and never advances gameplay time. Pause stops the gameplay group while keeping the device alive. Step updates intent/transforms once with the group still stopped; it does not request an audio timeslice. Resume starts the group. Play commands restart the source at frame0; Stop stops and resets it. Basic playback counts are diagnostic presentation state, not authoritative simulation clocks.

Recovery reconstructs autoplay from recovered authored source components, from frame0; other playback is stopped. No sample cursor or audio blob enters the private physics checkpoint or scene. Candidates start with their group paused; probes use no-device composition. Existing physics/private envelope and ABI1 activation/fallback contracts remain unchanged.

## Composition and SDK

`forge_runtime --project PATH --audio device` explicitly enables optional real output. Plain `--project` supplies the asset root only; protocol hello keeps the existing clock/input/gravity configuration authority, including recovery from the active play session. `--audio offline` selects no-device logic tests; omitting `--audio` remains headless. The experimental profile uses `--sdk-project PATH` with the same audio option. A consumer requiring Audio must declare dependency `forge.audio`; unavailable required services prevent consumer startup. Optional output failure emits a diagnostic and continues without Audio capability.

Exact SDK appends a size/fingerprint-checked `audio_source` callback taking scene UUID, entity UUID and Play/Stop operation. It is callable on the fixed owner thread; generation is captured at enqueue and revalidated before execution. Gameplay can register/query/edit FORGE AudioSource values through shared Flecs. No miniaudio types or headers enter the SDK; ABI1 is unchanged. AssetId is preserved by copying typed component references; out-of-line host identity/catalog helper implementations are not exported as a general SDK library.

## Validation and deferred work

Audio tests use offline PCM and explicit null-device callback teardown. Process tests cover schema-only mode, pause/step/resume, private recovery reconstruction and rejected candidate preservation; shared SDK fixture checks typed components, Play/Stop and missing capability. Existing physics/runtime/render tests remain required. These tests cannot establish subjective spatial quality or physical-device playback.

Later work includes asynchronous runtime first-use loading, streaming music, one-shots/voice stealing, buses/UI music separation, cue graphs/effects, multiple listeners, sample-accurate continuity, previews and device selection.


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
