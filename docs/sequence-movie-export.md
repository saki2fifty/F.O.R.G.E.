# Silent sequence movie export

Sequence preview export can retain its PNG frames and additionally write a silent
Motion JPEG AVI. The existing Timeline frame source and ViewImageTask own sample
times, borrowed preview state, render/readback readiness, output-folder ownership,
cancellation and the final receipt. The animation runtime does not encode movies.
See the [user guide](../manual/editor/animation-sequences.md).

## Shared ownership

The private `forge_sequence_encoding` tool library owns immutable completed-PNG
snapshot preparation, process supervision and final container publication for
WebP and AVI. Existing `write_webp_sequence` callers retain their API, format,
timing and limits. `write_avi_sequence` selects the AVI profile through the same
host path. A shared worker PNG reader checks snapshot names, aggregate bytes,
dimensions, sRGB, RGBA channels, byte precision and packed rows. Neither encoder
reads arbitrary project or catalog paths in its worker.

AVI recipe `forge.image.avi-sequence`, profile `mjpeg-opaque-srgb8-v1`, carries
width, height, planned frame count, reduced rate/scale and integer JPEG quality.
The packaged import worker validates the complete profile before encoding. JPEG
comes from the existing approved image encoder. Nonopaque alpha rejects; no
implicit compositing or color conversion changes the retained PNGs.

After a complete worker result passes the host's format/size checks, the existing
file-storage owner publishes `animation.avi` in the task's new folder. An existing
destination rejects. Failed or cancelled encoding retains completed PNGs and its
receipt; it never deliberately publishes a partial container. Cancellation after
the file commit leaves the complete movie. The preview cursor and playing state
are restored before container encoding, including failure and cancellation paths.

## Container and timing

The FORGE-owned writer emits a bounded classic RIFF AVI containing one MJPEG video
stream, a `movi` list and an `idx1` keyframe index. It writes little-endian fields
explicitly rather than serializing platform memory structures. Every frame is an
independent JPEG, with even-byte RIFF padding excluded from payload lengths. The
file, stream and index sizes are finalized only after all planned frames exist.
Storage grows geometrically within the output cap. No DirectShow API, extra codec
dependency, OpenDML extension or large-file segment is introduced.

The container uses a reduced rational rate/scale. Common integer, decimal and
fractional rates are preserved without accumulating per-frame millisecond rounding.
The numerator fits signed 32-bit readers and the denominator is at most 10,000,000.
Approximation error must be at most `max(1e-10, requested_fps × 1e-10)`; an
unrepresentable rate rejects. The AVI main-header microsecond interval is advisory;
the stream's rational timebase retains the more precise rate. Explicit source
sample times remain those in the existing frame plan and `frames.json`.

The movie begins at its first captured frame, independently of the source range's
nonzero start time. Its duration is `frame_count × scale / rate`; it contains no
audio stream or loop directive. Standards references:
[AVI RIFF structure](https://learn.microsoft.com/en-us/windows/win32/directshow/avi-riff-file-reference),
[stream fields](https://learn.microsoft.com/en-us/previous-versions/ms779638(v=vs.85)),
[index fields](https://learn.microsoft.com/en-us/previous-versions/ms779634(v=vs.85)).

## Bounds

- Opaque display-encoded sRGB RGBA8 PNG input; JPEG output is lossy.
- One to 4,096 frames, axes 1–4,096 and at most 64 MiB of decoded RGBA per frame.
- FPS 0.001–240, subject to the bounded rational precision above.
- JPEG quality 1–100, default 90, through the existing image encoder.
- Completed PNG snapshots: 256 MiB aggregate and 68 MiB per file.
- Completed AVI: 256 MiB. The profile stays below RIFF's 32-bit size ceiling.
- Supervised worker: 1 GiB address-space limit, 120 seconds wall time, 110 seconds
  CPU time and 250 ms cancellation grace, using the existing platform boundary.

These admission limits are not physical-hardware performance claims. PNG-only and
WebP output retain their documented independent bounds.

## Native and programmatic workflow

The existing export dialog's **Additional output** choice selects PNG-only, WebP
animation or AVI movie. AVI exposes **JPEG quality**. Native Start and Cancel use
the same task as `capture.sequence.start`, `capture.status` and `capture.cancel`.
MCP discovery exposes `format: "avi"` and optional integer `jpeg_quality`; the latter
rejects for other formats rather than being silently ignored. Existing output
grants, ready source revision/generation, replay and project ownership still apply.

The version 1 frame receipt retains its frame list and adds `output_format: "avi"`
when selected. After successful encoding, `animation` records the relative file,
MIME `video/vnd.avi`, codec `mjpeg`, `audio: false`, frame/byte counts, rate, scale,
duration seconds, quality and profile. Capture status uses the same metadata owner
and resolves the file inside its granted output folder. WebP receipt fields retain
their existing names and meanings. Inspection continues to select a retained PNG;
the movie is not presented as a still-image preview or imported runtime Texture.

## Scope

The sequence preview renders its existing camera image. It does not execute gameplay
C++, physics, advancing particle simulation, runtime UI or sound. Movie output does
not add synchronized gameplay/audio recording, runtime movie playback, HDR video,
16-bit video or automatic animated-Texture import. There is no Scene, asset, SDK,
runtime ABI or runtime-kit format change. Windows/full-editor, physical display
and external-player acceptance remain distinct from local fixture execution.

## Capture and validation provenance

The guide's AVI and WebP option images are unretouched production ViewImageTask
captures through Diligent's ImGui renderer on software Vulkan at 100% UI scale.
Controlled 7×3 opaque/alpha frame sources exercise the actual selectors, quality
input, scrolling and Start/Cancel controls at wide/narrow 100/150/200%. The Gallery
frame is independently decoded from its exported AVI, produced by the real Timeline
and FrameRenderer from Gallery181. Source PNGs match independent display samples
exactly; JPEG is lossy. The existing local FFmpeg 7.0.2 validation tool verifies
seven native/Gallery movie outputs and is not a new redistributed dependency.
Canonical image hashes and detailed limits are recorded in the
[capture inventory](images/readme/README.md). These fixtures do not establish
Windows/full Main, physical GPU/input/DPI or external-player acceptance.
