# Fresh offline game recording

`GameRecordingTask` is a scoped `ViewImageWork` composed with the existing PNG
capture/file owner. It borrows one `PlaySession`; that session launches the exact
SDK runtime in its private offline recording profile. The runtime retains its
one `GameSession`, fixed `RuntimeClock`, runtime world, lifecycle, physics and
`AudioRuntime`. Recording progress contains media indices and sample counts,
never a second world, simulation scheduler or mixer.

Optional `GameRecordingRequest.input_recording` selects a contained saved
`Recordings/<name>.json` take. The task validates/copies it before launch, queues
the shared fixed-input replay at paused tick zero, then starts the existing
offline media clock after acknowledgement. Physical input remains excluded.
The owner retains `input-recording.json` and its source/tick/digest receipt; the
bounded take is additional to the PNG/PCM movie budgets. Actions become neutral
after exhaustion. This preserves input samples, not arbitrary game-state
determinism. See [input recording](input-recording.md).

See the [user workflow](../manual/editor/game-recording.md). Silent Timeline
movies retain their [separate source semantics](sequence-movie-export.md).

## Time and lifecycle

Preparation uses tick zero. Once admitted, ordinary editor/transport polls cannot
advance the offline run. Capture the current Game image first; `record_frame`
then mixes the matching interval and advances the existing runtime through
cumulative media time in at most 128-stereo-sample host quanta. The fixed clock
alone schedules simulation ticks, preserving project frequency and time-scale
policy. Audio controls are bounded by those host quanta, not sample-accurate
native event scheduling.

Game rendering keeps the runtime's interpolated `presentation_seconds` sample.
The runtime UI host adapts acknowledged media progress into the existing
presenter's monotonic time domain, including staged tick-zero preparation. It
does not construct another Rml system interface or animation owner.

Only a fresh paused SDK launch can begin recording. Source Scene identity,
revision, document generation, launch, session, world generation and sequential
frame receipts must remain current. Interactive input, ordinary time controls,
world replacement and arbitrary live native-state restoration are excluded.
Cancellation, source edits and faults stop only the task's own launch. Committing
detached IO and the supervised encoder retire through their existing owners.
Shutdown settles detached jobs, attempts a dated receipt and never blocks on a
GPU ticket. Fresh UserData stays below the task's newly owned output folder.
The task restores the prior transient user-data selection after retiring its run,
including launch failures, so subsequent ordinary Play retains its normal storage
policy. A replacement launch's selection is never overwritten.

## Media contract

The `forge.image.avi-game` recipe uses profile
`mjpeg-pcm16-opaque-srgb8-v1` and publishes `game.avi`. The existing shared sequence
host snapshots completed PNGs and `audio.s16le` before invoking the existing
supervised worker. The worker accepts only its fixed flat names and bounded
copied bytes. Atomic final publication and cancellation use the same storage
boundary as silent AVI and WebP output.

Video is opaque display-encoded sRGB, independent JPEG frames at a reduced
rational rate/scale. Audio is signed little-endian 16-bit stereo at 48,000 Hz.
Each frame pairs `00dc` JPEG with an indexed `01wb` PCM packet. Audio boundaries
are rounded from cumulative rational video time, avoiding repeated per-frame
rounding drift. Nonfinite mixer output rejects; finite output clips explicitly
before quantization. The common bounded canonical binary receipt validates exact
frame/sample counts and numeric widths before exposing typed PCM to the editor.

Limits: 1–240 FPS, 1–4,096 frames, 1–4,096 pixels per axis, JPEG quality 1–100,
128 MiB PCM, 256 MiB combined PNG/PCM snapshots and 256 MiB final AVI. Existing
worker time/address-space/output limits apply. The optional audio stream extends
the private container implementation; existing silent AVI/WebP APIs stay intact.
The public audio loading-wait helper requires a matching rebuilt SDK; no runtime
callback-table ABI is changed.

`recording.json` version 1 records the source digest/Scene revision, engine build
identity, launch/session/world, start/end local timestamps, captured PNGs,
completed audio pairs, per-frame media and presentation times, fixed ticks,
interpolation alpha, error/reason and any complete movie metadata. A cancelled
recording can retain a PNG without its subsequent audio interval. Counts keep
those cases distinct. Disk failure can prevent the shutdown receipt itself.

## Automation and MCP

`capture.game.read` returns current source baselines, readiness, execution grant
and media bounds. `capture.game.start` requires execution and output grants plus
current Scene/revision/document baselines, a new scoped folder and bounded
FPS/count/extent/quality. MCP derives its schemas and open-world annotation from
that same automation method. Native controls and MCP use the same task factory.

`capture.status` exposes `game_recording`. `capture.cancel` needs the exact active
operation and its output grant. Completed PNG inspection uses the stable
`artifact_operation` with `capture.preview`; every preview receives a new
operation for `capture.image`. Neither operation grants arbitrary file reads.
Recording creates no Scene Undo entry and does not roll back user edits after a
source guard fails. General automation revision/replay/project protections remain
in force.

AVI field contracts follow Microsoft's
[AVI RIFF reference](https://learn.microsoft.com/en-us/windows/win32/directshow/avi-riff-file-reference),
[stream header reference](https://learn.microsoft.com/en-us/previous-versions/ms779638(v=vs.85))
and [PCM format fields](https://learn.microsoft.com/en-us/windows/win32/api/mmreg/ns-mmreg-waveformatex).
The FORGE writer emits its own explicit little-endian fields. These references
do not add a codec dependency or a runtime media-player capability.

## Capture provenance

The guide's two PNGs are unretouched local development captures. The options
image uses the production recording window through Diligent's ImGui renderer at
100% scale. The Game image uses the production PlayPresentation, FrameRenderer,
RuntimeUiHost and recording task with a test-owned Gallery copy at 640×360.
The same fixture checks six wide/narrow 100/150/200% layouts and native
cancellation during active GPU capture. Rendering uses software Vulkan; it does
not establish Windows/full Main, D3D12, physical GPU/input/DPI or player acceptance.

| Canonical manual image | SHA-256 |
| --- | --- |
| `game-recording-options.png` | `0e0f57c69b4f3492e17f3499a639c4bd1324c01239763cc78d42b85156b1b7d5` |
| `game-recording-frame.png` | `9f66928482c8f1d02961c0303a592d964c9bdbbf3237a93a77bec55d91b747df` |

Independent GPU references at ticks 0/30/60 match saved PNGs exactly. An existing
test-only FFmpeg 7.0.2 decoder verifies the 31-frame, 30 FPS movie and recovers all
49,600 stereo sample frames bit-for-bit. Audio is non-silent. The falling body's
height, the scene panel's motion and the HUD pulse widths advance in the same
run. JPEG RGB mean absolute error is at most 1.45 levels against the source PNGs;
this fixture value is not a general quality or hardware-performance guarantee.
The decoder is a local validation tool and is not a redistributed dependency.
