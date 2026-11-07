# 2026-11-07

## Finite Sound Cue runs

SoundCue adds an optional scheduled event limit in both timing modes. Finite runs
stop issuing requests at their limit and finish after all of that run's Source
voices and source effect tails retire, including earlier overlapping events.
Manual/other-cue/older-run voices and shared bus effects retain their own lifetime.
Pause, cancellation, replay and natural completion remain distinct.

Inspector, live progress, Scene/Prefab/history, automation/MCP and matching runtime
kit admission use existing component and playback owners. The public component
header requires rebuilding C++ against the matching SDK fingerprint; C ABI1
callbacks and operations are unchanged. Windows/physical review is separate.

## Shared linear audio bus effects

Project buses add default-bypassed high/low-pass, echo and reverb settings. Native
inputs mix sources before the existing shared processor implementation; bounded
fanout applies bus faders and sends afterward. Separate game-paused/independent
histories preserve the existing pause domains without duplicating clip cursors,
voices or stream readers. Source controls retain their own effects and change
future bus input; shared tails remain bus-owned and do not delay cue completion.

Project Settings, live memory observations, automation/MCP, both hosts, fresh
recovery and matching kit admission share the existing owners. Installed SDK
component layouts/fingerprint and C ABI1 callbacks/operations are unchanged.
Gallery196 is editable/exportable with two shared buses, three original tones
and illustrated controls. Windows/physical acceptance remains separate.

## Completion-aware sound-cue events

SoundCue adds an optional completion timing mode with a zero-to-3600-second gap.
Each event waits for its own Source-owned voices and effect tails; unrelated
manual/other-cue voices and older requests after Restart do not block it.
Transient event provenance stays inside the existing command/voice owner.
Deferred/stream preparation and rejected admission remain distinct from completion;
failures stop future production with diagnostics and preserve Source ownership.

The actual Inspector, live counts, Scene/Prefab/history, both hosts, matching C++,
automation/MCP and additive runtime-kit guards share this mode. Older content
retains interval behavior and native defaults; C ABI1 operations/tables are
unchanged with matching component headers/fingerprint required. Gallery195 and
original illustrated controls demonstrate repeated layered/ordered/weighted
completion events. Native transitions follow owner service. Arbitrary cyclic
graphs, finite playlist documents and shared bus DSP remain separate work.
Windows/full-editor/physical/external-client acceptance is queued; no numbered
delivery is implied.

## Repeating sound-cue events

SoundCue adds default-disabled periodic event production on the existing native
output clock. Configure a 0.25–3600 second interval, automatic startup and gameplay
pause policy. Existing playback operations start/restart, pause/resume or stop
future events while Audio Sources retain every already-requested voice. Independent
cues run outside game ticks. Late owner service skips bounded missed periods and
reports them. Preparation, source retirement and policy failures retain actionable
producer diagnostics; repair and Restart begin fresh selection.

Native metadata, Scene/Prefab/history, actual Inspector and Live controls, both
runtime hosts, matching installed C++ and automation/MCP share the same owners.
Nondefault intent requires a matching repeating-event runtime-kit capability;
C ABI1 and operation values remain unchanged. Recovery retains authored settings
and starts fresh. Original editable Gallery194 demonstrates independent repeated
layered/ordered/weighted events and timed Source leaves. The illustrated manual
uses authentic production controls. Arbitrary cyclic node graphs, completion-based
playlists and shared bus DSP remain separate work. Windows/full-editor, physical
audio and external-client acceptance remain queued; no numbered delivery implied.

## Timed sound-cue offsets

SoundCue Source leaves add default-zero offsets from zero to ten seconds, rounded
to the existing native output clock independently of pitch. Prepared one-shot
voices retain their remaining delay through applicable game, source and full
pause. Stop and source/world retirement keep the existing ownership and budgets.
Filters, echo, reverb and streams remain on their source voices. Old source values
default missing offsets to zero; nonzero offsets require the additional timing
kit capability. Matching component headers/fingerprints require a C++ rebuild;
C ABI1 callback tables and operation values are unchanged.

Inspector drafts, native metadata, portable Prefab/history, Timeline, both runtime
hosts, installed-only C++ and automation/MCP share those values and controls.
Waiting voices expose their count and remain pausable/stoppable in Live sound.
Original editable Gallery193 demonstrates staggered events; source-hidden cooked
PCM and independent ordinary-source deadlines verify its output. Instructions use
authentic native controls. Windows/full-editor, physical audio and external-client
acceptance remain queued; no numbered delivery is implied. Cyclic playlists and
shared bus DSP remain separate work.

