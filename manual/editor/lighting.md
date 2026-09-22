# Scene lighting

Use **Tools > Scene lighting...**, or search for **Scene / Lighting** in the Command
Palette, to change the scene's environment, shadows and display brightness. These settings
belong to the scene: **Save** keeps them, and **Undo / Redo** reverses each committed
change. Opening this window keeps Scene as the active editing task.

## Add an environment

1. Import a color image or cubemap using [Textures](textures.md). Use **HDR color** for a high-dynamic-range environment. A flat image should be an equirectangular panorama rather than an ordinary photograph.
2. Open **Scene lighting** and choose it in **Environment map**. Search by path, or drag the registered texture from Content onto the field.
3. Wait for the texture and lighting maps to finish loading. The environment lights imported PBR meshes and supplies their reflections. No punctual light is required.
4. Save the scene.

The environment uses the imported HDR-color variant when one exists, otherwise the
color variant. A texture imported only for normal or data use cannot supply environment
color. Change its import usages and reimport first.

**None / Clear** removes the environment lighting and background. **Reveal in Content**
selects the assigned asset without changing the scene reference.

## Adjust the light and background

**Intensity** multiplies both environment lighting and sky brightness. Enter a
nonnegative value and press **Enter**. Zero removes their contribution.

**Rotation (degrees)** turns the environment around world Y. The background and
reflections turn together. Positive angles turn +X toward -Z. Press **Enter** to commit.

Turn off **Show sky** to hide the background while keeping lighting and reflections.
The sky stays distant when you move the camera and remains behind scene geometry.

Changes to intensity and rotation reuse the prepared environment; they do not rebuild
its lighting maps every frame. If a replacement cannot load, **Problems** explains the
failure and the previous usable environment remains visible. Clear the field explicitly
to remove that previous environment.

## Adjust display brightness

**Exposure (stops)** affects the Game preview and is saved with the scene. Enter a
value from **-20** through **+20** and press **Enter**. **+1** doubles the light before
tone mapping; **-1** halves it. **0** restores normal exposure.

The Scene preview has a separate personal **View > Exposure** setting. This lets you
inspect your work without changing the game's authored exposure. See [Viewport](viewport.md).

## Add shadows

1. Select a Directional, Spot or Point light and enable **Cast shadows** in its Light component in the Inspector.
2. Open **Tools > Scene lighting...** and leave **Enable shadows** on.
3. Select the visible mesh. Its Mesh Renderer controls **Cast shadows** and **Receive shadows** independently.
4. Adjust **Map resolution** for detail, **Directional cascades** for camera-depth coverage, and **Shadow distance (m)** for the directional reach. Press **Enter** to commit a field.
5. Save the scene. Each setting change has its own Undo step.

Directional lights use 1–8 cascades. Spot lights use one perspective map; point lights
use six faces. The last directional cascade fades out over the final tenth of its
depth range. Neighboring cascades blend over an overlapping depth interval. **Shadow distance (m)** also limits shadow maps for point and spot lights
whose Range is unlimited; their light itself still follows its normal range rules.

**Maximum shadow lights** accepts 1–8 per camera. Shadow maps have a 256 MiB depth
payload limit per camera, in addition to the device's texture-size limit. Exceeding
these limits reports the affected light in **Problems**. That light continues to
illuminate the scene without a shadow; FORGE does not silently resize the maps.

A light's **Shadow bias** offsets the depth comparison. **Shadow normal bias** moves
the comparison point along the geometric surface normal in metres. Increase bias
carefully to remove surface speckling: too much bias detaches shadows from objects.

Alpha-mask materials use their base-color alpha and cutoff while casting shadows.
Alpha-blended surfaces do not cast opaque depth shadows. Mirrored meshes keep the
same casting behavior; fully collapsed geometry has no rasterized shadow area.
Legacy blockout shading remains unlit and does not receive PBR light/shadow terms,
although its derived mesh can cast. Use a PBR material for a lit receiver.

This shadow integration and its new native pixel fixtures are undergoing Windows
validation.

## Current integration limits

PBR materials receive scene lighting on both imported and built-in meshes. Legacy
blockout entities already resolve built-in Mesh/Material resources while keeping
their existing authored data and default unlit appearance. Assign a PBR material
to use the scene lights on a blockout object.
Game uses authored Camera components; add an enabled camera before starting Play.
See [Play mode](play-mode.md). The new camera/engine-mesh integration is undergoing
Windows validation.

The resizable lighting window keeps labels beside or above their fields as space
allows. At larger interface zoom, scroll to reach the shadow settings. Changing
window size or interface zoom does not change the scene.
