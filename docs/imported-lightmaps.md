# Imported diffuse lightmaps

Mesh Material graphs accept an optional **Lightmap irradiance** Surface Output
input, `lightmap_irradiance`, of type `color3`. Its unconnected value is zero.
The existing Shader graph, Material, Texture, mesh UV and resource owners retain
all source, binding, publication, history and lifetime responsibilities.

The input represents finite, nonnegative linear RGB incident diffuse irradiance
E. The shared graph shader adds `E / pi * DiffuseColor * Occlusion` to final
radiance and the indirect-light output. `DiffuseColor` is the prepared native PBR
reflectance, including dielectric and metallic energy selection. The contribution
is independent of the Scene environment gain. It is neither emission nor specular
lighting. Existing direct, environment and local probe lighting remains additive;
the map must describe the additional baked contribution the author wants.

Graph metallic/roughness, occlusion, base color and normal retain their existing
contracts. Roughness does not change this Lambertian term. The current graph has
no clearcoat, sheen or transmission output; this profile does not introduce those
lobes. Fog attenuates indirect radiance without adding fog color to that target.
The existing AO and reflection composition owners consume their normal outputs.
Invalid or overflowing irradiance diagnoses the pixel and does not publish a new
nonfinite lightmap value into the indirect target.

## Bindings and authoring

Texture nodes admit `color`, `data`, `normal` and `hdr` usages. HDR samples are
linear colors, without exposure or tone mapping. Convert a sampled `color4` to
`color3` before connecting this input. Ordinary graph parameters can supply gain;
Material instances and object-local numeric values remain the established
Inspector/live editing paths. Texture assignment is saved in the Material, not
in preview choices or a separate lightmap registry.

Logical UV numbers, sparse UV sets, atlas offset/scale/rotation, sampling and
resource compatibility follow [surface shaders](surface-shaders.md) and
[Material graphs](material-graphs.md). Missing UVs retain their existing diagnostic
or geometry fallback; this feature does not generate UVs or silently switch maps.
Authors must provide suitable charts, padding, map units and linear encoding.
This is an imported map workflow, with no editor baker, directional reconstruction,
automatic unwrap, dynamic rebaking or lighting-source subtraction.

## Cooked profile and runtime admission

Surface wrapper capability15 identifies the current generated contract. Newly
published surface Shaders include derived `forge.shader.surface_wrapper_version`.
Publication, resource loading and runtime package admission validate these facts
against admitted cooked bytes through the same private provenance helper. A missing,
malformed or mismatched fact for capability15 or later rejects the candidate;
resource failure retains the last-good lease. Older cooked surfaces remain readable,
including legacy metadata without the optional surface fact for profiles below15.

Runtime kits advertise their maximum `engine.runtime_surface_wrapper`. Common
standalone compatibility rejects exports whose explicit Shader capability exceeds
that kit. Existing build identity and project-module compatibility rules remain.
There is no new Scene schema, native SDK value layout or authored ownership model.

Current cooked custom Shaders use Windows/D3D12/FXC5.1. Local Vulkan source probes
compile the production generated graph/header/lighting and read real pixels; they
do not establish cooked Vulkan Shader, Scene/Game or exported EXE support. Windows
cooking and graphical acceptance remain a separate platform gate.

The Feature Gallery example's Shader and Material source records are deliberately
unpublished in the source checkout. The Windows package preparer cooks them with
the matching worker and validates the runtime content closure before inclusion.
Preparation failure aborts packaging; source-only drafts are not ready artifacts.

See the [user workflow](../manual/editor/imported-lightmaps.md).
