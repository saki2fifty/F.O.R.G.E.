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

This static profile currently rejects models needing skin/animation, camera/light
or material-variant realization. Those required stages remain active Phase7 work;
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
current static recipe retains the restrictions listed above; complete skeletal,
camera/light/variant realization, editor model documents and GPU rendering remain
required Phase7 work.

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
