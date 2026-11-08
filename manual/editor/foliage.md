# Place foliage

Use **Tools > Foliage placement...** to place independent copies of one Mesh
Renderer in a horizontal disk, using a plane or a published heightfield terrain.
Each placement becomes an ordinary object in a
named group, ready for the Hierarchy, Inspector, Scene Save and game export.

## Choose the template

1. Place or create an object with a **Mesh Renderer** and assign its Mesh.
2. Set its local rotation and scale. Assign the materials, numeric material values, layers, shadows and optional **Wind** settings you want on the new objects.
3. Select the object in the Hierarchy, open **Tools > Foliage placement...**, and click **Use selected Mesh Renderer**.

The tool starts the disk center at the template's world position. It copies the
Mesh Renderer and the template's local rotation/scale. It does not copy gameplay,
colliders, animation, children or Prefab links. A model's skin/animation instance
is not created by this tool. The template remains in the Scene. Parent transforms are not copied; use an
unparented template when you want its visible rotation and scale to match exactly.

## Set the layout

Enter the new **Group name**. Set these controls:

- **Center X (m)**, **Center Y (m)**, **Center Z (m)**: World center of the new group. With terrain projection off, every new origin uses the same Y. With projection on, Y sets the group's origin; terrain and Height offset set the child heights.
- **Radius (m)**: Horizontal disk radius, from 0.01 to 1000 meters.
- **Minimum spacing (m)**: Minimum horizontal distance between new origins in this group. Zero permits any spacing.
- **Count**: 1 to 512 objects per placement. With terrain projection or collision rejection, **Candidate count** generates this pool before filtering.
- **Seed**: 0 to 4294967295. Change it for a different layout.
- **Minimum scale multiplier**, **Maximum scale multiplier**: Random uniform multiplier of the template's local scale. Each value is 0.01 to 100; maximum must be at least minimum.
- **Minimum yaw (deg)**, **Maximum yaw (deg)**: Rotation about world +Y before the template's local rotation. Use -360 to 360 degrees; maximum must be at least minimum.

Choose **Check layout** to validate all settings without changing the Scene.
Choose **Place foliage** to create and select the group. Each click creates a new
group, including when you reuse the same seed. Equal settings reproduce the layout
with new independent object identities.

![Foliage placement controls with a selected template and checked layout](../images/foliage-placement-controls.png)

With projection off, the controls describe a horizontal origin plane. Choose a
Mesh whose pivot is at its base when each plant should rest on the surface at
every scale.

## Place onto a heightfield

Open **Gallery 147 — Terrain foliage** to see 128 blades resting on the layered
terrain. They are ordinary objects with saved heights, ready for Play and export.

1. [Create or open a heightfield terrain](terrain.md), save and publish it, and place its Mesh in the Scene. Keep it upright: rotation about Y and scaling are supported, with positive Y scale.
2. Choose the foliage template with **Use selected Mesh Renderer**. Enable **Project onto a surface**, then choose **Surface geometry → Heightfield terrain**.
3. Select the terrain's Mesh Renderer in Hierarchy, then choose **Use selected terrain**. The template and terrain have separate selections.
4. Set **Center X (m)**, **Center Z (m)** and **Radius (m)** so every new origin lies over a terrain tile. Set **Height offset (m)** from -1000 to 1000; zero places origins directly on the published surface. **Center Y (m)** controls the group pivot rather than moving the projected origins.
5. Set **Minimum slope (deg)** and **Maximum slope (deg)**. The default 0–90 keeps every covered candidate. For flatter ground, try 0–20. These angles measure the actual world-space triangle surface, including terrain scaling; they do not measure smoothed lighting normals.
6. Enable **Align to terrain slope** to tilt the placement frame onto the triangle surface. Leave it off to keep world-up yaw and the template orientation.
7. Choose **Check layout** and read how many candidates remain. **Place foliage** creates that subset. Save the Scene. Undo removes the entire new patch in one step.

![Foliage controls with terrain slope range, alignment enabled and checked candidate count](../images/terrain-foliage-controls.png)

The selected terrain supplies heights and geometric slopes; Check layout reports how many candidates remain.

Projection uses the published surface. Save and publish terrain edits before
placing a patch; unsaved source drafts are not used. If a saved source differs
from its published Mesh, preparation stops until it is published again. Every
origin must be covered; a disk crossing missing tiles or the outer edge can fail
without creating objects. Move its center or reduce its radius.

