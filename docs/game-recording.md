# Live and offline game recording

`GameRecordingTask` is a scoped `ViewImageWork` composed with the existing PNG
capture/file owner. It borrows one `PlaySession`: `recording_mode:"live"` attaches
to ordinary SDK Play, while the default `"offline"` launches the exact runtime in
its private offline recording profile. The runtime retains its
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

`GameRecordingRequest.capture_scope` defaults to `scene`. It accepts single-Scene
input takes and preserves the original fixed-world recording contract. Explicit
`session` scope admits game-requested Scene changes and an optional version-2
session input take. Mismatched take scopes fail before output-folder creation or
launch. Session Begin validates parameters and optional replay against a copied
`RuntimeInput` through `input_recording_command` before resuming; neither input nor
clock is changed by a rejected admission. The separate offline `input_recording`
command continues to refuse session takes.

See the [user workflow](../manual/editor/game-recording.md). Silent Timeline
movies retain their [separate source semantics](sequence-movie-export.md).

## Fresh offline time and lifecycle

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

Session movies retain one pending frame's copied PCM in `GameRecording`.
`poll_frame` performs at most16 quanta per call and checks readiness before mixing
and before publishing a complete interval. A pending response includes
`pending_frame` and `pending_audio_frames`, with no `recording_frame` payload.
The caller services the existing candidate/renderer/platform acknowledgement
channel and requests the same frame again with its observed current generation.
Already mixed quanta are never repeated. Idle transport polls cannot commit a
recording world or advance simulation; the next recording poll services host
preparation at the held media cursor before mixing resumes. This keeps generation
changes correlated with recording replies while retaining strict stale checks.
The5s request deadline and16MiB transport limit remain unchanged.
Scene publication reserves the same complete encoded PCM interval as the frame
reply. A world that fits ordinary Play can therefore be refused before admission
when its movie snapshot plus audio would exceed that bound; the active world is
retained.

The host's existing automatic-activation ticket distinguishes a requested Scene
change from background preparation with `activate:false`. Background preparation
can continue while fixed gameplay runs; only automatic activation waits for
Scene admission. A failed or cancelled automatic activation fails the movie
while retaining the prior world. No separate activation queue is maintained.

Every new Scene uses the existing GameSession preparation/adoption/retirement
boundary and starts its local fixed timeline at zero. Cumulative movie sample
time remains with the existing media owner. Runtime UI preparation holds its
presenter's last admitted media time; session temporal image samples use that
cumulative media cursor across local clock resets. Loading wait time contributes
no movie audio or fixed ticks. Up to64 Scene visits are admitted; a later candidate
is rejected through the existing preparation owner. Input replay may keep a
candidate unready until the current recorded segment has been consumed.

Only a fresh paused SDK launch can begin recording. Source Scene identity,
revision, document generation, launch, session, world generation and sequential
frame receipts must remain current. Interactive input, ordinary time controls,
arbitrary live native-state restoration are excluded. In scene scope, runtime
world replacement is excluded. Session scope admits only the existing correlated
Scene transition channel, preserves the initial authored document guards, and
checks the runtime's append-only Scene/generation/source/sample witnesses. PNG
metadata is captured before its later audio interval, including when several
Scene transitions occur inside that interval. A world change while a PNG is in
flight rejects rather than assigning that image to a different Scene.
Game-requested Pause/Quit, faults, failed/cancelled preparation and source edits
stop capture and retain completed output.
Cancellation, source edits and faults stop only the task's own launch. Committing
detached IO and the supervised encoder retire through their existing owners.
Shutdown settles detached jobs, attempts a dated receipt and never blocks on a
GPU ticket. Fresh UserData stays below the task's newly owned output folder.
The task restores the prior transient user-data selection after retiring its run,
including launch failures, so subsequent ordinary Play retains its normal storage
policy. A replacement launch's selection is never overwritten.

## Current Play capture

