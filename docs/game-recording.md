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
FPS/count/extent/quality. Optional `capture_scope` uses the same scene/session
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

The guide's two PNGs are unretouched local development captures. The options
image uses the production recording window through Diligent's ImGui renderer at
100% scale. The Game image uses the production PlayPresentation, FrameRenderer,
RuntimeUiHost and recording task with a test-owned Gallery copy at 640×360.
The same fixture checks six wide/narrow 100/150/200% layouts and native
cancellation during active GPU capture. Rendering uses software Vulkan; it does
not establish Windows/full Main, D3D12, physical GPU/input/DPI or player acceptance.

| Canonical manual image | SHA-256 |
| --- | --- |
| `game-recording-options.png` | `dbdd88c5504df38d514761b5216bc910f31c6af919483e6e772e6ad7ab04ecb7` |
| `game-recording-frame.png` | `9f66928482c8f1d02961c0303a592d964c9bdbbf3237a93a77bec55d91b747df` |

The options capture includes the saved-input field and **Across scenes** control.
Its native fixture build used source-input digest
`a541a11ddad00c8f7080b51e3016ea55217501882cefdcf1410a9988d49e2be8`.
The local configured build identity was `unassigned` with source metadata
`d70e63a360dd422c211e0d323c3c90567b001eb5`; the capture came from the current
development sources rather than a numbered Windows delivery. The unchanged Game
image retains its existing provenance.

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