Slope filtering keeps candidates in the original seeded layout. It does not
search again to fill the requested candidate count. A lower retained count is
expected; change the seed, center, radius or slope range for a different patch.
Retained origins keep their original spacing, yaw, scale and height offset. If no
candidate meets the range, the tool reports an error and creates no group.
Minimum and maximum must satisfy 0 ≤ minimum ≤ maximum ≤ 90 degrees. Both ends
are included. At an exact triangle diagonal or tile boundary, the same triangle
selection as the terrain height query determines the slope.

With **Align to terrain slope** off, objects retain their template orientation
and yaw rotates about world +Y. With it on, the tool tilts that placement frame's
+Y to the geometric terrain normal. Seeded yaw and the template rotation stay
relative to the tilted frame. A normally upright template follows the slope;
its own authored tilt or base-orientation correction remains relative. Scale and
its axis signs are preserved. Height offset moves along world Y, not the normal.
Heights and orientations are copied at placement.
Later terrain movement or sculpting does not update old patches. Place a new patch
or adjust the ordinary objects when the ground changes.

![Foliage following the slopes of the layered heightfield](../images/terrain-foliage-game.png)

Gallery 147 uses the existing blade Mesh and heightfield with saved, independent plant transforms.

## Place onto a static Mesh

1. Stop Play. Publish an editable Mesh or import a static Model, then place its Mesh Renderer in the Scene.
2. Choose your foliage template with **Use selected Mesh Renderer**. Enable **Project onto a surface**, then choose **Surface geometry → Static triangle Mesh**.
3. Select the surface object in Hierarchy and choose **Use selected Mesh**. The template and surface have separate selections.
4. Set the disk center, radius and candidate count. Every horizontal origin must lie over an upward-facing surface triangle. Where surfaces overlap vertically, the highest upward triangle supplies the height.
5. Set **Height offset (m)** and the slope range. Enable **Align to Mesh slope** to tilt each placement to the geometric triangle normal. Leave it off for world-up yaw and the template's orientation.
6. Choose **Check layout**, then **Place foliage**. The new group uses one Scene Undo entry. Save the Scene; export uses these ordinary saved Mesh objects with the matching runtime kit.

![Static Mesh surface selection, slope alignment and checked candidate count](../images/mesh-foliage-controls.png)

Choose Static triangle Mesh, select the surface and check the layout before placing.

Projection uses published LOD0 triangles at the surface's authored Scene transform.
Tilt, signed and nonuniform scale are supported. A flattened surface can work when
it retains valid upward triangles. Downward, vertical and unstable projected
triangles do not provide coverage. Image transparency, lighting and Material
appearance do not trim this geometric boundary.

The selected Mesh is limited to 8 MiB of decoded geometry, 16,384 vertices,
12,288 indices across its LODs and 32 parts, with at most 4,096 LOD0 triangles.
Skinned or morph-target Mesh data, enabled surface Wind and point/line parts are
unavailable. Reduce the Mesh or use heightfield mode for larger terrain.
Publish edits before preparing another patch. Unpublished source edits do not
change this mode's selected surface.

![Saved blades aligned to a tilted ramp and upright on a mirrored ramp](../images/mesh-foliage-game.png)

Gallery **200-mesh-surface-foliage** compares slope-aligned and world-up patches
on two instances of an editable ramp Mesh. Open its source under `Assets/Foliage`.

Later surface movement, geometry edits or animation do not move old placements.
They remain independent Scene objects. Optional **Reject collisions** composes
with this projection through the existing controls below. The optional Scene brush below creates independent objects; there is no live
foliage population.

## Paint foliage in Scene view

Open **Gallery 201 — Foliage surface brush** and press **Play → Game** to see two
saved strokes on tilted and mirrored ramps. Stop Play before painting another.

1. Open a project and choose a template with **Use selected Mesh Renderer**.
2. Enable **Project onto a surface**, choose its geometry profile, then select the ground object in Hierarchy and choose **Use selected terrain** or **Use selected Mesh**. Keep that surface selected.
3. Set **Radius (m)** and **Candidate count** for each dab. Configure slope, alignment, scale and yaw as for one patch. Disable **Reject collisions**.
4. Enable **Paint in Scene view**. Set **Dab distance (m)**, the minimum horizontal distance between sampled dab centers. Values from 0.01 to 2000 m are allowed. The single-disk center and Check layout / Place foliage controls become disabled. Enabling Reject collisions switches back to single-disk placement.
5. Drag the left mouse button over the chosen visible surface in Scene view. Green circles mark sampled centers. Release to create the whole stroke, or press Escape to cancel it. One Scene Undo removes the whole stroke.
6. Save the Scene. Keep the surface selected to paint another stroke. Expand the new stroke group in Hierarchy to edit its dab groups and individual plants.

