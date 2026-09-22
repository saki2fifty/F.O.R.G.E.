# Shader import

A Shader asset is a prepared shader program. The current document compiles an
existing project `.shader.json` program and its HLSL files for the Windows D3D12
profile. This is a programmer-facing import workflow. Programs with a material
surface declaration can be selected in the [Material editor](materials.md).

## Prepare and import a program

1. Put the `.shader.json` declaration and referenced HLSL/include files inside the project. Edit these files in your code editor.
2. Stop Play. Open **Content → Create / Register → Import shader...**.
3. Enter the declaration's project-relative path and choose **Review settings**.
4. Review its exposed permutation settings and choose **Import / Reimport**.
5. Wait for compilation and publication to finish. Diagnostics appear locally and in Problems.

The declaration owns the program's AssetId, stages, entry points and source root.
It must describe the supported source format; renaming a file does not allocate a
new logical program. The repository technical document `docs/shader-assets.md` describes the source
structure and limits.

Double-click the published Shader in Content to reopen its import settings. The
main Save action also imports when this document has focus. Closing with pending
settings uses the same Apply/Discard/Keep editing guard as texture/model import.

## Write a material surface

Create `Assets/Flat.shader.json` and `Shaders/flat.hlsl` in the project. Use a fresh
UUID for the declaration's `asset_id`; PowerShell's `[guid]::NewGuid()` generates
one. Keep that UUID when editing or renaming the same Shader.

```json
{
  "format": "forge.shader",
  "version": 2,
  "asset_id": "c676fbcb-c51d-4f35-a75c-7782b12c6ee9",
  "source_root": "Shaders",
  "stages": [{"stage": "pixel", "source": "flat.hlsl", "entry": "Shade"}],
  "surface": {
    "version": 1,
    "uv_sets": [],
    "parameters": {"tint": {"type": 5, "value": [0.2, 0.6, 0.9, 1.0]}},
    "textures": {}
  }
}
```

Type5 means linear RGBA. The surface function uses the generated interface:

```hlsl
float4 Shade(ForgeSurfaceInput input) {
    return ForgeParameter_tint() * input.Color;
}
```

Import the declaration using the steps above, create a material, then select this
asset in **Surface Shader**. Expand **Tint**, change its value and press Enter.
Save the material and assign it to an object's Mesh renderer. This example is
unlit; it returns color without a lighting calculation.

FORGE supplies geometry, transforms, skinning and morphing. Your function returns
linear HDR color and alpha. The material's **Alpha** mode controls opaque, cutout
or blended behavior; cutout shadows use the same function. This interface does not
allow custom vertex deformation. The technical contract `docs/surface-shaders.md`
describes typed texture declarations and generated sampling helpers.

## When a build fails

Compilation runs in a separate worker. Unsupported declarations, invalid HLSL,
missing includes, stale inputs and incompatible compiled results do not replace
the previous published revision. Correct the files, reload settings when needed,
and retry. **Cancel import** rejects pending work.

Scene Undo does not undo file edits or shader publication. A successful import
proves a validated program was prepared; it does not automatically assign it to
scene materials or claim that every shader layout fits the mesh renderer.
