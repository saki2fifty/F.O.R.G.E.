# Authored transform animation

The editor timeline authors reusable object-transform clips. Skeletal and model
animation continue through the existing [Ozz integration](animation.md). Both are
owned by `AnimationRuntime`; this feature adds no dependency or service manager.

## Source, publication and identity

`TransformAnimationSource` reads `forge.transform-animation` versions 1, 2 and 3 in a bounded
2 MiB `.animation.json` document. It stores its AssetId, a 1,000,000-unit-per-second
integer timebase, positive duration up to one hour, 1–4096 sorted keys at unique
times within duration, and, for versions1/2, at least one qualified channel:
`forge.local_translation`, `forge.local_rotation`, or `forge.local_scale`.
Each key has a stable UUID, integer `time_us`, full local pose and `linear` or
`held` interpolation. Quaternion rotations must be finite and normalized;
translation is bounded to ±1e9 and scale follows the existing signed/zero visual
scale contract. Version 2 adds an explicit `curves` object on a key: `interpolation`
(`linear` or `cubic`), plus `incoming`/`outgoing` objects containing three-component
`translation` and `scale` slopes in local units per second. Omitted curves retain
linear vectors; a held key overrides its outgoing curves without erasing slopes.
Curve-bearing edits promote source to version 2; older readers reject that version
instead of ignoring curve data. Pose updates and retiming preserve curve metadata.
Invalid edits are rejected before source/history replacement.

`TransformAnimationDocument` reuses `AssetSourceDocument` for writer ownership,
revision guards, atomic save, on-disk conflict checks and bounded history. The
central Timeline registers with `DocumentWorkspace` and `AssetEditors` under
ADR 004. Global Save/Undo/Redo routes to the focused document. Scene attachment
uses existing atomic authoring commands and creates one Scene Undo entry.

## Key selection and atomic batches

`move_keys`, `duplicate_keys` and `erase_keys` operate on 1–4096 distinct existing
key UUIDs. They validate the original source, build one final candidate and validate
its count, unique times, complete curves, segment bounds and byte budget before
replacement. Offset bounds use subtraction before addition, including extreme
signed integers. Moving an overlapping group validates final times rather than
intermediate single-key moves. Moves and copies preserve complete key objects,
including held mode, slopes and unknown extension fields. Duplication uses fresh
AssetIds; deleting all keys is rejected. Single-key retiming delegates to the same
atomic path and preserves those fields. Source/cooked formats remain versions1/2.

Timeline selection is ephemeral stable UUIDs, with one active key for fields. Ctrl
click toggles; plain list clicks select one; a ruler click on a selected key keeps
the group. Add/Update selects the authored key after a successful source revision.
The existing `TransformAnimationDocument` owns one history entry per batch and
prunes obsolete selections after source changes. Undo/Redo preserve duplicated
identities; selection itself is not saved into the source or Scene.

A group drag captures IDs, source revision, original time, pointer and group bounds.
Held frames draw temporary ruler positions without replacing or copying the source.
Relative offsets snap to1ms after the drag threshold; a click preserves fractional
millisecond times. The final release position is sampled even without an intervening
held frame. Offset clamping preserves spacing inside the duration. Escape, Play,
pending publication, focus loss, close, project or revision changes cancel capture.
Scene preview motion evaluates the applied clip until release. Numeric offsets are
bounded to ±3600seconds, then checked against every selected time. No ripple edit
or motion-preview retiming claim is made.

### Timeline view and rectangle selection

The ruler has an ephemeral microsecond time viewport, bounded to the admitted clip.
**Fit clip** restores the full duration; **Fit selected keys** adds a margin around
selected times (including a single key). **Zoom in/out** use the view center;
Ctrl-wheel anchors zoom at the pointer; middle-drag pans horizontally. View spans
stay between one microsecond and the clip duration. Scrubbing and group movement
map through this same viewport; group movement retains its1ms snap and clip bounds.
Duration changes reconcile the viewport, retaining full-duration framing when it
was full. Only visible keys/playhead are drawn or hit; ruler output is clipped.

Shift-left-drag selects intersecting visible key-diamond bounds in enabled channel
rows. Ctrl adds to the original selection; Alt removes, taking precedence over Ctrl;
otherwise the rectangle replaces selection. Each key UUID participates once even
when shown on several rows. Held selection uses editor state, not source/history;
an unchanged pointer does not rebuild it. Release samples the final position and
chooses a surviving active key, or clears it when no keys remain selected.

Captures retain revision, geometry and baseline view/selection. Escape, source or
geometry changes, focus loss, popup, Play, publication, close, Save or Undo cancel
and restore the relevant baseline. View/selection controls share current Play and
publication guards. View and selection are not serialized and do not request
publication, add document Undo, or alter runtime/exported motion.

### Whole-clip retiming and frame placement

