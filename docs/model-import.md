# Model import stages

Phase7 model integration is in progress. Native geometry preparation and official
Ozz conversion feed one validated family through the shared import service/CLI.
Runtime model binding, rendering and the model-document UI remain required work.

## Immutable source transport

The private glTF snapshot codec transfers the already captured source to a native
worker without reopening project paths. A bounded source-description file and
capture index accompany flat generated blob files. Shared GLB backing storage is
copied once, preserving buffer/image byte ranges and source dependency provenance.
Meshopt fallback buffers and image views remain unallocated placeholders until
native decompression. Their ranges must match the declared buffer view.

Decoding checks file counts and aggregate bytes, names, lengths, digests, bounded
JSON structure, resource counts, image/view agreement and cancellation. Unknown
files and duplicate names reject. This transport is not a persistent asset format
or a native extension ABI. Source JSON and original files remain unchanged.

## Geometry candidates

The private native stage prepares a single candidate containing:

- Separate Mesh, Material and referenced Texture members.
- Material texture-slot bindings and mesh material-slot assignments.
- Texture semantic variants under one image member, with per-material samplers.
- Original node labels and parent relationships, scene roots/default selection,
  morph defaults, explicit local TRS and derived affine transforms in FORGE row-major3x4 storage.
- Content and semantic-usage evidence for subsequent existing subasset reconciliation.
- Bounded diagnostics for processing and unreferenced images.

The cooked hierarchy belongs to an immutable asset. It does not replace Flecs
ChildOf/Parent or add another authored transform authority. The scene/runtime
consumer must resolve the selected member bindings before use.

A geometry candidate with skin or animation data is explicitly incomplete. The
complete validator refuses publication until the following animation stage succeeds.
Unreferenced
images remain in source and receive a diagnostic instead of an unused runtime
texture. Original material/mesh labels are preserved for display.

## Identity and dependencies

Candidate addresses such as `/meshes/3` only bind outputs of this one candidate.
They are not persistent IDs. The existing reconciliation service allocates/reuses
AssetIds and can reject ambiguous correspondence before owner-thread publication.

Mesh content evidence excludes source material indices, primitive order and display
labels. Separate usage evidence describes instancing contexts. Material evidence
uses cooked values and encoded-image content, not source image indices; usage
records associate materials with geometry. Texture evidence records image content
and material usage roles. Duplicate evidence does not authorize guessing which old
asset a new object represents. Explicit identity decisions remain necessary where
correspondence cannot be established.

The private bundle validator checks the complete typed reference set, nondefault
material slots, texture semantic/dimension compatibility, morph-default counts,
acyclic node hierarchy, finite composed transforms, file digests and total budget.
Unexpected or missing output files reject the whole candidate. The native stage
caps each cooked file at252MiB, reserves16MiB for index data, and bounds members and
outputs. The process supervisor enforces native memory/time limits as described
below. These checks precede the catalog publisher's atomic commit and stale-source
checks.

Multiple texture bundles share the same flat model artifact directory. Their
existing semantic filenames may now have a bounded lowercase/digit/hyphen/underscore
prefix. This avoids collisions without creating another texture-index format or
using filenames as logical identity. Existing unprefixed Texture bundles remain
compatible. Directory separators, dots in the prefix, reserved/path-like names and
semantic-suffix mismatches reject.

## Format boundaries

Core glTF image bindings require PNG/JPEG signatures; WebP and Basis bindings
require their corresponding declared extension and bytes. A declared MIME type
must match the bytes. The standalone image importer's wider format support does
not silently broaden glTF. Shared PNG/JPEG/WebP images can produce color/data/normal
variants. Basis data-format descriptors must match each usage, as required by the
KHR_texture_basisu specification; contradictory color/non-color reuse rejects.

The glTF source specification explicitly allows zero scale through TRS, but forbids
an all-zero column in a node's authored `matrix`. Existing admission preserves that
distinction. Derived cooked matrices may be singular or acquire shear through
hierarchical TRS composition; no approximation is introduced by this stage.

See[glTF admission](gltf-admission.md),[subasset identity](subasset-identity.md),
[publication](asset-publication.md),[materials](material-assets.md) and
[textures](texture-assets.md).

