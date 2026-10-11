# Authored transform animation

The editor timeline authors reusable object-transform clips. Skeletal and model
animation continue through the existing [Ozz integration](animation.md). Both are
owned by `AnimationRuntime`; this feature adds no dependency or service manager.

## Source, publication and identity

`TransformAnimationSource` uses `forge.transform-animation` version 1 in a bounded
2 MiB `.animation.json` document. It stores its AssetId, a 1,000,000-unit-per-second
integer timebase, positive duration up to one hour, 1–4096 sorted keys at unique
times within duration, and at least one qualified channel:
`forge.local_translation`, `forge.local_rotation`, or `forge.local_scale`.
Each key has a stable UUID, integer `time_us`, full local pose and `linear` or
`held` interpolation. Quaternion rotations must be finite and normalized;
translation is bounded to ±1e9 and scale follows the existing signed/zero visual
scale contract. Invalid edits are rejected before source/history replacement.

`TransformAnimationDocument` reuses `AssetSourceDocument` for writer ownership,
revision guards, atomic save, on-disk conflict checks and bounded history. The
central Timeline registers with `DocumentWorkspace` and `AssetEditors` under
ADR 004. Global Save/Undo/Redo routes to the focused document. Scene attachment
uses existing atomic authoring commands and creates one Scene Undo entry.

The trusted CPU importer `forge.animation.transform` / `forge.animation.transform.v1`
publishes a root `transform_animation` asset and one `animation.clip` artifact.
Its format is `forge.transform-clip` version 1, validated by the same bounded
parser/evaluator. Source bytes, stable key IDs, selected recipe, file hashes and
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

Position/scale use linear interpolation and rotation uses shortest-path normalized
quaternion slerp. A held key preserves its pose until the next exact key boundary.
Sampling clamps before the first and after the last key. Runtime/preview loop time
covers `[0,duration)`; nonloop playback holds duration and ends. Time accumulation
uses double microseconds while authored key times remain integer microseconds.
Speed is 0–4; there are no reverse, event, blending, bone-editing or IK tracks.

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
valid when there are no realized Transform Animators. There is no SDK ABI change;
adding the reflected component updates the existing exact SDK header fingerprint.

## Standalone content

The normal scene-reference scanner discovers Transform Animator's typed clip
field. Runtime content admits the cooked format, includes selection metadata and
hashes, and omits the authoring source. Relocated packages use the same evaluator
and loader. Windows ZIP assembly includes `.clip` Gallery data and the normal
manual. The Gallery's `08-transform-animation` scene demonstrates a looping local
position/rotation/scale clip on a platform.
