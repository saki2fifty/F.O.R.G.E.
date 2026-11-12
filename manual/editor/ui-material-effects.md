# Create a UI Material effect

A UI Material draws a custom color effect inside a UI element. It can use the
element's UV coordinates, size, presentation time, saved numeric Material
values and ordinary 2D textures. The element keeps its normal layout, clipping and opacity.

This workflow requires the Windows D3D12 editor, a project with a registered RML
document, and the matching runtime kit for export. Create the Shader with a
visual Material Graph or an HLSL function. See [UI source editing](ui-source.md)
to open and save RML/RCSS files.

To try an existing effect, open the bundled Feature Gallery's
`Scenes/213-ui-material-effects.scene.json` and press Play. It compares a still
textured panel with an animated panel using flipped UVs and 75% opacity. Stop Play
and open `Assets/UI/MaterialEffects/Motion.material.json` in Content to change
**Motion speed**, **Tint** or the image slot's UV transform. Save the Material and
restart Play. The Scene includes its Camera and export dependencies; no project
C++ module or Light is required. Set it as startup and save before exporting.

`Scenes/214-ui-material-graphs.scene.json` uses a visual graph for its textured
gradients. Open `Assets/UI/GraphEffects/Panel.shader.json` to inspect or edit
UVs, presentation time, element size and typed math feeding UI Output. Its two
Materials and export dependencies are included. See the
[Material Graph guide](material-graphs.md).

## Create a visual UI graph

1. In Content, choose **Create / Register → New material graph...**.
2. Enter a new `.shader.json` path and choose **Domain → UI**, then **Create graph**.
3. Select the **Tint** parameter to change its color and alpha. It connects to **UI Output → color**.
4. Use **Add node** for **UV Coordinates**, **UI Time**, **UI Size**, ordinary **Texture 2D** samples and typed math. UV0 is the element's coordinates; UI Time is presentation seconds and UI Size is the element's size in pixels. Use Component/Combine/Convert nodes for explicit type changes.
5. Choose **Save & Compile**. Wait for publication, then create and assign the Material as described below.

UI Output returns linear color and straight alpha. Native UI applies clipping,
opacity and target color conversion. UI Time continues while simulation is paused.
The graph's geometry preview is for Mesh and Sprite domains; inspect the assigned
UI Material in UI Source Preview or Play. Mesh geometry inputs, other UV sets and
array/cube/volume Texture nodes are rejected, including in unused function bodies.
See [Material Graph](material-graphs.md) for connections, functions and history.

## Create and import an HLSL Shader

1. Stop Play. Create the project folders `Assets` and `Shaders` if needed.
2. Create `Assets/Panel.shader.json` with the declaration below. Replace the example `asset_id` with a fresh UUID, then keep that UUID for this Shader.
3. Create `Shaders/panel.hlsl` with the function below.
4. Choose **Content → Create / Register → Import shader...**, select `Assets/Panel.shader.json`, choose **Review settings**, then **Import / Reimport**.
5. Wait for publication. Open Problems if compilation reports an error.

```json
{
  "format": "forge.shader",
  "version": 2,
  "asset_id": "6b8a461e-bf35-4ff3-a24d-aad09481c59e",
  "source_root": "Shaders",
  "stages": [
    {
      "stage": "pixel",
      "source": "panel.hlsl",
      "entry": "Panel"
    }
  ],
  "surface": {
    "version": 4,
    "domain": "ui",
    "uv_sets": [0],
    "parameters": {
      "tint": {"type": 5, "value": [0.2, 0.6, 1.0, 0.75]}
    },
    "textures": {},
    "labels": {"tint": "Tint"}
  }
}
```

```hlsl
#include "engine/forge.surface.hlsli"
float4 Panel(ForgeUiInput input) {
    float4 tint = ForgeParameter_tint();
    return float4(tint.rgb * input.UV.x, tint.a);
}
```

The function returns linear RGB and straight alpha. FORGE applies element opacity,
target color conversion and premultiplication. Do not premultiply the return value.
Type5 means linear RGBA; scalar and vector parameters use the ordinary Material
parameter types. `input.Size` contains element pixels and `input.Time` contains
presentation seconds. Presentation time continues while simulation is paused.

![Shader import controls](../images/shader-import.png)

Use Shader import to publish the declaration and its HLSL source.

## Create the Material and select it in UI

1. Choose **Content → Create / Register → New material...** and create `Assets/Panel.material.json`.
2. In the Material document, select the imported Shader in **Surface Shader**.
3. Adjust **Tint** and choose **Save** to publish the Material. Keep transparency at **Blend**, enable **Two-sided**, and leave **Depth test** and **Depth write** off.
4. Open the registered RML document, select an element in **Elements**, and expand **Inline style**. For selected siblings use **Group inline style**; in RCSS select a flat rule or a width-conditioned child rule and expand **Rule properties**.
5. Choose `decorator` from **Common**, then select the published Material in **UI Material**. You can search its path or drop it from Content. The Material must use the UI surface domain; unpublished, incompatible or damaged publications are rejected.
6. Choose **Set property** to replace the decorator value in one source Undo step, then **Save source**. Refresh the RML Preview or restart/reload UI in Play to inspect the result. Selecting a Material stages the CSS value without saving or changing the source.

