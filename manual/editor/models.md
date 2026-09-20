# Importing models with the tools command

The current Phase7 source can prepare glTF models from `.gltf` or `.glb` files.
It registers meshes, materials, referenced textures and, when present, a skeleton
and animation clips together with the model.
The original source files stay unchanged.

**Current limitation:** this is a tools command workflow. Placing imported models
in a scene, the model editor and rendering imported materials are still being
implemented. Imported Skeleton and Clip assets can now drive the existing
[Animator bone preview](animation.md). Cameras, punctual lights, node
visibility/selectability and material variants are retained in imported model data;
their viewport controls and rendering are not available yet.
This page describes the working model preparation path only.

## Prepare a model

1. Put the model and its external buffers/images inside the project's **Assets** folder.
2. Close the editor for this project so the tools command can obtain writer ownership.
3. Open a terminal beside `forge_tools.exe` and run:

```
forge_tools.exe --assets import "C:\Projects\MyGame" "Assets\Model\scene.gltf"
```

The command prints a JSON result. `ok: true` means the complete asset family was
published. `asset` is the model's permanent identity. `cache_hit: true` means a
previously prepared artifact was validated and reused.

Keep `forge.assets.json` and the source's adjacent `.forge-import.json` file with
version control. They hold identities, bindings and settings. `.forge/cache` is
rebuildable. Copying a sidecar to a different model is not an asset-duplication
operation and is rejected.

## Settings

An optional JSON object after the source path changes these settings:

- **normals:** `preserve`, `missing`, or `recalculate`. Keep normals, generate missing flat normals, or replace all normals.
- **tangents:** `preserve`, `missing`, or `recalculate`. Prepare the surface directions needed by normal maps.
- **weld_exact:** `true` / `false`. Merge only vertices with identical complete data.
- **vertex_fetch:** `true` / `false`. Optimize vertex storage while retaining triangle order.
- **compression:** `none`, `bc`, or `bc-high-quality`. Compress ordinary images; Basis images use the selected platform's transcode profile.
- **max_texture_size:**1–16384. Maximum texture dimension.
- **skin_influences:** `reject` or `reduce-to-four`. Reject vertices using more than four bones, or explicitly keep the four strongest influences and rebalance their weights.
- **animation_sampling_rate:**1–240. Samples per second when the official converter resamples animation curves; default30.
- **animation_optimize:** `true` / `false`. Use the official converter's animation optimization; default `true`.

Animated models require the packaged `tools/gltf2ozz.exe` converter. If conversion
fails, the previous complete model import remains selected. Importing a skeleton
and clips does not yet place or play the model in a scene.

The default normal/tangent choice is `missing`. Exact merging and vertex-fetch
optimization default to `true`. Compression defaults to `none`; maximum size is16384.
Shell quoting for JSON depends on the terminal you use.

## When a reimport cannot identify a member

Renaming and reordering can preserve identity when the model contains enough distinct
geometry or usage information. Indistinguishable meshes or source nodes may require a decision.
FORGE stops with `subasset.identity-ambiguous` and leaves the previous import usable.
Its `identity_conflicts` list shows candidate addresses and previous same-type IDs.

The command accepts a second optional JSON argument containing explicit decisions:

```
[{"address":"/meshes/1","previous":"<previous mesh UUID>"}]
```

Use a returned previous UUID to preserve that mesh's identity, or `null` to explicitly
create a new mesh asset. Supply `{}` as the first optional argument if settings are
unchanged. Addresses describe this candidate only; inspect the current conflict
before choosing. Do not use old array positions to guess correspondence.

Reimporting the exact same source with the same settings and tools preserves the
published member identities, including identical meshes. If the input changes and
members remain indistinguishable, FORGE asks for a decision again. An explicit choice
still takes precedence on an unchanged import. A future editor workflow will make
these decisions easier to inspect.

## Failures

Malformed files, unsupported required extensions, failed compatibility checks,
changed dependencies and cancellation keep the previous selected family. Removed
members retain records so references can report that they are missing rather than
silently selecting a different mesh or material. Importing does not add a scene Undo
step.

See [textures](textures.md), [Content](content-browser.md) and [animation](animation.md).


## Imported transforms

New imports preserve each source node's position, quaternion rotation and signed
scale, including zero scale. A zero-scaled node keeps its rotation instead of having
it reconstructed from a flattened matrix. Importing does not rewrite the source.
Older development imports remain readable; reimport them to prepare the additional
transform information needed for model placement. Import validation and placement
validation are separate: a value may be valid model data but exceed the scene or
animation consumer's supported range.


## Source node identities

Each node in a newly imported model has its own source identity. The import keeps
that identity when reordering or renaming nodes can be matched reliably. A scene
object created from the model will have its own separate entity identity.

Identical nodes remain distinct. An unchanged reimport preserves their selected
identities; changed source content can require a correspondence decision when FORGE
cannot tell which old node matches which new node. The conflict reports
`model_node` and addresses such as `/nodes/2`. Use the same explicit-decision argument
as for meshes. Deleting a source node retains a removed-member record so references
can report it missing instead of silently switching to another node.

Renaming objects can also change the information used to distinguish identical mesh
assets. In that case, the conflict asks for mesh correspondence even when the source
nodes can still be identified. Read the reported type before choosing a previous ID.

Animation channels can distinguish nodes whose rest transforms are identical.
Removing those channels can remove that evidence; a later import may then ask you
to confirm which previous source node to keep. This leaves the earlier model usable
until you resolve the conflict.


## Imported cameras and lights

Model preparation preserves perspective and orthographic camera settings, including
an omitted far clipping plane, and directional, point and spot lights. Source node
transforms remain unchanged. Camera and light directions follow glTF's local minus-Z
convention when the internal placement path creates their scene components.

These components do not yet drive the current Game preview. Camera/light creation
controls and the production renderer are still being connected. Importing a model
successfully does not mean those rendered workflows are ready for use.
