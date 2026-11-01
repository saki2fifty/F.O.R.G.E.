# Navigation

Navigation lets an object find a route across a floor and around static obstacles. Build a **NavMesh** from marked 3D shapes or 2D regions, then give an object a **Navigation Agent** and a destination. Choose the plane explicitly when building.

Agents follow baked static geometry. Add **Navigation Avoidance** for local steering
around other participating agents. For moving solid footprints, enable the
**Dynamic obstacles** bake profile and add **Navigation Obstacle** components. Navigation Agents cannot also have a Physics Body, Physics Body 2D
or an enabled Character Controller in either domain.

In **Window → Profiler → Play runtime**, **NavSourceCapture** shows the CPU time
spent checking marked surfaces during Play. Rebuild the NavMesh after changing
marked geometry.

## Build a 3D floor and obstacles

1. Create a Plane. Set its scale to **20, 1, 20** and position to **0, 0, 0**.
2. Select it. In Inspector, use **+ Add Component → Navigation / Navigation Surface**. Leave **Enabled** checked.
3. Create a Cube at **0, 2, 0**, with scale **2, 4, 4**. Add a Navigation Surface to it too.
4. You can add a second obstacle elsewhere on the floor. Mark every static shape that should affect the route.
5. Save the scene.

Marked built-in shapes, editable Mesh assets, and static imported Mesh entities contribute. For an imported Model, select its child entity that has a **Mesh Renderer**, then add **Navigation Surface**. Marking only its group/root does not include its children automatically. Unmarked objects are invisible to the build. Keep marked geometry static.

Marked sources and their spatial parents must have no enabled **Character
Controller** or **Character Controller 2D**. Disable the controller or use another
object as static navigation geometry. An inherited controller counts too. A
**Child space: World** object does not follow its structural parent. A failed build
keeps the previously selected NavMesh.

Skinned characters, morphing meshes, points and lines cannot be baked. The build uses the Mesh's first level of detail and all its triangle parts. The small-scene profile supports up to32 external Mesh assets and16,384 included triangles; an oversized/unsupported Mesh gives a diagnostic and preserves the previous NavMesh. After editing or reimporting a marked Mesh, rebuild that Mesh through its normal Content workflow, then rebuild navigation. Saving its source alone does not change the selected cooked revision.

Open **51-static-mesh-navigation** in Feature Gallery for a saved editable Mesh floor, static obstacle and ready-built route. Play, select the agent, and inspect **Live gameplay → Navigation progress**. You can export this scene without sending its editable Mesh source or a navigation build worker. Scene50 continues to demonstrate built-in shapes and retained older navigation assets.

## Build top-down 2D navigation

Open Gallery **80-xy-navigation** for a ready example. Press Play: the cyan agent
routes around the orange obstacle; the green and amber agents steer past each
other in the right lane. Their Sprites use an ordinary orthographic Camera. Select
an agent to inspect **Live gameplay → Navigation progress**. Stop restores its
starting position. This Scene needs no project C++ module.

To create your own:

1. Create an empty entity. Add **Navigation / Navigation Region 2D** in Inspector.
2. Leave **Enabled** on and **Obstacle** off. Set **Width = 20** and **Height = 14**. This centered rectangle is walkable. Its entity transform controls its XY position, rotation and scale; visual Z does not change the navigation footprint.
3. Create another empty entity with the same component. Enable **Obstacle** and set **Width = 2**, **Height = 4**. Put it in the walkable rectangle's center.
4. Select either region to see its outline in Scene: green for walkable, orange for obstacle, gray for disabled. Add Sprites separately if you want visible art.
5. Open **Content → Create / Register → Navigation**. Set **Plane → 2D (XY)**. Choose **Agent radius** for clearance and **Cell size** for build resolution, then click **Build NavMesh**. Enable **Scene → View → Navigation overlay**.
6. Create **Entity → Create → Rendering → Sprite**. Assign a published [image Material](sprites.md), set its size to **0.7 × 0.7 m** and position to **-8, 0, 0**. Add **Navigation Agent**, choose the generated NavMesh, set **Destination X = 8**, **Destination Y = 0**, and enable **Has destination**.
7. Add **Entity → Create → Rendering → Camera**. Set position to **0, 0, -20**, rotation to **0, 0, 0**, **Projection → Orthographic**, and **Orthographic Height = 17**. The Camera looks along +Z. An unlit Sprite image material needs no scene light.
8. Save, press **Play** and open **Game**. The Sprite follows the opening around the obstacle. In Scene, the navigation overlay shows its route.

