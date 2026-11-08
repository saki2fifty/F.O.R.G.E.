# Audio impulse responses

Open this folder as a FORGE project. The startup Scene includes a cube with an
Audio Source, a Camera and a directional Light. A short original pulse repeats
once per second through the project's Room bus and synthetic stereo response.
The response is generated audio, not a recording of a real room. No Project Code
or external asset download is required.

## Listen and compare

1. Open `forge.project.json` through **File → Open Project**, then start Play.
2. Stop Play and open **Tools → Project Settings → Game defaults → Custom audio buses → Room → Shared effects**.
3. Set **Impulse wet mix** to `0`, click **Save Settings** and start Play. This is the bypassed pulse.
4. Stop Play, change the wet mix to `0.75`, save settings and start again. The stereo response adds a short tail.
5. Adjust the cube's Audio Source Gain or the Room bus volume to balance the sound. Save the Scene after authored source changes.

`Assets/Signal.wav` is the original mono pulse; `Assets/SyntheticRoom.wav` is the
original half-second stereo filter. Both are resident48000Hz AudioClips with
selected cooked data included. Open either in Content to inspect or preview it.
The response is a project setting and need not appear on a Scene component.

## Export and test on Windows

1. Stop Play and save settings and the Scene.
2. Open **File → Export Game**, choose an empty output destination and use the matching runtime kit supplied with the editor.
3. Click **Export / Rebuild**, then run the exported executable.
4. Compare the repeated pulse and stereo tail with editor Play. Check Pause/Resume and Stop. Copy the entire exported folder when testing elsewhere.

The matching kit must support imported bus responses. A zero-wet assignment still
retains its clip and requires that kit. Missing/incompatible responses or an
exceeded processing budget report an error before required audio starts.

Automated source-hidden export and Linux CPU PCM checks cover the saved content.
Windows/full-editor, physical listening and display review remain pending; this
README supplies the manual verification steps and does not assert their outcome.

Use the audio guide in the editor's offline manual for response limits, shared
pause/tail behavior and troubleshooting.
