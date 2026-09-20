# Material values and CPU resources

Phase7 implementation is in progress. The cooked material and CPU resource path
below is implemented; the model import publication, shader reflection adapter,
GPU material effects and editor material document are separate integration work.
This page does not claim those consumers are available yet.

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
