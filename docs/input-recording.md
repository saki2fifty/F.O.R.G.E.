# Fixed gameplay input recording

`RuntimeInput` records the `InputSnapshot` consumed at the start of each actual
fixed simulation tick. Play's existing process, clock, world, input mapper,
Input Monitor, Input Messages and native gameplay pipeline remain the owners.
Recording creates no second simulation or input mapping path.

The `fixed-gameplay-input-v1` profile begins at tick zero in a fresh paused Play
launch. Frames are consecutive ticks starting at one. Polls, rendering and
control frames add no frames; Pause/Step and catch-up ticks retain their existing
clock/input semantics. A frame contains the mapped values and digital
held/pressed/released flags actually consumed, including coalesced quick taps,
one-time relative motion and neutralization after capture/focus loss.

## Document and admission

Interactive SDK Play also supports the opt-in `fixed-session-gameplay-input-v2`
profile. Its version-2 document replaces `frames` with `segments`. Each segment
has exactly `scene` (AssetId), `source` and `frames`; all segments share the
document's input map and simulation frequency. Local ticks restart from one for
each visit; `consumed_ticks` counts replayed frames across visits. Repeated visits
and zero-tick segments retain their order. A take requires at least one frame
and admits at most 64 segments, 4,096 total frames and 8 MiB. The initial source
equals the first segment's source.

SDK configuration supplies each candidate's witness through the existing source
hash helper. Before a world swap, `GameSession` stages the previous recorder in
the candidate's existing `RuntimeInput`, verifies Scene/source/map/Hz and polls
admission with the actual adopted state. Failure preserves the active world/take.
The editor admission adapter validates before publication and again only when
session-input adoption changes its admitted state. Ordinary repeated polls retain
their original single validation.

Replay delays host candidate publication until the current segment is consumed.
At that boundary, the clock's optional readiness predicate prevents additional
fixed ticks while loading/control/transport work continues. Waiting discards
elapsed debt and holds the current presentation, without phantom ticks or changes
to user Pause/time scale. Step refuses the boundary. Finished/canceled replay
stays neutral through later world changes; live input cannot take over.

The take records expected visits rather than issuing Scene requests itself.
Game logic must request the recorded sequence. Control-phase actions, asynchronous
completion times, saves/random state and arbitrary native state are outside this
document. Legacy captures and ordinary hosts retain single-Scene refusal rules.
Offline movies reject version-2 replay before runtime launch or output creation.

The version-1 JSON document has exactly six fields: `version`, `profile`,
`source`, `simulation_hz`, `input_map`, and `frames`. Action columns follow input
map declaration order, excluding control-phase actions. Each cell is
`[x, y, flags]`, with held=1, pressed=2 and released=4. Tick identity is its
one-based frame index. Digital x must agree with held and y is zero. Analog
actions have no digital flags. Values must be finite. The complete document
validates before admission; failed admission preserves the live input owner.

The source identity hashes the authored runtime Scene snapshot at admission,
saved project settings, engine source commit and SDK fingerprint. Replay also
requires an identical input map and simulation frequency. This guards these
inputs; it is not a digest of every imported asset or arbitrary external file.

Limits are 4,096 ticks and 8 MiB of compact JSON, including the input map.
Capacity completion retains the accepted prefix and reports its reason. Changing
the input map ends active capture with a failure notice; a nonempty completed
prefix can still be retrieved. Replay refuses map replacement and rebinding, including after completion or
cancellation. Finishing an empty capture fails without ending capture. A first
frame that cannot fit the byte budget reports failure. Replay progress counts
consumed saved frames and stops increasing at completion or cancellation. For
version 1, the GameSession owner rejects Scene preparation/activation during
recording or replay; explicit
runtime Scene replacement likewise requires a new launch after recording use.

## Replay and limits

Replay supplies these snapshots through the existing fixed input latch. Incoming
physical input still validates but cannot override replay. The independent
control-frame domain and UI/text/IME are not recorded. After the last frame,
gameplay input becomes neutral; previously held actions receive one release.
Cancel likewise neutralizes input. Physical input stays excluded until a fresh
launch, preventing a canceled or finished tape from unexpectedly handing a held
key back to gameplay.

The existing offline game/audio movie workflow can admit a saved replay before
tick one. Its media clock continues to schedule the actual fixed ticks; the tape
does not introduce a second clock. Longer movies use neutral input after the
tape ends. The movie folder retains a copy and receipt identity for the selected
take. Interactive physical input remains unavailable during offline capture.

This is input fidelity, not arbitrary game-state determinism. Native random
values, external storage, asset timing, control callbacks, physics, asynchronous
work and other external state can affect results. Recordings do not restore
arbitrary C++ state or support cross-build replay. The version-2 profile retains
game-requested Scene visits; attaching to a live run remains unsupported. Movie rendering does not
establish that other game-state effects match the original interactive run.

## Editor, automation and files

The Game view's Input recording controls launch the existing Play owner paused,
begin capture/replay, then resume after acknowledgement. Finish and save retrieves
the acknowledged completed document and uses the shared contained file codec.
Files live at `Recordings/<name>.json` within the active project. The folder is
created on Save. Existing files require explicit replacement. These ordinary
recording files are outside the asset catalog and do not create Scene Undo.
Cached takes belong to the project that launched Play; switching projects hides
them and blocks cross-project reads or writes.

Automation `play.start` accepts `paused: true`. Generation-checked
`play.input_recording.control` submits begin, finish, read or replay. Replay takes
a project-relative recording path. `play.status` reports mode, tick count,
pending operation, document readiness and errors. `play.input_recording.save`
requires execution and asset-write grants; normal automation revision/sequence
handling and MCP discovery apply. Both protocol2 hosts call the same operation
and core owner. Read returns a separate bounded document response instead of
duplicating a potentially large Scene snapshot.

See the [usage guide](../manual/editor/input-recording.md) and
[fixed input contract](input.md).

## Control capture provenance

The guide's input-recording image is an unretouched 700×700 capture of the
production control widget after an actual Across Scenes record/read/save workflow, rendered
through ImGui/SDL software draw data. The native fixture exercises both Play
hosts at 1180/700/400 widths, SDK session capture/cancel, fresh replay, input-consumer counts, project switching
and authored-source isolation. The capture shows one 700-wide viewport; it is
not a full-editor or Windows/physical-device acceptance image.

Canonical image: `manual/images/input-recording-controls.png`; SHA-256: `7c8f57d3d5536997aa0423b2622b21d73b0209072714fb9d8e98850ea708cd37`. The movie-options guide uses the current production
Diligent control capture documented in [game recording](game-recording.md).
