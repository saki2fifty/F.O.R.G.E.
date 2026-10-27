# Original audio codec fixtures

These recordings were generated for FORGE. They contain no purchased recordings,
competitor assets or third-party example audio. Use them freely with this project.

`MonoReference.wav` contains12,000 PCM16 frames at48kHz. `StereoReference.wav`
contains12,000 PCM16 frames at24kHz. For zero-based frame i, the gain envelope is
`min(1, i/240, (11999-i)/240)`. Left/mono is a440Hz sine with integer amplitude8192;
right is a660Hz sine with integer amplitude4096. Samples are rounded to signed
16-bit values. The reference WAVs contain the original samples.

The matching `.flac` and `.mp3` files were encoded from those WAVs using FFmpeg
7.0.2. Common arguments are `-map_metadata -1`; FLAC uses
`-c:a flac -compression_level 5`, MP3 uses
`-c:a libmp3lame -b:a 128k -write_xing 1`. Encoder identification in the files is
preserved. No encoding executable is included with FORGE.

Scene83 uses the three stereo files. Only WAV starts automatically; choose one
speaker at a time through Play On Start, or preview a clip from Content.
FLAC reproduces the reference PCM exactly. MP3 is lossy. The mono files also
serve the same shared codec-admission tests and can be imported from Content.
