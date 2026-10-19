# World sprites and image sheets

A Sprite places an image in the scene. It uses the ordinary camera, scene
transforms and Material assets, so it can appear beside meshes and move with a
parent. It does not require C++ code. The Feature Gallery includes two examples.

## Try the examples

Copy **Examples/FeatureGallery** to a writable folder and open that project.
Open **71-world-sprites** for image sizes, tints, transparency, mirroring and
different pivots. Open **72-sprite-frame-animation** and press Play: the large
arrow cycles up, right, down and left. The smaller arrows show the four frames.
The Gallery's README describes the expected results and export steps.

## Create an image material

1. Import your image through the [Texture import document](textures.md). Choose color usage and keep ordinary, uncompressed RGBA8 storage. Leave **Premultiply alpha** off. An ordinary 8-bit RGBA PNG is a suitable starting point.
2. Stop Play. Choose **Content → Create / Register → New material...**.
3. Enter a new `.material.json` path inside an existing project folder.
4. Choose **Preset → Sprite / image**, then choose the registered texture in **Image**.
5. Enable **Pixel filtering** for crisp pixels, or leave it off for linear filtering. Choose **Create**, then **Save** in the Material document to publish it.

This preset creates an unlit, two-sided transparent material. Scene lights do not
change its colors. Material settings still control alpha, depth testing and depth
writes. Keep the preset's identity UV transform and color texture role.

## Place and adjust the Sprite

1. Choose **Entity → Create → Rendering → Sprite** and select the new entity.
2. In Inspector, assign the published Material under **Image material**.
3. Under **Size and pivot**, set width and height in meters. The image lies in local XY; move, rotate, scale or parent it with the normal transform controls.
4. The pivot uses normalized coordinates from the image's top-left: `(0,0)` anchors that corner, `(0.5,0.5)` centers the image, and `(1,1)` anchors the bottom-right. Flip X/Y mirrors the image without moving its pivot or geometry.
5. Under **Appearance**, adjust the RGBA tint and order. Higher order draws later at equal transparent camera depth; camera depth still takes priority. Visibility and layers use the usual camera and entity rules.

Inspector edits use normal Scene Undo/Redo and saving. Picking uses the image's
rectangle, including transparent pixels. Add a game Camera facing the image before
testing Play or export; the Scene view's editor camera is independent.

## Select frames from a sheet

The visual **Choose frame...** controls are in development source; Build193 uses
numeric frame selection. Windows validation of the visual controls is pending.

Use **Sheet frames** to set columns, rows, outer pixel margins and pixel spacing.
The cells must divide the selected image into whole positive pixel rectangles.
Frame zero is the top-left cell, then numbering continues across each row. A
one-column, one-row grid shows the whole image. The Inspector limits the frame
index to the chosen grid.

Choose **Choose frame...** beside Frame to see the published image with cell
boundaries and the current frame outlined. The preview reports image and cell
sizes in pixels. Click a cell to select it, or use the arrow keys to highlight a
frame and **Enter / Use frame** to confirm. **Escape / Cancel** leaves the original
value unchanged. Wheel to zoom, right-drag to pan, and use **Fit image** to see the
whole sheet. Margins and spaces between cells do not select frames.

A choice in Scene Inspector is one normal Undo step. In a prefab source document,
it changes the draft; **Publish source** and **Discard edits** retain their usual
meaning. A Live gameplay choice is temporary and follows the normal Stop/Keep
values review. An animation can overwrite a manually selected live frame on its
next sample; use an unanimated Sprite when trying manual changes.

The chooser waits for the current published image. A failed or unsupported image
cannot select a frame from an older preview. Correct the material or import and
use **Retry image** when offered. Changing the Sprite, material, grid or loaded
image revision closes the old chooser; open it again for the new selection.

An incompatible grid or material shows a diagnostic and retains the last usable
complete image, frame and size. Correct the values to retry. A retained image
does not mean the current edit is valid. The first Sprite profile supports 2D
RGBA8/sRGB images with ordinary opaque or straight alpha. It rejects compressed,
HDR, premultiplied-alpha and custom surface Shader images. Sheet sampling uses
the base image level; this is not an atlas mip-generation feature.

## Animate the frames

1. Create a [transform animation](transform-animation.md), or open an existing one.
2. Choose the Sprite in **Preview object**. Under **Animated properties**, enable **Sprite frame (step)**. Disable Position/Rotation/Scale channels if you want only frame animation.
3. Enter a whole frame number, or use **Choose frame...** with a valid Sprite preview target. A visual choice only fills the pending frame field. Set the key time and choose **Add / Update key at playhead** to commit it. Repeat for the other frames.
4. Scrub or play Timeline preview, then save the animation and attach it to the Sprite using the usual animation workflow. Enable looping when needed.

A frame holds until the next key. There are no fractional frames, curves or
tangents for this property. Every keyed frame must fit the target Sprite's grid;
an incompatible clip is rejected before it partially changes the Sprite.

During Play, Inspector's **Live gameplay** section can temporarily change the
frame, size, pivot, flips, tint and order. Material and sheet layout changes belong
to Edit. Live changes follow the usual [Play mode](play-mode.md) keep/revert review.

## Export

Save the Scene, Material and animation. Set the saved Scene as the startup Scene
in Project Settings and use **Run → Export Game...**. The export includes the
prepared images, materials and animation. The resulting game needs no source
images, C++ compiler or editor to display and animate the Sprite.
