# ADR 009 — Coordinates, units and precision

## Implementation checkpoint — 2026-09-23

Phase7 signed/zero-scale admission and native D3D12/WARP transform, winding, normal, skin and morph fixtures have executed. See [transforms](../transforms.md) and [render features](../render-features.md). Physical GPU acceptance remains partial/pending; unavailable inverse-dependent operations and physics scale restrictions remain explicit.

The original decision and its baseline/deferred descriptions below retain their
2026-09-19 context. This checkpoint and linked current contracts describe what has
since been delivered; historical future-tense text is not a current capability limit.

Date: 2026-09-19. Decision frozen for review in Build260919-000063; automated package
validation is complete. Future implementation requires its own authorized scope.

## Decision

Preserve +Y up, meters, +Z camera-forward, right-handed world-vector cross products;
quaternions are normalized x,y,z,w. Affine storage is row-major3x4 with column-vector
application and parent*local composition. Translation/affine evaluation use doubles;
rotation/scale use floats. Render upload is explicit float conversion, not a raw
matrix memcpy contract.

## Current evidence and implementation boundary

include/forge/transform.hpp, transform.cpp, editor/camera.hpp and viewport shaders
state/implement these conventions. Current preview uses positive view depth and
D3D[0,1] clip depth. The Phase7 primitive preview now selects front-face state by
reflection parity, with explicit two-sided planar/singular draws. That does not prove a production winding
or skinned-import convention already exists.

## Consequences

Reserve counterclockwise outward object-space triangles and a tangent.xyz plus
handedness-sign w convention for imported mesh assets. Texture UV origin is top-left
in FORGE's normalized texture convention; importer/backend adapters own any necessary
flip and tangent correction exactly once. Linear lighting values and explicit sRGB
color textures are distinct; no implicit gamma correction in ECS properties.

## Deferred work and exact trigger

Before first imported rendering, test a labeled asymmetric basis model, winding,
mirrored UVs, normal map, skin bind pose and color swatches across boundaries. Jolt,
Ozz and Recast conversions stay in their adapters; never scatter sign fixes through
gameplay. No claim that these future rendering checks already passed.

## Approved Phase7 numerical-domain extension —2026-09-20

LocalScale remains the same independent authored component and now admits finite
[-10000,+10000], including signed/zero/tiny values. WorldTransform remains derived
forward parent×local state; no second hierarchy or matrix-authored entity.
Inverse availability is an operation-specific condition check, not visual
validity. Signed decomposition uses local sign/rotation continuity and verified
recomposition. Rendering distinguishes reflection parity and singular normals;
physics validates each actual shape separately. See[the transform contract](../transforms.md).
Existing version gates distinguish scene5/prefab2 expanded scale from earlier
positive-only documents. Exact SDK numerical-contract fingerprint changes; ABI1
and component identities/layouts do not. Full Phase7 rendered/imported/skinned
acceptance is still pending and cannot be inferred from this decision.
