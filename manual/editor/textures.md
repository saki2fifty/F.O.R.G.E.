# Importing textures

Import prepares an image for use as a texture and gives it a durable asset identity.
The original image stays unchanged. Texture import is available in the current
Phase7 source. Assign textures to surface roles in the [Material editor](materials.md);
its surface preview shows them on geometry. The Texture import document also displays
the last published texture.

For files outside the project, use **Content → Import files** or drop them onto Content. The [Content import review](content-browser.md) copies sources into a new folder and can import them with default settings. Turn off automatic import there to review the settings below before cooking.

## Import an image

1. Copy the source into your project's **Assets** folder.
2. Stop Play. In **Content → Create / Register**, choose **Import texture...**.
3. Enter its project-relative path, such as `Assets/brick.png`, and choose **Review settings**.
4. Choose its **Usage**, color space and other settings below.
5. Choose **Import / Reimport**. Progress appears in the Texture import document.
6. After success, use **Content → Refresh** to see the registered texture immediately.

The current source accepts supported PNG, JPEG, TGA, BMP, still WebP, HDR/RGBE,
DDS, KTX and KTX2 files. A recognized extension does not guarantee every variant of
that format is supported. Unsupported compression, color profiles, dimensions or
malformed files produce an error while preserving the previous usable texture.

## Choose what the pixels mean

- **auto:** ordinary color, or HDR when the source declares floating HDR data.
- **color:** surface color; automatic color space uses sRGB for ordinary images.
- **data:** numeric channels such as roughness or masks; linear values.
- **normal:** tangent-space normals; linear values and normalized generated mips.
- **hdr:** high-dynamic-range color; linear floating storage.

**Additional usages** prepares more interpretations under the same texture asset.
For example, one source can have both color and data variants without creating
separate AssetIds. Up to three additional distinct usages can be selected. A usage
already selected as primary is included once.

**Flip normal green** converts the opposite normal-map Y convention and applies
only to normal variants. **Premultiply alpha** affects color/HDR variants.
Anisotropy requires linear filtering. Image maximum size selects the first standard
mip level that fits. BC compression currently accepts 8-bit normalized images;
it does not silently reduce HDR or 16-bit pixels to 8-bit.

DDS/KTX containers keep their supplied dimensions and mip levels. Their settings
omit image resizing, mip generation and pixel flips. A usage that contradicts the
container's declared color space is rejected.

## Change settings later

Double-click a texture in Content, or select it and choose **Open texture** in
Inspector. The same Texture import document opens with its saved settings.

Edits stay in this draft until **Import / Reimport** succeeds. **Use default** removes
one explicit setting override. **Reset to defaults** resets the draft's known
settings. **Reload saved settings** discards the draft and reloads saved values.
The main Save command also imports when this document has focus.

Closing the document or switching scenes/projects with a pending import offers
**Apply**, **Discard**, or **Keep editing**. Apply waits for successful import.
Discard cancels pending work and keeps any previously selected texture. Keep
editing cancels the close/switch. Import settings have their own ownership;
**scene Undo does not undo an import or settings publication**.

## Errors and cancellation

Use **Cancel import** to reject pending work. The error appears beside the import
settings and in Problems. If a source or its saved settings changed while you were
working, reload the saved settings and retry. Failed, cancelled or stale imports
leave the previous selected asset usable.

Keep each source's adjacent `.forge-import.json` file and `forge.assets.json` with
the project. They preserve identity and settings. `.forge/cache` is disposable
prepared data; removing it causes a verified rebuild. Do not copy a sidecar to
create a different logical asset with the same ID.

## Inspect the published texture

Double-click a standalone texture in Content to open its import document and preview.
Textures generated inside a model open a read-only **Texture** tab. Change those
textures through the owning model source/import rather than importing the model as an image.

The preview uses the **published** asset. Unapplied import settings do not change
it. A failed replacement keeps the previous good revision visible with an error.
Viewing never changes scene content or creates an Undo entry.

- **Variant** chooses color, data, normal or HDR interpretation. A missing variant reports an error; it is not silently replaced with color.
- Expand **View controls** for **Mip**, **Array layer**, **Cube face** or **Depth slice**, as appropriate to the texture.
- **Channels** isolates red, green, blue or alpha as grayscale. Isolated RGB channels show sampled linear values; alpha has no gamma transfer.
- **Display** chooses raw data, ordinary color or HDR with PBR Neutral tone mapping. **Exposure (stops)** changes brightness without changing the pixels on disk.
- **Checkerboard** reveals transparency in RGBA mode. **Nearest pixels** shows individual texels instead of filtering between them.
- **Signed values to 0..1** makes negative numeric values visible by mapping −1 to black and +1 to white. Alpha is unchanged.
- **Fit image** fits the image. **1:1 pixels** shows one cooked texel per screen pixel; **Image zoom** and the scrollbars let you inspect other magnifications and regions. Ctrl+Plus/Minus still scales the whole editor.

Only the visible image region is rendered. Preview output is limited to 2048 pixels
per axis; exceptionally large visible areas are downsampled. This viewer has separate
256 MiB CPU and GPU texture payload budgets. An asset exceeding the preview budget
can remain valid for another consumer; the preview reports the limitation.
Import settings are under the separate **Import settings** foldout.
