# 2026-10-27 — Runtime timing, input, profiling, audio and painted collision

## Native ECS dispatch tracing

Play CPU scopes add a default-off **Trace ECS systems** option. Actual owner-thread
fixed-pipeline dispatch intervals feed the existing bounded Timeline/trace copy and
paged MCP through the same controls, permissions and lifecycle. Native timer skips
produce no interval; explicit nested runs are included. Depth64 losses are reported
and unmatched scopes cancel. Native ECS lifetime totals remain a separate view.
The startup bridge respects external OS policies and isolated Script file confinement.
Native instrumentation is private to pinned Flecs; SDK values/C tables/ABI1 remain
unchanged. Measured synthetic overhead is documented in CPU profiling. Windows,
full Main, physical review, external clients and authentic new captures are pending.

## Audio Source echo

Audio Sources add optional interval/feedback echo through the existing native voice
chain. Short clips retain bounded tails; source/game/full pause freezes appropriate
output, and Resume continues a tail without replaying a finished clip. Stop/Restart
clears old echo history. Inspector/history/live tuning/public C++/MCP share the
same saved fields and copied tail/buffer observations. Gallery90 compares the same
original processed recording with two echo settings. Matching SDK modules require
rebuilding; C tables/ABI1/pins remain unchanged. Windows/physical listening/full
Main/external client/new captures remain pending.

## Measured CPU scope timelines

The existing bounded Profiler retains host scope starts, offers a measured Timeline
and copies Trace Event JSON with capture date/time. The same mapper serves paged,
read-only Play automation/MCP traces with snapshot cursors. Duration-only SDK
records stay untimed; logical overlap lanes do not imply additional measured
threads. Existing frame clocks, four frame summary values and ECS lifetime views
retain their meaning. No public C table, SDK fingerprint or dependency pin changes.
Windows/full Main, external-viewer, new screenshot and physical acceptance remain
separate pending gates.

## Ordered Sprite frames

SpriteRenderer supports an optional ordered list of512 unequal image rectangles,
with crop-relative pixel coordinates and empty-list grid fallback. Native Inspector
collection controls, visual picking, Timeline, Scene/Prefab/history/live values,
matching C++ SDK and existing automation/MCP use the same data/admission owners.
Native collection bounds survive copied metadata; integer collection drafts retain
integer JSON. Ordinary Play accepts supported Sprite tuning through the existing
guard. Gallery87 and a lossless actual software-Vulkan Game image explain the
workflow. Native Windows/full editor/physical acceptance remain pending.

## Painted one-way platforms

Static nonsensor Tile Maps can use the existing One-way collision setting. Each
painted rectangle supplies its own solid face through shared native and character
response, with selected-collision arrows, source/history/MCP integration and
Gallery86. Existing public painted-cell/physics helpers and recovery are reused.
Moving/sensor one-way and per-tile polygons remain unsupported; Windows and
physical/graphical export acceptance remain pending.

Long-fall contacts retain native continuity through a bounded fixed-boundary
contact read. The exact pinned native snapshot source receives three checked
empty-array guards in a generated build-tree copy; strict sanitizers remain
enabled, and upstream pin, notices, API and checkpoint format are preserved.
Matching SDK modules require rebuilding.

Gallery83–86 catalog records match their saved Scene schema. The recent audio
examples use the built-in RGB Tint fields; relocated cooked-content checks cover
all three new audio scenes and the one-way Map, including actual offline playback
and saved pause/filter values.

## Source audio filters

Audio Sources provide optional low-pass and high-pass filters with saved cutoff
settings, true bypass defaults, per-voice processing and live source/one-shot
updates. Existing Scene/Prefab/history, Inspector, C++ values and MCP controls
share those fields. Gallery85 compares the same recording with either filter.
Exact SDK modules require rebuilding; Windows and physical listening remain pending.

## Independent audio pause