## Model worker and publication

The fixed `forge.model.gltf` recipe now runs through `forge_asset_build`, the same
supervised executable used for textures. Its registry entry is available to the
shared import service and headless `forge_tools --assets import` command. The
recipe includes skin/animation conversion when needed. Camera/light and variant
data are preserved as described below; their editor/runtime realization, model
documents and GPU rendering remain required Phase7 work.

Discovery captures source and external dependencies. Before process launch, a
second immutable capture must have the same source/settings/dependency build key.
The child reads only generated staging files, selects a compiled-in recipe, and
cannot choose its own limits or mutate the project catalog. Model limits are4GiB
process memory,512MiB per staged file,2GiB aggregate staging,16384staged files,
240seconds wall/220seconds CPU. Staging includes both input and output files; cooked
candidates remain bounded to4096files/512MiB total and256MiB per cache file. The
static cooker leaves further envelope headroom. Texture process limits stay smaller.
Both the cache lookup and publisher use the same descriptor-bounded cache policy.

Recipe fingerprints cover exact codec selection/options, source/admission/processing
code and compiler/configuration. Changes invalidate disposable artifacts rather than
reinterpreting previous bytes. Required extensions are admitted only by implemented
CPU preservation/codec paths. This is not a declaration of corresponding GPU shader
support; rendering must validate its own feature profile before use.

On the owner thread, complete bundle validation precedes subasset correspondence.
The Model root and active members use typed catalog Runtime edges for revision-local
bindings; the catalog adds Subasset ownership edges. The legacy dependency projection
contains the same unique target IDs. Cooked slot addresses never become durable IDs.
Mesh slots bind Material assets, and material texture roles bind Texture assets.
Removed members remain tombstones, preserving their identities and diagnostics.
There is no second member-reference map stored as arbitrary metadata.

Ambiguous correspondence returns structured same-type candidate addresses and prior
AssetIds. Publication leaves both catalog and sidecar unchanged until explicit choices
resolve it. A choice may retain a previous same-type member or deliberately allocate
a new one. Even a cache hit still performs correspondence, stale-input checks and
compatibility preflight. When both the complete input key and compiled model index
match the currently published revision, existing catalog member bindings supply
correspondence after sidecar, type, ownership and revision validation. This safely
preserves even identical unkeyed members on an unchanged reimport. A cache hit alone
is insufficient: changed source/settings/tool revisions use the regular evidence and
explicit-choice resolver. Explicit choices, including creating a new identity, always
take precedence. Inconsistent selected metadata rejects before publication.

The CLI owns no live renderer or world; its compatibility callback has no live-resource
work. Editor/runtime callers must still supply their actual compatibility preflight.
Single-writer ownership, whole-family publication, interruption recovery and unknown
metadata preservation use the existing AssetPublisher unchanged in scope. Import
publication is separate from scene Undo.


## Cameras, punctual lights and variants

Immutable model data now preserves camera definitions and node camera references,
directional/point/spot light values and node references, material variants and their
primitive mappings, and independent node visibility/selectability flags. Source and
cooked camera/light values use the same CPU admission rules; the parent revalidates
all counts, indices, projection/cone ranges and typed member bindings.

Omitted perspective far planes and aspect ratios become explicit null values in the
cooked index, retaining infinite-far and automatic-aspect meaning. Orthographic
magnifications must be nonzero; negative values are valid under the exact glTF
specification. These are asset values, not a new camera projection implementation.
The future camera extractor must handle the specification's undefined view for
singular, reflected or sheared camera transforms without changing authored visual TRS.

Punctual light color remains linear[0,1]. Intensity is nonnegative, in lux for
Directional and candela for Point/Spot. Point/Spot range is positive or absent/infinite;
Directional has no range. Spot angles satisfy0<=inner<outer<=pi/2. Valid inactive
spot parameters on another light type are admitted then omitted from the cooked active
values; source bytes remain intact. Light properties are not multiplied by node scale.
No rendered-light or extension-conformance claim follows from retaining these values.

