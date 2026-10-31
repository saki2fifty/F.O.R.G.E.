# Local diffuse irradiance

`ReflectionProbe.diffuse_intensity` optionally supplies local base diffuse lighting
from the same imported map used for reflections. The default is zero; existing
probe documents keep their previous diffuse lighting. `intensity` continues to
control base specular reflections. Both are independent finite multipliers in
0–1000000. Disabled, unassigned and both-zero probes are inactive.

## Source and volume ownership

The existing Scene/Prefab codec, Flecs Meta, authoring/history and runtime tuning
owners expose the appended scalar. Missing old values admit zero without saving
source files. Normal explicit saves serialize the current value. The installed
public structure changes its exact SDK fingerprint; matching modules must rebuild.
ABI1 callback tables and logical asset identities do not change.

MeshSceneRenderer retains one authoritative per-entity probe resource selection.
Its existing EnvironmentResidency already realizes diffuse, GGX and sheen Cubes from
Color/HDR cubes or panoramas. Diffuse lighting consumes that same admitted revision's
single-mip irradiance Cube. No separate lightmap asset, probe registry, GI world,
texture manager or cache lifetime is introduced. Shader material response and map
convolution call pinned Diligent APIs rather than a copied BRDF implementation.

The existing influence box supplies unit world planes, transformed center and map
yaw. Scale, mirrors and shear retain world-metre edge fades. Ready probes sort by
descending priority and stable EntityId. Each probe with positive diffuse intensity consumes its
fade from remaining diffuse coverage; the uncovered part keeps the Scene's global
diffuse light. Probes with zero diffuse intensity do not consume that coverage. Specular selection
independently omits probes with zero specular intensity. Box projection affects specular reflection
directions only; diffuse sampling uses the current mapped world normal and map yaw.

Eight active assigned probes total remain the Scene resource profile. Exceeding
that bound retains global lighting for the whole set with a diagnostic. Missing or
failed replacements retain a compatible last-usable map. Clearing, removal,
disablement, capacity fallback and successful replacement clear every persistent
draw-variant binding before old leases retire. A shared borrowed EnvironmentProbe
value feeds prepared draws and the existing specular pass; it owns no leases.

## Material integration

Current lit Mesh draws evaluate local diffuse lighting before native layer resolve.
The existing direct light, emission and global base/clearcoat/sheen specular terms
remain intact. Native metallic response, transmission reduction, material occlusion
and clearcoat attenuation still apply. Local light can contribute when the Scene
environment intensity is zero. No-probe draws keep their original lighting path.

The same world-position/normal inputs serve ordinary, posed, instanced and Mesh
particle draws. The existing queue, depth and material owners remain authoritative.
Diffuse lighting does not require the visible-depth reflection pass or its compatible
queue prefix. Unlit materials remain unlit. It introduces no history, clock, extra
render target or post-process pass. A per-camera numeric failure skips that probe;
an older lit receiver cook keeps global diffuse lighting and reports a recook hint.

Explicit map snapshots exclude local diffuse probes along with local specular and
screen-space reflections. They do not recursively bake their own previous maps.
Runtime recapture, stable-ID rebaking, spatially sampled irradiance grids, per-Mesh
probe masks, local coat/sheen map replacement and multi-bounce GI remain unavailable.

## Private Shader contract

Capability 13 keeps the existing 19 roles and readable 1–12 artifacts. Current lit
wrappers add `ForgeLocalIrradiance`: 41 float4 rows / 656 bytes. Row0.x is a uniform
count in 0–8; other header lanes are zero. Each five-row record contains a
camera-relative center and diffuse multiplier, three unit world planes with
positive half distances, then fade distance and cosine/sine map yaw.

`g_ForgeIrradiance0..7` are complete single-mip HDR Cube views. They share the existing
lighting sampler; projected map/sampler admission is unchanged. Source recipe hashing
and native staging include `ForgeLocalIrradiance.fxh`. Reflection rejects missing or
malformed packets, Cube dimensions, array counts and unsupported capabilities.

Every lane evaluates the same uniform sequence of native Lambertian lookups before
spatial coverage branches, preserving defined implicit derivatives at volume edges.
Prepared draws use initialized zero data when inactive and per-frame DISCARD data
when active. All winding/specialized bindings reset before map retirement. Finite
material-output checks retain the existing numerical rejection behavior.

## Validation boundary

The private prerequisite covers 360 software-Vulkan frames, actual source convolution,
independent source-face/world-distance oracles and a 16-sampler/43-image/3-storage
profile. Current combined area/projector receiver sources additionally reflect
10 pixel constant buffers, 16 samplers and three storage views, with 43 images
for temporal indirect/reflection output or 44 with transmission. This includes
the eight local irradiance Cubes; it is software-Vulkan resource evidence, not a
Windows compiler or hardware acceptance claim. Product checks exercise native Mesh
PBR layers and actual FrameRenderer
resource, capacity, particle and scene transitions. Editor, MCP and installed SDK
checks cover the same scalar, source/history, live validation and recovery owners.
Gallery 118 uses existing warm/cool maps and ordinary typed source-free export.

These local checks do not establish Windows/MSVC/FXC/cooked D3D12, full desktop input,
physical GPU behavior/performance or graphical exported-EXE acceptance. The existing
unsuppressed native sanitizer shutdown condition remains a separate limitation.
