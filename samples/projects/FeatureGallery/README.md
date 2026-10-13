# FORGE Feature Gallery

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
| `14-uv-islands` | Open **UvConnected.mesh.json**, **UvSplit.mesh.json** or **UvFitted.mesh.json** from Content, choose UV, select an island and drag, mirror or fit it. Save & Publish. | Three original checker-textured panels compare matching shared UV edges, a separate patch and a mirrored/fitted mapping. Play and standalone use the same published UV streams. |
| `15-brush-symmetry` | Open **SculptOneSide.mesh.json**, **SculptMirrored.mesh.json** or **PaintMirrored.mesh.json** in Content. Choose Sculpt or Paint and enable Mirror X/Y/Z before dragging. Save & Publish. | Three original spheres compare one-sided displacement, X-mirrored influence and XYZ-mirrored vertex color. Play and export use the same published Mesh streams. |
| `16-authored-triangulation` | Open **PolygonCube.mesh.json**, **TriangleCube.mesh.json** or **TriangleSculpt.mesh.json** in Content. Compare polygon and triangle edges; use Triangulate face/Mesh, then sculpt or move vertices. Save & Publish. | Polygon and triangle cubes render equivalent shapes; the third has a sculpted, colored corner. Published geometry/UVs/colors survive Play and export. |

## Export and run a scene

`27-connected-collisions` compares two original pairs of overlapping falling cubes.
The blue pair's joint disables **Collide connected**, so their overlap is retained;
the yellow pair keeps normal mutual contacts and separates. Both land on the floor.
Select either joint to edit the setting, save, then restart Play or export. This
demonstrates pair filtering without disabling the objects' ordinary scene layers.

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