Regions are independent navigation data. Sprite artwork, colliders and painted
TileMaps are not included automatically. Overlapping walkable rectangles form a
union; obstacle rectangles cut that union before agent-radius clearance is applied.
An obstacle outside all walkable rectangles adds no walkable area. Add at least
one walkable region. Keep sources and their spatial parents static and free of
enabled Character Controllers, as in 3D.

XY destinations and movement use world X/Y. The agent keeps its starting world Z;
**Destination Z** does not move it between visual depths. Avoidance considers
participants on the same NavMesh even when their visual Z differs. This is
top-down routing; it does not plan platformer jumps, falls or moving platforms.

A Scene has one generated navigation asset/profile. Switching **Plane** requires
an explicit rebuild and keeps that asset's identity. 3D Surface markers and 2D
Region markers are selected independently. Save the Scene and choose it as startup
before normal export; its built NavMesh is included in the game.

## Select and resize 2D regions in Scene

Enable **Scene → View → Navigation overlay** to show the region outlines. Click
an outline to select its region, or select it in Hierarchy. The selected outline
remains visible when the overlay is off. Hidden regions follow their hierarchy
visibility; locked regions cannot be picked. A disabled region has a gray outline
and remains editable.

Hover Scene and press **F** to frame the selected region's full XY footprint.
When a selected region also has visible art, framing includes both. Outlines show
the navigation plane at world Z0; visual Z remains separate.

Press **W** over Scene to use Move. Four gold squares appear at the selected
region's edge midpoints. Drag one to change **Width** or **Height** symmetrically
around its center. The tooltip identifies the dimension. **Ctrl** rounds that
local dimension to **View → Snap spacing**; entity scale still changes its world
size. To move the whole region, use the ordinary transform handles. If an edge
handle is too small or edge-on, zoom in or face the XY plane; Inspector dimensions
remain available.

Release to keep the resize as one Undo step. **Escape** or right-click cancels.
Changing selection, losing editing access or resizing the viewport also cancels
a drag. Handles are unavailable during Play and for hidden or locked regions.
For multiple selections, use Inspector to edit individual region dimensions.

After resizing, save the Scene and explicitly **Build NavMesh** again. Until then,
the previous baked asset remains selected and the changed geometry is stale.
Undo can restore matching geometry; it does not undo a later asset build.

## Build the NavMesh

Open **Content → Create / Register → Navigation**. Select **Plane → 3D (XZ ground)** for the floor example. Its defaults are:

- **Agent radius: 0.4 m.** Clearance around obstacles and floor edges.
- **Agent height: 2 m.** Required space above the floor.
- **Maximum climb: 0.4 m.** Largest step the baked route can traverse.
- **Maximum slope: 45°.** Steepest walkable slope.
- **Cell size: 0.2 m.** Horizontal build resolution.
- **Cell height: 0.1 m.** Vertical build resolution.

Click **Build NavMesh**. The editor stays responsive while the worker builds and validates the candidate. **Cancel navigation build** discards the candidate. A failed build keeps the previous usable navmesh.

The ready/stale message checks marked objects and the editor's current asset selection. Changing an included object or a build setting updates it immediately. Refreshing assets keeps your unfinished navigation settings. **NavigationStatus** in the editor CPU profiler shows the time spent checking this message.

Enable **Scene → View → Navigation overlay** to see translucent teal triangles. There should be an opening around the obstacle, with clearance determined by the agent radius. Yellow lines show runtime routes; dots mark their start and end.

Changing scenes or projects clears the previous overlay. Open **Content → Create /
Register → Navigation** to load the current scene's saved NavMesh. Loading it does
not rebuild or change the asset.

## Route around moving obstacles

![Navigation Obstacle properties beside the ready Dynamic obstacles bake profile](../images/navigation-dynamic-controls.png)

Set the obstacle's local dimensions in Inspector and choose the matching tiled
profile in Navigation before rebuilding.