![Foliage placement with pointer-driven brush mode, disabled single-disk centers and four green sampled-center markers in Scene view](../images/foliage-brush-native.png)

The Scene overlay marks brush mode and sampled centers while you hold LMB.

Each stroke allows at most 16 dabs and 512 total candidates. Exceeding either
limit cancels the whole stroke. Reduce Candidate count, shorten the stroke or
increase Dab distance. Fast motion does not fill unsampled gaps. Minimum spacing
applies within each dab; plants in different dabs can overlap. Markers show
centers, not the final plant shapes or a world-radius outline.

Move close enough to use visible LOD0 triangles. Foreground objects block sampling
through them. The entire disk at each dab needs surface coverage; a dab near an
edge can reject the whole stroke. Projection chooses the highest upward surface
at each candidate XZ origin, even if a Mesh has overlapping surfaces. Brush mode
uses the same static geometry and terrain transform limits described above.
Collision clearance is available with the single-patch controls when Paint in
Scene view is disabled. Close the panel or change view, selection, Scene, project,
settings or editing mode to cancel an unfinished stroke. Later surface edits do
not move existing plants.

![Two saved foliage brush strokes on tilted and mirrored ramps in the Game view](../images/foliage-brush-game.png)

Each plant is an ordinary saved Mesh Renderer object in the exported game.

## Keep clear of colliders

Open **Gallery 156 — Foliage clearance** for a meadow placed around a large box
collider. Its saved plants are ordinary objects; no placement task runs during Play.

1. Open a project, choose your template and set the layout. Surface projection, slope filters and slope alignment can stay enabled.
2. Enable **Reject collisions**. Objects to avoid need enabled 3D Physics Bodies and colliders; a visible Mesh alone does not block placement. Save and publish any Collision asset edits first.
3. Set **Clearance radius (m)** from 0.001 to 100. A sphere of this world-space radius tests each candidate; it does not scale or rotate with the template.
4. Set **Clearance offset X (m)**, **Y (m)** and **Z (m)** relative to each candidate origin. Each offset is -1000 to 1000 meters. The default (0, 0.5, 0) checks above the plant's base. Offsets follow world axes, including with slope alignment.
5. Set **Collision layer mask** to the unsigned combination of the layer bits you want to avoid. The default 4294967295 selects every layer; 0 selects none. Enable **Include sensors** when sensor volumes should also exclude candidates.
6. The template and selected projection surface are ignored automatically. To ignore another collider, select its object in Hierarchy and click **Ignore selected object**. Up to 14 additional objects are allowed. Children are separate objects. **Clear ignored objects** removes these additional exclusions.
7. Choose **Check layout**. Preparation loads the current published collision data and reports the number of candidates retained. It changes no Scene objects. **Cancel clearance** stops pending preparation.
8. Choose **Place foliage**. The tool prepares current clearance, then creates the retained subset in one Scene Undo step. Save the Scene.

![Foliage controls with terrain projection, collision sphere settings and a checked candidate subset](../images/foliage-clearance-controls.png)

The clearance sphere tests the authored colliders without advancing simulation.
It composes with the slope mask and keeps original candidate order, yaw, scale,
height and normal. It does not resample to fill the count. A completely blocked
layout reports an error and creates no group. Changing the Scene or project
cancels stale preparation. Closing the panel or entering a mode that locks Scene
edits cancels a pending placement. Assets or project settings that change during
preparation require another check. Preparation has a 30-second deadline; missing
or invalid collision assets are errors.

Clearance tests existing 3D colliders, including published Collision assets. It
does not test complete plant Mesh bounds, collisions between new plants, 2D
colliders or future object motion. Adjust radius, offset, spacing and seed for
your geometry. Every query origin must lie within +/-1 billion meters on each axis.
Tool choices and exclusions are session-only. Later collider changes do not move
already placed objects.

## Add wind

Open **Gallery 157 — Foliage wind** and press **Play → Game**. The left patch sways;
the right patch stays still. The camera, light and shadows are included. Project
C++ is optional for this built-in effect.

