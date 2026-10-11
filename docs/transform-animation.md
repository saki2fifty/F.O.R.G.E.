# Authored transform animation

The editor timeline authors reusable object-transform clips. Skeletal and model
animation continue through the existing [Ozz integration](animation.md). Both are
owned by `AnimationRuntime`; this feature adds no dependency or service manager.

## Source, publication and identity

`TransformAnimationSource` reads `forge.transform-animation` versions 1 and 2 in a bounded
2 MiB `.animation.json` document. It stores its AssetId, a 1,000,000-unit-per-second
integer timebase, positive duration up to one hour, 1–4096 sorted keys at unique
times within duration, and at least one qualified channel:
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
bounded to ±3600seconds, then checked against every selected time. No timeline time
scaling, ripple edit, box selection or motion-preview retiming claim is made.

The trusted CPU importer `forge.animation.transform` / `forge.animation.transform.v2`
publishes a root `transform_animation` asset and one `animation.clip` artifact.
It publishes `forge.transform-clip` version 2, validated by the same bounded
parser/evaluator. Previously selected version-1 artifacts remain readable without
rewriting their catalog or DDC records. Runtime and export require the cooked root
version to match the selected output version. Source and cooked readers allow
500,000 parser events, depth 12 and 2 MiB, including the 4096-key curve profile.
Source bytes, stable key IDs, selected recipe, file hashes and
size are covered by the normal sidecar/catalog/DDC publication. Target recipes
are Windows/D3D12/desktop and Linux/none/cpu. There are no runtime dependencies.
Asset move/copy uses the existing authored-document adapter; copies mint the
new asset identity and retain key IDs scoped to that new clip.

Clips contain no scene/entity name or index. `forge.transform_animator` attaches a
typed clip AssetRef to its own persistent scene EntityId. Rename, reparent, scene
persistence and prefab overrides use existing component ownership. Each attachment
has independent transient playback state. Channels overwrite local values; a
channel omitted from the clip preserves the object's current local value.

## Evaluation and preview

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

Timeline's **Vector curves** section applies the selected key's vector mode/slopes
as one source Undo step. Pending numeric fields do not replace the authored curve
until Apply. The selected-axis view samples the same evaluator 193 times across the
selected key and neighboring segments; gold tangent guides use pending slopes and
clip the true slope at view boundaries. Rotation has no cubic tangent controls.

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
model-node bindings cannot share a target with a Transform Animator. A rejected
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
