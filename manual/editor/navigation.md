# Navigation

Navigation lets an object find a route across a floor and around static obstacles. You first build a **NavMesh** from marked scene shapes, then give an object a **Navigation Agent** and a destination.

This is basic path following. Agents do not avoid each other or react to moving obstacles, and cannot have a Physics Body.

Navigation checks marked surfaces during Play without copying unrelated scene content. In **Window → CPU Profiler → Play runtime**, **NavSourceCapture** shows the CPU time used by that check. Changing marked geometry still requires rebuilding navigation; the performance improvement does not change authoring or export steps.

## Build a floor and obstacles

1. Create a Plane. Set its scale to **20, 1, 20** and position to **0, 0, 0**.
2. Select it. In Inspector, use **+ Add Component → Navigation / Navigation Surface**. Leave **Enabled** checked.
3. Create a Cube at **0, 2, 0**, with scale **2, 4, 4**. Add a Navigation Surface to it too.
4. You can add a second obstacle elsewhere on the floor. Mark every shape that should affect the route.
5. Save the scene.

Marked built-in shapes, editable Mesh assets, and static imported Mesh entities contribute. For an imported Model, select its child entity that has a **Mesh Renderer**, then add **Navigation Surface**. Marking only its group/root does not include its children automatically. Unmarked objects are invisible to the build. Keep marked geometry static.

Skinned characters, morphing meshes, points and lines cannot be baked. The build uses the Mesh's first level of detail and all its triangle parts. The small-scene profile supports up to32 external Mesh assets and16,384 included triangles; an oversized/unsupported Mesh gives a diagnostic and preserves the previous NavMesh. After editing or reimporting a marked Mesh, rebuild that Mesh through its normal Content workflow, then rebuild navigation. Saving its source alone does not change the selected cooked revision.

Open **51-static-mesh-navigation** in Feature Gallery for a saved editable Mesh floor, static obstacle and ready-built route. Play, select the agent, and inspect **Live gameplay → Navigation progress**. You can export this scene without sending its editable Mesh source or a navigation build worker. Scene50 continues to demonstrate built-in shapes and retained older navigation assets.

## Build the NavMesh

Open **Content → Create / Register → Navigation**. The defaults work for this example:

- **Agent radius: 0.4 m.** Clearance around obstacles and floor edges.
- **Agent height: 2 m.** Required space above the floor.
- **Maximum climb: 0.4 m.** Largest step the baked route can traverse.
- **Maximum slope: 45°.** Steepest walkable slope.
- **Cell size: 0.2 m.** Horizontal build resolution.
- **Cell height: 0.1 m.** Vertical build resolution.

Click **Build NavMesh**. The editor stays responsive while the worker builds and validates the candidate. **Cancel navigation build** discards the candidate. A failed build keeps the previous usable navmesh.

The ready/stale message checks marked objects and the editor's current asset selection. Changing an included object or a build setting updates it immediately. Refreshing assets keeps your unfinished navigation settings. **NavigationStatus** in the editor CPU profiler shows the time spent checking this message.

Enable **Scene → View → Navigation overlay** to see translucent teal triangles. There should be an opening around the obstacle, with clearance determined by the agent radius. Yellow lines show runtime routes; dots mark their start and end.

A build is a separate asset operation. Scene Undo does not undo a navmesh build. Successful rebuilds keep the same asset identity, so agents keep their reference.

## Make an agent follow a path

1. Create another Cube named **Agent** at **-8, 0.1, 0**. A small scale such as **0.2, 0.2, 0.2** makes it easy to see.
2. Use **+ Add Component → Navigation / Navigation Agent**. Do not mark this moving object as a Navigation Surface or give it a Physics Body.
3. Choose the generated **NavMesh**.
4. Set **Destination X = 8**, **Destination Y = 0.1**, and **Destination Z = 0**. Press Enter to commit each numeric field.
5. Enable **Has destination**. Leave **Enabled** checked and **Speed** at 2.
6. Click **Play**. The cube should travel around the obstacle and stop near the destination.
7. Click **Pause**. It stops. **Step** moves it one simulation tick; **Resume** continues.
8. Click **Stop**. The authored cube returns to its original position.

Destinations use world coordinates: X and Z run across the floor, and Y points upward. **Stopping distance** is the remaining distance along the route to the projected destination. Movement changes position; the agent does not turn to face its route. Its transform origin follows the navigation surface. For a larger visual model, use a child object with an upward local offset.

## See movement progress

Open **50-navigation-progress** in the Feature Gallery. Select **Agent - inspect progress during Play**, then press Play. The cube travels around the obstacle and stops. In Inspector's **Live gameplay → Navigation progress**, watch the state, route distance, measured speed and navigation tick. These are read-only runtime observations; Stop restores the saved scene.

- **Moving** means the agent advanced on its last navigation tick.
- **Arrived** means it reached its stopping distance along the route.
- **Paused** means its configured speed is zero. Pausing the game itself freezes the last displayed observation; Step updates it once.
- **Pending** means the current settings have not run through a navigation tick yet.
- **Failed** shows the cause, such as a stale navmesh, an outside destination or an unsupported Physics Body.
- **Blocked** means no progress on one tick. Agents still do not avoid each other or moving obstacles.

Distance and speed appear only for a valid sampled route. An exported Gallery50 scene uses the included built NavMesh; it does not need an editor, compiler or navigation build worker to run.

## Save, reopen and rebuild

Save the scene after choosing the navmesh and destination. Reopening restores those authored settings; generated navigation remains a project asset.

Moving or resizing marked geometry makes navigation stale. Stop Play and rebuild. Changing the build settings also requires rebuilding. The status under the build controls identifies stale navigation. Runtime agents refuse to move on stale geometry.

A NavMesh belongs to the scene that produced it. A duplicated scene needs its own build and agent references to that new asset.

## Prefab agents

Create a prefab from an agent using the normal [prefab workflow](prefabs.md). Instances inherit its navigation configuration. Override **Speed** or destination fields on one instance; use the Inspector's property **Revert** controls to follow the prefab again. Scene Undo/Redo covers these authored edits. Runtime routes are not saved in a prefab.

## Common messages

- **No enabled geometry:** add an enabled Navigation Surface to the floor and obstacles.
- **Stale navmesh:** rebuild after changing included geometry.
- **Point outside navmesh:** put the agent and destination over a teal walkable region. Projection searches nearby, not across the entire world.
- **Partial / no path:** the destination is disconnected. Check gaps, clearance, slope and included obstacles.
- **Missing or incompatible asset:** choose a valid NavMesh built for this scene, or rebuild it.
- **Physics ownership error:** remove the agent's Physics Body and any moving physics spatial ancestor, or use an independent World spatial binding.
- **Build/query limit:** reduce the included area or geometry, or increase cell size. A single build supports at most 512 × 512 cells.

Navigation is currently limited to static primitive scenes within roughly four kilometres of the origin and 64 agents. There are no jump links, moving platforms, collision-aware characters or crowd avoidance. See [Play mode](play-mode.md) and [Transforms](transforms.md) for related controls.
