# Authored transform animation

The editor timeline authors reusable object-transform clips. Skeletal and model
animation continue through the existing [Ozz integration](animation.md). Both are
owned by `AnimationRuntime`; this feature adds no dependency or service manager.

## Coordinated Scene sequences

[ADR021](decisions/021-animation-sequencing.md) records the sequence ownership and
Prefab boundary. `AnimationSequence` is a native controller with enabled/start
intent, duration1µs–3600s, speed0–4 and loop. Up to64 enabled controllers reuse the
existing transform provider's clocks. The existing256-player/resource limits stay
in force; no sequence asset, loader, registry or additional clock service is added.
`TransformAnimator.sequence` is an optional typed EntityRef and
`sequence_start_seconds` is a finite start offset within the controller's duration.
Absent fields backfill null/zero without rewriting old sources.

Resolve the controller in the target's loaded Scene membership. Wrong type,
missing/ambiguous references, invalid offsets and a controller that is also an
active skeletal/transform player report errors. A disabled controller retires
its tracks until re-enabled. Sequence configuration overrides each bound player's
retained standalone loop/speed/start intent. Sample a track at
`clamp(sequence_time - start, 0, clip_duration)`. The first/last poses hold outside
that span; the sequence end may truncate it. A group wrap/restart/seek snaps copied
presentation endpoints, and held transform crossings reuse the standalone policy.
Scalar presentation samples shared group time across offset boundaries.

The current complete transform/property candidate and writer-conflict checks run
before any pose write. Existing AnimationService status/control/seek operations
accept controllers and standalone TransformAnimators. Bound players expose status
but reject independent control/seek. Controller status has no clip AssetId; the
native ABI represents it with an empty `clip_uuid`. Existing fixed-thread, phase
and current-tick guards remain. C ABI1 tables do not change; native component data
and the exact installed SDK fingerprint do change, requiring module rebuilds.

Private animation checkpoint3 includes exact sequence configuration, clocks and
track bindings. Restore validates the complete candidate before adoption. Old
versions1/2 remain readable for configurations without sequence controllers.
Neither Scene/Prefab source versions nor animation clip/cook formats change.

The central Timeline's sequence mode borrows Scene Save/history. Numeric start
edits are pending until Apply; they use one Scene transaction and reject stale
revisions. Pending fields block destructive track changes and have explicit close
choices. Clip selection uses the existing typed asset picker. Refresh constructs
a private animation world with the existing CPU Resources provider from the Scene snapshot, preserves relevant writer
guards, admits published clips and reuses runtime seek. Preview merges supported
native fields into a copied Scene view, preserving opaque data and original
authoring/history. No gameplay module, audio output or physics simulation runs.
Scene revisions stop the preview; project/Scene identity changes close it.

Scene/subtree copies remap only declared EntityRefs, including bindings overridden
on prefab instances. Unknown payloads remain untouched. [ADR022](decisions/022-prefab-component-references.md)
defines portable source3 Prefab member links and their per-instance Scene resolution,
including sequence arrangements. Scene-owned bindings on ordinary prefab instances
remain supported and persistent.
Export closes over the existing saved Scene, selected clip cooks and typed audio
references; it does not ship an editor or require a compiler for sequencing.

## Sequence camera shots

`CameraShot` is a Scene-owned native component with `sequence` and `camera`
EntityRefs, `start_seconds`, `end_seconds` and `enabled`. An unbound shot is inactive.
The existing transform provider prepares up to 256 enabled/bound shots per world;
both references resolve in the shot's loaded Scene membership. The controller must
be an AnimationSequence and the target a Camera. Intervals require finite
`0 <= start < end <= controller duration`. Each loaded Scene admits one enabled
shot director, with nonoverlapping intervals; other object-only controllers remain
independent. Invalid arrangements report diagnostics and provide no shot override
for that Scene. Cross-entity arrangement errors block initial animation admission
and leave the authored source unchanged.

