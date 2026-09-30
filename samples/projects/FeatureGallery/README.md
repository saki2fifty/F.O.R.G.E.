# FORGE Feature Gallery

This is an editable FORGE project. Each saved scene demonstrates one focused
engine feature using normal authored entities. The project uses built-in geometry;
no imported assets, gameplay C++ module, SDK or compiler is needed for these four
scenes.

## Open and inspect

1. Copy the entire `Examples/FeatureGallery` folder to a writable place outside
   the FORGE installation. Keep `Scenes` and `forge.project.json` together.
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