![UI Source style controls with a published UI Material staged for assignment](../images/ui-source-material-effect.png)

The picker replaces the complete decorator value, including any existing gradient
or decorator list. Keep custom combinations in **CSS value** or **Source**. **None /
Clear** stages `none`, which disables decorators when applied. **Remove property**
removes this declaration so the stylesheet cascade can supply another value.
Reading mixed or unsupported CSS never replaces it automatically.

For direct source editing, use the Material UUID, which differs from the Shader
UUID. This example gives an element a class and an explicit size:

```css
body, div { display: block; }
.panel {
    width: 240px;
    height: 80px;
    decorator: shader("material:YOUR-MATERIAL-UUID");
}
```

```xml
<div class="panel" />
```

The panel fades from black to the chosen tint across its width. Changing element
opacity or parent clipping still affects the result. UI Materials preview in the
UI document or Play; the Material document's geometry preview is for other
surface domains. To change a graph effect, edit its nodes and **Save & Compile**.
For an HLSL effect, edit the function and reimport the Shader. Then save/rebuild
the Material. A failed publication keeps the previous published
Material usable.

## Sample a texture

Import the image as a **Texture** with the required usage, then wait for its
publication. For a color image, use **Color**. See [Texture import](textures.md).
Use surface version5 and declare an `image` slot in the Shader's `surface` object:

```json
{
  "version": 5,
  "domain": "ui",
  "uv_sets": [0],
  "parameters": {
    "tint": {"type": 5, "value": [1, 1, 1, 1]}
  },
  "textures": {
    "image": {
      "semantic": 0, "dimension": 0, "uv_set": 0,
      "offset": [0, 0], "scale": [1, 1], "rotation": 0,
      "sampler": {
        "min": 1, "mag": 1, "mip": 1, "u": 0, "v": 0, "w": 0,
        "compare": 0, "anisotropy": 1, "lod_bias": 0,
        "min_lod": 0, "max_lod": 1000, "border": [0, 0, 0, 0]
      }
    }
  },
  "labels": {"tint": "Tint", "image": "Image"}
}
```

This slot is a 2D Color texture with linear filtering and repeating coordinates.
Change the HLSL function to sample it:

```hlsl
#include "engine/forge.surface.hlsli"
float4 Panel(ForgeUiInput input) {
    return ForgeSample_image(input) * ForgeParameter_tint();
}
```

Reimport the Shader. In the Material document, expand **Texture slots → Image**,
choose the published Texture or drag it from Content, then **Save**. Assign the
Material to the UI element using the same **UI Material** control shown above.
Every declared texture slot needs an assignment before the effect can load.

`ForgeSample_image(input)` uses the element's UV0 coordinates. To compute your own
coordinates, call `ForgeSample_image(float2(...))`. Both forms apply the Material's
UV offset, scale and rotation and its sampler. Changing and publishing the image
updates its UI use without changing the Material assignment. A pending or failed
replacement keeps the previous complete effect; inspect Problems for the reason. When a
valid replacement becomes ready, the live rendering fault clears. A source import
or UI document error still needs its source problem corrected and published or
reloaded.

## Include the effect in an exported game

Select the registered UI document in **Content**. In **Inspector → Runtime
Dependencies**, add the Material with expected type **Material** and a reason,
then choose **Save declarations**. Review declarations after editing the source.
Previewing a reference does not add it to required runtime content.

Save the UI source and Material before exporting. The exported Material contains
its exact compiled Shader and declared Texture closure; the game runs without
HLSL, Material or image source files.
Use a runtime kit with UI effect support. An older kit is rejected before replacing
the export destination.

## Limits and troubleshooting

- Use one Pixel function, UV0, numeric Material parameters and ordinary 2D Texture slots. Array/cube/volume textures, live camera RenderTextures, custom vertex stages, mesh lighting and Mesh/Sprite assignments are unsupported.
- Keep an empty texture map with surface version4. Use version5 only with a nonempty map of 2D slots. Other combinations report a declaration error.
- A missing or incompatible Material reports an admission error. Check the UUID, publication, UI Shader domain and Runtime Dependencies declaration.
- The UI render state is transparent, two-sided and has depth testing/writing off. Incompatible values prevent publication.
- UI effects and native gradients share a maximum of256 compiled handles. Reduce the number of distinct decorated elements if the handle limit is reported.
- Nonfinite shader output displays diagnostic magenta. Check divisions and math in the function, then reimport and publish the Material again.

See [Shader import](shaders.md), [Materials](materials.md),
[Runtime UI](runtime-ui.md) and [Runtime content](runtime-content.md).
