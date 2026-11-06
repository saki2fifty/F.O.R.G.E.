# Skeletal animation retargeting

Retargeting bakes a new target-bound `AnimationClipAsset` from a project-owned
`.retarget.json` recipe. Imported models, skeletons and clips retain their existing
identities. Playback uses the existing Animator, `ModelAnimationResources`, sampler,
animation clock and recovery owner. There is no second pose runtime or rig database.

## Source and ownership

`AnimationRetargetSource` has format `forge.animation-retarget`, version1, an
`asset_id`, `source_skeleton`, `source_clip`, `target_skeleton`, `sampling_rate`,
`root_translation`, `translation_scale`, quaternion `alignment` in XYZW order and
`entries` containing explicit `{source, target}` ModelNode UUID pairs. New drafts
may have null references and an empty mapping; publishing requires complete inputs.
The native editor displays alignment as degrees and stores a unit quaternion.

Both skeletons must be selected members of successfully imported model revisions.
The source clip must belong to the source skeleton's imported family. Baked clips
cannot recursively serve as retarget sources. Joint selection uses durable node
identities, not names, equal joint counts or the joint order of another model.
`model_skeleton_binding.hpp` is the shared durable joint-order/rest owner used by
this workflow and bone masks.

The Source document owns recipe history, saved state and session/revision checks.
The existing import queue owns background capture/cook/cancellation; the asset
publisher owns immutable selection and last-good preservation. Recipe edits do not
edit the Scene. **Use on selected Animator** makes one ordinary Scene property
command after a successful publication and checks the target binding. Animators
using States or a Blend Space select the clip through that existing owner instead.

## Transfer equations and bounds

Mappings are one-to-one in both directions. Every mapped joint's direct parent must
also be mapped to its target's direct parent; roots map to roots. Unmapped target
joints retain their local rest transforms. For source model-space rotation `S`,
source model-space rest `Sr`, target rest `Tr` and alignment `A`, the desired mapped
target model rotation is `A * (S * inverse(Sr)) * inverse(A) * Tr`. Its local rotation
is solved against the actual sampled target parent's model rotation. Quaternions
are normalized and checked for finite output.

Target local rest translations and unit scales retain the target proportions.
Optional root displacement is target rest translation plus aligned source
displacement from its rest, multiplied by `translation_scale`. This option requires
one mapped source/target root pair. Other source translations and morph curves are
not transferred. This is an explicit skeletal mapping workflow; it does not infer a
humanoid rig, fit contacts or solve whole-body constraints.

Admission is bounded to256 joints per rig,60 seconds,1–120 samples per second,
4096 sampled frames and32768 sampled joint poses **for each selected rig**. Both
endpoints are included. A large rig or long clip may require a lower sampling rate.
Each selected model bundle is limited to64MiB; recipe JSON to256KiB,8000 parsing
events and depth12. Source edits validate their formatted saved representation
against the same reader before entering history, so opaque extensions cannot make
a saved recipe unreopenable. Skeleton rest scales must be one. The existing native archive validator
derives whether every stored scale key is one, including keys between bake samples;
constant unit scale channels are legal. Ordinary animation playback continues to
admit its existing supported non-unit scale channels.

## Immutable output and runtime binding

Importer `forge.animation.retarget`, recipe `forge.animation.retarget.v1`, produces
`clip.json` and `clip.ozz` using format `forge.skeletal-clip` version1. The descriptor
binds the root clip UUID to the target model, target skeleton, selected immutable
skeleton revision and exact durable joint-node order. `forge.animation_clip`
metadata retains the target binding and per-file SHA-256/byte facts. Descriptor,
provenance, file set, native archive and selected target order are checked together.
The descriptor is limited to32KiB and the native archive to16MiB.

Private pinned Ozz0.17.0 `RawAnimation`/`AnimationBuilder` compress FORGE-owned dense
target poses without seek iframes. The produced archive passes the existing bounded
same-span native admission and actual target sampling before publication. No
dependency implementation or version changes. Offline builder code is linked by
authoring; runtime does not acquire an offline-builder dependency.

Source skeleton and source clip are Build dependencies. The target skeleton is the
sole importer-owned Runtime dependency. Explicit declared Runtime dependencies keep
their normal export behavior. Export strips Build edges and the editable recipe; a game
using only the target animation does not need the source model. Provenance still
records exact build inputs. Selected source/target changes reject stale work before
publication and preserve the previously selected clip.

`select_animation_clip` is the shared binding admission for direct Animator,
States, Blend Spaces, resource requests and packaging. Imported pairs retain their
same-family/revision/generation checks. Baked root clips instead prove their exact
target skeleton binding and track independent clip and skeleton cache keys and
generations. No public SDK field, table, component layout or existing imported
archive format changes. Standalone kits advertise `runtime_skeletal_clip: 1`;
export rejects an older kit before writing incompatible runtime content.

## Native editing and MCP

Content creates the recipe; **Animation Retarget** edits/publishes it. Published
joint inspection runs off the UI thread against immutable selected model data.
Closing, changing a Source session or selecting different skeleton revisions cancels
or discards old inspection results. Read-only inspection opens an existing cache
and creates no derived-data directories. Play locks recipe controls.

Automation/MCP use the same `animation_retarget` Source owner and existing
template/create/open/read/patch/Undo/Redo/save/status/close operations.
`asset.animation_retarget.catalog` describes the supported workflow.
`asset.skeleton.joints` (`forge_asset_skeleton_joints`) is an `asset.read` operation
with optional exact selected-revision checking, offset and pages of1–128 joints;
it returns durable UUIDs, parent UUIDs, names and rest TRS for explicit client mapping.
Existing entity/component/property operations assign a published clip. Inspection
is not a source parser, publication operation or separate authoring implementation.

See [the user guide](../manual/editor/animation-retargeting.md),
[animation](animation.md), [MCP](mcp.md) and
[decision034](decisions/034-animation-retargeting.md).

## Capture provenance and acceptance scope

`manual/images/animation-retarget.png` captures the production
`AnimationRetargetEditor` with the shipped font and actual ImGui draw data through
the SDL CPU software renderer. `animation_retarget_editor` drives real controls at
100%,150%,200%, wide/narrow, using actual imported skeletons and published clips.
It covers recipe history, validation, publication, Scene attachment/history, Play
locks and inspection close/reopen. Intermediate input frames compute native layout;
the six retained screenshots are fully rasterized.

`manual/images/animation-retarget-gallery.png` is a lossless format conversion of
Gallery179's actual fixed-tick30 Game rendering from
`animation_retarget_gallery_vulkan`. The original blue/gold articulated-beam models,
geometry, bind matrices and source motion are FORGE-authored. The native
RuntimeWorld/FrameRenderer fixture captures three animated landscape frames and
one portrait frame, checks clear borders and compares all four pixel arrays with a
relocated Windows-target export after the source project is offline. Neither image
is retouched or represents full Main, Windows, physical GPU or physical DPI acceptance.
Those checks remain separate platform acceptance work.