Selection samples the existing controller's presentation time. Intervals are
start-inclusive/end-exclusive. A shot ending at sequence duration also holds at
that final nonlooping time. Loop discontinuities snap with the existing clip
presentation; there is no cross-camera blend. Gaps and disabled controllers restore
ordinary camera composition. Only copied Camera.enabled values change: the chosen
camera becomes enabled, including a disabled standby camera, and the other cameras
in that membership become disabled. Native Camera components, source files and
authored history remain unchanged. Lens/transform/resource admission stays with
the existing render and animation owners.

Timeline borrows Scene history for creation, camera/interval edits and removal.
Creation uses the existing `entity.create` recipe command with initial component
values, so a bound shot is one transaction. Pending edits retain their Scene revision
and reject stale commits. The existing isolated preview projects the same copied
selection; Game can render it while Play is stopped, with an explicit preview label
and without running runtime UI, native gameplay, sound or physics. Stop/close/Scene
changes restore the authored view.

Private animation checkpoint4 records exact active shot identities, references and
intervals alongside the existing sequence clocks. Nonshot configurations keep
checkpoint3; versions1–3 reject recovery when active shots cannot be described.
Shot mismatches reject before clock adoption. Scene/Prefab/clip formats and ABI1
function tables are unchanged. The installed animation component header changes the
exact SDK fingerprint; rebuild native modules with the matching SDK. ADR021/022
retain clock and portable-reference ownership. Gallery68 demonstrates cuts, a gap,
coordinated object motion and normal source-free export.

## Per-object material animation

Source5 adds up to64 `material_bindings`, each identified by the stable mesh
`slot`, parameter `key` and exact numeric semantic `type`. Types are Scalar,
Vector2/3/4 and LinearColor3/4; equal widths do not make a vector and color
interchangeable. Every key has one `material_values` entry per binding, matched
by(slot,key), containing a width-sized `value` and optional width-sized `curves`.
Each lane's curve stores `interpolation` (`linear`/`cubic`) and finite native-unit
`incoming`/`outgoing` slopes. Missing curves mean linear. A held key overrides
outgoing interpolation without deleting slopes. Values/slopes use the existing
finite ±1e9 generic source bounds, including analytic interior extrema.

Bindings and every key's entries must be unique and complete. Missing, duplicate,
extra, wrong-width or wrong-semantic values reject the whole source/edit. Dynamic
material key storage participates in the existing resource residency budget; old
keys do not gain fixed64-entry arrays. Sampling writes a caller-provided bounded
buffer. Key insertion, movement, duplication, deletion and retiming preserve tuple
identity and extensions. Retiming rescales known material slopes. Bulk curve edits
and Flat/Linear/Smooth presets validate one detached candidate and use one document
history entry; unknown fields remain untouched. Explicit promotion rejects reserved
field collisions. Source1–4 material-like extension fields remain opaque; cook4
cannot contain a source5 document. No track UUID or Scene/entity identity is added.

The existing transform playback provider owns material clocks, controller timing,
writer checks, presentation and recovery. Material tracks require an existing
MeshRenderer and CPU Resources provider. Old transform/property clips still work
without Resources. Animation deduplicates its subscriptions by unique(kind,asset)
and retires unused tokens before requesting replacements, within the shared256
observer bound. Existing Resources owns loading/adoption/retained mesh/material
leases; no animation-specific pool or replacement catalog is introduced.

Whole-clip target admission reuses the per-object numeric material projector and
the actual selected mesh slots/part-specific material variations. It checks exact
semantic declarations and every analytic lane extremum against the selected
material's supported domain and fixed render passes. This proof is cached by clip,
resource selection and nonanimated numeric intent, rather than repeated every tick.
Each sampled complete numeric candidate still passes that same projector before
any material, builtin-property or transform write. Pending or incompatible target
changes preserve the last accepted complete values and report the existing pending/
error status. Values never change a shared material resource, texture, shader,
feature or render-pass selection. Existing sequence/seek/loop/recovery boundaries
apply to the same material samples; no separate timeline clock is stored.

