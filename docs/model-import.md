# Model import stages

Phase7 model integration is in progress. The native static-model cooking and
validation stages below feed a supervised worker and the shared headless import
service/CLI. Animation conversion, rendering and the model-document UI remain
required work.

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

## Static model candidates

The private native stage prepares a single candidate containing:

- Separate Mesh, Material and referenced Texture members.
- Material texture-slot bindings and mesh material-slot assignments.
- Texture semantic variants under one image member, with per-material samplers.
- Original node labels and parent relationships, scene roots/default selection,
  morph defaults and exact local affine transforms in FORGE row-major3x4 storage.
- Content and semantic-usage evidence for subsequent existing subasset reconciliation.
- Bounded diagnostics for processing and unreferenced images.

The cooked hierarchy belongs to an immutable asset. It does not replace Flecs
ChildOf/Parent or add another authored transform authority. The scene/runtime
consumer must resolve the selected member bindings before use.

This profile currently rejects models needing skin/animation realization. Those required stages remain active Phase7 work;
their rejection here must not be reported as completing those features. Unreferenced
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
current recipe retains the skin/animation restriction listed above. Camera/light
and variant data are preserved as described below; their editor/runtime realization,
complete skeletal conversion, model documents and GPU rendering remain required
Phase7 work.

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
compatibility preflight. Identical unkeyed members may require an explicit decision
again on reimport; cache equality by itself does not currently bypass reconciliation.

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
actual pinned converter; whole-model worker composition and runtime skinning are
still required integration work, not completed by these adapter tests.

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
current tests exercise this runner directly; importer composition remains ongoing.

`MorphAnimation` is immutable companion curve data. It owns no clock, gameplay
entities or playback state. Evaluation receives seconds from its caller, clamps
before/after a channel to that channel's endpoint, and applies the glTF interpolation
formula in AppendixC. Cubic in/out derivatives are multiplied by key interval
length. Weights are not clamped to0–1 or normalized. Duplicate target nodes,
malformed/nonfinite curves and arithmetic overflow are explicit failures. A clip
cannot target a node outside its admitted rig or extend beyond its declared duration.
The evaluator is separate from skeletal Ozz sampling; scene/runtime application
and rendered morph proof remain ongoing work.