Audio Sources now have a saved Pause with game policy, on by default. Opted-out
music/UI voices and one-shots continue while fixed gameplay is paused; preparation,
Stop, replacement, preview Pause and runtime faults suspend complete output.
Existing Inspector/Prefab/history and Play/MCP owners expose the policy. Exact-SDK
control callbacks can request only opted-out active sounds; matching modules must
be rebuilt. Gallery84 and an original installed-SDK example demonstrate the workflow.
Native Windows/physical listening/graphical export and new captures remain pending.

## Audio source codecs

MP3 and FLAC now share WAV's supervised import, processing settings, selected PCM,
preview/waveform, source watcher, runtime/export and existing MCP asset workflows.
Distinct new recipes preserve WAV settings, identity and old cooked revisions.
The existing pinned miniaudio decoders are enabled; no new codec/encoder dependency
or public SDK ABI is introduced. Original mono/stereo recordings and Gallery83
compare the same WAV, FLAC and MP3 audio. Native Windows and physical listening,
external-client and new authentic screenshot acceptance remain pending.

Local source; Windows verification and numbered delivery remain pending.

- Runtime clock status exposes elapsed unscaled seconds, the latest distinct
  interval and a sample sequence through existing C++ Game queries and Play/MCP
  timing. Pause and zero/slow simulation speed retain real-time observations.
- Fixed physics/Flecs delta, catch-up bounds and command boundaries remain
  unchanged. Equal samples do not erase intervals; Step creates no wall time.
- The public time_scale_gameplay SDK example includes a per-entity control timer.
  Its opted-in RateProbe demonstration schema is version2; existing authored
  version1 examples require ordinary explicit schema migration after inspection.
- Scene switches retain the host timeline; a new Play/process starts a new origin.
  No C ABI layout, SDK fingerprint, persistence format or dependency is changed.

## Gamepad feedback

- Scoped C++ game requests can submit bounded low/high-frequency controller
  vibration through existing host/device and SDK Play effect owners.
- Real SDL return values determine receipts. Focus/routing/Play/device transitions
  stop feedback; unsupported controllers do not report success.
- The original public SDK example uses control actions and normal receipt release.
  No new C ABI layout, persisted data or dependency upgrade is introduced.

## Native ECS profiling

- Play Profiler shows the actual fixed-pipeline order, native merge points and
  cumulative system CPU totals, with current collection/Stop/Clear controls.
- Optional Play/automation/MCP observations share the same bounded runtime copy.
  Inspection never advances gameplay; native lifetime counters retain their meaning.
- Linux checks cover native order/counters, lifecycle, both Play hosts and MCP,
  and the real ImGui controls. Windows execution and authentic captures are queued.

## Analog input response

- Continuous analog bindings support an outer deadzone and power response exponent
  through Project Settings, saved input maps and normal gameplay/MCP observations.
- Radial sticks preserve direction; buttons and relative mouse retain their current
  mapping. Invalid effective ranges reject before settings publication.
- Rebinding clears incompatible processing. Windows/physical controller response
  review and an authentic updated settings capture remain queued.

### Touch contacts

- Eight stable touch slots with independent normalized X/Y/pressure, cancellation and focus/size/reset neutralization through the existing SDL/action owners.
- UI-owned touch gestures stay in UI; emulated mouse/touch gameplay duplicates are filtered. Semantic ranges and validation agree across Settings, runtime and automation/MCP.
- Original Gallery88, optional exact-SDK Sprite cursor example, installed client and focused SDL/UI/Play/MCP/source-free checks. Physical/native Windows touch acceptance remains queued.

### Two-finger gestures

- Derive stable-pair pan, aspect-correct logarithmic pinch and wrapped rotation through existing touch/input owners, with coincidence/replacement guards and ordinary one-latch semantics.
- Expose the same controls/units through Settings, C++, automation and MCP. Retained menu routing neutralizes pending physical input on release.
- Original Gallery89 and public installed C++ canvas example; source-free/SDK/both-host/UI/MCP checks. Windows/Main/physical touch/export review remains queued.
