# Material values and CPU resources

Cooked materials feed the shared mesh renderer. Standalone source authoring and
publication, the central Material document, native previews and mesh-slot assignments
are implemented. The [renderer contract](rendering-foundation.md) records executed
WARP coverage; final combined package validation remains a release gate.

## Asset selection and values

A logical Material has an AssetId. Its selected immutable artifact stores a
material-model key, alpha/depth/culling intent, typed scalar/vector/linear-color
parameters, and named texture slots. Stable parameter and slot keys are independent
of UI labels or source array positions. Linear color factors are never decoded as
sRGB texture samples.

The `FRGMAT` version1 envelope uses the same bounded little-endian envelope as
Mesh/Texture, with up to1MiB of JSON metadata and no binary payload. It admits at
most256 numeric parameters and64 texture slots, bounded256-byte identifier keys,
finite float values, and exact vector widths. Unknown enum values, malformed
counts, duplicate JSON fields, nonzero unused parameter lanes, trailing bytes,
truncation, overflow and nonzero-to-zero float underflow reject. Parameters carry
explicit scalar, vector2/3/4 or linear-color3/4 types.

Texture slots store semantic usage, dimension, per-binding sampler and UV
set/offset/scale/rotation. Signed/zero UV scale is valid. The renderer must separately
validate supported stream availability and its shader profile; storage does not
narrow a UV set to Diligent's native packed glTF selector bits. Sampler serialization
and validation are shared with Texture assets without changing existing cooked
Texture bytes.

## Identity-neutral artifacts and typed binding selections

The cooked material does **not** bake a source image index or newly allocated UUID
into shared derived-cache bytes. The publication selection binds each texture-slot
key to an `AssetRef<TextureAsset>` in its existing authoritative catalog dependency
metadata. There must be exactly one nonempty typed binding per used slot, with no
unknown bindings. One Texture AssetId may fill multiple slots with different
semantics/samplers. Its semantic variants remain one logical asset.

This is the existing separation of logical identity, build artifact and loaded
resource, not another identity scheme or dependency graph. Publication generation
already increments for sidecar-only identity decisions. Consumers must pass that
generation to the existing ResourcePool request even when the cooked file digest
is unchanged. The immutable resource owns a copy of its binding selection; an old
lease keeps the old bindings after a newer selection is adopted.

## Layout compatibility and failure

`MaterialLayout` is explicit compatibility input supplied by a material-model or
shader adapter. The CPU validator checks the model key, complete parameter keys
and types, texture semantics/dimensions and required slots. Vector4 and linear
color4 are intentionally different types. Optional texture slots may be absent.
This validator does not discover Diligent reflection, impose material-model numeric
ranges, or prove a GPU effect exists. Those responsibilities belong to the actual
model/shader adapter before publication/adoption.

The resource loader bounds file reads, checks the cooked digest, validates values,
bindings and layout, then returns an immutable CPU candidate. It does not parse
glTF, allocate UUIDs, mutate the catalog or create device objects. Existing pool
compatibility/generation/cancellation rules control adoption. Invalid layout or
corrupt content leaves the last good material selected. CPU memory estimates
include map storage allowances but are not exact allocator measurements.

## glTF cooking

The private native adapter cooks already admitted glTF factor/binding values to
this format. It preserves explicit authored alpha mode, cutoff, double-sided state,
linear base/specular/emission colors, independent emission strength, and all
admitted extension factors. Alpha cutoff has no arbitrary upper bound of1.
Texture bindings retain normal/data/color distinctions and source UV transforms.
Opaque/masked materials default to depth writes; blended materials default to no
depth writes. The generic artifact stores explicit state for other material models.

Model keys `forge.gltf.metallic-roughness.v1`,
`forge.gltf.specular-glossiness.v1` and `forge.gltf.unlit.v1` identify the imported
value interpretation. They do not assert that every stored extension is implemented
by the current renderer. See[glTF admission](gltf-admission.md) and
[the rendering contract](rendering-foundation.md).

## Standalone source document and instances

`*.material.json` uses `kind: forge.material`, integer `version: 1`, the logical
`asset_id`, optional typed Material `base`, and an `overrides` object. This is a
new asset source format; it changes neither scene nor prefab identity/schema.
The base is a published root Material or imported model Material member.
No separate instance identifier or ECS prefab hierarchy is introduced.

The optional override fields are `model`, `alpha`, `alpha_cutoff`, `double_sided`,
`depth_test`, `depth_write`, `parameters` and `textures`. Value types and enum
ordinals use the existing material value projection above. Each parameter stores
its explicit type and exact-width value array. Each texture override stores a
typed `asset` UUID and a `slot` with the existing semantic/sampler/UV settings.
The source remains sparse; generated shader defaults are never saved as overrides.

- Absent field: inherit the base value, or use the built-in model default.
- Explicit equal-value field: retain override intent across future base changes.
- Null parameter: explicitly reset to the model default, including absence for infinite attenuation distance.
- Null texture: explicitly remove the inherited texture.
- Revert: remove the override field. This differs from reset-to-default or clear.
- Unknown document/extension fields: retain unchanged. Unknown model parameter or texture keys reject instead of pretending to affect rendering.

The root default is metallic-roughness with the renderer's existing factor defaults.
A root Blend material defaults to disabled depth writes unless explicitly authored.
An instance inherits depth intent; changing its alpha mode does not silently erase
an independently inherited or overridden depth setting. Changing material model
must leave a compatible parameter/texture set; incompatible inherited fields reject.

