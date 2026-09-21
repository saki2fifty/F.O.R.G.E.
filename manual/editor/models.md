# Importing models with the tools command

The current Phase7 source can prepare glTF models from `.gltf` or `.glb` files.
It registers meshes, materials, referenced textures and, when present, a skeleton
and animation clips together with the model.
The original source files stay unchanged.

**Current limitation:** this is a tools command workflow. Placing imported models
in a scene and the model editor are still being implemented. Prepared mesh
materials can render through the shared Scene/Game mesh path. Imported Skeleton and Clip assets can now drive the existing
[Animator bone preview](animation.md). Cameras, punctual lights, node
visibility/selectability and material variants are retained in imported model data;
their complete model-placement workflow is not available yet.
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

Enabled scene Camera components now drive [Game](play-mode.md), and Light components
illuminate PBR meshes. Use **Create > Rendering** to add a Camera, Light or Mesh Renderer.
Model placement and its complete user workflow are still being integrated; importing
a source alone does not instantiate its nodes into the scene.


## Built-in meshes

A **Mesh Renderer** can use engine shapes without importing a model. In its **Mesh**
field, search for **Engine /** and choose a shape. The built-in mesh supplies its
shared default surface material. PBR surfaces need a light or environment to be lit;
use [Scene lighting](lighting.md) to add one.

Engine assets are read-only and do not create files in your project. Existing
blockout objects keep their Primitive and Color controls and their familiar preview
shading. Their compatibility rendering uses the shared mesh path without rewriting
saved scenes or creating material files.


## Transparent and glass surfaces

Imported glTF transmission is different from ordinary transparent alpha. Transmission
shows the opaque scene through a surface while retaining its reflections. Its factor
controls how much light passes through; metallic regions do not transmit. Roughness
blurs the view through the surface. A thickness value adds refraction and distance-based
absorption; dispersion can separate the red, green and blue refraction paths.

The current optical path is being validated as part of the larger Phase7 update.
It samples the opaque view behind the object. It cannot show another transparent
object through the same glass layer, recover objects outside the camera image, or
produce colored glass shadows and caustics. Looking from inside a volume remains
an open acceptance case. These limits are separate from the ordinary Alpha mode.

Mirroring an object preserves its optical thickness. Flattening its volume to a
surviving plane makes it thin; FORGE does not secretly replace zero scale with a
small positive number. Optical values outside the renderer's finite range produce
a rendering diagnostic without changing the scene's authored transform.

## Morph targets

Prepared mesh surfaces now use their imported default morph weights. These can
change shape, surface directions, vertex colors and texture coordinates. Negative
weights are preserved. Scene and Game use bounds that include the changed shape;
the shadow pass uses the same deformation.

Interactive morph controls and animation-driven model placement are still being
integrated. This source checkpoint does not yet provide a morph-editing panel or
establish skeletal mesh playback. The native rendering checks for this addition
are tracked separately from the completed model-import checks.


## Animation selection during placement

The internal placement command now accepts a specific clip from the imported model.
It checks the clip and skeleton together before creating an Animator on the model's
root. Without a selected clip, placement creates the model without starting an
animation. There is no implicit “first animation” choice in a glTF file.

This connection is undergoing integration; the public placement controls remain
unavailable in this source checkpoint. Existing import commands do not place models
or start playback. Once placement is exposed, scene Undo will remove the complete
placed subtree, while the imported asset files remain in the project.


## Hide a subtree or exclude it from viewport selection

Select an entity in **Hierarchy**, then use **Inspector > + Add Component**:

- **Node Visibility:** turn **Visible** off to hide its meshes, lights and all structural children. Cameras keep working. Animation and physics keep running.
- **Node Selectability:** turn **Selectable** off to skip the entity and its structural children when selecting in the Scene viewport. You can still select and edit them from Hierarchy. This does not lock their properties.

A child cannot override an off switch on an ancestor. Changing a child's spatial
space to **World** does not break these rules. Visibility and selectability are
independent: hiding a shape does not make it unselectable, and making it
unselectable does not hide it. Each property edit supports scene Undo/Redo.
Prefab instances use the same property Revert controls to follow their source again.

Imported node flags feed these components through the internal placement path.
Public imported-model placement and precise mesh picking remain under integration;
the existing blockout selection path already observes the node selection switch.