Live mode accepts only `capture_scope:"scene"` and no new `input_recording` path.
Admission requires settled ordinary SDK Play with its existing device output.
A NullDeviceTest backend is accepted by native test fixtures; Offline output is
refused. The task binds the original launch/session and current runtime Scene/
world without restarting, rewinding, pausing or redirecting user data. Ordinary
input and time controls retain their owners. Scene/world changes fail the clip,
retain partial output and retire its old audio lease while game loading continues.

`AudioRuntime`'s existing final48kHz stereo float callback observes output through
a private miniaudio `ma_pcm_rb` SPSC buffer. The exact dependency pin/options are
unchanged. The callback does no allocation, locking, waiting, IO or ECS/editor
access. A callback entry count and enabled flag establish quiescence before a
new capture resets storage. Two seconds/96000frames bound retained PCM; each
owner read copies at most4096frames. A whole overflowing/nonfinite callback fails
without overwriting the earlier complete prefix. Progress derives from the
committed queue plus the consumer's read cursor. Runtime shutdown joins the
producer before storage destruction; copied PCM can outlive world retirement.

Private SDK `audio_capture` begin/read/end replies include original Scene/world,
fresh capture generation, state, coherent captured/read/queued counts and bounded
PCM16 chunks with exact sequential cursors. They do not republish a complete
world. Begin never changes gameplay time. Read/end can drain an earlier retired
world. `live_audio_capture` advertises this backend primitive, not a separate
public SDK callback ABI. The existing PCM16 conversion is shared by both modes.

PlaySession validates identities, types, cursor, monotonic progress and payload
bounds. Media commands have lower priority than observations, admission and
ordinary controls. A normal snapshot follows every media reply so input/epoch/
acknowledgement traffic continues. Pause/Resume can queue behind a bounded media
read. Ending the observer leaves the device and game clock running. Cancel and
fault cleanup use the same end/drain path. If the recording task is destroyed,
PlaySession retires its abandoned observer asynchronously; closing the editor
still follows normal runtime shutdown. An unready new world does not prevent
retiring the previous world's copied audio.

The task warms visible Game output before Begin. At each actual Game copy it
retains Scene/world/tick/interpolation/presentation time, local timestamp and the
last observed captured mixer cursor. The first actual image establishes the
movie's audio source origin. These are observed transport/render boundaries:
they do not establish physical device latency or sample-accurate audiovisual
alignment. Ordinary live rendering and Runtime UI keep their existing clocks;
no offline temporal sample is substituted.

Actual saved PNGs have contiguous image indices. Logical movie frames reference
the latest saved image whose observed source cursor is at/before the nominal
video boundary. Missing intervals repeat the preceding PNG reference through the
existing sequence encoder; no extra PNGs, simulated ticks or fabricated captures
are created. The encoder's expanded snapshot budget counts repeated references.
At the planned audio end, the task ends/drains the tap, settles pending PNG IO
and crops its owned raw PCM to exact container duration before encoding. No
complete AVI is published for Cancel, Scene change or other failure.

Live receipts use version3/profile `live-scene-game-v3`. `images` lists actual
saved PNG observations and `frames` lists logical movie references with image
index/repeated flag and nominal audio position. `captured_png_frames`,
`completed_frames`, retained `audio_frames` and `observed_audio_frames` stay
separate. `capture_scene` is the runtime Scene; the existing top-level `scene`
remains the authored baseline. `audio_source_start_frame`, `raw_audio_frames`
and `audio_normalized` identify trimmed complete output versus a partial raw
prefix. Warm-up and retirement each allow two extra seconds beyond the128MiB
normalized PCM bound, still within the combined256MiB snapshot limit.

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