`retime_duration` scales every key time and the clip duration together. The Timeline's
**Retimed duration (seconds)** field is pending until **Scale whole clip timing**;
the existing **Duration (seconds)** field continues to change only the end time.
Known position/scale incoming/outgoing slopes multiply by old/new duration, retaining
the normalized Hermite motion when key times scale exactly. Key identities, poses,
held mode, channels and opaque fields remain. Exact unsigned rational arithmetic
rounds key times to the nearest microsecond, with positive half ties upward; rounding
can slightly alter segment spacing. Duration remains positive and at most one hour.
Coincident keys, excessive slopes or invalid interior curve bounds reject the edit.

`snap_keys_to_frames` places 1–4096 distinct selected keys on a whole1–240 FPS grid
starting at time zero. **Frame grid (FPS)** and **Snap selected keys to frames** use
the existing key selection. The nearest frame index is rounded first, then its time
to integer microseconds, with positive half ties upward at both steps. Only selected
key times change: slopes retain their units/second, so curved motion can change.
Final admission rejects collisions, times beyond duration or invalid curve bounds
without clamping or merging. Aligned keys are a no-op. The grid is transient editor
state; it does not set playback speed, runtime frame rate or a saved source field.

Each operation uses one existing document Undo step after complete candidate admission,
including pending curve fields. Play, publication and active drag guards still apply.
Save & Publish uses the existing CPU cooker, runtime sampler and export selection.
No new source/cook version, runtime owner or native SDK interface is introduced;
the authoring API header is outside the exported SDK header set.

## Selected key value transforms

`transform_key_values` validates 1–4096 distinct existing key UUIDs, then transforms
selected local translation as `value * XYZ multiplier + XYZ offset` and selected
local scale as `value * XYZ multiplier`. Existing incoming/outgoing translation and
scale slopes multiply by their corresponding lane multipliers. This preserves the
affine vector curve shape when every key is transformed; a partial selection changes
neighboring transitions. The origin is the local channel origin, with no hidden pivot
or world-space offset. Signed multipliers reflect values; zero makes a channel constant.

Key time, quaternion rotation, held mode, curve interpolation, channel masks, IDs and
unknown fields remain unchanged. Omitted curves remain omitted; stored disabled
channels transform but keep their playback masks. Controls must be finite within
±1e9. A full candidate must still satisfy position, signed/zero float-scale, slope,
interior curve and byte bounds. Overflow or nonzero scale underflow rejects the entire
operation before source/history replacement. Identity inputs validate selection and
are a no-op. Source/cooked versions and installed native SDK interfaces are unchanged.

Timeline **Selected key values** fields are temporary until **Apply selected key
values** commits one existing document Undo step. **Reset value adjustments** resets
only those fields to multipliers1/offsets0. Empty selection disables Apply; Play,
publication and captured key/curve/box/pan gestures disable the controls. Active key
fields and isolated preview refresh from the admitted clip. The preview status row
keeps its height when preview starts/stops, so the first key drag does not move the
ruler and cancel itself; Save & Publish supplies
Play and export through the existing owners. Gallery47 compares original motion,
changed position and changed scale using selected Windows-target CPU cooks.

The trusted CPU importer `forge.animation.transform` / `forge.animation.transform.v3`
publishes a root `transform_animation` asset and one `animation.clip` artifact.
It publishes `forge.transform-clip` version 3, validated by the same bounded
parser/evaluator. Previously selected version-1/2 artifacts remain readable without
rewriting their catalog or DDC records. Runtime and export require the cooked root
version to match the selected output version. Source and cooked readers allow
500,000 parser events, depth 12 and 2 MiB, including the 4096-key curve profile.
Source bytes, stable key IDs, selected recipe, file hashes and
size are covered by the normal sidecar/catalog/DDC publication. Target recipes
are Windows/D3D12/desktop and Linux/none/cpu. There are no runtime asset dependencies.
Asset move/copy uses the existing authored-document adapter; copies mint the
new asset identity and retain key IDs scoped to that new clip.

Clips contain no scene/entity name or index. `forge.transform_animator` attaches a
typed clip AssetRef to its own persistent scene EntityId. Rename, reparent, scene
persistence and prefab overrides use existing component ownership. Each attachment
has independent transient playback state. Channels overwrite local values; a
channel omitted from the clip preserves the object's current local value.

## Evaluation and preview

### Typed camera, light and audio properties

