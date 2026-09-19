# ADR 009 — Coordinates, units and precision

Date: 2026-09-19. Decision adopted for foundation planning; complete package
validation is pending. Future implementation requires its own authorized scope.

## Decision

Preserve +Y up, meters, +Z camera-forward, right-handed world-vector cross products;
quaternions are normalized x,y,z,w. Affine storage is row-major3x4 with column-vector
application and parent*local composition. Translation/affine evaluation use doubles;
rotation/scale use floats. Render upload is explicit float conversion, not a raw
matrix memcpy contract.

## Current evidence and implementation boundary

include/forge/transform.hpp, transform.cpp, editor/camera.hpp and viewport shaders
state/implement these conventions. Current preview uses positive view depth and
D3D[0,1] clip depth with culling disabled. That does not prove a production winding
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