Session receipts use version2/profile `offline-session-game-v2`, leaving version1
and the AVI codec profile unchanged. Each PNG adds its actual `scene`,
`world_generation` and source witness at image time. `visits` records ordered
generation/Scene/source/`audio_frame` boundaries, including repeated Scene visits.
`observed_audio_frames` can exceed retained `audio_frames` after cancellation or
failure: partial unpublished PCM is discarded, while completed PNGs remain. A
visit may therefore lie beyond the completed PCM prefix in a partial receipt.

## Automation and MCP

`capture.game.read` returns current source baselines, readiness, execution grant
and media bounds. `capture.game.start` requires execution and output grants plus
current Scene/revision/document baselines, a new scoped folder and bounded
FPS/count/extent/quality. Optional `recording_mode` defaults to `offline`; `live`
uses the same current-Play task path. Discovery lists `recording_modes`,
`mode_readiness` and `live_capture_scopes:["scene"]`. The legacy top-level `ready`
and `capture_scopes` describe the offline workflow. Optional `capture_scope` uses the same scene/session
values and task factory as the native **Across scenes** control. Discovery lists
those scopes and the64-visit bound. MCP derives its schemas and open-world annotation from
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

The guide's options PNGs are unretouched native development captures from the
production recording window and Diligent ImGui renderer at100% scale. Offline
and live controls pass wide/narrow100/150/200% layouts. Completion, active GPU
cancellation, an offline session movie and a live Scene-change guard make up
fourteen native fixture cases. Live capture uses the real renderer/runtime UI
and the existing clocked null test device with a private Gallery copy.
Software Vulkan does not establish Windows/full Main, physical GPU/input/DPI,
speaker latency or external player acceptance.

| Canonical manual image | SHA-256 |
| --- | --- |
| `game-recording-options.png` | `95e0dcb50577c9457af58702382ddda4aa7d0433249cbf74f1a93c421a467fe5` |
| `game-recording-live-options.png` | `ccbb0331e18fc6d0cf1653655618be8607b4f1400fdd8a63e331556d50712584` |
| `game-recording-frame.png` | `9f66928482c8f1d02961c0303a592d964c9bdbbf3237a93a77bec55d91b747df` |

The options captures use source-input digest
`152d22b050d0ffcf5e68e9654d3698d18c84c4f32da6e98c420f6eb03112336b`.
The configured local identity is `unassigned` with source metadata
`d70e63a360dd422c211e0d323c3c90567b001eb5`, rather than a numbered Windows delivery.
The unchanged Game image retains its existing provenance.

The live fixture independently compares every actual PNG with a Game readback
and Scene/tick/presentation metadata. Logical movie frames retain causal image
references, including repeated images. A separate test decoder recovers all72000
stereo sample frames exactly from the15-frame,10FPS clip and verifies non-silent
stereo, decoded image count and repeated JPEG payloads. JPEG loss is measured
against each referenced PNG. These checks verify the observed media contract,
not sample-accurate physical audiovisual alignment.

The same fixture also records private Room A/Room B copies using the original
SDK example, the existing Gallery182 HUD and autoplay tones. All four saved PNGs match
independent GPU readbacks and their actual Scene/local-tick receipts. Three Scene
visits occur at audio sample boundaries0,1600,3328; the PNG Scene order is
A/B/B/A. A separate test decoder recovers all6400 stereo sample frames exactly
from the four-frame movie. The middle Scene's doubled tone pitch is observable
in its audio region. These private fixture additions do not change the shipped
Gallery Scenes or establish physical/player acceptance.

Independent GPU references at ticks 0/30/60 match saved PNGs exactly. An existing
test-only FFmpeg 7.0.2 decoder verifies the 31-frame, 30 FPS movie and recovers all
49,600 stereo sample frames bit-for-bit. Audio is non-silent. The falling body's
height, the scene panel's motion and the HUD pulse widths advance in the same
run. JPEG RGB mean absolute error is at most 1.45 levels against the source PNGs;
this fixture value is not a general quality or hardware-performance guarantee.
The decoder is a local validation tool and is not a redistributed dependency.