1. Stop Play and select the blade template or an individual plant in Hierarchy.
2. In **Inspector → Mesh Renderer**, expand **Wind** and turn on **Enabled**.
3. Set **Amplitude (m)** for the maximum horizontal movement and **Height (m)** for the world height above the object origin where full movement is reached. A base pivot makes a useful plant origin.
4. Set **Direction X** and **Direction Z** for the horizontal world direction. Their relative values choose the direction; increasing both equally does not increase strength. Keep at least one nonzero when amplitude is nonzero.
5. Set **Frequency (Hz)** for cycles per simulation second, **Wavelength (m)** for the distance between repeating waves, and **Phase (°)** for an offset.
6. Save the Scene, then press Play. Pause holds the wind; Step advances it by one fixed tick. Simulation speed also controls its time progression.

![Mesh Renderer Wind expanded in Inspector, with direction and wave controls](../images/foliage-wind-inspector.png)

Edit holds a fixed wind preview. A particular phase can look straight; press Play
or change Phase to see another point in the wave. Zero amplitude keeps the original
geometry. Zero frequency holds the time phase; moving an object can still change
its position within the wave.

During Play, select the object and expand **Live gameplay → Mesh Renderer → Wind**.
The colored panel marks temporary values. Numeric edits commit with Enter. Stop
restores the authored settings. To keep a change, choose **Keep values after
Stop...**, stop Play, review it and choose **Apply to Scene**. Wind is one complete
reviewed property and one Scene Undo step; Save remains separate.

![Wind being tuned in the colored Live gameplay panel](../images/foliage-wind-live.png)

Set Wind on the template before placing foliage to copy it into the new patch.
Existing patches keep their own values when the template changes. Group rotation
does not rotate the world wind direction. Geometry, shading, shadows and picking
follow the displaced vertices; authored transforms and colliders stay fixed.
Use enough Mesh subdivisions for the bend you want: a coarse blade has straight
edges between its displaced vertices.

![Gallery 157 with a wind-enabled meadow and a still comparison patch](../images/foliage-wind-game.png)

Wind supports static Mesh geometry. Active skinning and morph targets cannot be
combined with nonzero wind. Amplitude is 0–100 m, Height is 0.01–10000 m,
Frequency is 0–60 Hz, Wavelength is 0.1–10000 m and Phase is -360–360°.
Each direction component is finite within ±1000000. Active wind object origins
must stay within ±1 billion world meters on each axis. Export a saved Scene with
the matching runtime kit supplied with the editor; an older kit without wind
support is rejected. Wind needs no extra file or placement task during gameplay.

## Edit, save and export

Expand the group in the **Hierarchy** to select individual objects. Move or rotate
the group to transform the whole patch. Use the **Inspector** to edit a child.
Changing the template entity does not update existing patches. Mesh and Material
assets remain shared, so editing those assets follows their usual behavior.

**Scene Undo** removes an entire placement in one step; **Redo** restores the same
objects. Save the Scene to keep the patch. Normal Play and **Export Game** use the
placed objects and their existing asset references. The placement tool is disabled
during Play and active tasks that lock Scene edits. Tool choices are session-only;
they are not a saved procedural recipe.

Open **Gallery 144 — Foliage placement** for a tapered blade Mesh, two seeded patches
and a moving foreground marker. Select its template to make another patch. The
Mesh source opens in the ordinary editable Mesh tools.

![Green and amber foliage patches with an independent template and moving pink marker](../images/foliage-placement-game.png)

Gallery 144 uses one original blade Mesh with its pivot at the base. Each patch is
an ordinary group that you can expand, edit, save and export.

## Troubleshooting and limits

If the requested count cannot fit, increase the radius, reduce spacing/count or
change the seed. The failed placement creates no objects. A corrected successful
check or placement clears the tool's Problem entry. The whole Scene remains limited
to 10,000 entities and the normal command data budget.

Spacing compares new origins in the current group. Mesh bounds can overlap, and
existing objects require **Reject collisions** and enabled 3D colliders to affect
clearance. Heightfield mode requires upright terrain with positive Y scale;
static Mesh mode supports the bounded published triangle profile above. Brush
strokes use the limits described above; live surface links and world streaming
are unavailable. Adjust objects with the normal
Scene tools. Very large coordinates still have the limits of their consuming
renderer, collision and navigation systems.

At very large centers, world positions have less precision. The layout checks
spacing after that rounding. A dense request can fail when too few distinct world
positions fit; increase the radius or reduce the count.
