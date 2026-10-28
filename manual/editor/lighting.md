# Scene lighting

Use **Tools > Scene lighting...**, or search for **Scene / Lighting** in the Command
Palette, to change the scene's environment, shadows and display brightness. These settings
belong to the scene: **Save** keeps them, and **Undo / Redo** reverses each committed
change. Opening this window keeps Scene as the active editing task.

![Scene lighting controls for environment and exposure](../images/scene-lighting.png)

Scene lighting controls affect authored lighting. The Scene preview light is a separate viewing aid.

Turn off **Preview light** on the Scene toolbar when adjusting authored lighting.
Game always uses authored lighting. Camera/light icons and selection guides are
explained in [Viewport](viewport.md).

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

## Add glow to bright surfaces

In **Tools > Scene lighting...**, scroll to **Bloom** and select **Enable bloom**.
Try **48-hdr-bloom** in the Feature Gallery to compare warm/cool emissive cubes with
an ordinary cube. Save the scene and export it through the usual Export Game window.

- **Brightness threshold** selects how bright a surface must be before it glows. Start at **1**; lower values include dimmer surfaces.
- **Intensity** controls the added glow from **0** to **4**. **0** bypasses bloom.
- **Spread** controls how far glow extends from **0.5** to **16**. Start at **2**.

Press **Enter** to commit a numeric value. **Undo / Redo** reverses each change.
These saved settings affect Scene preview, Play and exported games. Bloom costs
extra GPU memory and draw work; leave it disabled when you do not need it. Separate
camera views keep glow inside their rectangles. Overlapping cameras can apply glow
to retained earlier camera color again.

## Adjust display brightness

**Exposure (stops)** affects the Game preview and is saved with the scene. Enter a
value from **-20** through **+20** and press **Enter**. **+1** doubles the light before
tone mapping; **-1** halves it. With automatic exposure enabled, this value adds
manual brightness compensation to the automatic gain. **0** adds no compensation.

The Scene preview has a separate personal **View > Exposure** setting. This lets you
inspect your work without changing the game's authored exposure. See [Viewport](viewport.md).

## Adapt Game brightness automatically

1. Stop Play and open **Tools > Scene lighting...**.
2. Expand **Automatic exposure** and select **Enable automatic exposure**.
3. Press Play. The first image sets its brightness immediately; later light changes gradually adjust the gain.
4. Stop Play to adjust the settings. Press **Enter** to commit a typed value, then save the Scene. Undo restores each edit; Play and exported games use the saved settings.

![Automatic exposure controls with gain limits, percentile fields and adaptation rates](../images/automatic-exposure.png)

Set the brightness range and response in Automatic exposure.

- **Minimum gain (stops)** and **Maximum gain (stops)** limit the automatic correction from -20 through +20. Minimum must not exceed maximum; +1 doubles linear light. Equal limits hold a fixed gain.
- **Low percentile (%)** and **High percentile (%)** exclude dark and bright outliers from the light measurement. Each accepts 0–100 and low must be less than high. Defaults are 2 and 98. Empty black pixels are excluded already.
- **Target gray (linear)** accepts 0.001–1 and defaults to 0.18. It sets the target before tone mapping and color grading, so it does not promise an exact final screen brightness.
- **Brightening rate (1/s)** controls how quickly gain increases after the image becomes darker. **Darkening rate (1/s)** controls decreasing gain after brighter light. Both accept 0.01–20; larger values respond faster.
- **Reset automatic exposure** disables the effect and restores its defaults in one Undo step. Unchecking Enable retains your chosen values for later use.

Open Gallery **96-automatic-exposure** and press Play. Its sunlight switches from
dim to bright after two seconds and returns to dim just after four seconds,
repeating every eight seconds. Watch the initial brightness change settle. Stop,
change the rates, save and try again. Export it as the startup Scene to compare the
standalone result. No C++ module or compiler is required for this example.

![Gallery sunlight immediately after becoming brighter, with pale columns and a bright floor](../images/automatic-exposure-transition.png)

A light increase initially brightens the Game image.

![The same Gallery columns after automatic exposure settles, with distinct colors and a gray floor](../images/automatic-exposure-settled.png)

Automatic gain then brings the composed image toward the chosen target.

Automatic exposure measures the composed Game image, including bloom, before
manual compensation and grading. All camera rectangles share that measurement;
the game HUD is drawn afterward. Scene View keeps its separate manual preview.
Visual adaptation uses real elapsed presentation time, including while simulation
is paused. New Scenes, changed cameras/viewports, resizing and newly ready resources
restart the measurement, which can cause an immediate brightness change.

The meter samples a bounded grid and excludes empty black and invalid pixels.
There is no spot mask, per-camera exposure or HDR-monitor output mode. Strong gain
limits can clip display detail. An unsupported compute device reports a rendering
error when enabled; stop Play and disable the effect to use manual exposure.

## Adjust Scene colors

Open **Tools > Scene lighting...**, scroll to **Color adjustments**, and select
**Enable color adjustments**.

- **Color filter (linear RGB)** multiplies red, green and blue light before tone mapping. `1,1,1` leaves colors unchanged; each channel accepts 0–4. For a warmer image, try `1.1,0.85,0.7`.
- **Saturation** accepts 0–2: 0 is grayscale, 1 retains the original saturation, and 2 strengthens it.
- **Contrast** accepts 0–2: 1 retains contrast, higher values strengthen differences around 18% linear gray, and 0 produces a flat gray image. Strong settings can clip dark or bright detail.

Press Enter to commit a typed value. Each edit uses Scene Undo; save the Scene to
keep it. **Reset color adjustments** disables the effect and restores all default
values in one Undo step. Disabling it retains your chosen values for later use.

These are authored Scene settings shared by Scene preview, Play and exported games.
They affect geometry, the background and bloom; editor controls/overlays and the game
HUD keep their own colors. The Scene preview's personal exposure remains independent.
Stop Play to change these authored controls. Open Gallery
**69 — Scene color adjustments** to compare its warm preset with disabled, grayscale
and neutral settings, then export and run the same Scene.


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


## Current integration limits

PBR materials receive scene lighting on both imported and built-in meshes. Legacy
blockout entities already resolve built-in Mesh/Material resources while keeping
their existing authored data and default unlit appearance. Assign a PBR material
to use the scene lights on a blockout object.
Game uses authored Camera components; add an enabled camera before starting Play.
See [Play mode](play-mode.md).

The resizable lighting window keeps labels beside or above their fields as space
allows. At larger interface zoom, scroll to reach the shadow settings. Changing
window size or interface zoom does not change the scene.
