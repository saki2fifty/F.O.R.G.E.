# Importing and placing models

FORGE imports `.gltf` and `.glb` models as a complete asset family: meshes,
materials, referenced textures and, when present, a skeleton and animation clips.
Models can contain points, lines and triangles, with indexed or nonindexed source
geometry. FORGE chooses compact index storage automatically; no import setting is
needed for that choice. Original source files stay unchanged. Importing prepares assets; **Place model**
creates ordinary entities in your current scene.

For files outside the project, use **Content → Import files** or drop them onto Content. The [Content import review](content-browser.md) copies sources into a new folder and can import them with default settings. Turn off automatic import there to review the settings below before cooking.

## Try the packaged walkthrough

Open **Examples/Rendering** through **File > Open project**. Its `README.md` gives a short import-and-place walkthrough using the original FORGE checker cube, copper variant, floor, camera and light. Import `Assets/Rendering.gltf` with the steps below, then choose **Place model** and **Play**. The project begins with an empty scene so the import and placement steps are visible.

## Import from Content

1. Put the model and its external buffers/images inside the project's **Assets** folder.
2. In **Content**, open **Create / Register > Import model...**.
3. Enter the project-relative source path and choose **Review settings**.
4. Review the settings, then choose **Import / Reimport**. Progress appears in the document; **Cancel import** retains the previous usable import.
5. After publication succeeds, the model's placement controls load its validated metadata.

To reopen an imported model, select its model asset in Content and use
**Import settings / Place** in the Inspector. The model import document owns its
settings Save action. Closing or switching projects with pending work offers
Apply, Discard or Keep editing. Scene Undo does not undo asset publication.

## Inspect a model or mesh before placement

After a successful import, the central **Model import** document shows **3D preview**.
It uses the published model, including its transforms, material assignments, default
morph weights and skin-joint relationships. Animation does not autoplay.

- Hold MMB over the image to orbit; hold Shift+MMB to pan; scroll to zoom.
- Choose **Frame view** to fit the visible geometry, including mirrored placements.
- Expand **Preview lighting** for exposure, key light, source lights and background.
- If the source contains multiple scenes, **Preview scene** chooses which one to inspect. The separate source-scene choice under placement still controls **Place model**.
- Expand **Import settings** to change the import. Unapplied settings do not change the published preview.

Double-click a generated Mesh in Content, or use **View mesh** in Inspector, to
inspect its placements in a read-only **Mesh** document. Its owning model supplies
the joint scope; it is not converted to an unrelated static mesh. A mesh unused in
the selected source scene reports that limitation. **Open source import** returns
to the owning model's settings.

These controls only change the view. They create no scene entities and have no
Scene Undo entry. Preparation runs asynchronously. A failed replacement keeps the
previous complete image and shows a diagnostic; **Retry preview** retries inspection.
Switching to another asset clears the previous asset's image.

## Drag a model into the Scene

Drag the model asset from Content onto the Scene image for a default-scene placement without animation. The cursor places its root on a camera-facing plane through the view target. One Undo removes it. Use **Cancel placement** in Scene while loading if needed. For a different source scene or an explicit animation, use the placement controls below.

## Place a model

1. Open the model's **Import settings / Place** document.
2. Under **Place in scene**, enter the new root's **Name**.
3. Choose **Source scene**. A file with several scenes and no declared default requires an explicit choice.
4. Leave **Animation** at **None — source pose**, or choose a matching clip explicitly.
5. Choose **Place model**. A new root and its selected source nodes appear in Hierarchy at the world origin.

Every placement receives new entity identities. Mesh/material/skeleton references
continue to point to the imported asset family. Move the root using the viewport
or Inspector. One scene Undo removes the entire placement; Redo restores the same
entity identities. Save the scene normally when you want to keep it.

Selecting an animation adds an Animator with the matching skeleton and clip.
Press **Play** to begin playback. No first clip is chosen automatically. A model
with no selected clip uses its source pose and default morph weights.

**Source hierarchy** lists source node names and parent indices for inspection.
Edit placed entities in Hierarchy and Inspector. Reimport can refresh referenced
resources; it does not silently rewrite an already authored scene hierarchy.
If the selected source revision changes before placement, FORGE rejects the stale
placement. Reload the saved settings to load the current revision.

## Prepare from a terminal

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
fails, the previous complete model import remains selected. Use **Place model** to create scene entities and choose their optional animation.

The default normal/tangent choice is `missing`. Exact merging and vertex-fetch
optimization default to `true`. Compression defaults to `none`; maximum size is16384.
Shell quoting for JSON depends on the terminal you use.

## When a reimport cannot identify a member

Renaming and reordering can preserve identity when the model contains enough distinct
geometry or usage information. Indistinguishable meshes or source nodes may require a decision.
FORGE stops with `subasset.identity-ambiguous` and leaves the previous import usable.
Its `identity_conflicts` list shows candidate addresses and previous same-type IDs.

