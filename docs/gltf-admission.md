# glTF source admission

Phase7's admission helpers validate captured inputs before native Diligent parsing
and conversion. These checks are functioning internal code; they do not yet expose
a completed model importer, decoded image loader, renderer or extension support.

## Exact source contract

The Khronos glTF2 specification at
[`c18432787e6d545a1218c1926ccdcfaffd4c116b`](https://github.com/KhronosGroup/glTF/blob/c18432787e6d545a1218c1926ccdcfaffd4c116b/specification/2.0/Specification.adoc)
defines the container, URI, buffer-view and accessor rules used here. Native parsing
continues to target retained Diligent Tools
`7d1139064f36b14f911e5bca095be9c9dcfc5112`, including its vendored TinyGLTF2.8.10;
no dependency pin or vendor source was changed. Verified2026-09-20.

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
