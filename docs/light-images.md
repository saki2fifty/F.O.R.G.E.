# Light images

The existing `Light` component optionally modulates its direct RGB radiance using
an ordinary Texture asset. Directional and Spot use 2D images; Point uses a native
Cube Texture. Rectangle and Sphere reject image assignments. Material color,
emission, environment lighting, shadow ownership and light layers remain separate.
See the [user workflow](../manual/editor/lighting.md#use-an-image-to-shape-a-light).

## Authored values and compatibility

`texture` is a nullable `AssetRef<TextureAsset>`. `image_width` and `image_height`
are Directional full dimensions in world metres, finite 0.001–10000; both default
to 1. `image_tiled` defaults to false. A missing texture retains ordinary lighting.
Older documents receive these defaults during normal admission; opening or playing
does not save or rewrite the input file. Normal explicit saves serialize current
component values. Existing Scene/property/history/Meta owners supply Inspector,
automation/MCP, prefab and persistence behavior.

Image dimensions and tiling use the existing runtime tuning service. Texture
replacement remains Edit-only in authoring. Trusted matching C++ code can write
native component values through its existing access. The appended public Light
layout changes the exact SDK fingerprint: rebuild matching modules. ABI1 callback
tables and asset identities do not change.

## Coordinates and sampling

`render_view` owns `LightImageFrame` and camera-relative GPU conversion. Directional
projection is centered on the light's world position; dimensions set metres per
image. Clipped projection contributes black outside its rectangle. Tiled projection
extends the coordinates and retains the source sampler's wrap intent; it does not
force Repeat over Clamp or Mirror. Projection has no extra depth volume.

Spot projection uses the existing admitted float outer-cone cosine. Its square
image spans the cone's tangent plane; ordinary angular attenuation and range still
apply. Point samples the Cube using its light-to-receiver direction. DDS/KTX Cube
face order remains +X,-X,+Y,-Y,+Z,-Z. Panoramas and cross-layout images are not
implicitly converted to Cube resources.

A normalized world forward and perpendicular world-right axis form the image
frame; transformed Y selects the up sign. Node scale does not resize an image or
cone. Shear resolves to an orthonormal orientation; signed axes retain reflections.
The existing forward basis also applies to Point image orientation. Undefined
image axes diagnose image-only failure and retain the ordinary admitted light.
Each view validates float-representable anchors and rows before submitting them.
Positions are rebased to the camera; the shader subtracts the light anchor before
applying coordinate rows, avoiding an absolute-world affine translation.

Color/HDR-color selection uses the existing Texture resource owner. Stored RGB
multiplies direct radiance, with alpha ignored. Premultiplied source RGB retains
its stored values. Negative/nonfinite sampled RGB rejects the image contribution
through the existing numerical diagnostic path. Source filtering, wrap, mip, LOD,
anisotropy and backend sampler admission remain authoritative. Comparison samplers
are rejected. The shader supplies explicit gradients, including the Spot
perspective derivative and unnormalized Cube gradients; no per-light divergent
implicit derivative or forced base-mip sampling is introduced.

## Resources and draw admission

MeshSceneRenderer retains requested tickets and complete last-usable Texture/GPU/
sampler leases through the existing host pools and GPU residency. Adoption happens
only after dimension, color semantic, upload and sampler admission succeed.
Missing/failed replacements retain a compatible previous image, otherwise ordinary
lighting. Switching between Cube and 2D removes an incompatible previous selection.
All live winding and specialized bindings clear native references before releasing
old leases. Disabled/removed image selections and scene replacement release their
resource selections through the same renderer owner.

Light images and Decal channels share the existing per-receiver projected image
slots. The portable profile remains at most 16 pixel samplers: material samplers,
two lighting/comparison reservations and up to eight unique projected view/sampler
pairs. Duplicate views with equal sampler intent share a slot; different intents
consume separate slots. Cube and 2D views retain separate native bindings while
sharing the slot's sampler. The ordinary 64-light draw bound is unchanged.

Layer selection happens with the receiver's ordinary light list. If the complete
current Decal/image selection exceeds available slots, the receiver retains
ordinary materials/lighting with a contextual diagnostic. No prefix is silently
selected. An older custom receiver cook retains its supported Decals and ordinary
lighting when it cannot consume images. Unlit materials do not receive lighting.
No image registry, material reinterpretation, additional queue or agent-only
renderer is introduced.

## Private Shader contract

Capability 12 retains the existing 19 output roles and readable 1–11 cooks. Current
lit wrappers with projected slots add `ForgeLightImages`: 256 float4 rows, 4096 bytes,
four rows for each of 64 ordinary light indices. Row0 contains the camera-relative
anchor and one-based shared slot (zero disables). Rows1–3 contain coordinate axes;
row1.w contains the Light kind and row2.w the clipping flag. Remaining padding is
zero. Empty frames bind initialized immutable zero data. Active data uses the
existing per-frame DISCARD upload policy; no-image draws do not construct an active
packet.

`g_ForgeLightCube0..7` are bounded Cube view declarations using the existing
`g_ForgeDecalSamplers` and available capacity. Existing 2D map and projector packet
shapes remain unchanged. Reflection validates buffer size/fields, Cube dimensions,
slots and capability. Older cooks never receive these new bindings. Recipe hashing
includes the new shader source. Authored Surface function interfaces are unchanged.

## Coverage and platform boundary

Focused native checks exercise actual PBR RGB multiplication for all three kinds,
ignored alpha, source mip selection, mixed lights, shared-slot admission, empty
frames and large-origin placement. Scene checks cover failed/missing/wrong-dimension
replacement, compatible last-good retention, kind switches, layer exclusion and
scene replacement. Independent coordinate/source-bit-sRGB tests cover transformed
frames, Cube faces and wrapping. Editor, MCP and installed SDK process tests cover
shared authoring, live values, history, source-file preservation and recovery.

Gallery 117 supplies editable Directional/Spot/Point examples using existing source
Textures. All three have separately checked visible effects. Its typed export
closure contains the Scene and three Texture assets; four source-free relocated
displays match their authored captures. These checks use local software Vulkan and
software UI fixtures. They do not establish Windows/MSVC/FXC/cooked D3D12, full
editor input/DPI or physical hardware acceptance. The existing unsuppressed native
sanitizer shutdown condition remains separate from normal functional results.
