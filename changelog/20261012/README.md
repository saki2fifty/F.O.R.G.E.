# 2026-10-12

## Audio import processing

- WAV import settings support Force mono, Normalize peak, Trim silence and a
  finite dB threshold. Defaults preserve original samples; processing occurs
  once in the existing isolated cooker without changing source WAVs.
- Reimport updates selected channels/duration and preview waveform. Draft edits
  release the old preview; existing reset/reload and last-good publication apply.
- Original editable Gallery34 compares stereo with processed mono. Export ships
  the selected processed PCM and runs without development WAVs.
- Direct/worker, strict sanitizer, actual scaled controls, publication and
  source-free playback checks cover the processing path. Native images and
  physical listening remain separate acceptance evidence.

## AudioClip editor preview

- AudioClip tabs can Play/Restart, Stop and Loop a clip without game Play, with
  temporary preview volume independent of saved game mix defaults.
- Show waveform displays separate mono/stereo envelopes without opening sound
  output. Pause/Resume, waveform-click seeking and a release-to-seek position
  control preserve preview-only state and retain the output device/decoded clip.
- Preview controls precede collapsed import settings. Closing or changing clips
  clears the envelope; stopping sound keeps its overview visible.
- One isolated preview uses FORGE's existing audio provider and published clip
  admission; close, selection/revision/project changes and Play/import locks
  release its sound/device/cache. Selecting a clip does not autoplay.
- Original offline PCM, strict sanitizer, explicit test-device lifecycle and
  actual scaled controls cover loop/completion/restart/volume and authoring
  isolation. Native preview images are configured; physical listening is separate.

## Timed animation exits

Named states can advance to another stable state ID at a normalized exit time,
using the existing incoming fade and C++ selection path. Source controls/history,
original Gallery 30 and source-free exports share the same implementation.
Legacy manual source/cooked assets remain readable; new recipe v4 wraps the original
source in cooked version 2. Private animation checkpoint 2 preserves automatic
selection on partial prefab overrides without changing authored intent. Installed
SDK ABI/table/layout/fingerprint are unchanged. Native Windows execution, visual
review and physical acceptance require separate evidence.

## Elastic distance joints

Distance links now expose Spring Frequency and Spring Damping. Frequency 0 keeps
hard limits; positive frequency gives an elastic rest length or free interval with
soft bounds. Existing controls retain native geometry/order and support typed C++
updates/recovery. Inspector history and selected guide use the same values.
Original Gallery 29 compares oscillating and critical return, with source-free
export. Matching SDK rebuild required; ABI 1/private 4/authored formats unchanged.
Windows delivery/visual/physical acceptance require separate evidence.

## Swing-and-twist joints

Swing Twist adds limited ball-and-socket motion, two swing cone half-angles and
signed twist limits. Inspector controls/history and selected Scene cone/twist
guides use existing owners; original Gallery28 compares two gravity-driven
pendulums and supports source-free export. C++ uses the same typed settings.
Matching SDK rebuild required; ABI1/private4/authored formats unchanged.
Builtin reflection annotation now accepts components larger than a single pinned
descriptor batch while retaining physical-layout validation. Local/native Windows
execution and physical acceptance are recorded separately.

## Connected-body collision policy

All joint kinds expose **Collide connected**, enabled by default for compatibility.
Turning it off suppresses only that connected pair; ordinary floor/other contacts
and query visibility remain. Existing owner applies changes before native contact
solving and retains bodies/constraints/order. Inspector history, guide status,
typed C++ controls/recovery and original editable Gallery27 are integrated.
Matching SDK modules require rebuilding; ABI1/authored formats/private4 unchanged.
Local coherent validation and Windows delivery are tracked separately.

Windows166 passed core/SDK/editor checks but its final export rejected an unsupported
alpha field on two PhysicsVolumes example tints. The example now uses the actual
RGB contract, with compiled-schema source-free content export coverage. Unknown
dependency-field rejection is preserved. No new accepted ZIP is claimed here.

## Bounded hinge and slider velocity motors

