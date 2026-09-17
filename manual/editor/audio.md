# Audio

An **Audio Source** plays a registered WAV clip during Play. An **Audio Listener** is the position and orientation from which spatial sounds are heard. Both are ordinary scene components and work with prefab inheritance and Undo/Redo.

## Hear your first sound

1. Copy the included `Examples/Audio/test-tone.wav` into your project's `Assets` folder. You can also use your own small mono or stereo WAV.
2. Create a cube and select it. Expand **Audio** in the Inspector and choose **Add AudioSource**.
3. Enter `Assets/test-tone.wav` in **WAV in project**, then click **Register WAV**. The file must already be inside the project.
4. Select it in **Audio clip**. Enable **Play on Start** and **Loop**.
5. Create another entity at the origin, select it, expand **Audio**, and choose **Add AudioListener**. Leave **Enabled** checked.
6. Press **Play**. You should hear the tone. **Pause** freezes it; **Step** advances the game once and remains silent; **Resume** continues playback. **Stop** ends all runtime sound.
7. Save the scene, reopen it, and repeat.

Start with a comfortable speaker/headphone volume. The example is a quiet, four-second synthetic tone.

## Source controls

- **Audio clip:** a registered asset, stored by its persistent identity. A path is only its current location.
- **Play on Start:** automatically starts this source when the runtime realizes it. Changing this option on an already realized voice does not restart it.
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

Use the existing prefab property **Revert** controls or **Remove / Revert component** to return to inheritance. Scene edits and Revert participate in scene Undo/Redo. Direct prefab source publication has the existing separate history boundary; it is not undone by scene Undo.

## Registered clips

Registration writes `forge.assets.json` in the project root using the existing asset catalog. Keep that file in version control with the WAV files. Registration does not import, transcode, copy, or modify the WAV and is separate from scene Undo. Registering the same source again returns its existing identity.

Supported catalog relocation preserves identity; automatically detecting files moved outside FORGE is not implemented. Keep registered files at their listed paths unless updating their catalog locator through the supported asset API. Changing a WAV on disk requires restarting Play. A failed decode is not retried every tick.

## Recovery and limitations

After crash/reload recovery, FORGE rebuilds audio from the recovered component configuration. **Play on Start** voices restart from the beginning; other voices remain stopped. A paused recovery remains silent until Resume. Sample positions, manual playback commands and music continuity are not restored or saved in scenes.

The tested source format is 16-bit PCM WAV, mono or stereo, at 24 or 48 kHz. Other WAV encodings are not yet an acceptance promise.

The initial limits are 256 source voices, mono/stereo WAV files up to 16 MiB, up to 64 MiB decoded per clip, and 256 MiB total decoded data per runtime. Repeated sources share decoded data. Assets are decoded on the runtime owner thread when first used, so initial loading can briefly delay that runtime; streaming and asynchronous importing are later work.

Missing clips, invalid files and unavailable devices appear in the existing Play log in **Console**. Optional device failure allows the game to continue silently. Restart Play after fixing files/device selection if the operating system does not recover playback. There is no device picker, editor audition window, mixer bus editor, one-shot pool, MP3/FLAC support, streaming music, audio cooking or audio-file hot reload yet.

Headless runs can omit audio entirely. Automated offline/null-device tests verify logic and sample output; actual speakers/headphones require this manual test.
