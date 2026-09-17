# Audio module

`forge.audio` supplies FORGE-owned AudioSource/AudioListener schemas and an optional runtime provider. miniaudio **0.11.25**, commit `9634bedb5b5a2ca38c1ee7108a9358a4e233f14d`, is private static C implementation with the MIT-0 license option. Official source: https://github.com/mackron/miniaudio/tree/9634bedb5b5a2ca38c1ee7108a9358a4e233f14d . Existing dependency pins are unchanged.

## Build and ownership

Windows output explicitly selects WASAPI; Linux selects PulseAudio/ALSA with upstream dynamic backend loading. Null backend is enabled only for explicit internal tests, never an automatic physical-device fallback. WAV decoding, engine mixing/spatialization and threading remain enabled; MP3, FLAC, encoding and waveform generation are disabled. No external codec framework is linked. The default engine resource manager remains private; FORGE's bounded AssetId cache shares immutable decoded PCM through miniaudio buffer references. A generic AssetHandle remains deferred.

Each active Runtime WorldContext owns one AudioRuntime through EngineModule state. It owns stable-address context, engine, gameplay group, source voices and decoded clip data. Each voice maps a generation-bearing Flecs entity plus EntityRef to one miniaudio sound/buffer reference. Schema-only Authoring/Validation/default headless composition opens no device. `forge_audio` links core/assets privately to miniaudio; core and authoring do not require a device/backend.

Shutdown stops the device and waits for callback completion, then destroys voices, group, engine/resource manager and context. Decoded vectors outlive their voices. Module/code leases follow the existing world-finalization contract. Device notifications only set an atomic bit field; the owner consumes it into ordinary structured diagnostics. The callback never reads Flecs, EngineServices, editor state or gameplay code.

## ECS and assets

AudioSource holds `AssetRef<AudioClipAsset>`, autoplay/loop/spatialized flags, linear gain, pitch multiplier and minimum/maximum distance. AudioListener holds enabled. These use the existing qualified component/property schemas, nullable typed AssetRef and bool metadata, scene formats and prefab override machinery. Known references serialize as UUID or null, not file paths or runtime handles. No scene version or identity changes.

AssetCatalog owns metadata in version1 `forge.assets.json`; ProjectPaths resolves contained project-relative locators. Registration is UI-independent and idempotent for a locator. UI calls require project writer ownership; a detected external index edit prevents publication. Metadata persistence is separate from scene Undo; no cross-document atomicity claim. No importer, cooking, source watcher, VFS or generic resource framework.

Sources are decoded once into bounded immutable PCM. Limits:256 voices,1024 queued commands,16MiB source,64MiB decoded clip,256MiB cache. Mono/stereo WAV only; executable sample checks cover PCM16 at24/48kHz, including resampling. Native invalid configuration and resource failures disable the affected voice with entity/clip/source/module/tick diagnostics. Failed unchanged configurations do not retry every frame; changing configuration or restarting Play allows retry. Cache lives until runtime teardown. Asset source edits/reimport are deferred.

## Clock and presentation

RuntimeSimulation invokes audio synchronization only after a successful completed fixed tick and final transform evaluation; scene candidate realization can synchronize at the initial/recovered boundary. It publishes current effective world positions and listener direction/up. Affine directions are normalized/orthogonalized without changing authored transforms or imposing physics shear restrictions. Source scale does not scale range/gain. Spatial positions must be finite and within ±1e9 meters before conversion to backend float; this is a safety bound, not a large-world precision claim. Initial attenuation uses miniaudio inverse distance with rolloff1 and clamped min/max distances; no Doppler/cone semantics.

The device callback mixes continuously and never advances gameplay time. Pause stops the gameplay group while keeping the device alive. Step updates intent/transforms once with the group still stopped; it does not request an audio timeslice. Resume starts the group. Play commands restart the source at frame0; Stop stops and resets it. Basic playback counts are diagnostic presentation state, not authoritative simulation clocks.

Recovery reconstructs autoplay from recovered authored source components, from frame0; other playback is stopped. No sample cursor or audio blob enters the private physics checkpoint or scene. Candidates start with their group paused; probes use no-device composition. Existing physics/private envelope and ABI1 activation/fallback contracts remain unchanged.

## Composition and SDK

`forge_runtime --project PATH --audio device` explicitly enables optional real output. Plain `--project` supplies the asset root only; protocol hello keeps the existing clock/input/gravity configuration authority, including recovery from the active play session. `--audio offline` selects no-device logic tests; omitting `--audio` remains headless. The experimental profile uses `--sdk-project PATH` with the same audio option. A consumer requiring Audio must declare dependency `forge.audio`; unavailable required services prevent consumer startup. Optional output failure emits a diagnostic and continues without Audio capability.

Exact SDK appends a size/fingerprint-checked `audio_source` callback taking scene UUID, entity UUID and Play/Stop operation. It is callable on the fixed owner thread; generation is captured at enqueue and revalidated before execution. Gameplay can register/query/edit FORGE AudioSource values through shared Flecs. No miniaudio types or headers enter the SDK; ABI1 is unchanged. AssetId is preserved by copying typed component references; out-of-line host identity/catalog helper implementations are not exported as a general SDK library.

## Validation and deferred work

Audio tests use offline PCM and explicit null-device callback teardown. Process tests cover schema-only mode, pause/step/resume, private recovery reconstruction and rejected candidate preservation; shared SDK fixture checks typed components, Play/Stop and missing capability. Existing physics/runtime/render tests remain required. These tests cannot establish subjective spatial quality or physical-device playback.

Later work includes asynchronous asset loading/cooking, streaming music, one-shots/voice stealing, buses/UI music separation, cue graphs/effects, multiple listeners, sample-accurate continuity, previews and device selection. Phase6D and other subsystems are outside this change.
