# Audio

## Listen to a clip in the editor

In **Content**, double-click an AudioClip to open its **Audio clip** tab. Under
**Clip preview**, click **Play preview**. **Restart preview** starts it again;
**Stop preview** releases it. **Loop preview** repeats the sound, and
**Preview volume** changes how loudly you hear it.

Click **Show waveform** to see the sound without playing it. Mono clips show one
band; stereo clips show separate Left and Right bands. Playing also loads the
waveform. The time display shows the current position and total duration.

During preview, use **Pause preview** and **Resume preview** to hold or continue
the same position. Click the waveform to jump to a point, or adjust
**Position (seconds)**. Dragging the position control seeks when you release it.
Seeking while paused keeps the preview paused. **Stop preview** releases sound
output and keeps the waveform visible; closing the tab releases the preview.

The waveform shows a compact overview, including short peaks. It does not edit
or trim the audio. Loading happens once for the selected clip; larger clips may
take a moment on their first load. Import settings are below the preview.

You do not need a camera, Audio Source entity or game Play for this preview.
Its loop and volume changes are temporary: they do not change your scene or the
game's mix volumes. Closing the tab, choosing another clip/project, reimporting,
or starting game Play stops it. Save import changes before previewing.

To try it, open the packaged Feature Gallery and double-click
**Assets/FalloffTone.wav** in Content. The original tone used by its audio scenes
is ready to preview. If sound output cannot open, the tab explains the error.

An **Audio Source** plays a registered WAV clip during Play. An **Audio Listener** is the position and orientation from which spatial sounds are heard. Both are ordinary scene components and work with prefab inheritance and Undo/Redo.

## Process a clip during import

Open an AudioClip from **Content**, then expand **Import settings** below its
preview. All processing is off by default.

- **Force mono** combines the Left and Right channels. Opposite-phase stereo sounds can cancel when combined.
- **Normalize peak** raises or lowers the clip so its largest sample reaches full scale. It does not make different sounds equally loud.
- **Trim silence** removes quiet time at the beginning and end. **Trim threshold (dB)** chooses what counts as quiet; lower values preserve quieter sounds. Removed edges receive a short fade to reduce clicks. Entirely quiet clips keep one silent frame.

Click **Import / Reimport** to apply the settings. FORGE combines channels first,
then normalizes, then trims. The preview shows the resulting channels and duration.
Editing settings stops the old preview. **Reload saved settings** discards the
draft; **Reset to defaults** creates a draft with processing turned off.

Your original WAV remains unchanged. Games use the processed clip already built
during import, including in exported games. Stop and restart Play after reimport
to hear the new revision in a running scene. Scene Undo does not undo an import.

Try **34 — Audio import processing** in the Feature Gallery. Its two WAV sources
contain identical original tones: one keeps stereo and quiet edges; the other
uses mono, normalization and trimming. Preview either clip to compare.

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
- **Minimum distance / Maximum distance:** meters. Distance is clamped to these limits. Maximum is not a universal mute radius. Maximum must be at least minimum; equal limits disable distance loss.

- **Distance falloff:** choose **Inverse**, **Linear**, or **Exponential**. Inverse is the default. Linear at strength1 fades to silence at maximum; the other curves can remain audible there.
- **Falloff strength:** `0–10`, default `1`. Higher values fade more strongly with distance. `0` keeps left/right positioning without reducing volume over distance. These controls affect spatialized sources.

- **Directional sound:** aim spatial audio along the object's negative Z axis. Rotate the source or its parent to change its direction. Disable this for ordinary sound in every direction.
- **Inner opening (deg):** full angle receiving full angular volume, `0–360`. `360` is omnidirectional.
- **Outer opening (deg):** full outer angle, at least the inner opening. Volume fades between the openings; equal openings make a hard boundary.
- **Outside volume:** `0–1`, default `.25`; outside the outer opening, this multiplies distance falloff and gain. `0` is silent; `1` preserves volume.
- **Mix channel:** choose **Master**, **Music**, **Effects** or **Dialogue**. Music, Effects and Dialogue use their own volume before Master. Changing channel keeps the sound playing.

Numeric fields commit with **Enter**. Errors leave the previous authored value intact. Object scale does not multiply gain or range. Parent transforms still move a child's position normally.

## Balance music, effects and dialogue

Open **Tools → Project Settings → Game defaults**. The four volume sliders set
shared starting values. Master affects all sounds; the other sliders affect
sources assigned to that channel. **Save Settings** applies defaults to the next
Play/export. A game's saved player preferences can override these defaults.
Zero volume mutes without stopping a sound's playback position. All four channels
pause with gameplay.

Scene **33 — Audio mixer** plays low, middle and high tones through Music,
Effects and Dialogue. Stop Play, change one channel's default volume, save settings
and Play again to compare. Select each named emitter to inspect or change its
**Mix channel**. Export preserves its routes and cooked tone.

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

## Compare the included falloff scene

Open **Examples/FeatureGallery/Scenes/31-audio-falloff.scene.json**. The blue,
yellow and red cubes use inverse, linear and exponential falloff. The camera is
the listener. Only blue starts automatically, so the sounds remain easy to compare.

Stop, disable **Play On Start** on blue, enable it on one other cube, and Play.
Move the camera nearer/farther while stopped, or change the selected source's live
strength during Play. Save authored settings while stopped before exporting.
An exported game includes the compiled engine and cooked sound; it needs the
original WAV only when you return to authoring/reimport in the editor.

## Aim a directional sound

Open **32 — Directional audio** in the Feature Gallery and select **Directional
speaker**. The Scene helper shows its forward arrow and inner/outer angular
outlines. **Helpers** must be enabled. The outlines are visual aids, not a fixed
mute distance. The source points along negative Z; the normal transform gizmo's
blue axis shows positive Z.

Play to hear the tone. Stop, select **Aim parent**, rotate Y by180degrees and
Play again: the camera/listener is now behind the emitter, so it is quieter.
Try **Outside volume**0 or1, or change the openings in live gameplay controls.
Save authored edits while stopped before export. Older sounds stay omnidirectional
until you enable **Directional sound**. A source with zero scale along its forward
axis cannot be aimed and is muted while directional spatial sound is enabled.
