# Shader-backed material surfaces

Phase 7 implementation; native Windows/WARP surface fixtures pass. Final package acceptance remains separate. This
extends the existing asset publication, dependency graph, resource leases and
complete GPU candidate path. It adds no scene identity, ECS hierarchy or ABI1 change.

## Logical interface and geometry ownership

A `forge.shader` source version2 may declare a `surface` version1 interface and
exactly one pixel-stage function. General version1 stage programs remain supported;
an arbitrary vertex/pixel program is not automatically a material surface.

The surface declaration contains ordered semantic UV-set numbers, named typed
scalar/vector/linear-color defaults, and named texture declarations using the
existing MaterialTextureSlot semantics. It contains no byte offsets, registers,
descriptor indices, register spaces or native objects. Limits of32 UV sets,
256 parameters and64 texture roles are admission budgets, not promises that every
device can link that combination. Actual compilation, device features and Diligent
pipeline admission remain required.

FORGE generates the interface and color/depth entry points. Project HLSL supplies
`float4 Shade(ForgeSurfaceInput input)` or another declared function name, returning
linear HDR RGBA. The input includes camera-relative world position, transformed
normal/tangent/bitangent, front-face indication, vertex color and the declared compact UV values. Helpers
`ForgeParameter_name()`, `ForgeUV17(input)` and `ForgeSample_name(...)` expose the
declared values. UV semantic numbers never become hardware register indices.

The same engine geometry implementation serves built-in and custom materials:
vertex fetching, morphing, skinning, transform parity, bounds and static instancing.
This interface does not authorize user vertex deformation with stale engine bounds.
The wrapper owns opaque alpha, mask cutoff, blend output and nonfinite diagnostic
color. Shadow masks evaluate the same user function. Blended surfaces do not cast
a single opaque shadow depth. Custom functions own their color calculation; the
interface does not silently insert the built-in PBR lighting calculation.

## Textures and backend mapping

2D and2D-array helpers apply the material's signed/zero UV transform. Their
input-based overloads select one declared UV set; arrays also take a layer.
Cube, cube-array and3D helpers take explicit coordinates from the function.
Nondefault planar UV transforms on those roles reject instead of being ignored.

Texture and sampler bindings remain named Diligent variables. Bounded independent
samplers use an array with constant indices. The generator can lower these to the
pinned WebGPU named-element convention. There is no register-space workaround.
The [backend matrix](render-backends.md) separates this source mapping from actual
device execution. Current custom Shader artifacts are D3D12/FXC5.1 DXBC; another
backend requires its own cook/realization adapter, not a logical asset migration.
The shared renderer refuses a mismatched cooked profile explicitly.

Project source compiles only in the bounded Shader worker. Render-thread preparation
loads admitted bytecode through a narrow backend adapter and verifies its native
reflection. Cooked programs also use the presentation owner’s existing Diligent render-state cache; no second shader cache or source compilation is introduced. Shared mesh/SRB code uses Diligent resource names and copied validated
reflection to pack parameters. Unrecognized resources, changed types/dimensions,
overlapping/out-of-range fields and inconsistent generated buffers reject. An
optimizer may remove unused declarations; the renderer does not invent missing
resources to defeat valid compiler optimization.

## Material ownership and last-good retention

Material source version2 adds an optional typed `overrides.shader` AssetRef.
An absent field follows the base; an explicit UUID selects that Shader's currently
published revision; null explicitly clears the Shader. Equal-value selections
retain intent. Selecting a custom surface requires `forge.surface.v1` semantics;
incompatible inherited or explicit parameters remain errors, never silent deletion.
Parameter defaults, independent Revert and explicit reset use the declared interface.

The importer captures and validates an immutable Shader revision before material
publication. Its Build dependency participates in the existing reverse invalidation
graph. A child without its own Shader override follows its base's actual compiled
snapshot, including when a newer incompatible Shader has been published elsewhere.
Texture references retain existing Runtime dependencies and semantic preflight.

The cooked material bundle version2 adds `surface.shader` to `material.values` and
`bindings.json`. Bindings pin the Shader AssetId, revision and layout; the complete
validated program is embedded as cooked dependency data. Authored JSON still owns
only the logical reference and values. Shader's cooked envelope version2 distinguishes
color and depth entry roles from their common physical pixel stage. Legacy Shader
and built-in Material envelopes remain readable; older readers reject new versions.

This embedded snapshot is deliberate: merely looking up the latest Shader by
AssetId would break last-good behavior after a failed dependent material rebuild,
restart or cache pruning. Existing material retention and content packaging retain
the exact previous bytes, without a new revision registry or cross-document undo
format. Runtime packages need no source HLSL, compiler or independently selected
Shader for a material whose program is already embedded.

CPU resource accounting includes snapshot bytes. Complete GPU preparation retains
the material lease and all selected texture/mesh leases. Failed validation or PSO
preparation leaves the previous complete draw available. Source Save, Shader
publication, Material publication and scene history keep their existing separate
ownership. No atomic cross-document operation is claimed.

## Evidence

- CPU source/inheritance/equal-value/reset, versioned transport and compatibility tests.
- Material publication tests cover an incompatible Shader update, retained base/child snapshots, fresh resource loading after removal of the old standalone Shader artifact, source-free packaging, compatible replacement and old-lease preservation.
- The Linux probe compiles both generated surface entries to SPIR-V with all five texture dimensions and19 samplers; its existing Diligent material draw/readback remains a separate executed check.
- Native WARP fixtures compile real surface source and test reflection, static/morph/skinned draws, signed/zero transforms, alpha modes, shadow mask agreement, failed candidates and compatible include changes. These fixtures pass on Windows/WARP; full alternate-backend execution is not claimed.

Exact Diligent Core revision remains
`744f079f61cdbda15d371383682418fc927e4a61`. Its `Shader.h`,
`ShaderResourceVariable.h`, `GraphicsAccessories.cpp`, native reflection and
separate-texture/sampler tests supply the API contract. No vendor patch or upgrade
is introduced. See the [dependency policy](dependency-policy.md).
