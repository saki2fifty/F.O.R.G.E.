# Model import stages

Phase7 model integration is in progress. The native static-model cooking and
validation stages below are implemented internally. They are not yet a complete
editor/CLI model importer, animation converter, renderer or model-document UI.

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
outputs; its eventual process supervisor must also enforce native memory/time limits.
These checks do not substitute for the existing catalog publisher's atomic commit
and stale-source checks, whose model integration remains required.

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
