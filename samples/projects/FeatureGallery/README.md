# Feature Gallery

## Fixed ECS worker example

Open `161-fixed-ecs-workers` for sixteen colored cubes, camera and light. It is
static until the supplied C++ component/system is built and attached. Follow
[ParallelMotion setup](../../../manual/editor/native-gameplay.md) to create the
managed Component/System, use the canonical `CodeExamples/ParallelMotion.hpp`
and `.cpp`, then set **Fixed ECS workers** to 4 and save settings. Positive and
negative speed move each attached cube along its owned local X. Compare with 1
after restarting Play. Save the Scene and settings before exporting with matching
module/runtime kits. This small example illustrates behavior, not speedup.

## Input Message triggers

Open `160-local-fog-volumes` to compare blue box mist and warm ellipsoid mist
around depth markers. Change Shape, Density, local half extents or Enabled in
Inspector. Camera and light are supplied. No project C++ module is required;
save and export with the matching runtime kit.

Open `159-input-message-triggers` to try digital Released and analog Changed
sources. Create an empty C++ System named **InputMessageTriggers**, replace its
source with `CodeExamples/InputMessageTriggers.cpp` and copy the matching
`InputMessageTriggers.hpp` beside it in `Native/Systems`. Build project code with
the matching SDK, press Play and capture Game input. Release Space to toggle the
orange cube's height; Q/E or the right stick's horizontal axis sets the blue
cube's height; W/A/S/D or the left stick sets the green cube's position.
Stable values do not repeat; returning to neutral sends one change. Console's
Gameplay input section shows each source's trigger and dispatch status.

The receiver borrows the current fixed action value through the public SDK
inside the typed component observer. It does not replace the attached payload
with input fields. The installed `input_messages_gameplay` target includes this
receiver and the original Pressed example in scene150. Save authored settings and
scene, select the scene as startup, build and include its matching module and
runtime kit for export. See [the input-message guide](../../../manual/editor/input-messages.md).

## Imported diffuse lightmaps

Open `158-imported-lightmaps` to compare three cubes sharing one graph Material
with object-local lightmap gains zero, one and two. The original small HDR image
illustrates linear incident diffuse irradiance; it is not a bake of the scene.
There are no Scene lights and environment intensity is zero, so the map supplies
the diffuse contribution. Metallic surfaces have no diffuse response.

The Windows package prepares this example's Shader and Material with its matching
compiler before inclusion. In a source checkout, open
`Assets/Lightmaps/Irradiance.shader.json`, choose **Save & Compile**, then open and
save `Assets/Lightmaps/Shared.material.json` before Play or export. The source
records deliberately contain no fabricated cooked Shader or Material artifact.
Use the Material Graph's **Lightmap irradiance** input, HDR Texture usage and
ordinary Material UV transforms to author another map. FORGE does not bake maps
or generate lightmap UVs. See the manual's imported-lightmap guide.

## BC6 HDR image output

Open **Assets/TextureBlocks/HdrUnsigned.dds** or **HdrSigned.dds** in Content and
choose **Variant → HDR color**. These original BC6 patterns preserve two authored
mips in their cooked publications. **Export image...** writes decoded reference
texels as Radiance HDR, with RGBE quantization and no preview display processing.
Both mip0 images contain nonnegative HDR values. HdrSigned mip1 intentionally
contains negative red, demonstrating an export error that preserves a good file.
This is BC6 decoding/image output, not a general BC6 encoder. No Scene or project
C++ is needed. Sources, import recipes and matching cooked artifacts are included.
See [Texture export](../../../manual/editor/textures.md) for controls and limits.

## Signed block channel images

Open **Assets/TextureBlocks/SignedChannel.dds** or **SignedPair.dds** in Content
and choose **Variant → Data**. These original BC4/BC5 SNORM patterns include
negative and positive endpoints and two mips. **Export image...** writes PNG16
with each stored channel mapped from -1..1 to0..65535; absent channels stay zero
and alpha stays opaque. The mapping appears above the button. Preview display
controls do not change export. See [Texture export](../../../manual/editor/textures.md)
for precision, inspection and limits. Sources, import recipes and matching cooked
publications are included; previous Gallery asset identities remain unchanged.

## Compressed texture image export

Open **Assets/TextureBlocks/BlockColors.dds** from Content. This original BC1 sRGB
texture has color-selector stripes, punch-through transparency and two authored
mips. Click **Export image...** and save PNG or lossless WebP to inspect the
decoded reference texels. Select **Mip** 1 for opaque JPEG output. Preview color,
channel and exposure controls do not alter export. Publication preserves the
original compressed blocks; image export cannot recover pre-compression pixels.
See [Texture image export](../../../manual/editor/textures.md).

## Normalized16-bit image export

Open **Assets/TexturePrecision/Normalized16.png** from Content. This original
published RGBA16 texture has low-bit color variation, fully transparent hidden RGB,
partial alpha and opaque regions. Its import settings use **color** usage with **linear**
color space, no mips and no compression. Click **Export image...**, choose **PNG image**
and save a **.png** file. The export retains every 16-bit sample; a bounded agent
inspection preview is 8-bit and cannot prove low-bit precision. The source image
can be absent after publication. See [Texture image export](../../../manual/editor/textures.md).

## Foliage placement

Open **Scenes/144-foliage-placement.scene.json** and press **Play**. Two ordinary
groups contain 128 green and 64 amber tapered blades, with an independent template,
ground, camera, light and moving foreground marker. Stop Play, select the blade
template and use **Tools > Foliage placement...** to create another seeded patch.
**Check layout** changes no Scene objects; **Place foliage** creates one group with
one Scene Undo. Open **Assets/Foliage/MeadowBlade.mesh.json** in Content to edit the
original blade source. Its pivot sits at the base for uniform scale variation.
This example uses a horizontal plane; it does not project onto terrain. Save,
select this Scene as startup and export without project C++.
See [Place foliage](../../../manual/editor/foliage.md).

## Foliage on terrain

Open **Scenes/147-terrain-foliage.scene.json** and press **Play → Game**. A green
meadow contains 128 ordinary blades resting on the existing layered heightfield,
plus an independent blade template. Stop Play and open **Tools > Foliage
placement...**. Select the blade template and choose **Use selected Mesh Renderer**;
enable **Project onto heightfield terrain**, select the layered terrain and choose
**Use selected terrain**. Set the patch center/radius inside the terrain, then
**Check layout** and **Place foliage**. Each placement remains an ordinary object
and the whole group is one Undo step. Terrain must be published and upright;
every origin needs coverage. Later terrain edits do not move existing blades.
Save, choose this Scene as startup and export without project C++.
See [Place foliage](../../../manual/editor/foliage.md).

## Foliage collision clearance

Open **Scenes/156-foliage-clearance.scene.json** and press **Play → Game**. Saved,
slope-aligned blades surround a large static box collider. Stop Play and select
the blade template in **Tools > Foliage placement...**. Choose the layered terrain,
center(0,0,0), radius5, spacing0.5, candidate count96, seed91, scale0.65..1.15 and
**Align to terrain slope**. Enable **Reject collisions**, radius0.25 and world
offset(0,0.5,0). Check and place the retained subset. Select the obstacle and use
**Ignore selected object** to compare a layout that permits that volume. New plants
have no copied collider. The Scene includes camera/light and uses existing blade
and terrain assets. Save, choose it as startup and export without project C++.
See [Place foliage](../../../manual/editor/foliage.md).

## Foliage wind