## Event sound cues

Scene/Prefab-owned SoundCue components compose existing AudioSource one-shots
with bounded Source, Layer, Weighted and per-trigger Ordered nodes. Complete graph,
source and queue admission precedes selection advancement; existing source voices,
clips, effects, streaming and routing remain authoritative. Inspector graph drafts
publish one history transaction, retain project scope and reject stale edits.
Live cue controls, Timeline events, matching installed C++ and existing automation/
MCP source/Play workflows share those owners. Playback controls advertise unsafe
blind retries accurately.

The profile supports32nodes, eight children and16one-shots per event. Private
recovery restores graph data with fresh selection; timed/cyclic graphs and shared
bus DSP remain outside this profile. Runtime kit sound_cues:1 prevents unsupported
exports. Original editable Gallery192 combines all node kinds and includes camera,
light and looping Timeline events. Independent leaf-request PCM, source-hidden
cooked content, native controls/history, portable references, both runtime hosts,
installed-only C++ and both MCP framings have scoped local checks. Illustrated
instructions use original production controls with actual offline runtime state.
Windows/full-editor and physical-device acceptance remain separate; no numbered
Windows delivery accompanies this source change.

## Custom audio buses and sends

Project settings add up to16 stable-key scalar buses, one output and up to3
post-fader sends per bus. Shared validation rejects missing/duplicate targets,
cycles and excessive potential gain. AudioSource.custom_bus selects a saved key
through the existing codec, reflection, Inspector, Scene/Prefab and history owners;
empty preserves the stock channel. One existing per-voice gain owner applies the
derived terminal projection after source effects, retaining playback cursors,
pause policy, one-shots and owned streaming.

Project routing controls, source selection, live custom faders, user overrides,
both runtime hosts and automation/MCP share those owners. Numeric-map schemas use
the common authoring validator. Custom routing requires kit profile1; ordinary
private recovery uses root3/audio1 with complete graph/faders, without saving sound
cursors or effect memory. Exact-SDK Play retains its recovery restriction. Matching
SDK rebuild is required for the appended source string; ABI1 tables and pins are
unchanged. Original editable Gallery191 and illustrated guides cover the workflow.
Bus effects, convolution and broader acoustics remain unavailable. Windows and
physical acceptance remain queued; no new numbered delivery is implied.

## Project-wide C++ source search

C++ Sources adds Find in Project with literal text, case and whole-word controls,
background matching/cancellation, bounded file/byte/result budgets and explicit
incomplete/error reporting. Results include open-tab drafts and navigate through
the existing source owner without saving or changing history. Source digests
reject stale results before tab/focus/cursor changes, including external changes
to clean open tabs. Normal source locks and project retirement remain effective.

Read-only automation/MCP can start, page and cancel the same search. Existing
code.open accepts one-based line/column and an optional expected source digest;
navigation retains its normal code permission/revision rules. Shared workflow
metadata distinguishes a fresh ephemeral job from idempotent reads/cancellation.
Gameplay SDK values, ABI, runtime/exported games and dependencies are unchanged.
Full semantic completion remains outside this bounded navigation feature.

## Layered sequence clip overlaps

Animator and Transform Animator add default-off **Layer overlapping clips**.
Existing tracks retain two adjacent contributors; enabled collections blend up to
eight independent phases through the same root clock and complete candidate.
Numeric, Material, reflected, skeletal and morph sampling share normalized ordered
influences; exact Boolean/integer fields choose the strongest clip. Root motion
uses analytic interval influence means and existing layer/reset owners.

Timeline commits the option and pending starts in one Undo step. Live inspection
lists every local time/influence and labels the dominant-pair summary. Recovery15
composes prior profiles and validates exact flags; export requires matching kit
support. Native SDK layouts require a rebuild, while C ABI1 records/tables remain
unchanged. Original Gallery190 demonstrates locomotion, morph and mapped blends,
with editable sources and source-hidden export. The manual includes the real
Timeline controls.

## Repeated controller placements

One child sequence supports up to eight ordered, nonoverlapping placements on its
same parent, with separate starts, rates and loop settings. Finite sections hold
across gaps; loops end at the next placement or parent end. The existing root clock
and complete candidate owner handle inherited resets, cameras, audio cues, copied
presentation and skeletal root motion. Canonical authored boundaries survive the
internal clock conversion without changing source overlap validation.

