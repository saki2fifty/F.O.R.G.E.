# Scene lighting

Use **Tools > Scene lighting...**, or search for **Scene / Lighting** in the Command
Palette, to change the scene's environment and display brightness. These settings
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

## Current integration limits

The new lighting path applies to imported PBR meshes. Legacy blockout primitives still
use their existing preview lighting until their Mesh/Material migration is complete.
The current Game preview uses its existing viewing camera; authored-camera composition
is still being integrated. New sky and lighting paths are undergoing Windows validation.