Visibility and selectability are distinct booleans, each intended to cascade through
ancestors. Visibility hides visual features including lights, but does not disable
cameras or selection. Current model data preserves local intent; ECS/render/picking
consumers must apply the documented semantics before those workflows are available.

Material variants retain display labels (which may duplicate) and candidate-local
mesh/primitive/material mappings. Alternate materials join the same mesh dependency
graph. Both base and variant bindings require their referenced UV streams to exist.
The exact DiligentFX PBR implementation derives tangent frames from material UV
position gradients, allowing alternate normal-map UV sets without rejecting the
model because a single stored base-material tangent stream exists. GPU integration
still must prove that native behavior in FORGE's render profile.

Evidence: [glTF camera specification at the selected revision](https://github.com/KhronosGroup/glTF/blob/c18432787e6d545a1218c1926ccdcfaffd4c116b/specification/2.0/Specification.adoc#cameras),
[punctual lights](https://github.com/KhronosGroup/glTF/tree/c18432787e6d545a1218c1926ccdcfaffd4c116b/extensions/2.0/Khronos/KHR_lights_punctual),
[node visibility](https://github.com/KhronosGroup/glTF/tree/c18432787e6d545a1218c1926ccdcfaffd4c116b/extensions/2.0/Khronos/KHR_node_visibility),
[node selectability](https://github.com/KhronosGroup/glTF/tree/c18432787e6d545a1218c1926ccdcfaffd4c116b/extensions/2.0/Khronos/KHR_node_selectability),
and[DiligentFX PBR normal handling](https://github.com/DiligentGraphics/DiligentFX/blob/aaa41d47a101d0bf1d12267c4a85b2d9b38cd1da/Shaders/PBR/public/PBR_Shading.fxh).


## Canonical skeletal converter input

The private model animation adapter retains skin joints, animation targets and
all required ancestors in one bounded conversion rig (up to1024nodes and64clips).
Generated unique converter names identify only candidate-local nodes. They never
become EntityIds or durable subasset correspondence keys. Native depth-first joint
order, parent indices and model-space rest matrices must match the source-derived
plan after safe archive admission. Clips must match skeleton track count and source
duration; sampled model matrices must remain finite.

The exact Ozz0.17.0 converter reads unanimated fallback channels from nodeTRS even
when its skeleton loader reads a node matrix. FORGE therefore decomposes admitted
matrix-authored rest transforms into explicitTRS **only in its private converter
input**. Explicit signed/zeroTRS stays explicit; original source bytes remain intact.
The conversion rig uses the existing bounded Ozz rest profile(abs<=65504), distinct
from editable ECS LocalScale(abs<=10000). Matrix normalization before the shared
TRS decomposition helper avoids accidentally imposing the ECS limit on asset data.
ECS realization still requires its own authored-transform representability checks.

Transform tracks use the official converter's STEP/LINEAR/CUBICSPLINE handling.
Cubic interpolation is sampled at the configured rate; it is not an exact native
cubic runtime curve. Morph curves retain original times, values and derivatives in
separate private metadata because the pinned converter does not import weights.
When morph channels run longer, FORGE extends a transform channel with its final
clamped value. For cubic tracks, only the outgoing derivative beyond the original
last key is replaced by zero, and a constant endpoint is appended; preceding
segments retain their derivatives. Morph-only clips receive an unanimated rest
channel. A clip whose only key is at time zero uses an explicit constant duration
(default1second) because Ozz requires positive duration. No helper gameplay entity
or artificial skeleton joint is created to carry time.

The adapter bounds decoded animation inputs to64MiB before native decoding, binary
converter input to16MiB, plan/JSON files to16MiB each, and morph metadata expansion
before allocating its JSON arrays. Existing archive limits remain16MiB each with
bounded joints, keys and numerical values. Output validation allows at most256MiB
for a rig and its clips, rejects unexpected/duplicate/missing files, and admits
archives before calling the runtime. These internal stages are tested with the
actual pinned converter. Whole-family publication now composes these stages;
runtime skinning remains required integration work.

Evidence: [Ozz converter at the selected pin](https://github.com/guillaumeblanc/ozz-animation/blob/744eb9d99f606eda849acb0b1204f7a3dc20bca1/src/animation/offline/gltf/gltf2ozz.cc),
[skeleton builder](https://github.com/guillaumeblanc/ozz-animation/blob/744eb9d99f606eda849acb0b1204f7a3dc20bca1/src/animation/offline/skeleton_builder.cc).

### Supervised converter stage and morph sampling

The private second-stage runner reuses the shared job directory, cancellation and
OS process supervisor. It accepts only generated `source.gltf`, `config.json` and
optional `animation.bin`. Fixed configuration permits only the expected skeleton
and numbered clip output files; all possible input URIs are restricted to the
staged binary. CPU glTF container/accessor admission runs before launching the
fixed official converter command. Unexpected/missing outputs, changed inputs,
failed/cancelled conversion and invalid archives reject the candidate. Disposable
job files are removed on success and failure.

This stage allows1GiB process memory,16MiB per file,320MiB aggregate staging,
68files and240seconds wall/220seconds CPU. Collected archive outputs remain capped
at256MiB. It is intended to run **after** the native model worker has exited, never
nested inside it, so cancellation retains ownership of every child process. The
importer executes this sequence and validates the combined result before returning
it to the existing publisher.

`MorphAnimation` is immutable companion curve data. It owns no clock, gameplay
entities or playback state. Evaluation receives seconds from its caller, clamps
before/after a channel to that channel's endpoint, and applies the glTF interpolation
formula in AppendixC. Cubic in/out derivatives are multiplied by key interval
length. Weights are not clamped to0–1 or normalized. Duplicate target nodes,
malformed/nonfinite curves and arithmetic overflow are explicit failures. A clip
cannot target a node outside its admitted rig or extend beyond its declared duration.
The evaluator is separate from skeletal Ozz sampling; scene/runtime application
and rendered morph proof remain ongoing work.

## Complete animated family

One combined Skeleton member holds the union of skin joints, animation targets and
required ancestors. Multiple skins retain separate ordered joint mappings and
inverse-bind matrices. Prepared mesh palettes address `skin.joints`; each skin then
maps to the actual admitted Ozz joint order. Every skin-bound draw must have a
prepared palette within that skin's bounds. An unused unbound mesh can retain its
source influence streams without pretending they are GPU-ready.

Geometry alone carries `animation_pending` and fails complete validation. After the
native worker exits, the parent runs the official converter with bounded private
inputs, verifies names/parents/rest/duration, and joins raw Skeleton/AnimationClip
archives with the geometry. The combined validator additionally checks model-node
ancestry/rest agreement, morph target counts, archive hashes, typed clip/skeleton
bindings, source provenance and all existing material/texture references. No stage
writes a selected catalog revision. Only the complete candidate reaches the existing
single publication transaction.

Converter provenance records the exact Ozz revision, executable digest and source
digest. The executable digest joins the common build input as `tool_revisions`;
it does not impersonate an importer revision, project source file or logical AssetId.
Discovery and conversion check executable bytes before/after use. A missing or
changed converter rejects an animated candidate; static models do not require it.
The packaged executable is `tools/gltf2ozz` (`.exe` on Windows).

Rig correspondence uses rest/topology content plus the unique combined-rig role.
Clip correspondence uses decoded curves and target context, excluding clip display
names and source array positions. Node/clip reorder and clip rename preserve this
evidence. Indistinguishable members still use the existing explicit correspondence
workflow. Catalog Runtime edges bind clips to Skeleton AssetIds; cache metadata
contains revision-local addresses only. Source locators remain the original model,
not fabricated legacy `animation_source` records or generated archive paths.

Settings expose reject/explicit top-four influence reduction, official converter
sampling rate1–240(default30), and optimization(defaulttrue). The four-influence and
256joint draw limits remain; requested reduction emits diagnostics and does not
silently clamp unsupported content. Scene instantiation, resource adoption, playback
binding, GPU skinning and editor model controls are separately tracked integration
requirements and are not proved merely by successful family publication.

## Selected cooked-family loading

The CPU model-selection reader resolves the Model root and its active members from
one copied catalog revision, then opens the selected immutable cache artifact by
key. It reuses the common cache reader and complete model admission. It verifies
the catalog's artifact digest, source/recipe provenance, member ownership, exact
publication generation, file hashes and typed bindings against the cooked index.
Mixed generations, stale bindings and missing/corrupt files reject the load.

This reader needs neither the importer nor the source files or converter executable.
It can read a previous selected revision after the source changes or disappears;
source discovery still reports that separate condition. It does not publish,
convert, mutate a world, or replace a live resource. Cache bytes are owned after the
read, so eviction cannot invalidate them. Runtime resource adoption and the rendered
model remain separate integration requirements.


## Imported local transforms and cooked format compatibility

New model bundles use private format3 (explicit TRS was added in format2). Every node stores explicit translation,
normalized XYZW quaternion and signed scale in `trs`, alongside a checked derived
`local` affine3x4. Explicit source TRS never passes through matrix decomposition:
zero scale would lose the original rotation, and reflections can have several
mathematically equivalent decompositions. Matrix-authored source nodes pass the
exact glTF affine/nonzero-column/no-shear admission before the existing normalized
basis decomposition is used. This does not alter the source file.

The immutable source numeric profile is separate from authored ECS LocalScale and
Ozz rest storage. Static transforms are not silently clamped to either consumer's
limits. A consumer must reject values it cannot represent. Ozz private conversion
now uses the same canonical TRS helper followed by its existing finite float/rest
profile checks. Composition validation compares normalized columns, so an absolute
unit-sized epsilon cannot hide a corrupted tiny-scale transform.

Existing format1 bundles remain readable for their existing mesh/animation resource
consumers and retain version1 when decoded/re-encoded. They have no guaranteed
recoverable original TRS and must be reimported before a workflow requiring that
information. Format2 bundles also remain readable with their explicit TRS, but have no durable
node-member identities. The importer declares output version3 in the full build key;
new cooks cannot reinterpret older outputs as version3. Selected catalog, cache manifest and
bundle version must agree. Catalog metadata envelope version1 is independent of
this cooked format version. No scene format, identity or module ABI changes here.


## Stable imported node provenance

Format3 adds a typed `model_node` logical AssetId for every immutable source node,
using the existing sidecar reconciliation and selected catalog graph. A node member
contains only a revision-local selector into `hierarchy.nodes`; the owning model
index digest authenticates that data. It creates no separate node file or mutable
object tree. File-backed Mesh/Material/Texture/Skeleton/Clip members retain their
existing encoding. A node's optional Mesh binding is a checked typed Runtime edge.

Node evidence combines admitted local content, geometry/material content and
order-independent hierarchy context. Animation channel kinds provide independent
role evidence for otherwise identical rest nodes without using clip order or names. Independent structural/geometry evidence allows uniquely matched
nodes to keep identity after a transform edit. Display labels and source array
indices are not persistent keys. Renaming/reordering does not imply a new identity;
indistinguishable changed nodes require explicit correspondence decisions. Exact
unchanged reimports reuse the validated selected mapping. Removed entries retain
normal tombstones. These AssetIds identify source nodes, not placed scene EntityIds
or process-local loaded instances.

The immutable index still has a16MiB byte limit; total logical members are bounded
by100000, matching the existing hierarchy/identity admission ceiling. At most4096
members may have physical artifact files, with the existing worker/cache file and
aggregate-byte limits unchanged. Inline identities must not be counted as invented
files. Project catalog64MiB/100000record limits still apply. Read-only catalog query
measurements for node-shaped metadata observed approximately1.43MB/0.16s at1000nodes,
5.88MB/0.79s at4096, and14.37MB/1.74s at10000 on the Linux development host. These
include JSON output/graph traversal; they are not publication, memory or frame-time
benchmarks. Actual scene instantiation/reconciliation remains separate ongoing work.


The existing mesh correspondence algorithm also uses source-node usage context.
Renaming all uses of several identical mesh allocations may therefore report a
**mesh** ambiguity even when the new node identities themselves remain matchable.
The official negative-scale fixture exercises explicit resolution of that real
conflict before verifying node identity preservation. FORGE does not silently map
those meshes by their old array positions.