In the editor, **Subasset identity needs review** lists the ambiguous candidate
members. For each one, choose a previous same-type identity or **New logical asset**.
Then choose **Import / Reimport** again. Decisions apply only to the exact reviewed
source and settings. If either changes, review the new conflict before publishing.
A previous identity cannot be claimed twice. Failed review leaves the earlier model
usable. Identity choices are guarded as a pending document draft.

The tools command accepts a second optional JSON argument containing explicit decisions:

```
[{"address":"/meshes/1","previous":"<previous mesh UUID>"}]
```

Use a returned previous UUID to preserve that mesh's identity, or `null` to explicitly
create a new mesh asset. Supply `{}` as the first optional argument if settings are
unchanged. Addresses describe this candidate only; inspect the current conflict
before choosing. Do not use old array positions to guess correspondence.

Reimporting the exact same source with the same settings and tools preserves the
published member identities, including identical meshes. If the input changes and
members remain indistinguishable, FORGE asks for a decision again. An explicit choice still takes precedence on an unchanged import. The editor's
review list uses the same candidate identities as the tools command.

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
convention when placement creates their scene components.

Enabled scene Camera components now drive [Game](play-mode.md), and Light components
illuminate PBR meshes. Use **Create > Rendering** to add a Camera, Light or Mesh Renderer.
Importing a source alone does not instantiate its nodes; use **Place model**.


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

D3D12/WARP fixtures validate the implemented optical path.
It samples the opaque view behind the object. It cannot show another transparent
object through the same glass layer, recover objects outside the camera image, or
produce colored glass shadows and caustics. Inside-facing surfaces use the exit
refraction ratio, including total internal reflection at steep angles. This is
still a surface approximation, not a simulation of light travelling through
arbitrary nested glass volumes. These limits are separate from ordinary Alpha mode.

Mirroring an object preserves its optical thickness. Flattening its volume to a
surviving plane makes it thin; FORGE does not secretly replace zero scale with a
small positive number. Optical values outside the renderer's finite range produce
a rendering diagnostic without changing the scene's authored transform.

## Morph targets

Prepared mesh surfaces now use their imported default morph weights. These can
change shape, surface directions, vertex colors and texture coordinates. Negative
weights are preserved. Scene and Game use bounds that include the changed shape;
the shadow pass uses the same deformation.

A dedicated morph-editing panel is not available yet. Choosing a clip during
placement connects its supported animation to the model; native rendering validation
is tracked separately in the technical documentation.


## Hide a subtree or exclude it from viewport selection

Select an entity in **Hierarchy**, then use **Inspector > + Add Component**:

- **Node Visibility:** turn **Visible** off to hide its meshes, lights and all structural children. Cameras keep working. Animation and physics keep running.
- **Node Selectability:** turn **Selectable** off to skip the entity and its structural children when selecting in the Scene viewport. You can still select and edit them from Hierarchy. This does not lock their properties.

A child cannot override an off switch on an ancestor. Changing a child's spatial
space to **World** does not break these rules. Visibility and selectability are
independent: hiding a shape does not make it unselectable, and making it
unselectable does not hide it. Each property edit supports scene Undo/Redo.
Prefab instances use the same property Revert controls to follow their source again.

Imported node flags feed these components during placement.
The retained mesh and legacy blockout selection paths observe this selection
switch.

## Selecting model geometry

Click a model surface in the Scene view to select its corresponding entity.
Selection follows the loaded mesh, including its current morph and skeletal pose.
It also works with mirrored or flattened surfaces. Points and lines have a small
pick radius to make thin geometry easier to select.

Selection ignores material transparency and the visibility switch; **Node
Selectability** controls whether the geometry can be picked. Select hidden or
unselectable entities from **Hierarchy** when that is clearer. Geometry still
loading has no substitute selection cube.

For an exceptionally expensive click, FORGE leaves the previous selection in place
and reports that the selection work budget was exceeded. Use Hierarchy for that
entity.

## Reimport while playing

You can update import settings in an open Model import document during Play.
**Import / Reimport** prepares and validates the replacement while the previous good
model remains usable. Placement is still a scene edit: stop Play before using
**Place model**.

A successfully published model notifies the Play runtime. While paused, the
replacement can load, but the current animation and transforms stay frozen.
Press **Step** or **Resume** to adopt a ready compatible replacement. Playback
keeps its current time; a shorter clip wraps if looping or stops at its end.
A missing clip, corrupt asset or incompatible pose reports an error and leaves
the previous animation active. Fix the source and apply the import again.

Reimport does not recreate or rearrange your placed entities. Scene edits and
asset imports have separate histories. Older standalone Skeleton/Clip conversion
assets still require stopping and starting Play to use a new conversion.