Timeline add/remove/timing controls use one pending Scene transaction and Undo.
Read-only live/controller watches expose the admitted list and selected placement.
Existing source, portable Prefab, automation/MCP and installed SDK owners share the
native values. Recovery14 wraps prior profiles and validates the exact configuration;
runtime documents/kits use sequence_placement1 compatibility guards. Original
Gallery189 demonstrates locomotion, morph and nested mapped placements, with
illustrated manual guidance. Native modules need the matching rebuilt SDK; C ABI1
callback tables remain unchanged. Windows/full-editor and physical review is queued.

## Nested sequence playback rates

Scene controllers have a dedicated placement rate, independent of stored standalone
speed/loop settings. Positive 0.25–4 local rates compose within 1/64–64 through the
existing root clock. Transform/property/skeletal/morph tracks, root motion, Camera
Shots and Audio Cues follow that mapping. Recovery preserves exact rates and all
existing profiles; runtime-kit admission detects content requiring the new profile.
Timeline edits start/rate in one Undo step and shows parent-time length. Live child
controllers select their parent and reject independent playback controls. Existing
Scene authoring, Prefab, C++ SDK and MCP owners expose the same fields and statuses.
Gallery186 demonstrates fast, slow and nested sections using original assets; the
manual includes authentic controls and rendered Game output. Native modules require
a matching SDK rebuild; C callback tables and clip formats remain unchanged.
Windows/full-editor and physical acceptance remain pending.

## Skeletal tracks on Scene sequences

Direct Animators can share an existing Animation Sequence controller and arrange
one primary and one optional next clip. Each clip keeps its local phase; overlaps
blend skeletal poses and morphs, gaps hold then cut, and controller timing governs
the complete group. Exact rig/model admission, complete candidate publication,
resource replacement rollback, root-motion intervals and recovery use the existing
runtime owners. Unbound States, Blend Spaces, Graphs and standalone blending retain
their behavior. Public Animator values require rebuilding native modules with the
matching SDK; the fixed callback table remains ABI1.

Timeline supports existing Animators, distinct mixed property/skeletal rows, clip
selection, stopped native preview and one-step timing Undo. Inspector opens the
owning Timeline; live track timing is read only with controller selection. Shared
Scene and scoped Play automation/MCP expose the same workflow and typed observations.
Portable Prefab sources retain their member links while shared native templates
defer reference requirements until instance resolution.

Gallery185 includes original direct, morph and retargeted models with a camera/light
and one shared controller. Its independent numeric samples, recovery and relocated
source-hidden output match; four software Vulkan Game image pairs match as well.
The manual includes actual Timeline/Game captures. Windows/full-editor and physical
hardware review remain separate; no numbered Windows delivery accompanies this
local source change.

## Material and reflected-field sequence crossfades

Two-clip sequence tracks support matching Material parameters and reflected
component leaves, including property-only tracks. Binding order may differ;
Material semantics and reflected schema/type/path identities must agree. Numeric
lanes blend over the overlap. Boolean, integer, enum and flag leaves select the
incoming sample at the midpoint while preserving exact 64-bit values. Existing
detached candidates, target admission and shared fixed/presentation sampling remain
authoritative. Both incoming and primary whole curves require valid targets, and
incoming revision changes rebuild derived correspondence and Material admission.
Rejected replacements and incompatible recovery preserve native values and clocks.
No public component, SDK, source/cook or recovery layout changes are introduced.

Gallery184 includes editable per-object color/roughness and named HUD value pairs,
a camera/light and an original screen document. Timeline preview, ordinary Scene
history, generic Source/Scene/Play automation and MCP use their existing owners.
The illustrated manual explains compatible tracks, held values, publication and
export. General multi-clip arrangements remain outside this profile. Local CPU and software Vulkan evidence is separate from
pending Windows/full-editor and physical hardware review. No numbered Windows
delivery accompanies this local change.

Local validation covers the complete sequence regression, scaled native Timeline,
both Source/Play MCP formats, incoming revision/schema/semantic failures, unchanged
values/clocks after rejected recovery, and mixed track families on one object.
Ten selected strict units link five CPU sanitizer consumers; all five pass with
unsuppressed leak checks. Production Game/HUD rendering and relocated source-hidden
content produce four exact image pairs. The offline manual/package checks include
81 pages, 200 authentic images and all 543 Gallery records.