Open **129-moving-navigation-3d** or **130-moving-navigation-xy** in Feature Gallery
for a ready example. The orange obstacle follows an editable animation clip while
the cyan agent recalculates its route. No project C++ module is required.

1. Keep the floor's **Navigation Surface** or walkable **Navigation Region 2D**.
2. On the moving object, remove its Navigation Surface/Region marker. Add **Navigation / Navigation Obstacle**. Leave **Enabled** on. Do not add a Navigation Agent to this same object.
3. Set **Radius** for its local circular footprint. In 3D, set **Height** for a cylinder centered on its origin. The object can have separate visible art; the Navigation Obstacle does not create a renderer or physics collider.
4. Open **Content → Create / Register → Navigation**. Select the plane, check **Dynamic obstacles**, then click **Build NavMesh**. Agents keep their existing NavMesh reference when the same Scene's asset is rebuilt.
5. Save the Scene. During Play, move the obstacle through an ordinary transform animation or project C++. Agents recalculate their routes around its current footprint. Disabling/removing the component restores walkable tiles.
6. Use **Live gameplay → Navigation progress** on an agent to inspect its route. Yellow overlay paths follow the current carved mesh; teal triangles show the saved base. Stop restores authored starting values. Save outside Play before exporting; the generated layers and obstacle components travel with the game.

The 3D shape must stay upright, with equal X/Z scale and no shear; yaw and mirrored scale
are allowed. XY uses equal projected X/Y scale and ignores visual Z and Height. Radius
accepts 0.05–5 m and Height 0.2–10 m before entity scale. Agent-radius clearance is added
automatically. An unsupported ellipse, tilt or collapsed shape gives a diagnostic and
prevents stale-route movement.

Use at most eight enabled obstacles. Each inflated footprint can overlap at most eight
baked layers; reduce its size or change the tile resolution if that limit is exceeded. A
tiled build supports at most 64 layers of 32 × 32 cells, with four layers at one tile
coordinate. It may reach this limit before the general 512×512-cell grid limit. A
footprint may span at most 16 tile positions along either axis, even when many positions
are empty. Reduce a large footprint or rebuild with coarser cells. Keep larger stationary
obstacles in the static bake.

This updates blocked portions of the existing floor. It does not create new floors, rebake
moving platforms, stop physics bodies or push an agent out of an obstacle. If an obstacle
covers the agent's start, it cannot move. Inspect its failure diagnostic; navigation can
resume when a valid start and route become available. Obstacle properties are authored in
the regular Inspector outside Play; they are not part of the live agent-tuning controls.


## Let agents steer around each other

Open Gallery scene **52-navigation-avoidance** and press **Play**. Two agents pass each other in one lane; another goes around a parked agent in the other lane. The saved scene includes its built navigation, camera and light, and can be exported normally.

Add **Navigation Avoidance** to each Navigation Agent that should participate. Enable it and set **Horizon** to the number of seconds to look ahead (default 2). The baked radius defines clearance. In 3D, height also filters vertically separated participants; XY ignores visual depth. Idle and paused participants stay in the way; disabled agents or agents without this component do not participate.

During Play, select an agent and adjust **Enabled** or **Horizon** in **Live gameplay**. These changes affect the running world; Stop restores the authored scene. Edit the regular Inspector outside Play to save the settings.

Keep movement anchors aligned with the navmesh and use stationary parents. This is local steering, so tight or crowded spaces can still jam. It does not add physics collision response or moving obstacle carving.

A build is a separate asset operation. Scene Undo does not undo a navmesh build. Successful rebuilds keep the same asset identity, so agents keep their reference.

## Make an agent follow a path

1. Create another Cube named **Agent** at **-8, 0.1, 0**. A small scale such as **0.2, 0.2, 0.2** makes it easy to see.
2. Use **+ Add Component → Navigation / Navigation Agent**. Do not mark this moving object as a Navigation Surface or Navigation Region 2D, give it a Physics Body in either domain, or enable a Character Controller on it.
3. Choose the generated **NavMesh**.
4. Set **Destination X = 8**, **Destination Y = 0.1**, and **Destination Z = 0**. Press Enter to commit each numeric field.
5. Enable **Has destination**. Leave **Enabled** checked and **Speed** at 2.
6. Make sure the Scene has a [game Camera and Light](../getting-started/first-scene.md), save, then click **Play**. The cube should travel around the obstacle and stop near the destination.
7. Click **Pause**. It stops. **Step** moves it one simulation tick; **Resume** continues.
8. Click **Stop**. The authored cube returns to its original position.

