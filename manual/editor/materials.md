# Materials

A material controls how a surface responds to light and which textures it uses.
One material can be shared by many objects. Changing an object's assignment does
not change that shared material; editing and publishing the material does.

## Create a material

1. Stop Play. Open **Content → Create / Register → New material...**.
2. Enter a new project-relative path ending in `.material.json`, such as `Assets/Brick.material.json`. The folder must already exist.
3. Choose **Create**. The Material document opens in the center workspace.
4. Choose **Shader model**: **Metallic / Roughness**, **Specular / Glossiness**, or **Unlit**.
5. Expand a parameter, enter its value and press Enter. Preview changes are independent of the scene.
6. Choose **Save** to save the source and prepare a validated published material.

Double-click a published project Material in Content to reopen it. Materials inside
an imported model open that model's import document. To customize one without
changing the model source, create a project material and choose the imported
material as its **Base material**.

## Preview the surface

Choose **Sphere**, **Cube** or **Plane** under **Geometry**. Hold MMB over the image
to orbit; scroll to zoom. **Preview lighting and background** changes exposure,
key light, background color and the preview environment. These are preview controls;
they do not modify the scene's camera, lighting or entities.

A wide document places properties beside the preview; a narrow document stacks
them. Invalid edits show an error and keep the previous usable preview. A retained
preview therefore does not mean the current draft is valid or published.

## Colors, transparency and textures

**Parameters** contains the selected model's numeric factors. Color factors are
linear RGBA/RGB values. **Unlit** removes lighting response. **Surface state**
controls alpha behavior, alpha cutoff, two-sided rendering and depth testing/writes.
Use **Mask** for cutout surfaces and **Blend** for transparency.

Expand a role under **Texture slots** and choose a Texture asset, or drag a compatible
asset from Content onto its picker. The texture needs the matching imported usage:
color for surface colors, normal for normal maps, and data for numeric masks.
A cube environment texture cannot fill a two-dimensional surface texture slot.
An incompatible selection fails publication instead of replacing the usable material.

Each slot has its own UV set, offset, signed scale, rotation in radians, wrapping,
filtering, anisotropy and LOD controls. **Border RGBA** appears for border wrapping.
Anisotropy requires compatible linear filtering. See [Textures](textures.md) for
preparing usage variants. Sampling settings belong to this material binding; they
do not edit the shared image source.

## Assign a material to an object

1. Select an entity with **Mesh renderer** in Inspector. Imported model placement creates this component; it can also be added through **+ Add Component**.
2. Choose its **Mesh** if one is not already assigned.
3. Under **Materials**, choose a material for **Surface** or a named mesh slot. You can also drop the Material from Content onto that picker.
4. Save the scene to retain the assignment. Scene Undo/Redo reverses the assignment.

**Use mesh default** removes one scene assignment. **None / Clear** is an explicit
empty assignment, not a request to follow the mesh's original material. If reimport
removes a slot, **Unresolved slot** preserves its old assignment for inspection or
removal. The editor never guesses a replacement slot.

## Make a variation

Choose a published **Base material** in another Material document. Unmodified
fields follow that base when this material is rebuilt. Entering a value creates
an explicit override, even if it equals the base today.

**Revert** removes that override. **Reset to default** explicitly chooses the
shader model's default instead. Clearing a texture removes the inherited texture;
reverting the texture resumes inheritance. **Revert all overrides** removes the
known local overrides while keeping the base selection.

A base update invalidates dependent materials, but their previous published
revisions remain usable until their replacements are built. Save a dependent
material to rebuild it against the newly published base.

## Saving, history and failures

While Material has focus, the main Save and Undo/Redo actions target its source
draft. Scene history is separate. Closing with pending changes offers **Save**,
**Discard** or **Keep editing**. **Reload source** uses the same guard.

Save writes the source first, then validates and publishes its prepared revision.
If publication fails, the saved source may contain unfinished work while existing
objects continue using the previous published material. The document stays pending
and shows the error; correct it and save again. Discard does not undo a source file
already saved to disk. Material source Undo does not undo catalog publication.

External file changes are checked before overwriting the source. Reload and resolve
a conflict instead of assuming the editor can merge another writer's edits.

The current editor exposes built-in material models and numeric/texture properties.
It does not expose a node graph or let an arbitrary Shader asset replace the mesh
renderer's shader contract. See [Shader import](shaders.md) for that separate workflow.


### Preview framing and field ownership

The preview initially fits its geometry to the available image. Changing geometry
fits it again; **Frame view** restores fitting after you zoom manually. Hold MMB to
orbit and scroll over the image to zoom. Ctrl+Plus/Minus continues to scale the editor
interface. Preview framing and lighting do not change the scene or material source.

Surface-state fields show **Revert** when they own an override. Unmodified base values
show **Inherited**. You can also right-click a surface field to inspect its ownership
and choose **Revert to inherited / default**. Unmodified fields no longer use a separate
disabled Revert-button row.
