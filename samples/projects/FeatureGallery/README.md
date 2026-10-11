# FORGE Feature Gallery

This is an editable FORGE project. Each saved scene demonstrates one focused
engine feature using normal authored entities. The first four scenes use built-in geometry. The remaining scenes include published
editable Mesh sources, cooked Meshes and a checker Material. The eighth scene includes a published transform-animation clip; the ninth has an original two-clip skinned model. No
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
| `08-transform-animation` | Press Play to watch **Animated platform**. Open **Platform.animation.json** in Content, choose that object in Preview object, and scrub or change its keys. Save & Publish. | The blue platform moves between the two markers, turns and changes height over four seconds. Timeline preview restores the authored view when stopped. Play and export use the published clip. |

| `09-animation-blending` | Press Play and compare the three skinned columns. Select **Half Idle / half Reach** and open **Preview skeletal animation...** from Content. Pause, scrub and adjust the blend target/duration. | Idle, an equal blend, and Reach share normalized phase. The center column bends between the two poses. Preview is temporary; Apply settings stores one Scene Undo step. Both clips play in standalone export. |
| `10-animation-states` | Press Play to compare Idle, Walk and Run characters. Select a character root and use Animation Preview's named state selector. Open `Assets/Locomotion.states.json` to change names, loop, speed and incoming fade. | Real independently skinned Quaternius clips. C++ selects stable state IDs; ordinary export includes the entire set. CC0 license and immutable provenance are beside the GLB. |

| `11-animation-markers` | Open `Assets/MarkerPulse.states.json` and inspect/edit Pulse timing on Walk/Run. For an actual action, build the optional [C++ example](CodeExamples/README.md). | Markers are published/exported with the clips. The compiled example alternates character height at the authored timing points. Preview is muted. These are illustrative timing points, not calibrated footsteps. |

| `12-inset-panels` | Open **Frame.mesh.json**, **RaisedPanel.mesh.json** or **RecessedPanel.mesh.json** in Content. Select a face, preview Inset, then use a positive or negative Extrude distance. | Three original checker-textured panels show an inset border, a raised cap and a recessed cap. Save & Publish updates Scene, Play and source-free export. |
| `13-transform-curves` | Press Play to compare the three platforms. Open **EasedPlatform.animation.json** or **ArcPlatform.animation.json**, select a key and expand **Vector curves**. Apply slopes, preview, then Save & Publish. | The existing linear clip, smooth eased clip and custom vertical arc share a four-second cycle on separate parent lanes. Play and export load both legacy and new cooked clips. |
| `18-animation-key-batches` | Press Play to compare original eased travel, a one-second delay and duplicated travel. Open a **Batch*.animation.json** source, Ctrl-click keys, then drag or use **Key offset** and Move/Duplicate/Delete. Save & Publish. | Three lanes use original clips in a six-second loop. Every batch is one source Undo; exported games load the published motion. |
| `14-uv-islands` | Open **UvConnected.mesh.json**, **UvSplit.mesh.json** or **UvFitted.mesh.json** from Content, choose UV, select an island and drag, mirror or fit it. Save & Publish. | Three original checker-textured panels compare matching shared UV edges, a separate patch and a mirrored/fitted mapping. Play and standalone use the same published UV streams. |
| `15-brush-symmetry` | Open **SculptOneSide.mesh.json**, **SculptMirrored.mesh.json** or **PaintMirrored.mesh.json** in Content. Choose Sculpt or Paint and enable Mirror X/Y/Z before dragging. Save & Publish. | Three original spheres compare one-sided displacement, X-mirrored influence and XYZ-mirrored vertex color. Play and export use the same published Mesh streams. |
| `16-authored-triangulation` | Open **PolygonCube.mesh.json**, **TriangleCube.mesh.json** or **TriangleSculpt.mesh.json** in Content. Compare polygon and triangle edges; use Triangulate face/Mesh, then sculpt or move vertices. Save & Publish. | Polygon and triangle cubes render equivalent shapes; the third has a sculpted, colored corner. Published geometry/UVs/colors survive Play and export. |

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
