# FORGE Feature Gallery

This is an editable FORGE project. Each saved scene demonstrates one focused
engine feature using normal authored entities. The first four scenes use built-in geometry. The remaining scenes include published
editable Mesh sources, cooked Meshes and a checker Material. No
gameplay C++ module, SDK or compiler is needed to explore these scenes.

## Open and inspect

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
optional Developer Kit. A future gallery scene should demonstrate a feature only
when its authored content and standalone result can both be inspected directly.
