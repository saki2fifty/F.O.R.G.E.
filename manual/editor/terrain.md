# Shape terrain with height layers

Open **55-layered-heightfield-terrain** in the Feature Gallery to try two adjoining
tiles and three falling spheres. Press Play to see the spheres land. Stop before
editing terrain.

## Create or open terrain

In Content, choose **Create / Register → New terrain...**. Choose a project file
ending in `.terrain.json` and click **Create**. You start with one flat 16 m tile.
Open an existing terrain source from Content to return to its Terrain document.

Choose a tile from the list or use **Select tile** and click its surface. Set the
new tile coordinates before **Add tile**. Tiles share their borders. **Samples /
side** changes the resolution of every tile; higher values give more detail and
take longer to edit. **Tile size (m)** changes the size of every tile.

## Paint a layer

Select a height layer and choose a Tool:

- **Raise / lower** adds height. Use a negative amount to lower.
- **Flatten layer** moves this layer toward its height target.
- **Smooth layer** softens this layer's neighboring heights.
- **Paint layer mask** controls how much this layer contributes. A mask target of 0 hides its contribution; 1 restores it.

Set **Radius (m)** and the tool's amount or opacity, then drag on the preview.
Release to apply one Undo step. Press Escape to cancel before releasing. **Mirror
X** and **Mirror Z** reflect the brush around the asset's origin.

Layers add together. **Layer enabled** switches a contribution on or off, and
**Layer strength** scales it from 0 to 1. Add a layer for changes you want to adjust
separately. Flatten and Smooth change the selected layer; other layers still
contribute to the visible surface.

Middle mouse orbits the preview, the wheel zooms, and **Frame terrain** fits it.
Preview changes stay in this source document until **Save / Publish**. Source
Undo and Redo apply to terrain edits. Closing a dirty document offers Save,
Discard or Cancel.

## Place, collide and export

Select a scene entity with a Mesh Renderer, then click **Assign to selected Mesh
Renderer** after publishing. Position and scale it with the ordinary Inspector,
and assign a Material to its default surface slot.

For physics, create a Mesh-derived Collision asset from the terrain Mesh and use
it as the static terrain body's Asset Collider. After editing terrain, rebuild
that collision asset so the visible and physical surfaces agree. Gallery55
already has this setup; moving bodies need World space, as with other physics
scenes. Navigation can use a marked static terrain Mesh within its existing bake
limits; rebuild navigation after changing the terrain.

Save the scene, choose it as startup in Project Settings, and export normally.
The game uses compiled terrain and collision data. It needs neither terrain source
files nor a C++ compiler. There is no terrain editor inside the running game.

This first profile supports up to eight tiles and three height layers. It is a
surface heightfield: caves, holes and overhangs require separate meshes.