Source version3 optionally binds `properties` to unique qualified names:
`forge.camera.vertical_fov`, `forge.camera.orthographic_height`,
`forge.light.intensity`, `forge.light.color_r`, `forge.light.color_g` and
`forge.light.color_b`, `forge.audio_source.gain` and `forge.audio_source.pitch`.
Audio gain is linear0–4; pitch is a playback speed/pitch multiplier0.25–4.
Every key has a `values` object containing exactly those
names and finite numeric values. FOV is strictly0–pi radians; orthographic height
is positive through1e9; intensity is0–1e9; colors are linear0–1. A clip may omit all
transform channels when at least one property is bound. Missing components reject;
animation does not create Cameras/Lights. Unsupported bindings, duplicate/missing
or undeclared values, and property syntax in versions1/2 reject explicitly.

These typed built-in bindings use current native Camera/Light/AudioSource members
and existing scalar/domain validators. They are not a general custom-component/material/UI binding
registry. No native component layout, installed SDK input or callback changes.
Source/cooked version3 is explicit; selected old artifacts remain readable. New
publication uses recipev4 and cooked3 even for a legacy transform-only source.
Selected previous cooks remain readable; expanding the qualified-name binding set
does not change the cooked3 structure or renumber the original six bindings. Older
engines reject unsupported audio names rather than partially executing such clips.

Properties share existing key times/UUIDs and outgoing held/linear timing; vector
cubic slopes do not affect them. Adding a property initializes every key from the
chosen object's authored value. Key insertion defaults to the sampled property
values unless values are supplied; replacement without values preserves them.
Retiming/moving/duplicating keys and transform-value/slope edits retain property
values and key extensions. Removing the last property from a property-only clip
rejects. Numeric fields are temporary until Add / Update commits one source Undo.
FOV displays degrees while serialized/native values stay radians. Colors stay linear.

Fixed application builds typed Camera/Light/AudioSource candidates alongside any transform
candidate, validates all players before writes and updates only bound members.
Property-only application avoids building a transform map or claiming movement.
Existing AnimationRuntime clock/revision/resource/recovery ownership is unchanged.
Copied presentation patches sample between previous/current semantic tick times,
including continuous loop wrapping; held/nonseamless/replacement boundaries snap.
Reset/recovery collapses presentation endpoints. Extraction never writes live ECS.
Runtime scene snapshots contain animated values as expected for recovery; saved
Scene/clip files and authoring history are unaffected by playback.

Timeline uses current source-document owner, property checkboxes/fields and dynamic
ruler rows. Missing preview components preserve the input copy, stop preview and
report an error. Scene View retains its editor camera: camera FOV preview changes
the copied camera/frustum, while Game Play displays the animated camera image.
For Light preview, disable Scene's Preview light to use authored scene lighting.
Gallery49 and relocated source-free export exercise camera zoom and colored light.

Audio candidates copy and validate the complete current source before changing
gain/pitch. These scalars share the existing admitted profile and do not alter clip,
playback, looping, spatial or channel configuration. The post-tick AudioRuntime
synchronization updates its retained voice; it does not restart sound when these
values change. Common channel/master volume still multiplies source gain. Timeline
preview only substitutes copied values; sound runs in Play and never from render
presentation. Automation follows the existing fixed animation clock and audio pause
policy, with no sample-accurate or pitch-preserving time-stretch claim. Gallery53
exercises gain/pitch with a matching light pulse and a FORGE-owned tone.

Position/scale use linear or original cubic Hermite interpolation. Slopes are
scaled by the segment's duration in seconds; zero slopes ease into and out of a
segment. Validation checks endpoints and all interior derivative roots against the
existing translation/scale bounds before a candidate is committed or published.
Signed and zero visual scale remain valid; existing physics pose validation still
applies at runtime. Rotation uses shortest-path normalized
quaternion slerp. A held key preserves its pose until the next exact key boundary.
Sampling clamps before the first and after the last key. Runtime/preview loop time
covers `[0,duration)`; nonloop playback holds duration and ends. Time accumulation
uses double microseconds while authored key times remain integer microseconds.
Speed is 0–4; there are no reverse, event, blending, bone-editing or IK tracks.

### Vector curve authoring

Timeline's **Vector curves** section edits position/scale slopes on the active key.
The selected-axis view samples the same evaluator 193 times across that key and its
neighbors. Blue is the applied curve; gold guides use pending slope fields and
intersect the true slope with the view boundary. **Fit curve** fits the applied
values; **Fit curve and handles** also includes finite tangent endpoints. These
controls only change the view. Rotation has no cubic tangent controls.

Drag a visible gold handle vertically to edit one incoming/outgoing slope on the
chosen channel and axis. Its guide uses one third of the neighboring time interval;
key time/value and other slope components stay fixed. Capture records stable key
identity, source revision, channel/axis, original pending fields and view mapping.
Held frames change temporary fields only; release samples the final pointer and
commits one source Undo step after complete curve validation. Preview evaluates
the applied clip until release. Escape, Undo, focus loss, Play, close, publication,
document/revision changes or a changed view cancel capture and restore prior fields.
Incoming handles require a cubic, non-held preceding segment; outgoing handles
require the active key's cubic, non-held segment and a following key.