Timeline uses the existing async Inspector's common selected material definitions.
Tracks, typed key fields and ruler rows retain(slot,key); curve selection adds a
lane, including RGBA alpha and Vector4 W. Removing another track cannot rebind that
selection by ordinal. Capture uses current authored object intent or the selected
material's default, never a temporary preview. Graph handles, cancellation and
history reuse the existing curve editor. Preview preflights a detached renderer and
merges numeric rows into a copied Scene view by tuple, preserving unknown row/value
extensions. Pending loads show authored values; invalid targets stop preview without
partial edits. Play locks authored controls; close/project changes retire preview
resource ownership. Sequence preview uses the same existing Resources/animation
providers and merges the same copied numeric fields.

Scene/Prefab format and ABI1 tables do not change. The private retained-resource
interface changes an existing exact SDK implementation hash input, so rebuild native
modules with the matching SDK. The ordinary TransformAnimator and AnimationService
control/status/seek API drive material clips too. Gallery67 includes independently
animated objects, an ordinary Prefab instance and an unchanged shared-material cube;
export closes over the saved Scene/Prefab/material and selected cooked5 clips.

## Source, publication and identity

`TransformAnimationSource` reads `forge.transform-animation` versions 1 through 5 in a bounded
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
atomic path and preserves those fields, including version-4 property curves.

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
Known position/scale/property incoming/outgoing slopes multiply by old/new duration, retaining
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

The trusted CPU importer `forge.animation.transform` / `forge.animation.transform.v7`
publishes a root `transform_animation` asset and one `animation.clip` artifact.
It publishes `forge.transform-clip` version 5, validated by the same bounded
parser/evaluator. Previously selected version-1/2/3/4 artifacts remain readable without
rewriting their catalog or DDC records. Runtime and export require the cooked root
version to match the selected output version. Cooked4/5 are versioned envelopes
containing the original source document, including its source version. This keeps
older opaque fields opaque when republishing. Legacy cooked1/2/3 retain their raw
document readers. Source admission allows 500,000 parser events, depth12 and2MiB,
including the4096-key curve profile. The cooked4/5 envelope has128bytes of allowance
and one extra parse level; its enclosed source still passes the original limits.
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

### Typed camera, light, audio and particle properties

Source versions3/4 optionally bind `properties` to unique qualified names:
`forge.camera.vertical_fov`, `forge.camera.orthographic_height`,
`forge.light.intensity`, `forge.light.color_r`, `forge.light.color_g` and
`forge.light.color_b`, `forge.audio_source.gain`, `forge.audio_source.pitch`,
`forge.particle_emitter.size_multiplier`, `forge.particle_emitter.tint_r`,
`forge.particle_emitter.tint_g` and `forge.particle_emitter.tint_b`.
Audio gain is linear0–4; pitch is a playback speed/pitch multiplier0.25–4.
Particle size multiplier is0.01–16; tint factors are linear0–1.
Every key has a `values` object containing exactly those
names and finite numeric values. FOV is strictly0–pi radians; orthographic height
is positive through1e9; intensity is0–1e9; colors are linear0–1. A clip may omit all
transform channels when at least one property is bound. Missing components reject;
animation does not create required components. Unsupported bindings, duplicate/missing
or undeclared values, and property syntax in versions1/2 reject explicitly.

These typed built-in bindings use current native Camera/Light/AudioSource/ParticleEmitter members
and existing scalar/domain validators. They are not a general custom-component/material/UI binding
registry. No native component layout, installed SDK input or callback changes.
Version3 introduced these property values; version4 adds scalar curves. New
publication uses recipev7 and cooked5 even for a legacy transform-only source.
Selected previous cooks remain readable. Binding indices remain unchanged. Older
engines reject unsupported versions or names rather than partially executing clips.

Properties share existing key times/UUIDs and default outgoing held/linear timing;
vector cubic slopes do not affect them. Adding a property initializes every key from the
chosen object's authored value. Key insertion defaults to the sampled property
values unless values are supplied; replacement without values preserves them.
Retiming/moving/duplicating keys and transform-value/slope edits retain property
values and key extensions. Removing the last property from a property-only clip
rejects. Numeric fields are temporary until Add / Update commits one source Undo.
FOV displays degrees while serialized/native values stay radians. Colors stay linear.

Fixed application builds typed Camera/Light/AudioSource/ParticleEmitter candidates alongside any transform
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