Engine meshes include texture coordinates and tangents, so a surface material can
use textures and normal maps. Curved shapes wrap around their circumference;
flat faces and caps use planar coordinates. These are fixed built-in layouts,
not an editable UV unwrap.

## Repeated objects

FORGE combines compatible static objects into instanced rendering batches. You
still select, move, rename and override each object separately. Mirrored objects
use the appropriate winding group; different materials and lighting masks split
batches. Animated, morphing and transparent objects keep their individual draw
paths. No merge command or identity change is required.

A glTF model using `EXT_mesh_gpu_instancing` places each mesh copy as its own
child entity. Its source parent carries shared movement. The original mesh is
not added as an extra copy. Negative and zero instance scale stay intact, and
placement remains one Undo step. Custom instance attributes are retained in the
source with a diagnostic; instanced skin bindings currently reject explicitly.

A successful model import publishes its meshes, materials and other members together.
Those internal references do not trigger another import of the same model. Other
assets that depend on a changed member still receive source-update processing.


## Levels of detail

A model can supply detailed geometry for close views and simpler authored geometry
for distant views. FORGE switches by the object's projected size on screen.
It does not generate simplified geometry automatically.

Import a glTF file whose mesh leaf nodes use **MSFT_lod**. The model's combined
mesh member has an **LODs** suffix. Place the model normally, or assign that mesh
in the Inspector. Other objects using the original high-detail mesh remain unchanged.

Open the combined mesh from Content and expand **Mesh levels of detail**. Each
level shows its transition threshold, geometry counts, material count and local
bounds. Scroll over the preview to move closer or farther and observe switching.
The source file controls the geometry and thresholds; reimport to apply changes.

Alternatives must share the same local transform, skin, morph channels/defaults
and visibility/selectability. They must be root mesh leaves without independent
animation, cameras or lights. The owner node must also have no independent animation.
Whole-subtree LOD and material-only LOD report an unsupported-configuration error. Correct
the source and reimport; the previous good asset remains available.

When the source has no screen-size hints, transitions occur at half, quarter,
eighth and successively smaller screen coverage. The lowest level stays visible;
a source's optional final disappearance hint produces an import diagnostic.


## Choosing a material variant

A glTF file can contain named material sets, such as different colors for the same
model. Import the file normally. In **Model import > Place in scene**, choose
**Material variant**, then **Place model**. **Default materials** uses the original
surfaces. The choice applies to every mesh in the placement, including its LODs.

To change an existing placement:

1. Select the model’s root in Hierarchy.
2. Expand **Model Source** in the Inspector.
3. Click **Set model variant...** and choose a set or **Default materials**.

**Model Source** is a dedicated read-only provenance section, separate from optional
behavior components. Child nodes show their imported node identity; whole-model
variant selection is available on the placed root.

This is one scene Undo step. It changes the material selection without adding
objects or changing source files. Nested model placements keep their own choice.

For a single mesh, use **Mesh Renderer > Material variant**. The variant must
belong to that mesh’s imported Model. Explicit assignments in **Materials** take
precedence over the variant, including an explicitly empty assignment. Revert a
slot override to let that surface follow the variant again. A surface with no
mapping in the chosen set uses its original material.

Variant assets keep their identity across an unambiguous reimport, including a
name or order change. If a selected variant disappears or belongs to another
model, FORGE reports the problem and keeps the previous usable draw while you
correct the reference. Ambiguous reimports need an explicit correspondence choice.
A prefab instance can override and Revert **Material variant** independently of
other Mesh Renderer fields; selecting an equal value still records intent.

Programmers can set `MeshRenderer::material_variant` through the exact-version
SDK. Authoring tools can call the shared `model.material_variant` command with a
placed root entity and variant AssetId (or null for defaults). Its usual scene
revision, validation and Undo rules apply.

## Animation pointers and import notes

Models may use standard glTF animation channels or `KHR_animation_pointer` channels
for whole-node translation, rotation, scale and morph weights. They use the same
clip selection and Play workflow. Pointers that animate materials, cameras, lights,
visibility, custom metadata or individual vector elements are currently rejected;
export ordinary node animation or remove those unsupported channels first.

Expand **Import notes** in the model document to see notices from the published
revision, including optional extensions FORGE does not evaluate. The first notices
also appear in **Problems** with the asset and source file. An unsupported required
extension prevents publication and keeps the previous usable model.

New imports reject primary vertex colors outside the glTF [0,1] range. Correct
those vertex colors in the source tool and reimport; HDR material color factors
have separate rules and are not narrowed by this check.


## Placement from the Command Palette

With a ready Model import document open, press **Ctrl+Shift+P** and search
**Model document / Place configured model**. This is the same action as that
document's **Place model** button: it uses the selected source scene, animation
clip, material variant and name, places at the world origin, and adds one scene
Undo step. Finish importing and stop Play first. The separate **Assets / Place
selected in Scene** command uses the selected Content asset and the creation
target, with its default placement options.
