# glTF source admission

Phase7's admission helpers validate captured inputs before native Diligent parsing
and conversion. These checks are functioning internal code; they do not yet expose
a completed model-import workflow or production renderer. Texture decoding has its
own implemented pipeline; the glTF extension support below is private admission.

## Exact source contract

The Khronos glTF2 specification at
[`c18432787e6d545a1218c1926ccdcfaffd4c116b`](https://github.com/KhronosGroup/glTF/blob/c18432787e6d545a1218c1926ccdcfaffd4c116b/specification/2.0/Specification.adoc)
defines the container, URI, buffer-view and accessor rules used here. Native parsing
continues to target retained Diligent Tools
`7d1139064f36b14f911e5bca095be9c9dcfc5112`, including its vendored TinyGLTF2.8.10;
existing dependency pins and vendor source remain unchanged. Meshoptimizer1.2 is
added privately for the ratified compression extension below. Verified2026-09-20.

`GLTF::Document` can retain image metadata with `DecodeImages=false`, but at this
pin recognized external images bypass its file-read callbacks in that mode. FORGE
therefore captures image URIs independently. Native `GLTFBuilder.hpp` offers data
conversion facilities; its direct accessor pointer traversal is not the FORGE
untrusted-input validation boundary. The admission layer does not create a second
runtime scene hierarchy or replace Diligent's renderer.

## Captured source bundle

`capture_gltf_source` accepts JSON glTF2.0 and GLB2 containers by content, including
uppercase filenames. GLB header length/version, chunk bounds/alignment/order,
unique JSON/BIN chunks and zero BIN padding are checked. Unknown optional GLB
chunks are ignored with bounded diagnostics, as required by the specification.

Buffers and encoded images may use contained relative URIs, percent-encoded UTF-8
paths, base64 data URIs, percent-encoded binary data URIs, or admitted GLB/image
buffer views. Relative parent segments are allowed only when the resolved path
stays inside the project. Network/file schemes, authorities, absolute paths,
query/fragment components and nonportable path bytes are rejected; no network
request is made. ProjectPaths also rejects symlink escapes and reserved device names.

External reads become owned byte snapshots with SHA-256 dependency records. Repeated
locators share storage and a cached digest. A later source overwrite cannot change
the captured bytes. The publisher must still compare captured revisions against
current source generations before selecting outputs; capture alone is not a
cross-file filesystem snapshot or a sandbox against a concurrent host writer.

Default bounds:512MiB/file,1GiB aggregate captured bytes,64MiB JSON,4096 physical
source locators,100k entries per root array,64 JSON nesting levels and4million parse
events. Duplicate JSON keys are rejected before insertion using the exact pinned
nlohmann/json3.12 callback depth behavior. Cancellation is checked between bounded
operations. The separate worker still supplies process memory/time enforcement.

Required extensions must be declared used and explicitly supported by the concrete
importer profile; otherwise admission fails with the extension name. Optional
extensions are reported for later capability/semantic handling. Passing an extension
name to this helper is not proof that FORGE implements its semantics. Encoded-image
capture similarly does not validate pixel dimensions, codec data or GPU formats.

## Accessor admission

`validate_gltf_accessors` checks supported core byte/short/unsigned-int/float types,
scalar/vector/matrix dimensions, matrix column padding, component alignment,
interleaved strides, overflow-safe last-element bounds, normalized-type legality,
min/max dimensions and numeric ranges. Source-free accessors represent implicit
zeros; defining byteOffset without a bufferView is rejected.

Sparse indices must use an unsigned integer type, increase strictly and remain
inside the accessor count. Sparse index/value ranges cannot declare a stride or
GPU target. Both base and sparse float data must be finite. Limits bound individual
element counts and aggregate component counts without allocating decoded arrays.
Errors identify the affected accessor. Mesh semantic constraints, index-to-vertex
bounds, animation requirements, joint/weight compatibility and extension decoding
remain additional importer validation, not claims supplied by this helper.

## Native loader and conversion

`forge_gltf_native` is a private tooling target. `NativeGltfDocument` validates
accessors, creates a virtual manifest backed only by captured bytes, and invokes
Diligent's native `GLTF::Document` with image decoding disabled. It never supplies
the native parser with original filesystem paths. Buffer and image callbacks
reject requests outside that manifest; original source/provenance JSON remains
unchanged. A regression deletes source files before native loading to prove the
loader uses the captured inputs. Encoded-image fixtures intentionally do not claim
successful pixel decoding.

Float attributes and exact unsigned integer IDs are converted through native
`GLTF::VertexDataConverter`, with explicit handling of padded matrix columns,
interleaved strides, zero-backed values and sparse patches. Integer indices never
take a float round trip. Conversions enforce a512MiB output budget per accessor;
the eventual importer/worker must additionally enforce aggregate/process budgets.
No native Model/Node becomes a Flecs gameplay object.

Configure `FORGE_BUILD_ASSET_TOOLS=ON` to build this native tooling target and its
tests independently of the editor. It defaults to the editor setting. On Linux,
native Core requires Vulkan to be configured and contributes static archives,
but tests run without a display, usable driver or device creation. The standalone
model profile disables shader compilers, optional codecs, archiver and unrelated
rendering facilities. It does not claim shader import or complete model import.
Editor builds reuse their existing native targets and shader compiler profile.

## CPU primitive processing — integration in progress

The private native adapter now extracts bounded CPU primitives using admitted
native conversion. Core POSITION/NORMAL/TANGENT, consecutive UV/color sets,
joint/weight sets and application-specific underscore attributes retain their
actual component counts. JOINTS use exact integer streams. Extra joint/weight
sets remain import inputs for the explicit skin processing policy below.
Unsupported core formats are diagnosed;
`KHR_mesh_quantization` adds its signed/unsigned8/16-bit position and UV formats,
signed normalized normal/tangent formats and signed morph deltas. It must be
declared required. Native conversion preserves integer-versus-normalized meaning;
normal/tangent directions are normalized after quantization, tangent W is retained,
and bounds are recomputed from decoded values. Quantized declared bounds are
normalized before comparison. No hidden extra dequantization transform is added: the
source node/inverse-bind/texture transforms retain that responsibility.

Indexed byte/short/int and nonindexed sources are supported. Index references,
forbidden source restart values, attribute counts, normalized directions, tangent
signs, semantic formats and vertex/index view roles are checked. Bounds are computed
from positions; disagreement with declared source bounds produces a diagnostic.
Core COLOR_0 is clamped to its required range; additional color streams are retained.

Points, line lists and triangle lists retain their type. Line loops/strips become
line lists, and triangle strips/fans become triangle lists with winding preserved.
No line or point source is interpreted as triangles. The CPU result currently uses
uint32 working indices; efficient cooked index-width selection remains later work.

Morph streams preserve supported position/normal/tangent, optional UV/color and
custom deltas with matching base attributes/counts. Mesh target counts/default
weight dimensions are checked once per document, avoiding repeated whole-mesh
checks for each primitive. These CPU streams are not evidence of rendered morphs,
skinning, generated tangents/normals, immutable Mesh artifacts or model instantiation.

Current working-data limits are64 attributes/primitive,256 morph targets and512MiB
decoded data/primitive. Native processing has sanitizer-tested semantic/malformed
fixtures; production importer/cooker/profile and editor integration remain open.


## Hierarchy and source transforms

Native tooling validates disjoint node trees, single/unique parents, child and
asset indices, root-only scene membership, optional default scenes, and mesh/node
morph defaults. A node may participate in several scenes. Iterative traversal and
binary ancestor lookup handle deep hierarchies without recursive stack growth or
repeated long parent scans. Limits include100k nodes and1m aggregate hierarchy,
joint and scene-membership work entries; the100k-deep fixture is tested.

Both column-major source matrices and TRS are admitted. Matrix/TRS coexistence,
non-affine matrices, matrix shear/singular columns, nonfinite values and invalid
quaternions are rejected. Source TRS reflection and zero scale remain intact in
private CPU data. Phase7 approval expands the existing LocalScale numerical domain
to[-10000,+10000], including zero/tiny magnitudes, with independent inverse and
physics admission. Model publication, runtime skinning and full rendering acceptance
remain integration work; clamping or taking absolute visual scale is not a conversion.

Perspective camera source parameters include optional infinite far plane/aspect;
orthographic parameters preserve signed nonzero magnification. Invalid clipping,
projection and node references fail. These are source metadata checks, not a new
Camera ECS component or renderer. Source camera-forward remains−Z; conversion to
FORGE's+Z camera convention is still an importer-instantiation responsibility.

## Skin bindings and influence preparation

Skin joint order remains source order. Duplicate joints, disconnected joint roots,
invalid skeleton ancestors, and missing skeleton-root membership in a scene that
contains its skinned mesh are rejected. Inverse binds require float MAT4, enough
entries, finite affine values and a non-vertex/index buffer layout. Omitted inverse
binds become identity. Extra source matrices are validated, without inventing extra
joints. Source skin binding admits up to65536 joints; that is not the draw limit.

A private CPU preparation operation validates all joint indices, including unused
zero-weight slots, rejects duplicate positive influences/zero-total weights, and
normalizes renormalizable float weights with an affected-vertex count. Finite
positive float weights above1 are accepted: the exact specification requires
nonnegative values, while a float sum of1 is a recommendation. Prepared render
weights must still be normalized. Pure
quantized weight sets must sum exactly to one before further processing. Source
influences are not silently removed: `Reject` refuses more than four positive
influences, while explicit `ReduceToFour` chooses highest weights, breaks ties by
source joint index, renormalizes, and reports reduced vertices.

The resulting draw-local palette references ordered source joints and has at most
256 entries. A larger draw is rejected as requiring partitioning; partitioning is
not yet implemented. The full skeleton may be larger when one primitive uses only
a bounded subset. Skin preparation bounds output to512MiB and examined influences
to128Mi entries. This is not evidence of rendered skinning, an Ozz compatibility
mapping, palette uploads, or completed Mesh artifacts.

## Animation admission and decoded tracks

Core translation/rotation/scale and morph-weight channels preserve LINEAR, STEP
and CUBICSPLINE mode and data. Time inputs are finite, nonnegative and strictly
increasing, with checked source bounds; cubic inputs require at least two keys.
Output shape/count/type follows the exact glTF channel contract, including legal
normalized-integer rotation/morph outputs. Quaternion **values** must be unit;
cubic in/out tangents are retained without normalization or sign rewriting.

Duplicate node/path targets, TRS channels targeting a matrix-authored node,
invalid accessor/sampler/node references, and morph channels without matching
mesh targets fail. Core channels with no node target remain unbound with a
diagnostic; this is not support for `KHR_animation_pointer`. Shared accessors own
one decoded byte payload per clip; input/rotation validation is also shared,
preventing repeated large scans per channel. Decoded clip data has a512MiB cap.

The CPU result is an admitted source track description. Ozz conversion,
morph evaluation, runtime playback, cooked animation artifacts and visible
skinning remain integration work. Admission tests cannot substitute for those
end-to-end checks.

## Official negative-scale fixture

The unmodified Khronos NegativeScaleTest from Sample Assets revision
`c6a6bd13ab2b3c685c7903d03561b8a9392f38b8` is retained under
[samples/gltf/NegativeScaleTest](../samples/gltf/NegativeScaleTest/README.md), with
CC-BY-4.0 attribution, exact hashes and source provenance. The native test checks
real source capture, geometry, parent composition and nested determinant parity.
CPU admission is separate from full textured/PBR WARP rendering acceptance.

## Ratified meshopt compression — private native import

The exact official [EXT specification](https://github.com/KhronosGroup/glTF/blob/c18432787e6d545a1218c1926ccdcfaffd4c116b/extensions/2.0/Vendor/EXT_meshopt_compression/README.md)
and meshoptimizer1.2 source define this path. `EXT_meshopt_compression` supports
ATTRIBUTES, TRIANGLES and INDICES, plus NONE, OCTAHEDRAL, QUATERNION and
EXPONENTIAL filters. It can decompress ordinary attributes, indices, morph data,
animation data or other valid buffer-view contents before normal accessor checks.

Source capture represents required URI-less fallback buffers without allocating
those declared bytes. Native admission validates every fallback reference, compressed
range, count, stride, mode, filter and EXT bitstream header. An additional aggregate
512MiB decoded-view budget bounds output allocation; original source capture and
worker/process budgets remain independent. Cancellation is checked between views
and during filter admission. Native codec calls themselves complete synchronously
inside the disposable import worker.

FORGE rejects filter inputs for which the specification leaves decoding undefined:
octahedral/quaternion precision markers and component ranges, and exponential
exponents outside[-100,+100]. Native filtering follows admission. No codec is
reimplemented or patched. Subsequent accessor checks reject nonfinite data;
primitive checks reject invalid indices, topology, weights and semantic formats.
Successful native decompression alone is not asset validation.

Decoded views use a separate temporary native transport. Original source JSON,
compressed bytes and dependency digests remain unchanged. `encoded_images()`
exposes admitted encoded image bytes even when their buffer view was compressed;
`source()` remains captured provenance. Runtime resources consume cooked Mesh and
Texture artifacts, not compressed glTF or meshoptimizer objects.

`KHR_meshopt_compression` is a **release candidate** in the checked registry;
it is not enabled as a required extension. Its vertex bitstream v1 and COLOR
filter are not silently admitted under the ratified EXT name. Required KHR files
are rejected. An optional extension may use its ordinary, valid uncompressed
fallback. This distinction is a registry/version boundary, not absence of a decoder
in meshoptimizer1.2. Re-evaluate when the official extension is ratified.

Regression evidence covers actual source capture, native conversion, index modes
and widths, filter decoding, undefined filter inputs, truncated streams, rejected
newer formats, bounded output, cancellation and unchanged provenance. No complete
Content model-import UI or GPU renderability is implied by these private tests.

## Mesh preparation and attribute preservation

Native mesh cooking now uses the private shared mesh preparation pass. The default
recipe retains supplied normals/tangents, generates missing flat normals and
MikkTSpace-compatible tangents when normals and the selected UV set are available,
welds exactly identical vertices, and optimizes vertex fetch. Explicit recipes can
preserve all streams/order or recalculate flat normals/tangents. Normal-map UV
selection is per material slot; a selected missing UV set is an error. Automatic
preparation without UVs does not invent a texture mapping or tangent stream.

Missing normals require flat shading under glTF; existing tangents become invalid
when those flat normals are generated. Each morph target gets its own generated
flat normal and tangent delta where applicable. Morph tangent W remains the base
sign, as specified by glTF; endpoint sign differences are diagnosed. A collapsed
face has no unique normal, so it receives a finite+Y fallback and a diagnostic;
a zero target normal uses the base direction during tangent preparation. Neither
case claims a physically meaningful surface direction for a collapsed face.

The exact meshoptimizer tangent API is **experimental within stable1.2** and stays
behind the private tool boundary. FORGE selects its Mikk-compatible weighting.
Positions and UVs are scaled only in temporary native working copies,
using a positive uniform power-of-two scale to avoid float intermediate overflow/underflow.
The inverse scale must reproduce each source float exactly; it cannot silently
collapse distinct source coordinates or lose tiny nonzero values.
The original cooked position and UV values stay unchanged. The generated direction
must still be finite/unit length. Unrepresentable ranges fail the candidate.

Tangent output is per corner, so mirror seams may split vertices. Every base stream,
exact integer joint index, skin-weight set, custom channel and morph stream follows
the same remap. Native custom equality compares all channels; it is not restricted
to the sixteen streams of meshoptimizer's Multi convenience API. Bounds are
recomputed after unused vertices are removed. Material slots, morph labels/defaults
and primitive topology remain intact.

Primitive/corner order is retained by default. Triangle-cache reordering requires
an explicit order-independent material-slot list; transparent materials must not
be opted in blindly. This pass does not perform lossy simplification, manufacture
LODs, change scene transforms or allocate logical asset identities. Corner expansion
and output remain within configured Mesh limits, and native allocations remain
subject to the import worker's hard memory/time limits. Failure/cancellation leaves
the admitted input mesh unchanged.

## Draco compressed primitives

The private model tool uses official Draco1.5.7
[`8786740086a9f4d83f44aa83badfbea4dce7a1b5`](https://github.com/google/draco/tree/8786740086a9f4d83f44aa83badfbea4dce7a1b5)
for ratified `KHR_draco_mesh_compression`, using the glTF bitstream2.2 build
profile. Native Draco owns compression decoding; FORGE owns source bounds,
accessor/mapping admission and the decoded transport. No codec objects enter
runtime resources, persistent identities or the gameplay SDK.

The retained Diligent TinyGLTF Draco bridge is disabled. At that exact revision
its optional bridge dereferences a unique-ID lookup without checking for a missing
attribute and uses a four-element conversion array without first admitting the
decoded component count. FORGE instead calls the official Draco decoder privately,
checks its result, and supplies ordinary admitted buffers to Diligent. This changes
no vendor source or pin.

Checks cover declared extension use, compressed view ranges, scalar/vector layouts,
exact component types and normalized flags, matching vertex counts, unique attribute
IDs, mapped-value bounds, decoded face indices and representability in the declared
index width. Output is limited to512MiB with at most16million vertices and16million
triangles per decoded primitive. Native intermediate allocations are additionally
subject to the disposable worker's hard memory/time limits; the output limit alone
is not a claim of prevalidating every native allocation. Upstream writer alignment padding (at most three zero bytes to a four-byte boundary)
is accepted; other trailing compressed data, invalid decoder results and cancelled candidates are rejected.

Attributes outside the compressed map, sparse attribute patches and morph targets
continue through ordinary glTF admission. Reused accessors must have identical
decoded values; conflicting declarations are rejected. Compressed accessors ignore
their old buffer-view/offset as required by the extension. The original captured
JSON, buffers and dependency provenance remain unchanged.

Draco returns triangle connectivity. The private transport normalizes both source
TRIANGLES and TRIANGLE_STRIP to that triangle list, including an explicit index
accessor when the source omitted one. It does not interpret decoded face triples
as a strip. Indexed triangles must match their declared index count; a strip must
have sufficient declared corners for its decoded faces, allowing degenerate strip
connectors. Sparse strip-index replacement is rejected because a patch indexed
into the discarded strip sequence cannot be applied reliably to native face order.
This limitation has an explicit diagnostic; no replacement strip or guessed patch
is silently invented. It does not affect sparse vertex-attribute patches.

This is private import admission, not yet a completed Content model workflow or
GPU preview. Full model-worker integration and publication validation remain
Phase7 completion requirements.

## Material factors, bindings and variants

Private surface admission checks core PBR factors and the pinned Khronos schemas
for clearcoat, specular, sheen, anisotropy, iridescence, transmission, volume, IOR,
dispersion, emission strength, unlit and the archived specular-glossiness workflow.
Unsupported values reject before native material loading. Diagnostics identify the
material or sampler and the affected numeric field. This CPU boundary does **not**
claim all these effects are implemented in the production renderer.

Native `GLTF::LoadMaterial` supplies supported factor conversion. FORGE keeps
texture bindings separately, with candidate-local texture/image addresses,
color/data/normal usage, sampler state and offset/rotation/scale/UV-set values.
This prevents the native three-bit packed selector from truncating a valid source
UV-set index. Actual mesh UV availability and renderer profile compatibility still
have to pass before publication. Zero and negative texture scales are preserved.
Normal-map and clearcoat-normal scale may be negative under the source schemas.

Bindings preserve all six glTF minification choices, both magnification choices
and repeat/mirror/clamp-edge wrapping. Explicit non-mip filters clamp maximum LOD
to zero. Unspecified filters choose linear magnification and trilinear minification.
Sampler intent belongs to the binding; two references to the same image can use
different samplers and semantic variants without requiring duplicate image identities.
Basis KTX2 and WebP image alternatives are selected explicitly, preferring Basis
when both exist. Their declaration, fallback requirement, source index and MIME
are checked; the selected image still requires real codec admission. Failure is
not silently replaced with another fallback image.

Source alpha mode remains independent of transmission, unlike the native loader's
convenience blend-mode choice. Occlusion strength is retained explicitly; the pinned
native factor loader leaves it at its default. Emission color and strength remain
separate values so animation and authoring do not lose independent channels.
Standalone IOR and dispersion are preserved even where the native CPU layout has
no matching independent field. Missing volume attenuation distance remains absent,
meaning infinite distance, rather than becoming a finite authored constant.

Schema-backed numeric cases include IOR zero or at least one, alpha cutoff above
one, specular color above one, and iridescence thickness minimum above maximum.
IOR zero is the specification's constant-Fresnel compatibility mode, not physical
zero refraction; renderer/animation support must honor that special contract.
Values must remain finite and representable in the selected float parameter profile.
Cross-extension combinations forbidden by the specifications reject; legacy
specular-glossiness with emission strength remains valid.

Unlit bindings retain only base color. Legacy specular-glossiness bindings replace
the metallic-roughness fallback textures, preserving their native workflow defaults.
Original source fields and unknown metadata remain captured provenance.

Material variants retain their names and candidate-local primitive/material
mappings. Duplicate display names are allowed; they are not identity. A variant may
occur only once across a primitive's mappings, all references must be in range,
and missing mappings retain the ordinary primitive material. Publication must bind
these addresses through stable subasset identity; this helper alone does not assign
AssetIds or expose a finished variant-selection UI.

The private [model candidate stages](model-import.md) now exercise these native
mesh/material/image consumers together, with immutable source transport and
reorder-safe identity evidence. Full model publication/rendering remains in progress.

## Official fixtures and comparison

[glTF validation evidence](gltf-validation.md) records the pinned18-asset official
corpus, native mesh/material/image/skin/animation processing coverage, exact licenses,
and external Khronos validator comparison. The actual production extension profile
is supplied to source admission; an unconfigured required-extension allowlist must
continue to reject Draco/quantized inputs. Validator warnings and unsupported Draco
validation are preserved as distinct evidence from FORGE's native codec checks.