PhysicsJoint supports optional velocity drives for Hinge and Slider. Hinge speed
uses degrees/s with a maximum torque; Slider speed uses m/s with a maximum force.
Zero target speed brakes within that budget, while zero budget allows coasting.
Inspector controls, Undo/save/reopen and selected Scene guide status use the
existing joint owner. C++ can change the same typed fields before Physics without
replacing the native constraint. Legacy documents default to disabled motors.

Original Gallery25 contains a force-limited slider and torque-limited hinge for
Play and source-free export. Matching SDK modules must be rebuilt because the
component layout changes. Authored formats, ABI1 and private checkpoint4 remain
unchanged. Windows delivery and physical review are pending; this entry does not
claim a new delivered ZIP.

## Bounded joint position drives

Hinge and Slider can return to an angle or signed displacement using bounded
force/torque, spring frequency and damping. Inspector mode selection retains hidden
values, supports Undo/save/reopen and adds selected Scene target markers. Active
targets outside joint limits are rejected before native mutation. The existing
owner retains native constraint geometry/order when controls change; C++ edits use
the same typed component before Physics. Legacy controls default to velocity mode.

Original Gallery26 returns a slider to-.75m and hinge to-45degrees and supports
source-free export. Fixed-step implicit integration adds damping even at authored
ratio zero. Matching SDK modules require rebuilding; authored formats, ABI1 and
private checkpoint4 remain unchanged. Windows delivery and physical review are
pending; no new delivered ZIP is claimed.

## Spatial-audio distance falloff

Audio Source adds **Distance falloff** (Inverse, Linear, Exponential) and **Falloff
strength**0–10. Existing scenes keep inverse/1 defaults. Edits update the current
voice without restarting it; strength0 keeps positional panning with no distance
loss. Distance limits clamp the curve rather than defining a universal mute radius;
equal limits disable distance loss. Inspector Undo/Redo and independent prefab
override intent use the existing authoring system.

Original Gallery31 includes three colored emitters, a camera/listener and a
synthetic WAV, with cooked source-free export. Offline PCM equations, compiled
and installed-only SDK consumers, Inspector history/save/reopen and relocated
gallery playback form the local gate. Public AudioSource layout/header fingerprint
changes require matching C++ modules to rebuild. ABI1 tables, authored scene
versions and the existing autoplay-from-frame0 private recovery policy remain.
Native Windows delivery, image review and physical listening are pending.

## Directional spatial sounds

Audio Source adds optional **Directional sound**, full inner/outer openings and
**Outside volume**. Emitters follow effective local negative Z, including parent
rotation and explicit/world spaces. Angular gain combines with distance falloff;
updates preserve a playing voice. Old scenes default to omnidirectional sound.
Degenerate emitter forward mutes spatial output until corrected without retiring
the voice. Existing prefab masks and Inspector Undo/Redo remain the authoring owners.

Selected sources have an editor-only sound marker, arrow and bounded spherical
angular guides. Original Gallery32 reuses the tone and exposes parent-directed
front/back comparison with cooked source-free export. Independent PCM equations,
scaled actual controls/history/save and matching C++/recovery consumers validate
the integration. Public component layout/fingerprint changes require C++ rebuild.
ABI1 tables and existing private autoplay recovery remain. Windows image review
and physical sound acceptance are pending.

## Common audio channels

Audio Sources can route through Master, Music, Effects or Dialogue. Shared game
defaults and saved player preferences expose four independent volume controls.
Master affects the entire mix. Volume changes and route edits retain playback;
zero category volume mutes while sound time continues. Flat gain mixing uses the
existing voices and gameplay Master owner. All channels follow gameplay pause.

Original Gallery33 plays three pitches from the original tone and supports cooked
source-free export. Actual PCM, settings rollback/scene inheritance, scaled controls
and matching C++ consumers form the gate. Existing scenes default to Master;
legacy game settings resolve category volumes to1 without rewriting source files.
The exact SDK component fingerprint/layout changes require matching C++ rebuild.
Windows rendering and physical listening remain separate acceptance work.
