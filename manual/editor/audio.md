# Audio

An **Audio Source** plays a registered WAV clip during Play. An **Audio Listener** is the position and orientation from which spatial sounds are heard. Both are ordinary scene components and work with prefab inheritance and Undo/Redo.

## Hear your first sound

1. Copy the included `Examples/Audio/test-tone.wav` into your project's `Assets` folder. You can also use your own small mono or stereo WAV.
2. Open **Content → Create / Register → Import audio...**, enter the project-relative `Assets/test-tone.wav` source, choose **Review settings**, then **Import / Reimport** in the central **Audio clip** document. Alternatively, **Content → Import files...** copies external WAVs into a new project folder and imports them after your review.
3. Create a cube and select it. Use **+ Add Component → Audio / Audio Source** in Inspector.
4. Select it in **Clip**. Enable **Play On Start** and **Loop**.
5. Create another entity at the origin, select it, use **+ Add Component → Audio / Audio Listener**. Leave **Enabled** checked.
6. Press **Play**. You should hear the tone. **Pause** freezes it; **Step** advances the game once and remains silent; **Resume** continues playback. **Stop** ends all runtime sound.
7. Save the scene, reopen it, and repeat.

Start with a comfortable speaker/headphone volume. The example is a quiet, four-second synthetic tone.

## Source controls

- **Clip:** a registered asset, stored by its persistent identity. A path is only its current location.
- **Play On Start:** automatically starts this source when the runtime realizes it. Changing this option on an already realized voice does not restart it.
- **Loop:** repeat at the end of the clip. Disable it to play once.
- **Gain:** linear volume. `0` is silent, `1` is nominal; the supported range is `0–4`. Values above `1` amplify and may clip when mixed.
- **Pitch:** playback speed and pitch together; `1` is normal, `0.25–4` is supported. This is not independent time stretching.
- **Spatialized:** enable positioning and distance attenuation. Disable it for ordinary stereo playback that needs no listener or transform.
- **Minimum distance / Maximum distance:** meters. The inverse-distance attenuation calculation clamps distance to these limits; maximum distance is not a hard mute radius. Maximum must be at least minimum.

Numeric fields commit with **Enter**. Errors leave the previous authored value intact. Object scale does not multiply gain or range. Parent transforms still move a child's position normally.

## Position and listener

Spatial sources use the scene's effective world positions. **Follow Structure**, **World**, and **Explicit** child spaces behave as described in [Scenes](scenes.md). Position updates follow completed game ticks. Audio does not move objects or run an independent transform simulation.

Use exactly one enabled listener. Its forward direction is local negative Z, with positive Y up. Multiple enabled listeners produce a diagnostic and mute spatial sources until corrected; FORGE does not choose an arbitrary listener. Missing or unresolved source/listener transforms also mute spatial playback. Positions outside ±1 billion meters are unsupported; float backend positioning does not promise large-world precision. Nonspatial sources remain available.

To check positioning, Stop, move the source to either side of the listener, and Play again. Move it farther away to hear attenuation. The editor's orbit camera is not an automatic audio listener.

## Prefabs and Undo

Create a prefab from an entity with AudioSource, then instantiate it twice. Edit the prefab source to change defaults. Instances follow successfully published changes unless they override the property. Gain, clip, loop, and other source properties can have independent override intent, including an override equal to the current default.

Use the existing prefab property **Revert** controls or the component header’s **Remove component / Revert component** to return to inheritance. Scene edits and Revert participate in scene Undo/Redo. Direct prefab source publication has the existing separate history boundary; it is not undone by scene Undo.

## Registered clips

Import runs in a separate worker. It validates the WAV, prepares immutable audio
data, and records **Duration**, **Channels**, **Sample rate** and format. Select
the AudioClip in Content to see these details in Inspector, or double-click it to
open **Audio clip**. Its **Clip details** update even when Content is closed or folded. The WAV importer has no configurable conversion settings. Drop it onto an Audio Source's **Clip** field to assign it;
an incompatible asset type is rejected.

Keep `forge.assets.json`, the WAV and its `.forge-import.json` sidecar in source
control. Import does not modify the WAV and is separate from scene Undo. Reimport
preserves its AssetId and scene references. Existing clips created by the older
Register WAV command still work; opening and importing one adds validated data
without replacing its identity.

The source watcher can reimport changed WAVs after a clip has been imported. Use
Content's **Reimport** action to retry a failed source. A bad or stale candidate
keeps the previously selected audio and metadata. **Restart Play** to hear a newly
published revision; an active voice keeps its current decoded clip. There is no
automatic replacement of a playing waveform. Runtime loading of imported clips
uses the selected cooked data and does not require the source WAV.

Supported Content file operations preserve or deliberately duplicate identity;
automatic relocation of files moved outside FORGE is not implemented. Move the
source through Content rather than renaming it externally. Missing/corrupt cooked
data is diagnosed; reimport from the source to rebuild it.

## Recovery and limitations

After crash/reload recovery, FORGE rebuilds audio from the recovered component configuration. **Play On Start** voices restart from the beginning; other voices remain stopped. A paused recovery remains silent until Resume. Sample positions, manual playback commands and music continuity are not restored or saved in scenes.

The tested source format is 16-bit PCM WAV, mono or stereo, at24,48 or96kHz. Other
WAV encodings supported by the selected miniaudio decoder still require admission;
they are not all covered by the format acceptance fixtures.

The initial limits are256 source voices, mono/stereo WAV files up to16MiB, up to
64MiB decoded per clip, and256MiB total decoded data per runtime. Repeated sources
share decoded data. Import preparation runs off the editor thread in a supervised
worker. Runtime first-use loading/admission remains on its owner thread and can
briefly delay that runtime. Legacy registered-only WAVs also decode there.

Missing clips, invalid files and unavailable devices appear in the existing Play
log in **Console**. Optional device failure allows the game to continue silently.
Restart Play after fixing files/device selection if the operating system does not
recover playback. There is no editor audition player, device picker, mixer bus
editor, one-shot pool, MP3/FLAC support, streaming music or live voice replacement.

Headless runs can omit audio entirely. Automated offline/null-device tests verify logic and sample output; actual speakers/headphones require this manual test.