Pending numeric curve fields participate in dirty/Save/close handling. **Apply
curve settings**, **Save & Publish**, another source edit or changing key selection
commits them through the existing document/history owner. Invalid fields retain
the prior source and remain editable. Undo first cancels pending fields. Preset
buttons deliberately replace pending slopes. Pose/curve edits preserve opaque key,
curve-root and tangent-side extensions.

**Flat tangents**, **Linear tangents** and **Smooth tangents** atomically edit all
distinct selected keys using the original neighboring poses/times. Selected keys
gain cubic vector settings; key identities, times, poses, rotation and Hold remain.
Flat sets zero slopes. Linear uses each side's neighboring secant. Missing endpoint
sides copy the available secant; a lone key uses zero. Smooth uses a common slope
on both sides, `(left_secant * right_interval + right_secant * left_interval) /
(left_interval + right_interval)`, bounded to three times the smaller secant's
magnitude. Opposite signs or a zero secant give zero at an extremum or plateau.
Endpoints use their one available secant. The complete candidate must satisfy the
existing slope/interior-value limits before source replacement. Source/cooked
versions, import recipe and runtime evaluator remain unchanged.

Timeline preview substitutes local components in a copied scene and
recomputes its render transforms through `Scene::preview_document`. It never
writes the authored ECS, physics world or Scene history. Preview target is an
EntityRef; close, project/scene switch, Play or target deletion stops preview.
Viewport authoring gestures stop preview before starting a competing operation.

## Runtime, failures and recovery

A private `TransformAnimationPlayback` helper inside `AnimationRuntime` borrows
its published catalog, fixed animation phase and physics pose validator. Its typed
ResourcePool asynchronously admits immutable cooked clips with normal selection,
recipe and digest checks. It limits active players to 256 and cached resources to
64 identities / 16 MiB. The authoring world registers the schema without loading
clips. Runtime does not require source importers or editor code.

Held-key boundaries, asset replacements and nonseamless loop boundaries use the
existing runtime presentation snap mechanism. Seamless loop endpoints retain
normal local-pose interpolation.

Pause does not tick; Step advances one fixed tick. Play on Start is consulted when
a clip/entity attachment is realized; changing it later does not restart the
clock. Clip changes restart only after successful admission. Failed replacement
retains the previous admitted clip and reports a diagnostic; incomplete or invalid
players prevent a complete recovery checkpoint. A newly adopted clip also waits
for its first successful pose application before recovery can be saved.

Application builds a complete candidate transform map and invokes the existing
physics validator before writing any enabled channel. Dynamic Physics Bodies,
Character Controllers, active navigation agents, skeletal Animators and animated
model-node bindings cannot share a target with enabled transform channels. Property-only attachments
do not claim movement ownership and can coexist with those controllers. A rejected
pose reports `animation.transform` and retains the previous world pose.

The existing private animation checkpoint v1 gains optional `transform_entries`:
EntityRef, clip AssetId, exact selected revision, `time_us` and playing state.
Recovery validates target/configuration/count/revision/time and candidate poses
before replacing playback clocks. Older checkpoints lacking this field remain
valid when there are no realized Transform Animators. The transform-animation authoring header is outside the installed native-module
header set in `cmake/native_sdk.cmake`. These authoring API/curve additions do not
change the module ABI fingerprint or require a gameplay module rebuild by
themselves. Modules still require an SDK matching the actual installed headers,
bridge semantics, compiler and configuration when those inputs change.


## Standalone content

The normal scene-reference scanner discovers Transform Animator's typed clip
field. Runtime content admits the cooked format, includes selection metadata and
hashes, and omits the authoring source. Relocated packages use the same evaluator
and loader. Windows ZIP assembly includes `.clip` Gallery data and the normal
manual. The Gallery's `08-transform-animation` scene demonstrates a looping local
position/rotation/scale clip on a platform.

Gallery `13-transform-curves` compares the unchanged legacy linear clip with two
original Windows-target version-2 clips: smooth easing and custom vertical arc
slopes. Parent lanes separate their local motions. All three use normal scene
references, independent clocks and source-free cooked export.

Gallery `18-animation-key-batches` compares two eased keys, their one-second shift,
and complete copies offset by three seconds in a six-second loop. The three original
Windows-target sources use the normal publication owner and selected cooked roots.
The gallery test independently computes cubic vector and linear shortest-arc
rotation values through runtime ticks and relocated source-free export.

Gallery `45-animation-timing` uses three original eased clips: four-second timing,
an eight-second whole-clip retime, and key placement on a two-FPS grid. The timing
test independently evaluates every segment and loop through runtime ticks and a
relocated source-free package. Organizational lanes explicitly render no primitive;
the scene keeps three animated platforms, six markers, a floor, camera and light.