Admission is bounded to1MiB,32JSON nesting levels,32768 structural entries,
256parameters and64texture slots. Duplicate keys, nonfinite/unrepresentable values,
wrong enum/type/width, self-reference and unsupported source versions reject.
These are bounded document/admission profiles, not universal renderer capacity claims.

## Resolution, publication and ownership

The existing CPU import queue resolves against the selected immutable base revision.
It never recursively reads unfinished base source edits. The catalog's existing
typed dependency graph owns base Build edges, texture Runtime edges, cycle checks
and reverse invalidation. Rebuilding an instance includes the base revision in its
cache key. Updating a base invalidates its dependents; their previously published
revisions remain usable until each replacement passes validation and publishes.

The output bundle contains the existing identity-neutral `material.values` envelope
and a separate `bindings.json` containing the checked logical owner/texture mapping.
The catalog copies and verifies that mapping against the selected bundle; neither
is a second editable authority. Runtime loads only immutable cooked data through
the existing ResourcePool, shared by model, engine and root Material consumers.

Source Save and cooked publication are separate ownership events. Saving an invalid
or unfinished source must not be described as a successful renderable publication.
The shared AssetPublisher retains the prior catalog/artifact on invalid, stale,
cancelled or incompatible candidates. MaterialDocument owns bounded source history
(64 entries and8MiB per history stack), explicit revisions and exact saved-byte
conflict checks. Scene Undo cannot undo asset publication. Explicitly declared
[material-surface Shaders](surface-shaders.md) add a version2 source and compiled
snapshot path. General stage programs are not automatically compatible surfaces.

Texture preflight uses the existing cooked Texture resource provider and checks
actual image dimension and requested semantic variant before material publication.
It loads one selection at a time, releases each lease before the next, observes
cancellation and has a60-second total deadline and512MiB pool admission budget.
This budget is not an RSS measurement. Invalid selections retain the previous
publication, not just the previous rendered frame.

## Editor consumers

The central Material adapter routes Save and history to its source owner, with an
independent close guard. Unsaved preview values enter a separately opted-in
MeshResourceHost through immutable copied material selections; they never publish
to the project catalog or mutate a scene. The host uses the shared FrameRenderer,
mesh/texture resources and complete-bundle adoption. Bad replacements retain the
last good preview. Preview geometry/camera/environment are transient editor values.

Inspector asynchronously resolves copied logical mesh bindings and releases the
mesh payload immediately. Assignment uses the existing `property.set` operation
on `MeshRenderer.materials`, preserving scene history and prefab field intent.
An equal/default-valued assignment remains explicit; removing it resumes the mesh
default. Unresolved keys remain authored and visible. No physical slot ordinal is
saved as a logical key, and no second asset registry is introduced.


## Built-in shape authoring and compatibility

Explicit primitive creation recipes now compose local TRS and MeshRenderer, assign
an immutable engine Mesh UUID and a surface Material UUID, and omit Primitive/Tint.
Navigation Surface's plane uses the same composition plus NavigationSurface.
Planar shapes use the two-sided default. No material asset file is created as an
incidental scene edit; material sources retain independent history/publication.
The pre-Phase7 `entity.create` kind/default form retains its legacy composition for
existing command clients; current editor creation routes all pass explicit recipes.

Old Primitive/Tint scene/prefab bytes remain authored authority for those objects.
Presentation adapts their effective values to built-in Mesh resources and the legacy
surface, preserving tint and independent native inheritance without dirtying or
rewriting source documents. Explicit MeshRenderer always wins, even when disabled,
empty or invalid. Inspector omits legacy controls in that case to avoid ignored
writes. Built-in geometry queries recognize the assigned engine mesh; arbitrary
imported meshes do not silently become a cube. Navigation's existing bounded
built-in-geometry profile accepts equivalent built-in MeshRenderer sources.

## Built-in resources and failed draws

Engine-owned white, black, flat-normal and checker Texture assets have reserved
AssetIds for each supported dimensional shape. They use the ordinary typed asset
lookup, CPU resource pool, semantic variant key, Diligent upload and GPU retirement
path. Material source dependencies can reference them without project catalog rows
or authoring files. Their immutable engine recipe revision is captured and checked
by publication and survives source-free content packaging. Existing primitive-v2
and material-v1 recipe digests retain their exact meaning.

A first-load draw with a missing/failed Material uses an unlit magenta error Material.
A missing/failed texture uses a checker for color, flat normal for normals, white
for data, or black floating-point data for HDR color. The fallback matches the
material's declared dimension. Inconsistent demands for one texture reference are
rejected, not guessed. Initial GPU surface preparation can retry once with the
error Material; failed error preparation stops with a diagnostic. Missing meshes
remain omitted with a diagnostic.

Fallbacks have their own engine resource identity. Detached draw candidates record
transient substitutions; authored AssetRefs, catalog revisions and material source
values are never rewritten. Cache keys include the physical leases and substitutions.
Once a real authored draw has succeeded, a later failed replacement retains that
complete draw instead. A fallback is not a known-good authored revision, so a later
valid selection can replace it. Stale catalog candidates are still rejected.

Pinned Diligent PBR BRDF/environment defaults remain presentation-owned resources.
They are not duplicated as project assets merely to increase asset-type coverage.
These policies use shared Diligent abstractions and do not introduce native backend
handles or descriptor layout assumptions.