For a 3D NavMesh, destinations use world X/Z across the floor and Y upward. For an XY NavMesh, use X/Y across the region; visual Z remains unchanged. **Stopping distance** is the remaining distance along the route to the projected destination. Movement changes position; the agent does not turn to face its route. Its transform origin follows the navigation surface. For a larger visual model, use a child object with an upward local offset.

## See the requested destination

1. Build the NavMesh, or expand **Content → Navigation** to load its saved overlay.
2. Select the agent and confirm its **NavMesh** matches that Scene's saved asset.
3. Enable **Has destination** and turn on **Navigation overlay** in the Scene tools.

A cyan diamond labeled **Requested destination** shows the selected agent's
destination. A thin cyan line joins its world origin to that point. Edit the
destination fields in Inspector to move the guide; normal Undo, Save and reopening
keep their usual behavior. A disabled agent has a gray guide.

For XY navigation the guide keeps the agent's visual Z. For 3D it uses all three
world coordinates. The requested point still needs projection onto the NavMesh
when the game runs; a visible diamond does not confirm that a route is available.
During Play, the amber path shows the calculated route. Clearing **Has destination**,
hiding the agent or turning off **Navigation overlay** removes the requested guide.
The guide is an editor overlay and is absent from exported games.

## Move the destination in Scene

1. Select one placed agent with **Has destination** enabled and its saved NavMesh loaded under **Content → Navigation**.
2. In **Navigation Agent**, click **Edit destination in Scene**, or choose **Scene / Navigation destination** in the command palette. This opens the Scene view, centers the requested point without changing zoom, and enables **Navigation overlay**.
3. Drag a colored axis to change one coordinate. Drag the gold center to move across the navigation plane. For 3D, handles change world X/Z and keep Destination Y; for XY they change world X/Y and keep Destination Z.
4. Use **Snap** or hold **Ctrl** to snap world metres with **View → Snap spacing**.
5. Release to apply one Undo step. **Escape**, right-click, middle-click or losing application focus cancels the preview.
6. Press **W** for ordinary object Move or **Q** for selection. Save the Scene to keep the destination, then Play to test its route.

The preview leaves the agent's transform in place. A view looking along the plane
may hide the center handle; orbit to see both axes and the center. Hidden or
selection-locked targets are not editable. Loading the saved NavMesh does not bake
a new one. Prefab instances use ordinary property overrides, Undo and **Revert**.
Destination editing is unavailable during Play; use **Live gameplay** fields to tune
a running agent.

Keep the agent's spatial parents stationary and free of enabled character
controllers. A controller on the object or its spatial parent stops navigation
movement and reports a failed/invalid state. A disabled controller allows ordinary
navigation. If C++ controls your character, query the Navigation service for a
route and let that controller own movement; do not add a second active Navigation
Agent driver.

## Tune an agent during Play

Select an agent in Hierarchy during Play. Open **Inspector → Live gameplay →
Navigation Agent**. These fields affect the running copy:

- **Speed** changes its movement rate. Set it to **0** to hold that agent while other gameplay continues.
- **Enabled** turns its navigation movement on or off. **Has Destination** starts or clears its route request.
- **Destination X**, **Destination Y** and **Destination Z** set a world-space goal. Use X/Z for 3D or X/Y for top-down 2D. XY movement keeps visual Z.
- **Stopping Distance** changes how close along the route it tries to get.

For a goal requiring several coordinate changes, **Pause** first, enter each value,
then **Step** or **Resume**. Edits themselves do not advance simulation. Navigation
uses the new settings on its next fixed tick. Watch **Navigation progress** for the
result; a goal outside the walkable area can fail even when its numeric values are
valid. A rejected field value retains the previous value. The NavMesh asset is
chosen in the authored Inspector while stopped.