Open **Scenes/157-foliage-wind.scene.json** and press **Play → Game**. The left
64-plant patch sways while the right 24-plant comparison stays still. Both use
the existing blade and heightfield assets. Camera, light and shadows are included.
Stop and select **Blade template - Wind controls in Inspector**, then expand
**Inspector → Mesh Renderer → Wind**. Change amplitude, world height/direction,
frequency, wavelength or phase. During Play the colored **Live gameplay** section
offers temporary values and **Keep values after Stop...**. Save and export with
the matching runtime kit; project C++ is optional. Wind changes rendered geometry
and shadows while transforms and colliders stay fixed. New placements copy the
template's renderer values; existing patches stay independent. See
[Add wind](../../../manual/editor/foliage.md#add-wind).

## Fast approximate anti-aliasing

Open **Scenes/143-fast-approximate-antialiasing.scene.json** and press **Play**.
A pink bar moves across thin cyan and amber lines and tilted cubes. Stop Play
and open **Tools > Scene lighting... → Fast approximate anti-aliasing**. Compare
enabled/disabled output, **Subpixel smoothing** and **Edge search steps**. The
defaults are **0.5** and **12**; temporal anti-aliasing is disabled in this example so the
current-frame filter is easy to compare. Camera, light and the original animation
clip are included. Save, select this Scene as startup and export to run it without
project C++. See [Scene lighting](../../../manual/editor/lighting.md).

## Prefab variants

Open **Scenes/142-prefab-variants.scene.json** and press **Play**. Five sliding
beacons use one base, amber and blue variants, and a pink variant derived from
amber. Each instance's track resolves its own sequence controller. Sequence
speeds are 1×, 1.5×, 0.5× and 2×; the second blue instance repeats the 0.5× variant.

Stop Play and open **Assets/Variants/AmberBeacon.prefab.json**. The root owns its
name and sequence speed; its body overrides color. Other values and all member
structure follow the base. Revert an override to follow the base again, or use
**Rebase draft** to review changed base values without discarding draft intent.
Save the Scene and export it to run the same example without project C++.

## Collect static collision geometry

Open **Scenes/141-navigation-collision-collection.scene.json** and press **Play**.
The cyan agent patrols between the green markers around the orange wall. The
floor uses a Static Box Collider; the wall uses the editable
**Assets/Physics/NavigationWall.collision.json** asset. Neither has a Navigation
Surface marker. Camera, light, Patrol behavior and built NavMesh are included;
no project C++ module is needed.

Stop Play and open **Content → Create / Register → Navigation**. Choose
**Build profile → Physical** to see **Collect static colliders** and **Collision
layers**. Collection selects enabled Static nonsensor bodies by their Layer.
Moving/disabled/sensor bodies are skipped; an explicit disabled navigation marker
excludes an object. Rebuild after changing included physical geometry, its selected
Collision revision or the build filters. Save the Scene, select it as startup and
export normally. The exported patrol uses the selected cooked Collision/behavior
and NavMesh without a navigation worker. See
[Static collision collection](../../../manual/editor/navigation.md).

## Authored navigation links

Open **Scenes/140-navigation-links.scene.json** and press Play. The cyan agent
patrols across the upper gap in both directions using the dynamic `Loop` profile.
The orange agent crosses the lower gap once using the one-way `OneWay` profile.

Stop Play and select a link to inspect **Navigation Link**. Change its local
endpoints, attachment radius or **Bidirectional** setting, then rebuild the
matching navigation profile and save the Scene. The example uses ready navigation
assets and the existing patrol behavior; no project C++ module is required.
Export includes the selected built navigation data. These links provide straight
agent traversal; they do not simulate a physical jump. See
[Navigation links](../../../manual/editor/navigation.md).

## Chase and patrol behavior tree

Open **Scenes/139-behavior-tree.scene.json** and press Play. The cyan agent follows
the moving orange target while it is within 3.5 m. Otherwise it pauses for half a
second, then walks between the two green patrol markers. Select **Tree agent -
inspect Node and Tree result** to read its current node, result and step count in
**Live gameplay → Navigation behavior**.

Stop, then open **Assets/Navigation/PatrolChaseTree.behavior.json** in Content.
The Selector prioritizes a Reactive sequence for chasing over the patrol Sequence.
Select Patrol to inspect its ordered children; use **Earlier**, **Later** or
**Disconnect** to change that order. Publish a connected tree, save the Scene and
export normally. The example reuses the original target animation and needs no
project C++ module. See [Behavior trees](../../../manual/editor/navigation-behavior.md).

## Navigation build profiles

Open **Scenes/138-navigation-profiles.scene.json** and press Play. Cyan uses the
`default` Nav asset (radius 0.2 m) and crosses the 3 m passage. Amber uses `Large`
(radius 1.8 m) and moves within the left room. Its clearance cannot cross the gap.
Both agents use **Navigation Avoidance → Shared group = 1**, allowing participants
on these different profiles to steer around each other using their own baked radii.
No project C++ module is required; ordinary export includes both navigation assets.

Outside Play, open **Content → Create / Register → Navigation → Build profile**
to select saved settings and overlays, or choose **New profile...** and build
another. Each agent's **Navigation Agent → NavMesh** keeps its own named asset
reference. Rebuild every affected profile after editing the marked floor/walls,
then save. See [Navigation profiles](../../../manual/editor/navigation.md).

## Selected 2D collision navigation sources

Open **Scenes/137-navigation-collision-sources.scene.json** and press Play. The
cyan agent avoids Circle and Capsule Collider footprints on a Box Collider floor;
the amber agent avoids the published CollisionProfiles Tile Map layer. No project
C++ module is required. Select an agent for **Live gameplay → Navigation progress**.

Outside Play, select a marked source and inspect **Navigation Region 2D → Shape
source → Collider 2D**. Edit its collider dimensions or the selected Map collision
layer, then rebuild with **Content → Create / Register → Navigation → Plane →
2D (XY)** and save. Map and Tile Set edits must be saved/published first. The
Scene, NavMesh and existing selected tile/visual assets export through the ordinary
workflow. See [Collision navigation sources](../../../manual/editor/navigation.md).

## Polygon navigation regions

Open **Scenes/136-navigation-polygons.scene.json** and press Play. The cyan agent
turns around the concave floor's notch; the amber agent routes around a concave
orange obstacle. No project C++ module is required. Select an agent to read
**Live gameplay → Navigation progress**. Stop restores the starting poses.

Outside Play, select **Walkable L - edit polygon Points** or **Concave L obstacle -
edit polygon Points**. Edit **Navigation Region 2D → Points** in local metres.
Contours need3–64simple counterclockwise points. **Use rectangle** clears the
contour; **Create polygon** seeds a rectangle's four corners. Rebuild with
**Content → Create / Register → Navigation → Plane → 2D (XY)** after shape edits,
then save. Enable **Scene → View → Navigation overlay** for boundaries and routes.
The saved NavMesh and ordinary visual meshes are included in a normal export.
See [Navigation](../../../manual/editor/navigation.md).

## Activity Lab: State Conditions

Open **Scenes/135-state-activity.scene.json**, press Play and capture gameplay
input. **Run** activates a spinning cube, falling physics body, particles, a quiet
tone and a computed UI badge. **Rest** keeps the structural group's marker and
disables its nested activity. **Hide all** disables the whole group. Controls and
the camera stay available. State buttons work while paused; **Step** admits newly
eligible physics bodies without resuming the clock.

Select **Activity group - structural condition** or a child to inspect **State
Condition**. The falling body's World spatial binding still follows its parent's
structural condition. Existing enable fields stay authored. The badge uses the
computed `badge:visible` state; the other nested activity uses `app:playing`.
The `app:game` ancestor keeps the marker visible during both Run and Rest.

No project C++ module is needed. State definition, rotation clip and UI source
are in **Assets/UI/StateActivity/**. Stop Play before editing and publishing them,
save the Scene, then export. Future-active dependencies remain in the exported
game. See the [State Conditions guide](../../../manual/editor/application-states.md).

## Signal Lock: application states

Open **Scenes/134-signal-lock.scene.json** and press Play. Capture gameplay input,
click the game's **Play** button and follow the receiver's hint: choose **Cyan**
and **Right**, then **Open connection**. **Return to menu** resets the choices.
The controls work while paused; Pause, Resume and Step use the normal game clock.

**Assets/UI/SignalLock/SignalLock.appstates.json** defines a direct game mode,
shared Game parent, subordinate signal/route choices and computed connection.
RML and RCSS beside it provide the playable menu. No C++ module is required.
Stop Play, open the definition in Content, edit and Save / Publish, then save the
Scene before export. The Game panel's Application states selectors use the same
runtime owner. See the [Application States guide](../../../manual/editor/application-states.md).

## Localized menus and HUDs

Open **Scenes/133-ui-localization.scene.json** and press Play. Capture gameplay
input, then choose English, Français or Español. The catalog changes literal
headings and messages immediately, including while paused. **fr-CA: primary
fallback** selects French; **de: default fallback** selects English. A note has no
French/Spanish translation and deliberately uses the default language. Accented
messages keep `<b>` and `{{braces}}` as visible text.

Select **Language controller - inspect Locale** to tune its live `locale` value.
The Game controls report the language actually displayed and default-message
coverage. Stop restores the authored value. No project C++ module is required.
Edit **Assets/UI/Localization/Messages.localization.json** through Content's UI
Localization document, Save / Publish, save the Scene and restart Play. RML and
RCSS live beside it. The saved example exports its selected cooked catalog without
editable table source. See the [localization guide](../../../manual/editor/ui-localization.md)
for language/key limits, fallback, font coverage and player-preference behavior.

## Navigation behavior graphs

Open **Scenes/131-patrol-behavior.scene.json** and press Play. The cyan and violet
agents share **Assets/Navigation/Patrol.behavior.json**, move between points and
pause independently. The second uses a scene Anchor for a separate lane.

Open **Scenes/132-chase-behavior-xy.scene.json** for an animated orange target and
cyan chaser. Near/far transitions switch between watching and following; its
Camera preserves the course framing in portrait windows too.

Select the agent and read **Live gameplay → Navigation behavior** and
**Navigation progress**. Its live **Enabled** checkbox holds/resumes progress.
Stop before opening the graph from Content, changing actions or conditions, or
editing **Assets/Navigation/ChaseTarget.animation.json**. Save / Publish sources
and save the scene before export. These examples have complete cooked assets and
need no project C++ module or runtime build worker. See
[Navigation behavior](../../../manual/editor/navigation-behavior.md) for controls,
reference assignment, priorities, saving and limits.

## Moving navigation obstacles

Open **Scenes/129-moving-navigation-3d.scene.json** or
**Scenes/130-moving-navigation-xy.scene.json** and press **Play**. The orange
cylinder/circle moves out of the route, pauses, then crosses to the other side.
The cyan agent updates its path and reaches the yellow destination marker.

Select **Animated obstacle - select Radius and Height**. Outside Play, inspect
**Navigation Obstacle → Enabled / Radius / Height** and the ordinary transform
animation clip. Height affects the upright 3D cylinder; XY uses its circle and
ignores visual depth. Entity scale changes the local dimensions. The saved
**Navigation → Dynamic obstacles** profile includes agent-radius clearance.

Enable **Scene → View → Navigation overlay** to inspect the current yellow route;
its teal triangles are the saved base. Save/publish an edited animation clip and
save the Scene before export. These examples contain ready navigation layers and
selected cooked clips. No project C++ module or runtime navigation build worker
is required. See [Navigation](../../../manual/editor/navigation.md) for shape and
budget limits, editing, saving and exporting.

## Glow for Sprites and Tile Maps

Open **Scenes/128-sprite-tile-bloom.scene.json**. The three large shapes are Sprites;
the lower pattern is one Tile Map. They share the original **GlowShapes.png** image
and an ordinary unlit image material. No custom shader compiler or C++ module is
needed for this example.

In **Tools > Scene lighting... > Bloom**, toggle **Enable bloom**. Try **Brightness
threshold → 0.25**, **Intensity → 1.25** and **Spread → 3**, pressing Enter after each
number. Compare Scene preview and Game. The scene's saved threshold acts on linear
image brightness before display exposure; values below1 include ordinary image
highlights. This applies to bright world content and does not create Sprite lighting
or a selective per-object glow switch. Save the Scene before exporting; choose this
Scene as startup in Project Settings and save settings.

See [World sprites](../../../manual/editor/sprites.md)
and [Scene lighting](../../../manual/editor/lighting.md).

## Source-aware Sprite atlases

Open **Scenes/127-source-aware-sprite-atlas.scene.json** and press Play. The top
row selects stable frame keys 7, 41 and 99 from one packed Texture. The lower row
cycles through those images over three seconds. Columns use Disabled, Nearest
and Linear frame mip filtering.

Open **Assets/Sprites/SourceAware.atlas.json**. The recipe reuses the original
Badge, TrimArrow and TrimMarker images. Change padding or input order, then
**Save / Publish**: frame keys keep the same image choices after repacking. Replace
an image in its existing row to keep that key; removing a row retires it. Dirty
recipe drafts block automatic publication until saved or discarded.

Use **Texture frames** on a Sprite to follow recipe keys. Its raw grid, crop and
frame-region fields must stay at defaults. The assigned **SourceAware** Material
provides the atlas Texture and alpha settings. Edit
**Assets/Sprites/SourceAwareFrames.animation.json** to change the held frame keys
or their timing. Save the Scene and choose it as startup before exporting. The
game uses its cooked atlas and Clip without the original source images.

See [World sprites and image frames](../../../manual/editor/sprites.md).


## Camera-driven Tile chunk loading

Open **Scenes/126-tile-chunk-residency.scene.json** and press Play. The camera
visits five painted regions over a 20 second loop, then returns to the first.
Open **Tools > Loaded resources** and watch **Tile chunks** as the view moves.
The complete Map has 322 cells in 10 occupied chunks; each stopped camera view
needs two decoded pages. Shared Scene and Game views can reuse identical pages.

Stop, open **Assets/Tiles/ChunkResidency.tilemap.json** and paint a region. Save/
publish the Map, then save the Scene. Edit
**Assets/Tiles/ChunkResidencyCamera.animation.json** to change the camera route or
its timing. Select the camera's **Transform Animator** to inspect its Clip and
loop controls. Use the Scene camera to inspect other regions; background loading
changes presentation residency while all logical cells remain available.

Choose this Scene as startup before export. The exported game keeps the complete
Map and its camera Clip. This is bounded decoded-page loading, with no unlimited
disk-world or automatic collision-unload behavior. See
[Tile Maps](../../../manual/editor/tile-maps.md).

## Custom polygon one-way collision

Open **Scenes/125-custom-polygon-one-way.scene.json** and press Play. Amber lands
on the outer ring roof, cyan on the hole floor and rose on the separate island.
Violet rises through the ring to an ordinary Static ceiling. Green falls between
the bars to the catch floor. Internal triangle edges do not become platforms.

Stop, select **One-way course - ring, island and separate bars**, and toggle
**Physics Body 2D → One-way collision** to compare solid response. Keep **Sensor**
off and use **Static** or **Kinematic** motion. Enable **Selected collision** to
see arrows on eligible original edges. Edit the shared
**Assets/Tiles/ContourTiles.tileset.json**, publish the Set and Map, then save.
Choose this Scene as startup before export. See
[Tile Maps](../../../manual/editor/tile-maps.md) for boundary and margin controls.

## Dynamic Tile Map bodies

Open **Scenes/124-dynamic-tile-map-bodies.scene.json** and press Play. Three
two-cell Maps fall onto the amber receiver. The density-mass and mirrored 12 kg
instances tumble; the rotation-locked instance keeps its orientation. Every Map
is one body containing a ring, its island and separate bars.

Stop and select a Map to change **Physics Body 2D → Density**, **Mass**, **Sensor**
or **Lock rotation**. Keep **Spatial binding → World**, **Motion → Dynamic** and
**One-way collision** off. The instances share
**Assets/Tiles/DynamicTiles.tilemap.json** and the existing contour Tile Set.
Save the Scene and choose it as startup before export. See
[Tile Maps](../../../manual/editor/tile-maps.md) for controls and runtime cell edits.

## Tile collision contours

Open **Scenes/123-tile-collision-contours.scene.json** and press Play. Amber lands
on the ring roof; cyan falls within its void; rose lands on the island inside the
void. Green falls between the separate bars onto the catch floor. The transparent
image areas match the collision profiles.

Stop, open **Assets/Tiles/ContourTiles.tileset.json**, select a tile and choose
**Tile collision → Contours / holes**. Use **Contour** and **Loop** to select the
outer boundary or a hole, then edit its counterclockwise unit-cell points. Apply,
save/publish the Set and **Assets/Tiles/ContourTiles.tilemap.json**, then test
again. Each tile allows 1–4 solid contours and 0–4 holes per contour, with 3–8
points per loop. Save and choose this scene as startup before export. See
[Tile Maps](../../../manual/editor/tile-maps.md) for controls and current limits.

## Tiles from neighbors

Open **Scenes/122-neighbor-tiles.scene.json** and press Play. One painted tile key
forms the edges around a hole, the flipped L and the isolated arrow. Connected
interiors animate. Stop, open **Assets/Tiles/NeighborDirections.tileset.json** and
select **Paint this tile - animated interior**. Edit **Tile neighbor rules** in
Inspector: click compass conditions, choose result tiles, and change rule order.
Apply, save/publish the Set and **Assets/Tiles/NeighborDirections.tilemap.json**,
then Play again. Paint or erase the logical tile to change its selected neighbors.
Save and choose this Scene as startup before export. See
[Tile Maps](../../../manual/editor/tile-maps.md) for conditions and collision.

## Animated tiles

Open **Scenes/121-animated-tiles.scene.json** and press Play. The four rows run
forward, reverse, with a held final frame, and as a static arrow. Cells also show
mirrors and transposition. Stop and open **Assets/Tiles/AnimatedDirections.tileset.json**,
select a tile, and edit **Tile animation** in Inspector. Apply, then Save / Publish
the Set and **Assets/Tiles/AnimatedDirections.tilemap.json**. The source tab has
local preview Play/Pause/Restart controls. The game uses simulation ticks: Pause
holds, Step advances once, and restarting Play resets the loop. Collision does
not change with the image. Save and select this Scene as startup before export.

## Concave tile collision

Open **Scenes/120-concave-tile-collision.scene.json** and press Play. Amber and rose
land on the upper edges of the blue L and green U. Cyan and green fall into their
empty notches. The original image silhouettes match the authored collision outlines.

Stop, open **Assets/Tiles/ConcaveTiles.tileset.json**, select either tile and edit
**Tile collision → Polygon (decomposed)**. Enter3–8 counterclockwise unit-cell
points with Y up. Apply collision, then Save / Publish the Set and
**Assets/Tiles/ConcaveTiles.tilemap.json**. Enable Selected collision on the course
to inspect the published pieces. Save and choose this Scene as startup before
export. No Project Code module is required. Polygon (decomposed) uses one outline;
choose Contours / holes for voids or separate solid regions. Custom-polygon
one-way response is unavailable.

## Moving one-way platforms

Open **Scenes/119-moving-one-way-platforms.scene.json**, press Play and switch to
Game. The painted course and narrow cyan ledge share a four-second horizontal
translation clip. The amber box falls onto the moving ledge; reversed gravity
pushes the cyan circle through both one-way surfaces to the ordinary ceiling.

Stop and select **Moving one-way inline ledge - same translation clip** or
**Moving one-way painted course - edit its translation clip**. Both have
**Physics Body 2D → Motion: Kinematic**, **Sensor** off and **One-way collision**
on. Open **Assets/Physics2D/Platform.animation.json** to edit their shared motion,
then **Save / Publish**. The ledge's fixed parent gives it a different world height
while reusing the same local clip. Stop before changing saved body settings.
Save and choose this Scene as startup before export. No Project Code module or
additional imported asset is required. Dynamic and sensor one-way collision are
unavailable. Open **125-custom-polygon-one-way** for custom boundary examples.

## Local diffuse lighting

Open **Scenes/118-local-diffuse-irradiance.scene.json** and press Play. The included
warm and cool maps light the floor, rounded receivers and boxes. There are no
direct lights or global environment. Select **Warm diffuse volume - priority 1**
or **Cool diffuse volume - priority 0** and toggle Enabled or adjust Diffuse intensity,
Edge fade, Priority, map yaw or the influence bounds. Intensity controls reflections
separately and is zero here. Scalar controls support Live gameplay tuning.
Save and choose this Scene as startup before export. No C++ module or new import
is required. At most eight active assigned reflection/diffuse probes are admitted
in total; this example is static map lighting, not an automatic multi-bounce solver.

## Light images

Open **Scenes/117-light-images.scene.json** and press Play. Directional arrows
repeat over the left receiver, a patterned Spot illuminates the middle, and a
Point light uses the included captured cubemap on the right. A weak ordinary fill
keeps unlit parts visible. The receivers use separate layer masks.

Select a **... change Light image** entity. Assign **Light image**, rotate the
light, or adjust intensity. Directional **Image width**, **Image height** and
**Tile image** also support Live gameplay tuning. Stop and review values to keep,
then save. Clear the image to compare ordinary illumination. Choose this Scene as
startup before export. It reuses the included arrow-sheet, panel and cubemap
Textures; no C++ module or additional import is required.

## Many shared projectors

Open **Scenes/116-clustered-projectors.scene.json** and press Play. The floor
receives 64 separately tinted badge projectors sharing one Material and Texture.
Select a **Paint row ... column ...** entity and adjust its tint, opacity or box.
Save and choose this Scene as startup before export. No C++ module or new import
is required. The rendering profile supports 128 active projectors, with up to eight
unique projected map/sampler combinations per receiving Mesh part; complex receiving
materials can leave fewer map slots.

## Projected normal and roughness

Open **Scenes/115-projected-surface-channels.scene.json** and press Play. The left
cube receives normal detail, the right receives a roughness patch and the floor
combines both. The rear block is an unchanged comparison. Select a projector and
change **Normal influence** or **Roughness influence** between 0 and 1. **Color
influence** is 0, preserving the receiver's color. All three reuse the existing
ripple material and normal texture. Save and choose this Scene as startup before
export; no new import or C++ module is required.

## Weighted transparency

Open **Scenes/114-weighted-transparency.scene.json**. Three intersecting panels
reuse the badge image with separate object color and alpha values. The floor is
opaque. In Edit, open **Tools > Scene lighting...**, expand **Transparency** and
toggle **Weighted transparency** to compare the intersections. Move or rotate a
panel, then save. Choose this Scene as startup before export. This approximate
blend needs no C++ module or additional import. Views containing transmission,
2D layers or incompatible transparent surfaces retain ordinary sorted drawing.

## Projected material color

Open **Scenes/113-projected-color-decals.scene.json** and press Play. A shared
badge material paints the two lit cubes. The left projector mirrors local X;
the right uses a green tint. Their layer masks select separate receivers. Floor
projectors show translucent color, box clipping and priority overlap.

Select a projector and edit **Width**, **Height**, **Projection depth**, tint or
**Opacity** in Inspector's **Decal** section. The box follows node scale and its
arrow points along local +Z into the receiver. Higher **Priority** paints last.
Live gameplay exposes scalar appearance, volume and channel influences; Source Material is
assigned in Edit mode. Stop, review any values you want to keep and save. Choose
this Scene as startup before export. It needs no C++ module or additional import.

The profile admits 128 active projectors per scene and a material-dependent
per-part shared image/sampler limit. Problems explains omitted projections; ordinary materials remain
available. Physical Mesh surfaces receive the selected color, normal and roughness
channels. Custom Surface Shader sources are unsupported. See the manual's
**Project material channels onto a Mesh** guide for placement, saving and limits.

## Rectangular and spherical lights

Open **Scenes/112-area-lights.scene.json** and press Play. A warm Rectangle lights
the left metal sphere, and a cool Sphere lights the right one. Both illuminate the
floor and rough receiver. Select **Rectangle - change Emitter Width and Height**
or **Sphere - change Emitter Radius** and try its size controls under **Live
gameplay**. Stop, keep selected tuning through the review and save. You can also
edit the authored controls while stopped. Turn off **Preview light** in Scene.

Rectangle width/height and Sphere radius are world metres, independent of entity
scale. Rotate the rectangle to aim its front face or turn its highlight. The
spherical source is opaque; receivers inside it receive no light. These lights do
not cast shadows. Choose this Scene as startup and export after saving. The
example uses built-in PBR assets and needs no C++ module or additional import.
See the Scene lighting manual chapter for intensity, layers and current limits.

## Contact-hardening shadows

Open **Scenes/111-contact-hardening-shadows.scene.json** and press Play. Three boxes
sit at different heights above a lit floor. The shadow edge grows softer as the
box moves away from the floor. Stop and select **Soft sun - change Shadow Angular
Radius**. Compare **Shadow Angular Radius (rad)** at **0.08** and **0**, pressing
Enter after editing. Zero uses the ordinary fixed shadow filter. Turn off
**Preview light** when comparing the authored Scene view.

For a Point or Spot light, use **Shadow Source Radius (m)** instead. These settings
change shadow softness; they do not change direct illumination into an area light.
Both source-size fields can be tried under **Live gameplay** while playing. Stop
restores authored values unless you keep the selected tuning through the review.
Save, choose this Scene as startup and export. The example needs no C++ module or
new import. See the Scene lighting manual chapter for limits and bias controls.

## Capture reflection maps

Open **Scenes/110-reflection-map-capture.scene.json** and press Play. Two smooth
metal spheres and a floor use the supplied captured HDR map. Amber and blue
capture sources use **Layers = 1**; the game Camera uses **Layers = 2** to keep those
panels out of its direct view. The spheres use **Layers = 2**, so capture with **Layers = 1**
excludes the spheres themselves. The shared room geometry uses **Layers = 3**, combining both bits.

Stop Play, select **Captured reflection volume - create a new snapshot** and click
**Capture reflection map…**. Set **Face size (pixels)** to **128**, **Layers** to **1** and choose a new folder
below **Assets**. Click **Capture map**, then keep **Import with defaults after
copying** enabled and click **Copy sources** in the generated-source review. Wait
for publication, close the dialog and save the Scene. Each capture creates a new
map; changing a source panel does not update an old map automatically. Compare
**Enabled** on the probe, or change a panel and capture again to see the difference.
Box projection starts off for this directional-map comparison.

Set this Scene as Startup Scene and export after saving. The captured Texture
travels with the game; the editable DDS source is not required at runtime. No C++
module is required. See **Capture a reflection map from your Scene** in the included Scene lighting
manual chapter for clipping, layer masks, cancellation and assignment rules.

## Roughness-filtered reflections

Open **Scenes/109-rough-reflection-filtering.scene.json** and press Play. Alternating
amber and blue columns reflect in a rough metal floor. Stop, open **Tools > Scene
lighting... → Screen-space reflections**, and toggle **Roughness filtering**.
Compare the reflected detail, then change the floor's **roughnessFactor** below
**Maximum roughness**. Save the Scene, select it as startup and export. The example
uses built-in assets and requires no C++ module or additional import.

## Material-normal ambient occlusion

Open **Scenes/108-material-normal-occlusion.scene.json** and press Play. A blue wall
uses the supplied original **Assets/OcclusionNormals/RippleNormals.png** through
**RippleWall.material.json**. Stop Play and open **Tools > Scene lighting... >
Ambient occlusion**. Compare **Normal influence** at **0** and **1**, pressing
Enter after editing. The normal map changes surface directions without changing
Mesh geometry or depth. Radius, Strength and Depth bias retain their usual roles.
Save the Scene before export; cooked normal texture and Material variants travel
with the game. No C++ module is required. Hidden surfaces are not reconstructed.

## Local reflection probes

Open **Scenes/107-local-reflection-probes.scene.json** and press Play. Warm and
cool imported room panoramas supply base reflections inside overlapping boxes.
Stop and select **Warm reflection volume - priority 1** or **Cool reflection volume - priority 0** to change Enabled,
Intensity, Half extent X/Y/Z, Edge fade, Priority, Map rotation or Box projection.
Move/rotate/scale the probe to adjust its volume; press **F** to frame the outline.
The lower-priority cool map is revealed as the warm map fades near its box edge.
Outside both boxes the Scene environment remains. The maps do not capture Scene
objects or change diffuse lighting. Keep at most eight active assigned probes.
Scalar values can also be tested in Live gameplay; map assignment is Edit-only.
Save the Scene and choose it as startup before export. The maps and cooked texture
variants travel with the game; no C++ module is required.

## Screen-space reflections

Open **Scenes/106-screen-space-reflections.scene.json** and press Play. Amber and
blue blocks reflect in a smooth metal floor. Stop Play, open **Tools > Scene
lighting... > Screen-space reflections** and toggle **Enable screen-space
reflections**. Compare the floor beneath each block. **Maximum distance (m)**
controls reach from 0 to 100; **Thickness (m)** controls depth tolerance from
0.0001 to 1. **Strength** blends from 0 to 1 and **Maximum roughness** fades
rough receivers from 0 to 1. Zero distance, strength or roughness cutoff skips
the effect. Press Enter after typing. Save before exporting. No C++ module or
additional import is required. Off-screen and hidden objects are unavailable;
misses retain environment reflections. The rough wall stays an environment-lit
comparison. Turn off **Preview light** for authored Scene lighting comparisons.

## Ambient occlusion

Open **Scenes/105-ambient-occlusion.scene.json** and press Play. Three amber posts
and a lower ledge sit in front of a blue wall, lit by a built-in environment and a
direct light. Stop Play, open **Tools > Scene lighting... > Ambient occlusion** and
toggle **Enable ambient occlusion**. Compare shading around the visible depth
junctions. Try **Radius (m)** from 0 to 10 and **Strength** from 0 to 1; zero skips
the effect. **Depth bias (m)** from 0 to 1 limits self-occlusion. Press Enter after
typing a value. Shadows are disabled to isolate ambient contacts. Save before
exporting. No C++ module or additional imported asset is required. Hidden and
off-screen surfaces do not supply screen-space occlusion. Normal influence can
refine directions with a base normal map; Gallery108 supplies a mapped example.

## Motion blur

Open **Scenes/104-motion-blur.scene.json** and press Play. A checker panel moves
in front of a stationary checker panel. Stop Play, open **Tools > Scene lighting...
> Motion blur**, and compare **Shutter fraction** from 0 to 1 and **Maximum trail
(px)** from 0 to 32. Zero keeps the view sharp. Save the Scene before exporting.
The **Sequence - continuous checker motion** controller drives an existing clip
through the Timeline; no C++ module is required. This bounded surface profile
softens detail within compatible surfaces and preserves silhouettes.

## Depth of field

Open **Scenes/103-depth-of-field.scene.json** and press Play. The three original
checker panels sit at different distances: the middle panel is in focus, while
the near and far panels soften. Stop Play, then open **Tools > Scene lighting... >
Depth of field**. Change **Focus distance (m)** to 3.95, 9.95 or 15.95 and press
Enter to select the near, middle or far plane. **Focus falloff (m)** controls how
quickly blur increases; **Maximum blur (px)** caps its radius from 0 to 12.
Disable the effect or set its radius to zero to compare sharp views. Save the
Scene before exporting. No C++ module or additional imported asset is required.
Depth edges keep their silhouette; background and current-frame layers remain
sharp in this bounded spatial profile.

## Temporal anti-aliasing

Open **Scenes/102-temporal-antialiasing.scene.json** and press Play. The pink
foreground bar moves and rotates across thin cyan and amber diagonal edges.
Stop Play, open **Tools > Scene lighting... > Temporal anti-aliasing**, and compare
enabled/disabled views or change **History stability** (0–0.95). Save the Scene
before export. The original scene reuses the existing platform animation clip;
no C++ module is required. Opaque and masked Mesh geometry accumulates; transparent,
particle, Sprite/Tile and overlay layers retain their current-frame rendering.
Both camera clears must be enabled for this profile.

## Tile collision profiles

Open **Scenes/101-tile-collision-profiles.scene.json** and press Play. Circles
roll on amber triangular ramps, including horizontally mirrored tiles. The box
falls through pink decoration onto the cyan full-cell floor. No C++ module is
needed. Stop before editing **Assets/Tiles/CollisionProfiles.tileset.json**:
select a tile, choose Full cell/None/Convex polygon in Inspector and Apply its
complete collision profile. Save/publish the Set and Map, then Play or export.
Select the Map actor and enable Selected collision to inspect its published
shapes. Convex polygons have3..8 counterclockwise unit-cell XY/up points. Custom
polygons do not support one-way collision. The image silhouettes are original
deterministic RGBA geometry created for FORGE.

## Animated game UI

Open **Scenes/100-ui-animation.scene.json** and press Play. The blue bar changes
width/opacity and the yellow marker translates. Capture gameplay input, Pause and
Resume: the tick counter stops while UI motion continues, and the state panel
changes color smoothly. Buttons transition on hover/focus. No C++ module is needed.

Edit **Assets/UI/Motion/hud.rcss**, save and use **Reload UI** in Game to adjust
durations, keyframe endpoints or colors. Invalid replacements retain the previous
UI. Set this Scene as startup and export normally; its RML/RCSS travel with the game.
Animation progress is transient presentation state and is independent of gameplay
pause and simulation speed. See the Runtime UI manual for timing names and limits.

## Sprite frame filtering

Open **Scenes/99-sprite-frame-filtering.scene.json**. The left column uses
Disabled, the center Nearest and the right Linear mip filtering. Fine checks,
stripes and transparent pixels become stable colors when reduced; the bottom
row shows ring edges. Each column uses the same four frames of the original
**Assets/Sprites/MipSheet.png** and its ordinary image Material.

Select a Sprite and change **Image fill → Mip filtering**. Reduce its Width/Height
or increase the orthographic camera's height to inspect smaller images. Nearest
selects one smaller level; Linear blends adjacent levels. The Material keeps
nearest pixel filtering within each level. Save, set this Scene as startup and
export to use the saved settings. No C++ module is required. The camera keeps a
fixed aspect, so portrait windows show the whole comparison with letterboxing.

## Distance fog

Open **Scenes/98-distance-fog.scene.json** and press Play. Twelve colored columns
fade toward blue gray with distance. In **Tools > Scene lighting... → Distance fog**,
try Start12 and End34; toggle Enable to compare, or change the color. Distances
belong to each camera; the Scene editor camera can show a different amount.
Sky/background stay separate. Save, set this scene as startup and export normally.
The example needs no C++ module, custom Shader cook or new Material.

## Packed Sprite fills

Open **Scenes/97-packed-sprite-fills.scene.json**. Originals on the left match the
trimmed, clockwise-stored versions on the right. The top arrow pair uses Sliced
fill; the bottom marker pair uses Tiled fill with Flip X and Flip Y. Both reuse
the ordinary original/packed Textures and Materials from the frame-metadata example.

Select each member of a pair and enter the same **Width**, **Height**, **Pixels per
meter** or border values. Sliced borders belong to the original canvas, including
transparent margins. Tiled density repeats that full canvas, including partial
tiles. Keep Blend alpha and depth writes off for actual trim. Save, set this Scene
as startup and export to test the same result without C++ code or a compiler.

## Trimmed Sprite canvases and stored turns

Open **Scenes/95-sprite-frame-metadata.scene.json**. Each original transparent
canvas on the left has a packed counterpart on the right with the same world
size and pivot. The second pair uses Flip X. The bottom Sprite shows the actual
24×56 PNG storage, including padding. Both retained images are stored clockwise.
Frame0 keeps the48×40 arrow canvas with offset(15,10); its stored rectangle is
(2,36),20×18. Frame1 keeps the64×40 marker canvas with offset(17,11); its stored
rectangle is(2,2),16×30. Inspect **Frame regions** on either packed Sprite.

To regenerate, select the published **TrimArrow.png** and **TrimMarker.png**
Texture assets in that order and choose **Pack sprite atlas...**. Use maximum128,
padding2, extrusion, **Trim transparent edges** and **Allow stored quarter turns**.
Prepare/review into a new Assets folder, copy/import, then **Copy atlas frames**.
Create a Sprite / image Material using the new PNG, with Blend and depth writes
off. Assign it to a Simple Sprite and **Paste atlas frames**. Set world width and
height for the original canvas, save the scene and select it as startup for export.
No compiler is required. The original FORGE images and generated PNG remain
independent ordinary assets; changed originals require another explicit packing.

## Automatic Sprite atlas packing

Open **Scenes/94-sprite-atlas-packing.scene.json**. Originals are on the left;
the same Badge and Panel pixels from one generated PNG are on the right. The
bottom Sprite shows the complete132×66 atlas. Frame0 is Badge at(99,1),32×32;
Frame1 is Panel at(1,1),96×64. Both packed Sprites use the same ordinary Material.
Their width/height remain independent world values. No compiler is required.

To regenerate: select the published **Badge.png** and **Panel.png** Texture assets
in Content, in the listed order; choose **Create / Register → Pack sprite atlas...**.
Use padding1, edge extrusion and a new Assets folder. Prepare and review the
source order, copy/import, then **Copy atlas frames**. Assign a Sprite / image
Material using the new Texture and **Paste atlas frames** in Inspector. Save the
scene and set it as startup before normal standalone export.

The PNG is a one-time source copy. Original images do not automatically repack it.
This scene uses whole image rectangles with trimming and stored turns off.
Sprite sampling uses the base level. Original FORGE images are reused, and the
PNG was generated by FORGE's packing worker.

## Automatic exposure

Open **Scenes/96-automatic-exposure.scene.json** and press Play. The sunlight is
dim for two seconds, bright for the next two, then dim for four; the eight-second
cycle repeats. Watch the brightness change settle. The original held property
clip **Assets/Exposure/Daylight.animation.json** drives ordinary light intensity.

Stop Play and open **Tools > Scene lighting... > Automatic exposure**. Change
**Brightening rate (1/s)** or **Darkening rate (1/s)**, press Enter, save and play
again. Larger rates respond faster. Toggle **Enable automatic exposure** to compare
manual gain. **Exposure (stops)** adds manual compensation. Scene View keeps its
own manual preview. Save the Scene, choose it as startup and export to run the same
effect without a C++ module/compiler. See the packaged Lighting guide for percentile,
target and limit controls. Game HUD colors are unaffected by the metering/display pass.

## Unequal Sprite frames

Open **Scenes/87-sprite-frame-regions.scene.json**. One Sprite shows the complete
original arrow sheet; the others use ordered rectangles of different sizes. Play
to cycle the animated bank. Inspect Frame regions and Choose frame, compare the
Sliced panel and mirrored Tiled arrow, then Stop to edit the saved list. Coordinates
are relative to Image region. Item1 is Frame0; clearing the list restores the grid.
Save the Scene and choose it as startup before export.

## Painted one-way platforms

Open **Scenes/86-one-way-tile-maps.scene.json**. The cyan circle rises through the
right painted platform to an ordinary ceiling; other bodies land on painted roofs.
Select the Map and turn on Selected collision to inspect each solid-face arrow.
Stop, disable One-way collision and replay to compare the underside response.
The existing CodeExamples/PaintedCollision.hpp helpers work with this profile too.
Only Static nonsensor Maps support this policy; moving/sensor one-way remains
unavailable. Save before export.

## Audio echo

Open **Scenes/90-audio-echo.scene.json**. Compare the dry recording with quarter-
second and half-second echoes using Live sound → Play / Restart on one source at
a time. Only Dry recording starts automatically; each clip plays once, followed
by its fading echo. All sources use the same original processed recording and
Effects channel. Pause/Resume preserves the tail, Stop clears it, and the ordinary
Audio Source properties save/export without a C++ module.

## Audio filters

Open **Scenes/85-audio-filters.scene.json**. Only Dry tone starts automatically.
Use Live sound to stop it and restart the low-pass or high-pass speaker. All
three use the same original recording. Filter settings are ordinary saved Audio
Source properties and apply to overlapping one-shots as well as the main sound.
Live changes preserve clip position; changing filter state can cause a short
transition. Save authored values to use them in export.

## Independent music and UI sound

Open **Scenes/84-independent-audio.scene.json** and Play. Pause the game: the
lower gameplay tone pauses while the higher music tone continues. Select the
UI speaker and choose Live sound > Play one-shot while paused. Pause with game
is an ordinary saved Audio Source property, enabled by default. Stop and scene
replacement silence all old output. Export uses the same saved policy.

## Audio source codec comparison

Open **Scenes/83-audio-source-codecs.scene.json**. Three speakers play the same
original stereo recording as WAV, FLAC and MP3. Only WAV starts automatically.
Stop, disable its Play On Start, enable one other speaker and Play again. Open
Assets/AudioCodecs clips in Content to audition, seek, inspect their waveforms or
apply the same import settings. Save before exporting; exported games use cooked
PCM and do not need the original recordings. FLAC is lossless; MP3 is lossy.
The folder also contains original mono recordings for import practice.

## Moving Maps and trigger regions

Open **Scenes/81-moving-tile-maps.scene.json** and Play. The painted course moves
left/right through the existing Transform Animator; its Physics Body2D is Kinematic.
Inspect the selected Map's Collision layer, Motion and animation clip. Stop, change
the saved settings and Save the Scene. This example needs no compiled game module.

Open **Scenes/82-tile-map-triggers.scene.json** and Play. The translucent painted
regions have Sensor enabled. The four colored visitors pass through those regions
and land on a separate solid receiver. Ordinary C++ 2D sensor events identify the
Map actor and visitor; multiple rectangles may emit the same canonical pair.
Fast-moving sensors can miss crossings. Stop restores the authored state.

Both scenes export with their normal dependency closure. Kinematic/sensor Maps use
one native body per Map instance, shared layers/masks and ordinary physics queries.
Dynamic Maps and moving/sensor one-way collision remain unsupported. Static
nonsensor one-way Maps are demonstrated in scene86. The user manual's
Tile Maps guide covers authoring, runtime edits, saving, limits and examples.

## Top-down XY navigation

Open **Scenes/80-xy-navigation.scene.json** and Play. The cyan Sprite routes
around the orange obstacle. The green/amber pair steers past each other in the
right lane, keeping their separate visual depths. Select an agent for
**Live gameplay → Navigation progress**; Stop restores the authored starts.

Enable the Navigation overlay and click a region outline, or select **Walkable
region** / **Obstacle region** in Hierarchy. Press **F** to frame its full extent
and **W** to drag its gold edge handles. Release keeps one Undo step; Escape or
right-click cancels. Outside Play, change **Navigation Region 2D → Width / Height / Obstacle**, then
open **Content → Create / Register → Navigation**, choose **Plane → 2D (XY)** and
**Build NavMesh**. Sprite art and physics are separate from these rectangles.
Enable **Scene → View → Navigation overlay** for baked triangles and runtime routes.

Save, choose this Scene as startup and export normally with the matching runtime
kit. It needs no project C++ module. Routing is top-down; platformer links and
runtime obstacle rebaking are unavailable. Local avoidance can jam in dense spaces.

## One-way platforms

Open **Scenes/79-one-way-platforms.scene.json**, Play and open Game. The left
two circles land from above. On the right, reverse gravity pushes circles toward
the platforms: the gray ordinary Box blocks its circle, while the green one-way
Box lets its circle reach the ordinary ceiling.

Stop, select **One-way - rising from below** and turn off **One-way collision**:
the circle now stops at its underside. Undo restores passage. **One-way margin**
controls solid-side tolerance in world metres. **Selected collision** shows an
arrow toward the platform's solid face; negative Y scale reverses that face.

Save the Scene, choose it as startup and export with the matching runtime kit.
This scene needs no project C++ module. For a controllable jumping character,
open the separate [Character2D project](../Character2D/README.md). It also contains
an editable moving platform and a player with **Follow moving platforms** enabled.

## Sprite image regions

Open **Scenes/78-sprite-image-regions.scene.json**. The top row shows the complete
image. The middle row compares an asymmetric crop, an animated cropped strip and
a crop extending to the image edge. The bottom row shows cropped Sliced and Tiled
images, including mirrored partial repeats. All reuse this Gallery's original art.

Select **Asymmetric image crop** and expand **Image region** in Inspector. Change
its horizontal pixel offset from **18** to **34** to select the next signal. Turn
the region off to see the whole source; turn it on to restore the stored crop.
Width or height **0** reaches the corresponding image edge. World size and pivot
stay authored separately.

Select **Animated cropped strip** and open **Choose frame**. Its four frames sit
inside the selected rectangle; pixels outside it are not selectable. During Play,
the existing signal clip advances those relative frame numbers. Try a temporary
horizontal crop offset in **Live gameplay**; animation continues and Stop restores
the authored selection. The Sliced and Tiled objects use the same region controls.

Save the Scene, choose it as startup and export with the matching runtime kit.
The exported game contains its prepared images, materials and animation. It needs
neither the source project nor a project C++ module.

## C++ 2D walk and jump

The separate editable [Movement2D project](../Movement2D/README.md) combines this
Gallery's painted floors with a C++ capsule support sweep, ordinary box stepping
stone, input actions and Inspector movement/jump settings. Open that project's
own folder to compile/run/export it. The compiler-free Gallery scenes remain
independently runnable. See its short experiment for walking, jumping, changing
speed and editing the painted course. Build201 predates this example.

## Painted 2D collision

Open **Scenes/77-painted-2d-collision.scene.json**, press Play and open Game.
Four colored bodies fall onto floors and platforms painted in one Tile Map.
The blue decorative arrows let bodies pass through them.

Stop and open **Assets/Tiles/PaintedPlatforms.tilemap.json**. On **Solid
platforms**, erase the three cells of the left raised platform, then Save / Publish.
Play again: the amber box drops to the lowest floor. Undo the source edit and
publish to restore it. Select the Map actor's **Tile Map Collider 2D** to inspect
its named solid layer and use the Scene collision outline to see the published boxes.

The Scene needs no C++ module. **CodeExamples/PaintedCollision.hpp** demonstrates
queued cell removal and a normal physics query for a matching compiled C++ module.
Runtime edits disappear on Stop. Save/publish and save the Scene before exporting;
the exported game uses the same prepared Map collision without the editor or source.
This profile supports Static nonsensor Map bodies and rectangular occupied cells.

## Connected 2D bodies

Open **Scenes/76-connected-2d-bodies.scene.json**, press Play and switch to Game.
From left to right:

- The cyan pendulum swings about its fixed anchor. Its thin arm is an ordinary
  visual child of the moving body.
- The amber body swings while its Distance joint keeps the two centers two metres apart.
- The rose body oscillates toward a two-metre rest distance. Gravity is disabled
  on this body so you can see the spring's rest length directly.
- The green carriage travels right along its rail and stops at the travel limit.
  The motor keeps pushing within its force budget; reversal needs an edit or C++.

Stop before changing saved settings. Select **Slider - positive rail motor**,
change **Slider Motor Speed** to `-0.9`, and Play again to travel left. Set
**Max Travel** to `0.5` to shorten the rightward journey. Select
**Distance - elastic two metre rest** and set **Spring Frequency** to `0`:
the connection becomes rigid. Undo restores the spring. Increasing **Spring
Damping** reduces the oscillation. Select a joint in Scene to see its authored
anchors, limits and settings; these guides are absent from the exported game.

The station strips and slider rail are visual markers. This demonstration disables
body collisions to isolate the joint behavior. It needs no optional C++ module.
**CodeExamples/Physics2DJoints.hpp** shows how a matching C++ fixed system can
change motor targets and spring settings. Stop restores the saved starting state.
Save, choose this Scene as startup and export using the matching runtime kit.
The standalone game uses the same saved joint settings without source or a compiler.

## 2D physics

Open **Scenes/75-2d-physics.scene.json**, press Play and switch to Game. Five
colored bodies—three boxes, a circle and a capsule—fall, cross the translucent
green sensor, and collide
with the blue moving platform or the gray floor. The green strip lets bodies
pass through. The platform repeats a four-second horizontal journey.

Stop, select **Dynamic amber box**, and change **Physics Body 2D > Restitution**
to `0.8`. Play again to compare the bounce. Stop to restore the saved starting
positions. Change **Motion** to Static to hold that body in place, or adjust its
box collider dimensions separately from the Sprite size. Select a body and
enable **Selected collision** in the Scene view to compare its collision outline
with the image. The capsule's three ordinary Sprite children follow its body.

Select **Green sensor**, turn off **Sensor**, and Play to turn the strip into a
solid shelf. Stop and Undo to restore the sensor. Its Begin/End crossings are
available to C++; the strip does not change color automatically. The optional
**CodeExamples/Physics2D.hpp** demonstrates queued launch velocity and copied
sensor events. This scene runs without compiling an optional module.

Save the Scene, choose it as the Project startup Scene, and export with the
matching runtime kit. The exported game uses the saved bodies, Sprite materials
and prepared platform clip. Source files and a C++ compiler are unnecessary on
the player's machine. This scene requires a build containing the 2D physics
milestone; Build261021-000195 does not contain it.

## Tile Maps (development)

Open **Scenes/74-tile-maps.scene.json**. The arrow course and smaller mirrored
copy share **Assets/Tiles/ArrowCourse.tilemap.json**. The course uses negative
grid coordinates, two layers and the original FORGE arrow sheet. The Scene
includes its orthographic camera and needs no light.

In Content, open the map, select **Loop across negative cells**, choose an arrow
and drag to paint. Release for one Undo step; try Undo/Redo and Escape to cancel
a held stroke. Try Erase, wheel zoom and middle-button pan. **Corner markers**
overlays the loop. Choose **Save / Publish** and return to Scene: both instances
show your saved edit. Stop Play before editing source cells.

Open **Assets/Tiles/Directions.tileset.json** to inspect the four stable named
definitions and their shared image material. Cell dimensions use local meters;
the right instance's Scene transform creates the reflected, smaller copy.

Select this Scene as startup before export. Its complete prepared dependency
closure ships with the game; it needs no project C++ module. Optional
**CodeExamples/TileMapEdits.hpp** shows one instance-specific queued C++ cell
write. See [Tile Maps](../../../manual/editor/tile-maps.md) for the full workflow.
This Scene is newer than Build261020-000194; use a delivery that includes it.

## World sprites

The Sprite scenes open with a front orthographic Scene bookmark and an XY
grid. Use **View → Axis views** for another direction, or **Projection** to switch
to Perspective. **Save view** replaces your bookmark without changing the Game
camera or adding Scene Undo.

Open **Scenes/71-world-sprites.scene.json**, press Play, then open Game. The scene
includes its camera. The original circle images show tint, horizontal flipping,
transparent overlap, a top-left pivot and different sizes. These unlit images do
not need a light.

Select an image and use its **Sprite Renderer** Inspector groups. Width and height
use meters; the pivot is measured from the image's top-left corner. Higher Order
values draw later at equal camera depth. During Play, the colored Live gameplay
panel offers temporary appearance changes; Stop restores the authored scene.

## Sprite frame animation

Open **Scenes/72-sprite-frame-animation.scene.json** and press Play. The large
signal changes direction and color every half second, looping after two seconds.
The four smaller signals show frames 0 through 3 of the same original image sheet.
Select a smaller signal to try its Frame or Flip fields during Play.

Open **Assets/Sprites/SignalFrames.animation.json** in Timeline to edit the large
signal's frame keys. **Sprite frame (step)** holds each whole frame until the next
key; it has no curve tangents. Animation continues to control its bound fields,
so use the smaller, unanimated signals for manual live Frame changes.

In development source, stop Play and select one of the smaller signals. Beside its
numeric Frame field, **Choose frame...** shows the four original arrow cells:
green up, blue right, orange down and pink left. Click one, then use Scene Undo to
restore it. Try wheel zoom, right-drag pan and **Fit image**; Cancel changes nothing.
The same control during Play changes only the temporary live value. On Timeline,
choose the large signal as Preview object: selecting an image fills its pending
Sprite frame field, and **Add / Update key at playhead** commits the key. These
visual controls are absent from Build193; automated Windows input checks pass.

These scenes use ordinary materials and built-in components. Choose the desired
scene as startup before export. Their prepared images, materials and animation
ship as game content; no project C++ module or compiler is required. The two PNGs
are original FORGE artwork.

## Sprite image fill (development)

Open **Scenes/73-sprite-image-fill.scene.json** and press Play. The three large
panels on the left use the same original image and material:

- Top: **Simple** stretches the complete image, including its corners.
- Middle: **Sliced** keeps the borders at their chosen width while resizing the center.
- Bottom: **Tiled** repeats the image, showing partial tiles at the right and bottom.

The small panel on the right compresses its borders when the target becomes too
small for a center. The other right-hand panel mirrors the complete repeated fill.
Select a panel and try **Image fill**, Width and Height in Inspector. During Play,
fill settings are temporary; Stop restores the authored scene. Density is measured
in pixels per meter before the entity's world scale. Borders use pixels of the
selected image cell. Opposing sliced borders must leave a positive image center.

Choose this Scene as startup before export and use the matching build's runtime
kit. Its prepared image and material ship with the game; no project C++ module is
needed. These fill modes and Scene73 are absent from Build193. Automated Windows
input/render checks and source-free production exports pass, including relocation
to a fresh runner without source or SDK. Physical testing remains deferred.

## Sequence sound cues

Open **Scenes/70-sequence-audio-cues.scene.json** and press Play, then open Game.
The red, green and blue sources request lower, middle and higher tones at 0, 2 and 4
seconds. A gold panel and two camera shots use the same six-second sequence. These
sources use the Master mixer channel. The sounds are original Gallery audio.

Select **Six-second sequence - three sound points** and open its sequence Timeline.
Under **Audio cues**, select a point, choose its source/time and **Apply cue fields**.
Save the Scene, then use Play to hear edits; stopped Timeline preview is silent.
Audio Sources own gain, pitch and routing. Their Play on start and Loop settings
are off here so the three cues supply the starts. Removing a cue from the sequence
keeps its entity and source. See the packaged manual's Animation Sequences page.

Choose this Scene as startup before export. Its saved cue links, camera shots,
prepared animation and sound ship with the game; no C++ module or compiler is
required. Audio points run on the fixed game clock, without sample-accurate music
scheduling or waveform editing.

## Scene color adjustments

Open **Scenes/69-scene-color-adjustments.scene.json**. Five colored columns and
neutral steps share the existing Material path. Its saved warm preset affects
Scene preview, Play and the exported game.

Choose **Tools > Scene lighting...**, scroll to **Color adjustments**, and toggle
**Enable color adjustments** to compare the original image with the preset. Set
**Saturation** to 0 for grayscale. **Color filter (linear RGB)** multiplies channels
before tone mapping; **Contrast** changes mapped linear light around 18% gray.
Press Enter to commit. Save the Scene to keep changes; Undo restores an edit.
**Reset color adjustments** restores all neutral defaults in one Undo step.

Choose this Scene as startup before export. It includes prepared shared Material
values and the Scene settings, with no source compiler or C++ module required.
Editor overlays and the game HUD keep their own colors. Use the editor's Help menu
to open the packaged manual and see its Lighting page.

## Sequence camera shots

Open **Scenes/68-sequence-camera-shots.scene.json** and press Play. The front view
lasts two seconds, the side view lasts two seconds, and the close view lasts one
and a half seconds. The last half-second uses the normal overview camera. The
moving panel and shots share one six-second sequence.

Select **Sequence - camera shots and moving object**, then choose **Open sequence
timeline** in Content's Create / Register menu. Select a shot under **Camera shots**,
change **Shot start**, **Shot end** or **Shot camera**, and choose **Apply shot fields**.
Undo restores that edit. Use **Refresh clips → Preview sequence**, then open Game
and scrub a strip to view the cuts without gameplay. Stop preview restores the
authored camera settings. Standby cameras intentionally have Enabled turned off;
the active shot selects them in the rendered view.

Try extending the last shot to six seconds to remove the overview gap. Save the
Scene and select it as startup before export. The exported game uses the saved
shots and prepared moving-panel clip, with no compiler or C++ module required.
The optional **SequenceControl.hpp** helper changes the same controller's speed
from a C++ system. See the [sequence guide](../../../manual/editor/animation-sequences.md).

## Animated per-object material values

Open **Scenes/67-animated-material-values.scene.json** and press Play. The gray
cube stays unchanged; the other cubes compare linear, smooth and held changes
between red/polished and blue/matte. All use the same shared material. The smooth
cube is an instance of **Assets/ObjectValues/SharedCube.prefab.json**.

Open **Assets/ObjectValues/LinearAppearance.animation.json** in Timeline and
choose its cube as **Preview object**. Expand **Animated material values** to
select slot/value tracks. Change their key fields, or choose a material value and
R/G/B/A lane in **Animation curves**. Apply, Save & Publish, and restart Play.
Preview leaves Scene history and shared material assets unchanged. Save the Scene
and select it as startup before export. Export includes prepared clips/materials
and the Prefab, without authored clip sources or a compiler. **MaterialAnimation.hpp**
and the installed SDK's **material_animation_gameplay** example use ordinary C++
TransformAnimator controls for the same clips.

## Per-object material values

Open **Scenes/66-object-material-values.scene.json**. Six cubes share
**Assets/ObjectValues/SharedSurface.material.json**: a gray default, red polished
and blue matte cubes, two matching green cubes, and an amber prefab instance.
Select a cube and open Inspector's **Object material values**. Choose **surface**,
expand **Base Color Factor** or **Roughness Factor**, enter a value and press Enter.
Undo or **Revert to material** follows the same Scene history. Publishing a shared
material change still affects objects without an explicit value for that parameter.

Play uses the same values. Inspector's colored **Live gameplay** controls are
temporary. To keep a tuned color or roughness, choose **Keep values after Stop...**,
Stop, then open **Run → Review runtime tuning...**, choose the fields and **Apply
to Scene**. Save before exporting; one Undo restores the previous intent. The amber
cube retains its renderer from **Assets/ObjectValues/SharedCube.prefab.json** with
only an instance material-value property override. Export uses one shared cooked
material and saved Scene/Prefab intent, without this project's material sources.
**CodeExamples/ObjectMaterialValues.hpp** shows ordinary C++ set/revert helpers;
the installed SDK's **material_value_gameplay** sample exercises them. Timeline
material tracks are not part of this example.

## Two-dimensional animation blend spaces

Open **Scenes/65-two-dimensional-blend-spaces.scene.json** and press Play.
Five instances of FORGE's original column compare the Rest, X and Y corners,
a three-way interior and the other triangle. Select a model holder and tune
Animator's **Blend position X/Y**, or use Animation Preview to scrub and adjust
both controls temporarily. Apply saves both coordinates as one Scene Undo step.

Open **Assets/MotionPlane.blendspace.json** to see its four points and triangles.
Select/drag points or edit their numeric X/Y values; release commits one Undo step
and Escape cancels a drag. Save / Publish adopts changes through the ordinary asset
pipeline. Clips share one two-second cycle. Outside the hull, blending clamps to
its nearest boundary. No authored triangle topology is needed.

**CodeExamples/SetBlendPosition.hpp** includes `forge_set_blend_position(actor,x,y)`
for atomic two-axis C++ control. Rebuild modules with the matching SDK after this
update. Export includes cooked blend/model data; authored sources and an editor
are unnecessary to run the exported scene. This scene adds no downloaded assets.

## Runtime Prefab creation from C++

Open **Scenes/64-runtime-prefab-creation.scene.json**. This starts as an empty stage
with floor, camera and light. Its optional C++ example creates two blue robots
with gold caps, then a third after two seconds. Each group rotates independently.
Follow **Runtime Prefab creation** in **CodeExamples/README.md** to compile it in a
writable project copy. Without that optional code, no robots spawn. The Scene's
Runtime Dependencies already include **Assets/RuntimeRobot.prefab.json**; no
authored instance is needed. Export after saving/building to run the same compiled
behavior and saved Prefab in a standalone game.

## Portable prefab rigs

Open **Scenes/63-portable-prefab-rigs.scene.json** and press Play. Both door and
pendulum rigs use **Assets/PortableSequenceRig.prefab.json**. The left sequence
runs at1x and the right at0.5x. Each distance joint targets its own anchor and bob;
the right bob uses lower gravity, so its motion differs too.

Select either instance in Hierarchy and inspect its overrides. **Open prefab
source** shows the source members and reference pickers. Rename a member without
breaking its link, or duplicate an instance to get independent target identities.
Publish source changes deliberately; Scene Undo does not undo direct publication.

Dynamic bodies use World space. Their initial world positions are authored per
instance, so moving a structural rig root does not move its bob. Save the Scene
and choose it as startup before exporting. The resulting game consumes the saved
scene, portable prefab and prepared clip without this editable project.

## Coordinated animation

Open **Scenes/62-coordinated-animation.scene.json** and press Play. The camera
moves immediately; the door starts at0.5seconds; light color, brightness and sound
volume change from1second. Each clip holds its last value, and the arrangement
repeats every6seconds. Select **Sequence - door, camera, light and sound**, then
**Content → Create / Register → Open sequence timeline**. Refresh clips and preview,
scrub tracks, or edit a track start and Apply it. Arrangement Save/Undo use the
Scene; individual clips under **Assets/Sequences** use Save & Publish. Preview is
silent; Game Play uses the animated camera and sound. Export includes the saved
Scene and prepared clips/sound and needs no compiler. See the
[sequence guide](../../../manual/editor/animation-sequences.md).

## Smooth property animation

Open **Scenes/61-animated-property-curves.scene.json** and press Play. The left jet
eases size and color at the ends of each transition; the right changes at a constant
rate between the same values. Both use the same particle effect and emission timing.
Open **Assets/AnimationTiming/SmoothParticleAppearance.animation.json** in Timeline,
select a key and expand **Animation curves**. Choose a property in **Curve value**,
then try Flat, Linear or Smooth tangents or drag a gold handle. Apply and Save &
Publish before restarting Play. Export ships cooked clips/effect without sources
or a compiler. See the [Timeline guide](../../../manual/editor/transform-animation.md).

## Animated particle size and color

Open **Scenes/60-animated-particle-appearance.scene.json** and press Play. The left
jet grows and changes color in a two-second loop; the right stays unchanged. Both
share the same effect, seed and emission timing. Select the left emitter and open
**Assets/AnimationTiming/ParticleAppearance.animation.json** in Timeline. Its
**Animated properties** use the existing size and linear color factors. Preview,
edit keys, Save & Publish, then restart Play. Export ships cooked animation/effect
content and requires no C++ compiler.

## PNG and JPEG game UI

Open **Scenes/59-ui-png-jpeg.scene.json** and press Play. A transparent PNG health
icon blends over the panel; the JPEG landscape is original procedural artwork.
Capture gameplay input to use Pause, Resume and Step. Resize the Game viewport;
the panel stays inside it and scrolls when needed. Images live together under
**Assets/UI/BitmapFormats**. Export ships the required UI files and needs no C++
compiler. Use Reload UI after changing an image; invalid images keep the last
working HUD.

## Simulation speed

Open **Scenes/58-simulation-speed.scene.json** and press Play. A bouncing object,
transform animation and particles share the same simulation speed. Capture game
input and try the HUD's Hold, quarter, half, normal, double and four times buttons.
Hold keeps the buttons responsive. Pause remains separate; Step advances one tick
while paused. Existing sound keeps normal speed. Stop/next Play resets speed to 1x.
The Scene reuses existing clips/effects and exports with its HUD, requiring no C++
module or compiler in the exported game. The editor's **Speed** control offers the
same presets; narrow windows expose them in **More global actions**.

## Shared effect, different emitter appearances

Open **Scenes/57-particle-emitter-appearance.scene.json** and press Play. The front
row compares sizes 0.5, 1 and 2; the back row adds warm, green and blue RGB tints.
All six objects share **Assets/Particles/SharedJet.particles.json**. Select an
emitter and try **Size multiplier** and **Tint R / G / B** in Inspector. During
Play those controls appear in the colored **Live gameplay** section; Stop restores
saved values. Stop before editing values you want to keep, then save. Appearance
edits keep the jet running. Export ships the cooked effect and emitter values.

## Particle shapes and lifetime curves

Open **Scenes/56-particle-shapes-and-lifetime.scene.json** and press Play. The front
row compares Point, Box, Sphere and Disc origins. The back pair shows a free jet
beside one with drag and a curve that grows then shrinks. Open effect sources
under **Assets/Particles/Shapes** to change dimensions/radius, drag, size stops or
linear-RGB gradient stops; preview and publish, then restart Play. This scene
exports already cooked effects and needs no C++ compiler.

## Layered terrain and collision

Open **Scenes/55-layered-heightfield-terrain.scene.json** and press Play. Three
spheres fall onto two adjoining tiles, including their shared seam. Stop Play,
open **Assets/Terrain/LayeredHills.terrain.json** in Content and try its height
layers, strength, mask or brushes. **Save / Publish** updates the visible Mesh;
rebuild **LayeredHills.collision.json** afterward to update the static collider.
Save the scene and export normally to run the same cooked surfaces and physics.
No C++ compiler is needed. See the [terrain guide](../../../manual/editor/terrain.md).

## Mesh particle fountains and burst

Open **Scenes/54-cpu-mesh-particles.scene.json** and press Play. Orange and blue
fountains emit continuously; the middle effect bursts once and expires. Stop and
restart Play to repeat the burst. Open **Assets/Particles/OrangeFountain.particles.json**
in Content, change cone angle, acceleration or size, preview and **Save / Publish**.
Select an emitter to change its seed, speed, visibility or layers. Save authored
values, export normally and expect the same effects in the standalone game.
No C++ compiler is needed for this scene. See the [particle guide](../../../manual/editor/particles.md).

## Navigation avoidance

Open **52-navigation-avoidance** and press Play. The passing cube and sphere steer around each other; the other moving cube goes around the parked sphere. Select an agent to see navigation progress and tune **Navigation Avoidance → Enabled / Horizon** in Live gameplay. Stop restores saved settings. The scene includes a baked NavMesh, camera and light and exports without C++ project setup. This is predictive local steering; dense or blocked configurations can still jam.

## Navigation on a static Mesh

Open **51-static-mesh-navigation**. Press Play and select **Agent - inspect progress during Play** to see the route around the obstacle and its progress in Live gameplay. The floor uses the original editable **NavigationFloor.mesh.json** asset, rather than a built-in Plane. Its cooked Mesh and built NavMesh are saved with the Gallery.

Stop Play before editing the floor. Rebuild the Mesh through Content, then rebuild its NavMesh through the Navigation controls. A changed selected Mesh makes the previous route stale. Static imported Model Mesh children use the same workflow; skinned/morphing geometry cannot be baked. For moving footprints use129/130 and the Dynamic obstacles profile; for local agent steering use52-navigation-avoidance. Export carries selected cooked data; it does not need the editable source or a runtime navigation build worker.

## Navigation progress

Open **50-navigation-progress**. Select **Agent - inspect progress during Play**
and press Play. The cube follows the floor around a static obstacle and
stops at the destination. Inspector's **Live gameplay → Navigation progress** shows
state, remaining route distance, measured speed and navigation tick. Pause freezes
these values; Step updates once; Stop restores authored state. The states and
metrics are read-only. Agents do not avoid each other or moving obstacles.

The saved scene includes its built NavMesh and exports through the normal workflow
without a C++ module/compiler or runtime navigation build worker. Moving marked
floor/obstacle geometry requires stopping Play and rebuilding navigation.



## Bright-surface glow

Open **48-hdr-bloom** and press Play. Two original warm/cool emissive cubes glow
beside an ordinary reference cube. In **Tools > Scene lighting... > Bloom**, toggle
**Enable bloom** and adjust **Brightness threshold**, **Intensity**, or **Spread**.
Press Enter for numeric changes. Scene Save and Undo retain the normal history.

Export the saved scene normally. The two Material sources and selected Windows/
D3D12 cooks are included; the exported game uses cooked values without source files
or a C++ compiler. Local native Vulkan pixel and CPU/UI checks are separate from
pending Windows/GPU and physical review.

This is an editable FORGE project. Each saved scene demonstrates one focused
engine feature using normal authored entities. The first four scenes use built-in geometry. The remaining scenes include published
editable Mesh sources, cooked Meshes and a checker Material. The eighth scene includes a published transform-animation clip; the ninth has an original two-clip skinned model. No
gameplay C++ module, SDK or compiler is needed to explore these scenes.

For compiled C++ physics forces, impulses and torque, open the separate
[Physics Loads project](../PhysicsLoads/README.md). Its six-cube comparison uses
the same editor and standalone export workflow with the matching native module.

## Swing-and-twist comparison
Scene **28-swing-twist-joints** compares a blue wide-swing pendulum with a yellow
narrow one under gravity. Inspect their joint entities to adjust cone half-angles,
twist limits and body-local axes. Both start inside their limits; no C++ module is
needed. The same saved scene can be exported and run independently.

## Open and inspect

Scene **36-animation-root-motion** compares the same original skinned column
with Root motion **Off**, **Apply**, and **Extract**. Apply moves its object around
a square; Extract keeps the root pose neutral and makes motion available to C++.
Inspect each holder's Animator settings, then save and export normally. The
scene needs no C++ compiler. `CodeExamples/RootMotionKinematic.hpp` separately
shows how a compiled controller can pass extracted motion to a Kinematic Body.
The editor package includes the guide at `manual/editor/animation-root-motion.md`.

1. Copy the entire `Examples/FeatureGallery` folder to a writable place outside
   the FORGE installation. Keep the complete folder, including `Assets`,
   `forge.assets.json` and the hidden `.forge` cooked cache.
2. In FORGE, choose **File → Open project...** and select the copied folder.
3. Open a scene by double-clicking it in **Content** (filter Type to **scene**),
   or use **File → Open scene...**. Select named objects in the Hierarchy and
   inspect their components. Use **Frame selected** if a view needs recentering.
4. Press **Play** to see the authored game camera and runtime behavior. Use
   **Stop** to return to editing. Save any changes you want to keep.

| Scene | What to try | Expected result |
| --- | --- | --- |
| `01-transforms` | Move or rotate **Move or rotate this parent**. Inspect its three children. | Children follow the parent without losing their independent local positions. In Play, all three remain visible from the game camera. |
| `02-lighting` | Select **Sun**, **Warm point light**, and **Cool spot light**. Change intensity or color and press Play. | The same pedestals respond differently to the directional, point, and spot lights. The sun also casts shadows. |
| `03-physics` | Press Play and watch **Falling cube A/B/C**. Select a cube to inspect its Physics Body and Box Collider. | The three dynamic cubes fall onto the static floor and obstacles. Stop restores the saved authored positions. |
| `04-cameras` | Select the two cameras and compare their projection and viewport fields. Press Play. | Perspective fills the left half; orthographic fills the right half. Both show the same four shapes. |
| `05-editable-mesh` | Select **Editable extruded cube**. Find **Shape.mesh.json** in Content and open it to change its form or face-corner UVs. Save & Publish, then return to the scene. | The center cube has an extruded face; its edited geometry appears in Scene, Play and standalone export. The assigned checker Material makes the authored UV placement visible. |
| `06-sculpt` | Open **SculptSphere.mesh.json** in Content. Choose **Sculpt** in Model and drag over the sphere preview, then Save & Publish. | A smooth, low-poly sphere starts with a raised area. Sculpt changes its geometry and is saved as one Undo step per stroke. |
| `07-vertex-paint` | Open **PaintedSphere.mesh.json** in Content. Choose **Paint** in Model, change the color and drag over the sphere preview, then Save & Publish. | The sphere carries blue and warm painted vertex colors into Scene, Play and export. Paint applies to its vertices, so detail is limited by the Mesh resolution. |
| `08-transform-animation` | Press Play to watch **Animated platform**. Open **Platform.animation.json** in Content, choose that object in Preview object, and scrub or change its keys. Save & Publish. | The blue platform moves between the two markers, turns and changes height over four seconds. Timeline preview restores the authored view when stopped. Play and export use the published clip. |

| `09-animation-blending` | Press Play and compare the three skinned columns. Select **Half Idle / half Reach** and open **Preview skeletal animation...** from Content. Pause, scrub and adjust the blend target/duration. | Idle, an equal blend, and Reach share normalized phase. The center column bends between the two poses. Preview is temporary; Apply settings stores one Scene Undo step. Both clips play in standalone export. |
| `10-animation-states` | Press Play to compare Idle, Walk and Run characters. Select a character root and use Animation Preview's named state selector. Open `Assets/Locomotion.states.json` to change names, loop, speed and incoming fade. | Real independently skinned Quaternius clips. C++ selects stable state IDs; ordinary export includes the entire set. CC0 license and immutable provenance are beside the GLB. |

| `11-animation-markers` | Open `Assets/MarkerPulse.states.json` and inspect/edit Pulse timing on Walk/Run. For an actual action, build the optional [C++ example](CodeExamples/README.md). | Markers are published/exported with the clips. The compiled example alternates character height at the authored timing points. Preview is muted. These are illustrative timing points, not calibrated footsteps. |

| `12-inset-panels` | Open **Frame.mesh.json**, **RaisedPanel.mesh.json** or **RecessedPanel.mesh.json** in Content. Select a face, preview Inset, then use a positive or negative Extrude distance. | Three original checker-textured panels show an inset border, a raised cap and a recessed cap. Save & Publish updates Scene, Play and source-free export. |
| `13-transform-curves` | Press Play to compare the three platforms. Open **EasedPlatform.animation.json** or **ArcPlatform.animation.json**, select a key and expand **Vector curves**. Apply slopes, preview, then Save & Publish. | The existing linear clip, smooth eased clip and custom vertical arc share a four-second cycle on separate parent lanes. Play and export load both legacy and new cooked clips. |
| `18-animation-key-batches` | Press Play to compare original eased travel, a one-second delay and duplicated travel. Open a **Batch*.animation.json** source, Ctrl-click keys, then drag or use **Key offset** and Move/Duplicate/Delete. Save & Publish. | Three lanes use original clips in a six-second loop. Every batch is one source Undo; exported games load the published motion. |
| `45-animation-timing` | Press Play to compare the lanes. Open a clip under **Assets/AnimationTiming**, change **Retimed duration** and apply **Scale whole clip timing**, or select keys and use **Frame grid (FPS)** / **Snap selected keys to frames**. Save & Publish. | Original motion loops in four seconds; the middle lane stretches it to eight seconds; the last snaps keys to a two-FPS grid. Play and export use the published clips. |
| `14-uv-islands` | Open **UvConnected.mesh.json**, **UvSplit.mesh.json** or **UvFitted.mesh.json** from Content, choose UV, select an island and drag, mirror or fit it. Save & Publish. | Three original checker-textured panels compare matching shared UV edges, a separate patch and a mirrored/fitted mapping. Play and standalone use the same published UV streams. |
| `15-brush-symmetry` | Open **SculptOneSide.mesh.json**, **SculptMirrored.mesh.json** or **PaintMirrored.mesh.json** in Content. Choose Sculpt or Paint and enable Mirror X/Y/Z before dragging. Save & Publish. | Three original spheres compare one-sided displacement, X-mirrored influence and XYZ-mirrored vertex color. Play and export use the same published Mesh streams. |
| `16-authored-triangulation` | Open **PolygonCube.mesh.json**, **TriangleCube.mesh.json** or **TriangleSculpt.mesh.json** in Content. Compare polygon and triangle edges; use Triangulate face/Mesh, then sculpt or move vertices. Save & Publish. | Polygon and triangle cubes render equivalent shapes; the third has a sculpted, colored corner. Published geometry/UVs/colors survive Play and export. |

For Model face groups, open the editable Meshes in scenes 12, 16 or 19. Ctrl-click
faces in the Model list or wireframe, use all/clear/invert/connected selection, then
transform, triangulate or reverse the selected surface. Inset and extrusion use
the active face. For cleanup, delete selected faces and explicitly remove unused
vertices; on scene 16, select a shared triangle diagonal in Edge mode and join it
back into a quad. Duplicate face groups with a local offset, then snap their
vertices onto a local grid using the XYZ controls. See the [face-group walkthrough](../../../manual/editor/editable-meshes.md#work-with-several-model-faces).

`27-connected-collisions` compares two original pairs of overlapping falling cubes.
The blue pair's joint disables **Collide connected**, so their overlap is retained;
the yellow pair keeps normal mutual contacts and separates. Both land on the floor.
Select either joint to edit the setting, save, then restart Play or export. This
demonstrates pair filtering without disabling the objects' ordinary scene layers.

## Export and run a scene

1. Open the scene you want to export and save it.
2. In **Tools → Project Settings**, choose **Use saved current scene as startup**,
   then **Save Settings**. The gallery exports the selected startup scene; it
   does not switch scenes from an in-game menu.
3. Choose **Run → Export Game...**. The Runtime kit is the `runtime-kit` folder
   beside the packaged editor. Choose an output folder outside this project and
   run **Export / Rebuild**.
4. Open `forge_game.exe` in the exported folder. The selected scene uses its
   authored game camera. To export another scene, repeat from step 1. Close the
   first game before replacing its export folder.

Export uses the normal project/settings/content pipeline. Only the startup scene
is needed for these independent demonstrations. No source project is needed to
run the completed standalone folder. See the offline **User Manual → Export a
game or package content** for details and error recovery.

These scenes are intentionally small. For a combined gameplay example with input,
menus, save/reload, audio, animation and navigation, see **FIELD TEST** in the
included development tools. A future gallery scene should demonstrate a feature only
when its authored content and standalone result can both be inspected directly.


## Drive the two-clip mix from C++

The Gallery needs no native module to run. In your own C++ System, copy the
selected object's `forge::Animator`, set `blend_weight` to `0.f` or `1.f`, set
`transition_seconds` (for example `.5f`), then call `actor.set(animator)`. FORGE
advances the transition from its current mix. See the packaged manual's
**Preview and blend two animations** page for a complete function.

`BlendColumn.gltf` is an original FORGE fixture with a two-joint skinned column
and Idle/Reach clips, built for this Gallery. It uses no external character art.


## Drive named states from C++

Scene 10's set uses Idle `1`, Walk `2`, Run `3`; `0` selects its entry.
Copy the character root's `forge::Animator`, set `state` and call `actor.set(animator)`
from your C++ System. Repeating the same ID keeps time; a different ID starts its
clip at zero and uses its incoming fade. The Gallery needs no native module to
run. See **Choose named animation states** in the manual for a complete function.
The Quaternius clips are CC0; `Assets/Quaternius-PROVENANCE.md` identifies the
original download and the three-clip derivative. `Quaternius-LICENSE.txt` is the
creator's bundled license.

During Play, select an animated root to see live clip progress in the Inspector.
CodeExamples includes an optional original one-shot return-to-Idle helper; see its
README before building/exporting compiled behavior.

- **17-mesh-shading**: two identical original spheres show flat facets versus
  smooth lighting; open their Mesh sources to change shading and publish.

- **18-animation-key-batches**: **BatchOriginal.animation.json** has two eased keys
  at0 and2seconds; **BatchShifted.animation.json** moves both to1 and3seconds;
  **BatchRepeated.animation.json** copies them to3 and5seconds. Organizational lane
  parents explicitly use **None** as their primitive. The six-second loop holds the
  nearest pose outside the key range. Play and source-free export use identical cooks.

- **19-mesh-facing**: the same original open-front room appears with outward and
  inward surface directions. Open **OutwardRoom.mesh.json** or **InwardRoom.mesh.json**,
  select a face in Model, then choose **Reverse connected surface** or **Reverse Mesh
  facing**. Undo, Save & Publish and export use the normal Mesh workflow. Geometry,
  corner UVs, colors and identities remain attached; facing controls the visible side.

### 20 — Physics joints

Open `Scenes/20-physics-joints.scene.json` and press Play. The blue bob swings at a
two-meter distance from its support; the amber pendulum swings around its pivot
with 45 degree limits. Select either joint entity in Edit to assign bodies and
change settings. Anchors use each body's local origin and follow its scale. The
support collision masks are zero to avoid contacts at attached pivots. Joints do
not render ropes or hinge meshes. Save/select this startup scene and export it to
see the same physical motion without the editor or a C++ compiler.

## 21 — Point and fixed joints

A blue pendulum swings around a free point joint in three dimensions. Two amber
bodies fall together as a fixed assembly and settle on the floor. The joint
entities select body references and scaled local anchors; fixed reference frames
use the normal axis as frame X and axis cross normal as frame Y. Open the scene,
Play, Stop and export through the same compiler-free static runtime route.

### 22 — continuous collision

Two small cubes fall from24m onto thin platforms under ordinary project gravity.
The blue cube uses continuous collision; the amber cube is the discrete comparison.
The platform height deliberately exercises a missed discrete step in the fixed60Hz
example. A lower floor catches the amber cube. The camera frames their landing area;
the cubes enter from above after Play. Select the blue cube's Physics Body to change
Continuous Collision, then stop/restart Play. This is a numerical example, not a
promise of tunneling in every discrete scene or perfect angular collision detection.

## Body motion tuning

Open `23-body-motion-tuning` and press Play. From left to right, blue falls freely,
amber slows with damping3/s, green falls at at most2m/s, and violet has a zero
linear speed limit and stays suspended. The two cubes already on the floor compare
sleeping permitted and disabled; both look still, but the latter keeps consuming
physics work. Select a cube's Physics Body to change damping, speed limits and
sleep permission. Stop restores the authored positions. Angular speed uses
**degrees per second**; linear speed uses **metres per second**. Drag is distinct
from contact friction. Large damping is sensitive to the fixed timestep.

These are authored settings, not a live C++ control panel. Ordinary saved startup
scene selection and export include all settings; no compiler is needed.


## Slider joints

Open `24-slider-joints` and press Play. The left carriage drops along its vertical
guide; the right carriage moves down its inclined guide. Both stop at the lower
travel limit and retain their rotation. Select either slider joint while stopped
to see body anchors, frame axes and the signed travel interval in the Scene view.
Edit `min_travel` and `max_travel` in metres; the interval must include zero.
Axis/normal choices use each body's rigid frame. Travel limits remain metres even
when bodies are scaled. Use Fixed for zero travel. Collisions between each small
base and carriage are disabled with existing body masks. Motors are not included.

## 25 — Velocity motors

Open `Scenes/25-velocity-motors.scene.json` and Play. With gravity disabled, a bounded
Slider drive moves its carriage to the positive1.5m stop, and a Hinge drive turns its
carriage to90degrees. Stop to change motor direction, speed, torque/force budget or
limits in the Inspector, then test/export again. Zero speed brakes with the given
budget; zero budget coasts. No gameplay source/compiler is required. Scene guides
show authored drive settings rather than measured runtime speed.

## 26 — Position drives

Open `Scenes/26-position-drives.scene.json` and Play. Its slider returns to-.75m
and hinge to-45degrees with bounded restoring force/torque and critically damped
2Hz springs. Stop to edit Position mode, angle/displacement target, frequency,
damping or budget in the Inspector. Targets must fit the authored joint limits.
Lower damping ratios can overshoot; the fixed-step solver also adds integration
damping. Selected Scene guides show a pink target marker and authored settings.
Ordinary source-free export needs no project C++ module or compiler.

### 29 — distance springs

Open `Scenes/29-distance-springs.scene.json`. The blue link oscillates with damping
0.15; the yellow link returns with critical damping 1. Both have a 1.3 m rest length,
1 Hz frequency and no gravity. Sleeping is disabled for the two demo bodies so the
full numerical response remains measurable. Stop/restart Play to repeat. Select a
Distance joint to edit frequency/damping and view its hard/elastic range guide.
Export uses the same native physics after source files and kits are removed.

### 30 — timed animation exits

Open `Scenes/30-animation-exits.scene.json` and press Play. Three real skinned
characters cycle Idle → Walk → Run, each beginning in a different state.
Open `Assets/TimedLocomotion.states.json` from Content to adjust automatic targets,
exit times and destination fades. Save / Publish and restart Play. The existing
CC0 Quaternius clips and license are reused. Source-free export runs the same cycle.

## 31 — Spatial audio falloff

Open `Scenes/31-audio-falloff.scene.json`. Blue uses inverse, yellow linear, red
exponential; each has strength1 and range2–18m. The camera contains the listener.
Only blue autoplays. Stop and switch **Play On Start** to solo another source;
move the camera and compare **Distance falloff** / **Falloff strength** in Inspector.
Strength0 preserves positional sound with no distance loss. Maximum clamps the
calculation; linear strength1 reaches silence there, while inverse/exponential
remain audible. Equal distance limits disable distance loss. Save before export.

`Assets/FalloffTone.wav` is an original one-second mono48kHz PCM16 440Hz sine tone
generated for FORGE, with amplitude10000. No downloaded sound or third-party
animation/audio license applies. The source and its immutable cooked revision
ship with this editable gallery; source-free games include only cooked PCM.
The same project/sample license applies as the other original gallery examples.
Physical listening remains part of user acceptance.

## 32 — Directional audio

`Scenes/32-directional-audio.scene.json` reuses the original FalloffTone clip.
Select **Directional speaker** to see the forward arrow and angular outlines.
The effective local negative Z points toward the camera/listener. Full openings
are60/120degrees, outside volume.25; distance strength0 isolates angular volume.
Play, Stop, rotate **Aim parent** around Y180degrees, then Play again to compare.
Behind the emitter, amplitude is one quarter (power one sixteenth) of the front.
Change **Directional sound**, the openings or **Outside volume** in Inspector.
Angular gain fades in cosine between half-opening boundaries. Inner360 disables
angular loss; equal openings create a hard boundary. Guides are finite spherical
angular outlines at a displayed radius, with no effect on game geometry or range.
Save authored values before export. The compiled game uses cooked PCM and current
typed settings; no source WAV or editor is needed for playback.

## 33 — Audio mixer

`Scenes/33-audio-mixer.scene.json` reuses the original tone at low/middle/high
pitches through Music, Effects and Dialogue. Stop Play, edit a category volume
under Project Settings → Game defaults, save settings and Play again. Master
changes all tones. Source **Mix channel** can be changed in the Inspector while
playing. Export includes original routes and cooked PCM; the source WAV is not
needed to run the exported scene.

While Play is active, open Project Settings → Live audio mix. Adjust Master,
Music, Effects or Dialogue and Apply live mix to balance these tones without
restarting them. Reload live values discards a draft. Voice/cache/output status
comes from the current game. These temporary values do not change saved defaults;
Stop and Play again restores the starting mix.

## 34 — Audio import processing

`Scenes/34-audio-import-processing.scene.json` compares two copies of an original
synthetic tone. **ProcessingOriginal.wav** keeps its two channels and1.4second
duration, including0.2seconds of quiet at each end. **ProcessingMono.wav** uses
Force mono, Normalize peak and Trim silence; its published clip is mono and1second
long, with short fades at the trimmed edges. Both source WAV files remain identical.

Double-click either WAV in Content. Use Show waveform or Play preview to compare
channels, duration and volume; preview volume is temporary. Expand Import settings
to change processing and Import/Reimport. Reset to defaults and reimport restores
the original stereo sound. Gameplay uses the selected cooked revision.

Only the processed cube starts automatically. To compare in Play, stop, turn its
Play On Start off and turn that setting on for **Original stereo**, then Play again.
Both sounds are nonspatial and loop quietly through Master. Export the saved scene
to compare the same processed PCM in a standalone game without the original WAVs.

## 35 — Moving audio

`Scenes/35-moving-audio.scene.json` moves two child emitters with the original
`DopplerPass.animation.json` translation clip. The camera is the fixed listener.
**Doppler emitter - motion pitch** starts with strength1; **Reference emitter - unchanged pitch** has strength0 and
does not start automatically. Both reuse the original cooked FalloffTone.

During Play, select a source and use Inspector → Live sound → Stop sound or
Play / Restart to compare one at a time. Approaching motion raises pitch and
receding motion lowers it. The animation's one-second loop jump resets inherited
motion. Controls do not change saved autoplay, animation or scene values.

**Play one-shot** plays the selected clip once alongside the main sound, without
restarting it. Click again to overlap effects; the panel shows the active count.
Stop sound clears all effects for that source. These temporary Inspector triggers
are not saved into the scene. The matching C++ SDK's `audio_one_shot` can trigger
the same behavior in exported gameplay; the Native SDK audio_probe sample covers
that command path.

Pause sound holds that source's playback position and becomes Resume sound. The
animation keeps moving while only the source is paused. Game Pause holds the whole
game; game Resume keeps an explicitly paused source silent until you resume it.

Save before exporting. The standalone uses the selected animation and PCM with
development sources removed. Physical listening is separate from offline PCM
and source-free export validation.

## Inspect animation playback

Open scene36, start Play and select an animated entity. The tinted **Live animation**
section offers clip Pause/Resume, Restart and a release-to-seek playhead. Pause the
game to inspect a pose without advancing game time. A seek skips movement across
the jump; root motion continues on the actual next interval. Stop restores saved
scene values. `CodeExamples/ClipSeek.hpp` provides the ordinary compiled C++ path
for the same behavior in an exported game. The scene runs without project code.

### 37. Skeletal subtree blending

`Scenes/37-animation-subtree-blending.scene.json` compares base Idle, Reach on the
Upper joint subtree and full-skeleton Reach. The original MaskColumn extends the
FORGE-owned column with a tip and separate side joint, plus root movement/turning.
Select **Upper subtree - Reach** while stopped and change **Animator → Blend
subtree**; None selects the whole rig. Preview, Play and source-free export share
the selection. Local tests check actual skin geometry and subtree membership.

## Additive and morph animation examples

**38-animation-additive-overlays** compares the original base motion, full Blend
replacement and Additive overlay. The overlay keeps base motion and adds offsets
from the imported rest pose. Stop, select the right actor and change Animator's
**Blend mode**, **Blend Weight** or **Blend subtree**. Preview and export use the
same bone and signed morph curves.

**39-animation-morph-states** starts three columns in Base, Overlay and Rest and
automatically cycles them with smooth fades. Rest has no morph curve and returns
to the model's default shape. Open **MorphMix.states.json** in Content to edit the
states and exits. Both scenes use FORGE's original **OverlayColumn.gltf**, with
editable source and cooked animation/mesh data included. No optional C++ code is
needed to run them.

## Bone masks

**42-animation-bone-masks** compares a whole-skeleton blend, an upper subtree and
a reusable fractional mask. Open **Assets / Weighted.mask.json** to edit per-bone
influence and descendant scope. Save / Publish, Play and export use the same mask.
The root has influence.25, Upper.5, Tip.75 and the animated mesh.25; Side keeps the
base motion. **CodeExamples / SetBoneMask.hpp** shows ordinary typed C++ control.

## UV island packing

**44-uv-island-packing** compares three original six-patch checker panels: overlapping
UV regions, padded rectangle packing and packing with quarter-turn rotation for
tall islands. Open **Assets / UvPackingOriginal.mesh.json**, **UvPackingPacked.mesh.json**
or **UvPackingRotated.mesh.json** in Content, then choose UV. Preview selected/all
islands, adjust padding/rotation, Apply and Save & Publish. Source Undo restores
the mapping; ordinary Play/export use the published UVs. See the
[walkthrough](../../../manual/editor/editable-meshes.md#pack-several-islands-together).

The same UV workspace also supports corner/face/island box selection, all/clear/
invert controls and U/V alignment and coordinate spacing. Selection stays temporary;
applied edits use source Undo and Save & Publish. Corner arrangement can change
texture distortion or seams; packing keeps each island's proportions.

## Numeric animation blending

**43-animation-blend-spaces** compares Rest-to-Base at.5, Base-to-Overlay at1.5
and the Overlay endpoint at2. The clips share a two-second cycle despite different
source durations. Open **Assets / Motion.blendspace.json** for sample positions,
clip selection and cycle duration. Select a holder to change Animator's **Blend
position**, preview, Play or export. **CodeExamples / SetBlendPosition.hpp** shows
ordinary C++ edits without restarting the cycle.

## Reach and aim

**40-two-bone-ik** and **41-joint-aim** show three copies of the original column
with weights0, .5 and1. Select the model holder and adjust its Two Bone IK or Aim IK
component while stopped, then use Animator preview or Play. Targets are measured
in the model's own space. The same saved scenes export with cooked model data and
compiled engine constraints; no optional C++ code is needed. **SetRigTarget.hpp**
shows how your own C++ can move the target. See the [walkthrough](../../../manual/editor/animation-rigging.md).

## Model topology refinement

Open **46-model-refinement** to compare three cubes with the same shape:
**OriginalCube**, **CenterSplitCube**, and **SubdividedCube**. In Content, open their
Mesh sources under **Assets/ModelRefinement**, use the **Model** tab and inspect the
wireframe: the original has six quads, the center split has24 triangles, and the
midpoint-refined cube has48 triangles with shared vertices.

Select faces and try **Split selected faces at centers**, or choose **Subdivide Mesh**
to refine every triangle. Undo restores the previous geometry. **Save & Publish**
updates the placed cube; save the scene and export it through the normal workflow.
These operations add geometry while retaining the silhouette. The original sources
and selected Windows-target cooks are included; native Windows/GPU acceptance is
pending. No C++ module or compiler is needed.

## Animation key values

Open **47-animation-key-values** and press Play. **OriginalValues** uses the original
four-second loop, **PositionValues** reflects and offsets its motion, and
**ScaleValues** changes its X/Y/Z size. Open the clips under **Assets/AnimationValues**
in Content. Select keys and adjust **Position multiplier**, **Position offset**, or
**Scale multiplier**, then choose **Apply selected key values**. Undo restores the
previous clip; Reset returns only the temporary adjustment fields to their defaults.

Curve slopes follow the value multipliers. Timing and rotation remain. Save & Publish
updates Play and the normal standalone export. The supplied selected cooks target
Windows/D3D12; native Windows/GPU and physical acceptance remain pending. No C++
module or compiler is needed.

## Animated camera and light

Open **Scenes/49-animated-camera-and-light.scene.json** and press Play. The camera
zooms between 40 and 80 degrees while the sunlight pulses and shifts between cool
and warm colors on ordinary cubes. Open **Assets/AnimatedProperties/CameraZoom.animation.json**
or **LightPulse.animation.json** in the Timeline to inspect the property-only keys.
Select the matching camera/light as Preview object. For light preview, disable
Scene's Preview light; camera image zoom is visible in Game Play. Save & Publish
applies edits to Play/export. Game export includes cooked clips and needs no source
editor/compiler. See the [Timeline guide](../../../manual/editor/transform-animation.md).

## Animated sound and light

Open **Scenes/53-animated-audio.scene.json** and press Play. A FORGE-owned tone grows
louder and higher pitched as the light brightens, then returns over six seconds.
Open **Assets/AnimatedProperties/AudioPulse.animation.json** in Timeline and choose
the named audio/light object as Preview object. Volume, pitch and light intensity
share three editable keys. Preview displays values; sound starts in Play. Save &
Publish applies edits to Play/export. Export includes the cooked clip and tone;
no source editor or compiler is needed. Channel/master volume still applies.

In80-xy-navigation, select a Sprite agent during Play and adjust Live gameplay /
Navigation Agent / Speed or destination. Pause before editing several coordinates,
then Step/Resume and inspect progress. The NavMesh stays authored. Stop restores
source settings; Keep values after Stop stages chosen scalar values for review,
ordinary Scene Undo and Save. Runtime movement itself is not kept.

Navigation progress shows concrete failed details. Expand World coordinates for
sampled position/projected goal; a newly retargeted Pending agent does not keep an
old failure reason. Find named ancestor conflicts through Hierarchy name/ID search.

### 88 — Touch input

`Scenes/88-touch-input.scene.json` authors a fixed16x10 camera canvas and original image marker. Project input includes digital Touch0 down and independent Axis1 X/Y/pressure actions. Capture Game input and inspect Console Gameplay input values. Optional `CodeExamples/TouchCursor.cpp` with `TouchInput.hpp` reads the saved ActionIds through the exact SDK, moves only the scoped scene/entity and sizes its Sprite with pressure. Register `forge_register_system_TouchCursor`; installed sample `touch_input_gameplay` supplies its complete wrapper. Stop restores source. The Scene is usable without C++ for input monitoring; marker behavior requires the built module. See the manual Touch input guide for slot/capture/UI/export limits.

### 89 — Two-finger gestures

`Scenes/89-touch-gestures.scene.json` is an original16x10 canvas/camera with five project gesture actions. Optional `CodeExamples/TouchGestures.cpp/.hpp` reads fixed relative pan/log-pinch/screen-clockwise rotation through the exact SDK and changes ordinary transforms on its scoped marker. Register `forge_register_system_TouchGestures`; the installed `touch_gestures_gameplay` target wraps it as `project.touch_gestures`. Apply each delta once, without dt or an Active-held gate: a quick gesture retains its deltas after ending. The example bounds scale0.25..4 and pan X±32/Y±20. Sources are unchanged; Stop/restart restore authored transforms. Physical input requires desktop driver support and Game capture in the editor; UI-owned gestures remain with UI. No inertia/pen/sensor/mobile-host claim. Single-finger classifications use the separate Gallery91.

### 91 — Tap, hold and swipe

`Scenes/91-single-touch-gestures.scene.json` supplies an original canvas/camera and
seven saved digital actions. Register `forge_register_system_SingleTouch` from
`CodeExamples/SingleTouch.cpp` with `TouchGestures.hpp`, or build the installed
`touch_gestures_gameplay` module that includes both89 and91 systems. Tap turns the
Sprite90degrees, Double tap resets transforms, Long press toggles scale1/.5 and
swipes move one unit within X±6/Y±3. Each recognition is a pressed/released pulse;
Double tap also emits Tap, so the example gives reset precedence. Console can
monitor actions without code. Capture Game input for native touch; use one finger
and lift before another gesture. Save Settings/Scene, select91 as startup, build
the optional system and export normally. Stop/restart restores authored values.
Thresholds are fixed:250ms/8px Tap,350ms/24px consecutive-release Double tap,500ms/8px
Long press and750ms/64px Swipe in window coordinates. Physical Windows touch, full
Main/game and graphical exported-EXE acceptance remain separately pending.

### 92 — Four local gamepads

`Scenes/92-multiple-gamepads.scene.json` shows four colored canvas markers.
Project actions **Player 1 move** through **Player 4 move** use separate
`pad.1.left_x/left_y` through `pad.4.left_x/left_y` with matching 0.2 radial
deadzones. Game's **Gamepads...** assigns connected devices to these transient
slots. Capture input to monitor actions; optional original
`CodeExamples/MultipleGamepads.cpp` moves only each scoped marker at three world
units/second, within the visible canvas. Register
`forge_register_system_MultipleGamepads`, or build the installed
`multiple_gamepads_gameplay` wrapper (`project.multiple_gamepads`). The module
needs `forge.input` and `forge.transforms`; no custom authored component. Without
the optional module, the markers remain authored. Save Scene/Settings, select 92
as startup, build current source and export. Standalone creates its own
assignments; connected device tokens are never saved. This scene uses four of the
sixteen logical slots. Up to16 tracked connections and legacy active `pad.*`
bindings are supported. Separate per-player UI focus is unavailable. Controller motion uses Gallery93. Physical Windows/controller verification is
not implied by virtual-device tests.

### 93 — Raw controller motion

`Scenes/93-gamepad-motion.scene.json` shows two markers against a canvas camera.
Original `CodeExamples/GamepadMotion.cpp` reads copied Game service sensor values,
not normalized stick actions. Its unscaled `forge_control_GamepadMotion` explicitly
opts into enabling Player1's supported accelerometer/gyroscope; its fixed
`forge_register_system_GamepadMotion` displays X/Y at0.2 units per m/s² and1 unit
per rad/s, bounded to the canvas. Missing measurements reset markers at the next
fixed update; a paused view needs Step. There is no orientation/fusion estimate.
Build the installed `gamepad_motion_gameplay` wrapper (`project.gamepad_motion`)
or register both callbacks in your module. Dependencies are `forge.input`,
`forge.transforms` and `forge.game`; Game is optional in ordinary simulation,
where the markers remain neutral. No custom authored component is needed.

In Game, **Gamepads... → Motion sensors** shows native modes/rates; capture input
and monitor **Console → Controller motion** without surrendering Game focus.
Modes/tokens are host-session state, never saved hardware identities. Save
Scene/Settings, select93 as startup, build and export normally. The standalone
sample enables its own Player1 sensors. Raw acceleration includes gravity;
physical device support and Windows/MSVC/full Main/game acceptance are separate
from software/virtual-controller tests. Raw touch-device sensors, mobile hosting,
calibration and fusion remain outside this profile.

## C++ component field animation

Open the separate **ComponentAnimation** example project to animate the Speed of
an ordinary C++ spinning cube. Its editable Native component/system and reusable
clip demonstrate the Timeline's **Component fields**, numeric cubic slopes, held
Enabled/Direction keys and matching-module export. A source checkout needs Build Project Code before Play;
the combined Windows package supplies the compiled example. Feature Gallery's
ordinary scenes remain usable without a project C++ module.

## Two-clip sequence crossfade

Open `Scenes/145-sequence-clip-blending.scene.json` and press **Play → Game**.
The cube uses two original local-transform clips on one four-second sequence.
The primary clip starts at zero and runs for two seconds; the next starts at one
and ends at three. The overlap blends position, rotation and scale, then the last
pose holds until the loop repeats. Both clips are in `Assets/Sequences`.

Open the controller's sequence Timeline, select the cube track and inspect **Clip**,
**Next clip**, **Primary clip start** and **Next clip start**. The two strips mark
each clip and highlight the overlap. Apply timing, refresh clips and preview; Scene
Save persists the arrangement. Export uses the same prepared clips without the
source editor or a compiler. The [sequence guide](../../../manual/editor/animation-sequences.md)
explains limits, gaps, clearing the next slot and frame export.

## Nested sequence arrangement

Open `Scenes/146-nested-sequences.scene.json` and press **Play → Game**. An
eight-second parent contains a six-second child at one second. The child reuses
the original sound-cue scene's panel motion, three pitched tones and camera cuts.
Tones sound at parent times one, three and five seconds; the ordinary overview
camera covers time outside the child placement. The whole arrangement loops.

Open the parent's sequence Timeline and select the child under **Nested sequences**.
The green span shows its placement. Change **Start in parent**, apply and refresh
to preview the combined arrangement. **Open nested timeline** edits the child locally;
**Open parent timeline** returns to the arrangement. The child's stored speed of two,
loop and disabled Play on start demonstrate standalone intent: the placed child
instead follows the parent's clock and rate. Save the Scene before exporting.
See the [sequence guide](../../../manual/editor/animation-sequences.md) for
detachment, Prefabs, controls, sound behavior and current limits.


## Source reverb

Open **Scenes/148-source-reverb.scene.json** and press **Play → Game**. A ten-second
sequence plays the same original tone dry at0seconds, with bright reverb at3seconds
and with stronger damping at6seconds. The red/green/blue objects identify those
sources. Camera, light, motion, sound cues and all resource references are included;
no project C++ is required for Play or export.

Stop Play and select the green or blue source. In **Inspector → Audio Source**,
change **Reverb**, **Reverb decay (s)**, **Reverb damping** and **Reverb wet mix**.
Save and Play again to hear the next cue with those settings. During Play the
ordinary live controls can tune values or pause/resume a retained effect tail.
Gain and the Effects mix channel control the complete processed signal; mute
keeps its time advancing. Active effect edits clear history without rewinding the
clip. This is a per-source effect, with no shared effect buses or room simulation.
See [Audio](../../../manual/editor/audio.md).

## Sixteen independent controllers

Open **Scenes/149-sixteen-controllers.scene.json** to see sixteen Sprite markers
in a four-by-four layout. Project Settings includes **Player 1 move** through
**Player 16 move**, each with matching radial left-stick bindings. Build/register
**CodeExamples/MultipleGamepads.cpp** with **ControllerSlots.hpp** beside it, or
build the installed **multiple_gamepads_gameplay** sample. The same code also
serves the four-player scene92. Without the optional module, use Console to
inspect the actions; the markers remain still.

In Game, open **Gamepads...**, scroll to a controller and choose **Player 1** through
**Player 16**. Choosing an occupied slot unassigns its previous controller. Close
the popup and capture gameplay input again. You can move one controller between
slots to test high numbered markers, or use multiple connected controllers for
independent movement. Assignments are temporary for this host session. Save the
Scene and Project Settings, choose this scene as startup, build the matching code
and export. Physical controller support depends on the host and device.

## Input-triggered component messages

Open **Scenes/150-input-messages.scene.json** and set up project C++ code in Build.
Use **Content > Create / Register > C++ > System...**, name it **InputMessages**
and choose **Component: None (empty registration)**. Replace the generated
`Native/Systems/InputMessages.cpp` with the bundled `CodeExamples/InputMessages.cpp`,
save source and build with the matching SDK. The managed registration function
is `forge_register_system_InputMessages`.
The installed `input_messages_gameplay` sample target wraps this same source.
Two cubes each have **Input Message**, the **Send component message** digital
action and **Local Translation** as their attached payload. Capture Game input
and press Space: each fresh press moves both cubes half a unit right, wrapping
back from their range end. Holding Space sends once.

Edit each cube's Input Message choices through Inspector. Console / Gameplay
input shows dispatch counts and diagnostics. Stop and save authored values before
export; include the matching compiled module. Without a receiver, events can be
sent but the cubes remain still. See [the complete workflow](../../../manual/editor/input-messages.md).

## Scene sequence UI

Open **Scenes/151-sequence-ui.scene.json** and press Play. The bar grows, fades and
changes color on a four-second root loop. Its two-second child starts at root1s
and animates the HUD's keyed UI Values amount and width. Pause holds the bar; a
small independent RCSS pulse continues. Step advances one simulation tick.

Select **HUD - keyed Timeline values** to inspect UI Values and Transform Animator.
Open **Assets/Sequences/UiValues.animation.json** to edit the two component-field
tracks. Reordering entries preserves their bindings; renaming/removing an animated
name requires updating the clip. RML expressions live in **Assets/UI/Sequence/hud.rml**.
The Scene has a Camera and needs no project C++ compiler. Save/publish changes and
select it as startup before normal export. Edit Timeline preview and silent frame
export do not render Runtime UI; evaluate the HUD in Game Play.

## Live camera output images

Open **Scenes/152-camera-render-textures.scene.json** and press Play. Two producer
Cameras view rotating red and cyan cubes and share **Split.rendertexture.json**
using left/right half viewports. Monitor A samples that image. A relay Camera
views Monitor A and writes **Relay.rendertexture.json**, sampled by Monitor B.
The main Camera shows both monitors. No project C++ compiler is required.

**Assets/RenderTextures** owns both 512×256 configurations, three Materials and
an original two-second rotation Clip. Source cubes use layer1, Monitor A layer4,
Monitor B layer8 and the main Camera mask12. Producer masks exclude their own
screens. Keep that separation when editing; feedback loops are rejected. Publish
outputs and Materials, save the Scene, then select it as startup before export.
After resizing an output, republish dependent Materials before export.
See [the camera image guide](../../../manual/editor/render-textures.md).

## Smooth Camera Shot views

Open **Scenes/153-camera-transitions.scene.json** and press Play. The narrow left
view holds, blends for one second into a wider right view, then blends back. Both
cameras ride an animated shared rig. A final half-second uses the enabled overview
camera before the six-second loop restarts.

Stop, select **Director - six-second loop** and open its sequence Timeline. Select
**B - one-second smooth incoming view** to edit **Shot blend in**. Apply the fields,
refresh clips and preview in Game. A zero value creates an exact cut. Smooth pairs
must share projection, viewport, layers and clear/background settings; the manual
lists the complete compatible profile. Save the Scene and export it normally;
this example needs no project C++ module.


## UI filter lab

`Scenes/155-ui-filters.scene.json` compares original reference, grayscale,
brightness, contrast, opacity and blur shape samples. Pause and edit Filter lab
HUD / Live gameplay / UI Values / blur (0..16 resolved pixels). The pulse uses
ordinary RCSS filter keyframes on presentation time. Stop restores saved values.
`Assets/UI/Filters/Lab.rml` is editable RML/RCSS source; save and Reload UI to try
changes. Select this Scene as startup and export with the matching runtime kit.
No project C++ module or compiler is required. See the Runtime UI manual for
filter ranges and temporary-layer limits.

## UI drop shadows

`Scenes/162-ui-drop-shadows.scene.json` compares original hard, left-offset, soft,
stacked and live shadow samples. Pause, select Shadow lab HUD and change Live
UI Values / blur from0to16; Stop restores the saved value. The small pulse uses
presentation time. Edit `Assets/UI/Shadows/Lab.rml` and Reload UI for saved source
changes. Drop-shadow uses color, signed x/y offsets within128pixels and sigma0..16.
Set this Scene as startup and export with the supplied matching runtime kit.
The same UI document/values, filter pool and presentation owners run in the game.

## UI backdrop filters

`Scenes/163-ui-backdrop-filters.scene.json` compares background blur, grayscale,
contrast and foreground filtering over original color stripes. Pause, select
Backdrop lab HUD, and tune Live UI Values / blur0..16. Stop restores the saved
value. The small color bar animates during Pause. Edit
`Assets/UI/Backdrops/Lab.rml` and Reload UI for saved source changes. Backdrops
read earlier-drawn pixels within their rounded border and preserve foreground
text. Shared layer/image limits apply; use the supplied matching runtime kit.

## UI linear gradients

`Scenes/164-ui-linear-gradients.scene.json` compares horizontal, vertical,
multiple-stop, alpha, repeating and live-angle gradients. Select **Gradient lab
HUD** and tune **UI Values / angle** while paused. The small bar keeps animating
on presentation time. Authored values and RML/RCSS save and export through the
normal UI workflow; use the supplied runtime kit. See the Runtime UI manual for
stop, resource and clipping limits.

## UI radial and conic gradients

`Scenes/165-ui-radial-conic-gradients.scene.json` compares circles, offset
ellipses, repeating rings, conic colors, transparent wedges and live angle tuning.
Select **Gradient lab HUD → UI Values / angle** during Play. The small bar animates
on presentation time while simulation is paused. Use the ordinary source refresh,
Reload UI, save and matching-kit export workflow. The Runtime UI manual explains
size, stop, shared-record and clipping limits.

## UI alpha masks

`Scenes/166-ui-alpha-masks.scene.json` compares a reference card with linear,
radial, conic, PNG and live-angle alpha masks. It reuses the canonical health PNG
from the bitmap-format example. Tune **Mask lab HUD / UI Values / angle** while
paused; the small bar keeps animating on presentation time. Source refresh,
Reload UI, save and supplied-kit export use the ordinary UI workflow. The Runtime
UI manual explains alpha coverage and shared image/layer/filter-chain bounds.

## UI box shadows

`Scenes/167-ui-box-shadows.scene.json` compares a reference panel with hard,
soft, spread, inset and live shadows. Select **Box shadow lab HUD → UI Values →
blur** and tune `0..32` during Play. The small bar animates opacity while paused;
box-shadow keyframes are unavailable. Save authored values after stopping to keep
them. Save the startup Scene and export with the supplied runtime kit. The
[Runtime UI manual](../../../manual/editor/runtime-ui.md) covers syntax,
viewport clipping and shared resource limits.

## Morphological anti-aliasing

`Scenes/168-morphological-antialiasing.scene.json` reuses original edge-laboratory
geometry and the shared transform clip with an independently authored spatial
filter. Play to view motion. Stop and use **Tools > Scene lighting... →
Morphological anti-aliasing** to compare threshold 0.01..0.5, search 1..32 and corner
rounding 0..1. The defaults are 0.1, 16, 0. One spatial method is active at a time;
temporal anti-aliasing remains independent. Save and export the startup Scene with
the supplied kit. The [lighting manual](../../../manual/editor/lighting.md) covers
controls and resource/current-image limits.

## Occlusion culling

`Scenes/169-occlusion-culling.scene.json` contains a moving blue wall,32 rigid
spheres, two visible side landmarks, a floor and an authored game camera/light.
The wall follows the existing shared Platform transform clip on its parent.
Press Play to see geometry revealed as the wall moves and turns. Stop Play and
use **Tools > Scene lighting... > Occlusion culling** to compare the default-on
example with ordinary drawing; the image should remain consistent. Save before
export. This profile accelerates eligible built-in opaque rigid triangle Meshes;
mask/blend, custom, skin/morph/wind and uncertain bounds retain ordinary submission.
See [Occlusion culling](../../../manual/editor/occlusion-culling.md) for the safety bias and bounds.

## Texture painting

`Scenes/170-texture-painting.scene.json` shares the original painted
`Assets/Painting/Signal.paint-texture.json` Texture and
`Assets/Painting/Signal.material.json` between a cube and Sprite. The cube uses the
existing shared Spin transform clip. Press Play, then stop and open the paint
source in Content to try brush color/radius/hardness/opacity, erase, fill and
stroke Undo/Redo. Choose **Save / Publish** to prepare the ordinary Texture used
by both objects. For an imported image, create a canvas of matching dimensions and
Color/Data usage, then use **Copy published pixels**; the original stays unchanged.
Save the consuming Material and Scene before export. See the
[painting guide](../../../manual/editor/texture-painting.md) for controls and limits.

## UI luminance-mask samples

Open **171-ui-luminance-masks** and press **Play**. Compare opaque gray in an
alpha mask and a luminance mask, then try the brightness fade, color weights,
PNG image and live angle. Select **Luminance mask lab HUD → UI Values → angle**
in live Play tuning. The mask affects the panel and its text; the small bar keeps
its presentation animation while simulation is paused. The image dependency is
already declared. Save an authored angle and the Scene before exporting with the
current runtime kit. See the runtime UI guide for syntax and shared layer limits.

## Audio streaming

Open **172-audio-streaming** and press Play. A blue cube rotates while a quiet
twelve-second stereo pulse loops. In Content, open
`Assets/AudioStreaming/PagePulse.wav` to inspect **Playback storage: Stream**,
48000 Hz cooked output, the waveform and seeking. The original source is 44100 Hz.
Use Live sound to pause/resume, restart or add a one-shot; check stream reservations
and backing in Live audio mix. Save before exporting with the matching runtime kit.
See the [audio guide](../../../manual/editor/audio.md#choose-resident-or-streamed-playback)
for eight-voice, temporary-storage and import limits.
