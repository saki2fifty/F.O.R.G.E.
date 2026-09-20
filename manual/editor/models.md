# Importing models with the tools command

The current Phase7 source can prepare static glTF models from `.gltf` or `.glb`
files. It registers the model, meshes, materials and referenced textures together.
The original source files stay unchanged.

**Current limitation:** this is a tools command workflow. Placing imported models
in a scene, the model editor, rendering imported materials, and model files containing
skins or animation are still being implemented. Cameras, punctual lights, node
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

The default normal/tangent choice is `missing`. Exact merging and vertex-fetch
optimization default to `true`. Compression defaults to `none`; maximum size is16384.
Shell quoting for JSON depends on the terminal you use.

## When a reimport cannot identify a member

Renaming and reordering can preserve identity when the model contains enough distinct
geometry or usage information. Two indistinguishable meshes may require a decision.
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

In the current implementation, identical unkeyed members can ask for correspondence
again on a later reimport, including a cache hit. A future editor workflow will make
these decisions easier to inspect.

## Failures

Malformed files, unsupported required extensions, failed compatibility checks,
changed dependencies and cancellation keep the previous selected family. Removed
members retain records so references can report that they are missing rather than
silently selecting a different mesh or material. Importing does not add a scene Undo
step.

See [textures](textures.md), [Content](content-browser.md) and [animation](animation.md).