Particle appearance binds the existing per-emitter size/linear RGB factors after
shared lifetime curves. Effect, seed, emission speed/age, visibility/layers and
trajectory remain unchanged; appearance does not restart the resource or clock.
Complete native candidates use existing reflected scalar validation; sampled fields
share the same admitted ranges. The fixed path builds no validation JSON. Copied
preview uses the ordinary builtin codec/validation; render-only interpolation patches
feed existing particle bounds and instancing without mutating ECS. Timeline Scene
preview uses the existing frozen0.5s particle sample, not another running simulation.
Gallery60 shares one effect between animated and constant emitters. Export ships
selected clip/effect cooks without their editable source files.

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

### Scalar property curves

Version4 keys may store `curves.property_curves`, an object keyed by a declared
qualified property name. Each entry stores `interpolation` (`linear` or `cubic`),
`incoming` and `outgoing` slopes in native property units per second. Missing entries
remain linear; a key's Hold overrides all outgoing interpolation. Existing vector
settings remain separate. FOV slopes are radians per second on disk.

The sampler reuses the original cubic Hermite segment and derivative-extrema math.
Admission validates keys, finite slopes bounded to ±1e9, and all segment extrema
against the property's existing range, before replacing source or publishing. Invalid
overshoot rejects the complete candidate. Endpoint evaluation retains the authored
endpoint exactly; scalar sampling evaluates from the nearer endpoint to preserve
small positive camera heights beside large values. Already-admitted cubic samples correct only floating-point boundary
excursions within 64 double epsilons times the profile magnitude; this does not relax
key or extrema admission. Nonfinite or larger sample excursions reject.

`set_property_curve` and `set_property_tangent_preset` use the same key/source owner.
Flat, Linear and Smooth use the existing vector preset algorithm on the chosen
property. Edits preserve other bindings, vector settings, UUIDs and opaque settings;
moving/duplicating retains complete settings, whole-clip retiming rescales slopes,
and removing a binding removes its associated curve entries. Scalar-curve syntax
is interpreted only in version4. Versions1/2/3 retain `property_curves` as an opaque
extension if present. Conversion rejects any legacy collision without changing
source/history; it never overwrites or reinterprets that extension. Ordinary edits,
retiming and republishing preserve it. Curve-bearing edits promote atomically when
there is no collision.

Preview, fixed application, copied presentation, recovery and source-free export
use the existing shared sampler and typed candidate paths. No native SDK layout,
clock, animation registry or renderer resource changes. Gallery61 compares cubic
easing with the unchanged version-3 linear particle clip, sharing one effect.

### Animation curve authoring

Timeline's **Animation curves** section edits slopes on the active key. **Curve
value** selects Position, Scale or an enabled scalar binding. **Outgoing
interpolation** selects linear/cubic timing for the chosen target. Vector mode keeps
the existing XYZ fields; property mode offers one incoming/outgoing slope in displayed
units per second. FOV displays degrees while stored slopes remain radians. Presets
edit both vector lanes in vector mode, or only the chosen property in scalar mode.
The selected vector-axis or scalar view samples the same evaluator 193 times across that key and its
neighbors. Blue is the applied curve; gold guides use pending slope fields and
intersect the true slope with the view boundary. **Fit curve** fits the applied
values; **Fit curve and handles** also includes finite tangent endpoints. These
controls only change the view. Rotation has no cubic tangent controls.

Drag a visible gold handle vertically to edit one incoming/outgoing slope on the
chosen property or vector channel/axis. Its guide uses one third of the neighboring time interval;
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
distinct selected keys using the original neighboring poses/property values and times.
Selected keys gain cubic settings for the selected target; key identities, times,
poses, rotation and Hold remain.
Flat sets zero slopes. Linear uses each side's neighboring secant. Missing endpoint
sides copy the available secant; a lone key uses zero. Smooth uses a common slope
on both sides, `(left_secant * right_interval + right_secant * left_interval) /
(left_interval + right_interval)`, bounded to three times the smaller secant's
magnitude. Opposite signs or a zero secant give zero at an extremum or plateau.
Endpoints use their one available secant. The complete candidate must satisfy the
existing slope/interior-value limits before source replacement. Presets retain the
same source/runtime owners and do not change installed native SDK interfaces.

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