Collapse the Navigation Agent section to reach Navigation Avoidance in a short
Inspector, or scroll to the controls. Supported live fields remain available while
the game is paused. Their temporary values do not change the Scene or Prefab source.

To keep selected settings, choose **Keep values after Stop...**, Stop, then use
**Run → Review runtime tuning... → Apply to Scene** and save. This uses one Scene
Undo step and copies only selected tunable values; it does not keep the agent's
runtime position or change its NavMesh. See [Play mode](play-mode.md).

## See movement progress

Open **50-navigation-progress** in the Feature Gallery. Select **Agent - inspect progress during Play**, then press Play. The cube travels around the obstacle and stops. In Inspector's **Live gameplay → Navigation progress**, watch the state, route distance, measured speed and navigation tick. These are read-only runtime observations; Stop restores the saved scene.

- **Moving** means the agent advanced on its last navigation tick.
- **Arrived** means it reached its stopping distance along the route.
- **Paused** means its configured speed is zero. Pausing the game itself freezes the last displayed observation; Step updates it once.
- **Pending** means the current settings have not run through a navigation tick yet.
- **Failed** shows the status and **Details** from the last navigation tick. For example, it can identify a conflicting physics component, an unavailable NavMesh or changed source geometry. Ancestor conflicts show the authored name and ID; use Hierarchy's search to find that object.
- **Blocked** means no progress on one tick. Optional local avoidance can still jam in crowded or blocked spaces.

Distance and speed appear only for a valid sampled route. Expand **World coordinates**
to see the sampled **Position** and **Projected goal** in X, Y, Z order. These are
read-only navigation-tick values; the running image can interpolate between ticks.
Pending, disabled and failed states do not show invented route coordinates. Changing a
live target clears its previous target-failure details until the next navigation tick.

A failed agent can recover after you correct its settings or sources. For stale
baked geometry, stop Play, restore matching geometry or rebuild navigation, then
start again. Use Console/Problems for the timestamped log history; Inspector details
explain the current failed sample. Some unavailable assets are rejected during
Scene preparation instead; a failed replacement retains the previous active world. If an agent-count limit recurs after being
resolved, it produces a fresh warning.

An exported Gallery50 scene uses the included built NavMesh; it does not need an
editor, compiler or navigation build worker to run.

## Save, reopen and rebuild

Save the scene after choosing the navmesh and destination. Reopening restores those authored settings; generated navigation remains a project asset.

Moving or resizing marked geometry makes navigation stale. Stop Play and rebuild. Changing the build settings also requires rebuilding. The status under the build controls identifies stale navigation. Runtime agents refuse to move on stale geometry.

A NavMesh belongs to the scene that produced it. A duplicated scene needs its own build and agent references to that new asset.

## Prefab agents

Create a prefab from an agent using the normal [prefab workflow](prefabs.md). Instances inherit its navigation configuration. Override **Speed** or destination fields on one instance; use the Inspector's property **Revert** controls to follow the prefab again. Scene Undo/Redo covers these authored edits. Runtime routes are not saved in a prefab.

## Common messages

- **No enabled geometry:** add enabled Navigation Surfaces for 3D, or Navigation Region 2D rectangles for XY. Check **Plane** and include a walkable region.
- **Stale navmesh:** rebuild after changing included geometry.
- **Point outside navmesh:** put the agent and destination over a teal walkable region. Projection searches nearby, not across the entire world.
- **Partial / no path:** the destination is disconnected. Check gaps, clearance, slope and included obstacles.
- **Missing or incompatible asset:** choose a valid NavMesh built for this scene, or rebuild it.
- **Physics ownership error:** remove the agent's Physics Body and any moving physics spatial ancestor, or use an independent World spatial binding.
- **Build/query limit:** reduce the included area or geometry, or increase cell size. A single build supports at most 512 × 512 cells.

Navigation supports static shapes/Meshes in 3D and rectangular regions in XY, within roughly four kilometres of the origin and 64 agents. There are no jump links, moving-platform routes, automatic physics-character movement or general runtime geometry rebaking. Moving cylinder/circle carving requires the Dynamic obstacles profile described above. Optional avoidance is local steering and does not guarantee collision prevention. See [Play mode](play-mode.md) and [Transforms](transforms.md) for related controls.
