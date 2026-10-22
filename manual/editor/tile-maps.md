# Tile Sets and Tile Maps

A **Tile Set** gives names to cells of an image sheet. A **Tile Map** stores which
tile appears at each grid position. A Scene's **Tile Map Renderer** places that
map in the world. Multiple renderers can use one map asset.

## Try the Gallery

Copy **Examples/FeatureGallery** to a writable folder and open that project.
Open **Scenes/74-tile-maps.scene.json**. The left course and smaller mirrored copy
use the same map. Their camera is included; these unlit images need no light.

1. In Content, open **Assets/Tiles/ArrowCourse.tilemap.json**.
2. Select **Loop across negative cells** and choose an arrow in the palette.
3. Drag with the left mouse button to paint. Releasing the button creates one Undo step. Try **Undo** and **Redo**; press Escape during a stroke to cancel it.
4. Choose Erase to remove cells. Use the middle mouse button to pan and the wheel to zoom. **Corner markers** is a second layer over the loop.
5. Choose **Save / Publish**, then return to Scene. Both instances show the saved map. Press Play and open Game to compare the mirrored copy.

Map source editing is disabled during Play. Stop before changing the authored
cells. Scene changes and map-source changes have their own document Undo histories.

## Make a map

Create a Sprite image material from an imported image. Use Content's **Create /
Register** menu to create a **New Tile Set...**. Add that image material, set its
sheet grid, and define the named tiles. Save and publish the set.

Create a **New Tile Map...**, select the published set, and paint its cells.
After **Save / Publish**, drag the map from Content into Scene, or create a Tile
Map Renderer and use **Assign to selected Tile Map Renderer**. Save the Scene.
Cell width and height use local meters; ordinary entity transforms position,
rotate, scale or mirror the complete map. Coordinates may be negative.

Changing a set's sheet grid or definitions requires saving/publishing the set and
then the affected map. A failed publication retains the last working result and
reports the problem. The built-in profile supports up to 262144 occupied cells,
1024 chunks and 8 layers per map; it does not stream an unlimited world.

## Make painted floors and walls

Open **Scenes/77-painted-2d-collision.scene.json** in the Feature Gallery, press
Play and open Game. The amber box and cyan circle land on the raised painted
platforms. The rose box and green circle land on the lowest floor. Blue decorative
arrows do not stop the bodies.

Stop, then open **Assets/Tiles/PaintedPlatforms.tilemap.json**. On **Solid
platforms**, erase the three cells in the left raised platform. Save/publish and
Play again: the amber box falls to the lowest floor. Undo the Map edit and publish
to restore the platform.

For your own Map actor:

1. Add **Physics Body 2D** and leave **Motion** as Static. Turn Sensor off.
2. Add **Tile Map Collider 2D**. Choose its **Solid layer** by name.
3. Put decoration in a different layer. Every occupied cell in the selected layer is solid; hiding the layer or renderer does not remove collision.
4. Turn on the Scene collision outline to inspect the selected published layer. Save the Scene, then test with ordinary Dynamic2D bodies.

The first profile uses rectangular painted cells and Static Map bodies. It does
not include slopes, one-way platforms or moving Map bodies. An empty solid layer
has no collision. Excessively large or complex collision reports a problem and
keeps the previous working runtime cells and collision.

## C++ runtime changes

Each running map instance has its own cell state. C++ code can query cells and
queue bounded edits from a fixed-step system. Changes appear at a fixed-tick
boundary and disappear on Stop. They do not save into the authored map.

**CodeExamples/TileMapEdits.hpp** shows one queued cell write using the public SDK.
Use the actor's Scene/entity IDs, layer key and a tile key from its set. Tile zero
erases a cell. A returned token must be inspected on a later tick and released
when it finishes. The matching installed SDK includes this helper and the tested
`tile_map_gameplay` sample. The Gallery's default Scene needs no C++ module.

**CodeExamples/PaintedCollision.hpp** shows removal of Gallery77's left platform
and an ordinary physics ray query. An accepted cell edit updates collision at the
same fixed-step boundary. Scene outlines show the published source; they do not
display these temporary runtime edits. Stop restores the published Map.

## Export

Save/publish the assets and save the Scene. In Project Settings, choose **Use saved
current scene as startup** and save settings. Export Game with the matching
runtime kit. The exported game includes prepared maps, sets, materials and images;
it runs without the editor, source files or a compiler. Optional C++ modules must
be built and supplied through the ordinary matching module-kit export flow.
