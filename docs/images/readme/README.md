# README screenshots

## Many shared projectors

[Clustered-projector Gallery](../../../manual/images/decal-cluster-gallery.png) is
the complete 640×360 FrameRenderer display capture for Gallery 116, Scene
`e9 da9407-f9 dc-4 b6 a-88 cf-e5 f888 a39629`. It shows 64 tinted badge projectors sharing a
Material and Texture on a lit floor. It was captured through
`forge_decal_gallery_tests <Gallery> <scratch> --clustered` on software Vulkan,
GCC 12 Release, with the pinned backend and normal runtime assets. The same fixture
checks actual source-free export closure and four exactly equal relocated display
pairs at 640×360,360×640,96×64 and17×13. The PPM-to-PNG conversion preserves all RGB
pixels; no controls, geometry or colors were retouched. PNG SHA256 is
`a6097950 e70 d5 ff8 fc140552 d1 cdf245 cfeea37759 f51 a2 f123 ed1 c6 e49 c6 f4 f`.
This establishes the renderer/example appearance; Windows/D3D12, full graphical
editor interaction and physical GPU acceptance remain pending. Existing Decal
Inspector controls and the earlier Gallery captures are unchanged.

## Projected material channels

[Decal controls](../../../manual/images/decal-projector-controls.png) is the complete
1080×900 production ComponentInspector capture from
`forge_reflection_probe_editor_tests <scratch> --capture-decal`, using pinned
ImGui/SDL software rendering and the dummy video driver. The same fixture checks
actual creation, Material picking, numeric/toggle input, invalid-value rejection,
Undo/Redo, live-field availability, guides/framing and save/reopen at100/150/200
percent in two window sizes. The canonical PNG is copied unchanged; SHA256 is
`a285ff3a8a75f9944d8eb98780278e3f4a101fe7c8bc4003998029a326ecc912`.

[Surface-channel Gallery](../../../manual/images/decal-surface-gallery.png) is the
complete640×360 FrameRenderer display capture for Gallery115,
Scene `374c17f6-609a-40ad-8095-f746d16aeea8`. The left projector affects normals,
the right roughness, and the floor both, with color influence0. It comes from
`forge_decal_gallery_tests --surface-channels` on software Vulkan/lavapipe. Its
four authored/source-free relocated displays match exactly; every projector has
a separately measured visible effect. Lossless PPM-to-PNG conversion preserves
all RGB bytes; SHA256 is
`13f41a29e8104baa910d50251d989c20c40aa747e58add8603aac17b6c7d46f1`.
The current controls and Gallery115 capture receipts are in the local
`AgentFiles/build/phase194-projector-surface-channels` evidence directory, based
on7cc06f0 plus the current projected-channel batch. They are authentic production
controls/rendering, not Windows/full-editor/physical GPU acceptance.

[Gallery image](../../../manual/images/decal-gallery.png) is the complete640×360
FrameRenderer display output for Gallery113,
Scene `0aa828ed-f49d-4b9a-9367-ab1b9b251e92`. It shows mirrored/tinted badge
projections and overlapping floor paint. The source is
`forge_decal_gallery_tests`, software Vulkan/lavapipe and the pinned native shader
stack. Lossless PPM-to-PNG conversion preserves every RGB byte; SHA256 is
`cdf4f28fcbdcdc2830a77e7fde272d6dbe9ef7f2833fbc03aa6ae9b213acc78c`.
Each projector has a separately checked visible effect. Four authored/relocated
readback pairs match after the source project is unavailable.

The unchanged Gallery113 image uses local decal work based on1749efc; its source
commit contains the corresponding fixture and renderer inputs. Its private evidence resides in
AgentFiles/build/phase192-authored-decals, with the gallery capture under
decal-gallery/856e6da8-517f-4fe7-b4e2-582fefc157e6 in the native cache. The
captures contain no fabricated, repainted or retouched controls. They demonstrate
actual software controls/rendering, not a full Windows desktop, physical GPU/input,
FXC/D3D12 or graphical exported-EXE acceptance.

## Temporal anti-aliasing controls

[Controls](../../../manual/images/temporal-antialiasing-controls.png) is the complete
900×420 actual Scene lighting control render captured on2026-10-29 from
unassigned development based on422e0b99. Reproduce it with
`forge_hdr_bloom_editor_tests <scratch> --temporal`. It calls the production
Scene controls and Scene authoring owner with pinned ImGui, SDL software rendering
and Lato. Input tests at100/150/200 percent in two window sizes cover enabled,
stability, range rejection/recovery, reset, Undo and authored Play locks. The PNG
is copied unchanged, without painted controls, retouching or cropping. Its SHA256
is `e6d236f479abc5f3317d8196a692ef277b2cd564963698b4d8051d042f3ea01e`. This capture demonstrates controls, not full Windows editor or physical
GPU acceptance; software Vulkan rendering evidence is recorded separately.

[Game image](../../../manual/images/temporal-antialiasing.png) is the complete
640×360 Gallery102 rendering from the actual shared Game renderer and Runtime
animation owner. The original test target is `forge_temporal_filter_vulkan_probe`;
its Gallery fixture also renders360×640 and a second animated pose. The capture
uses corrected dynamic history bindings and the final corrected native test source. It uses the
selected software Vulkan/DXC stack. The PPM-to-PNG conversion preserves all RGB
bytes; the canonical PNG SHA256 is `2ba29f20b6b920dfab9b73624ae7939ff15345117c198a9b1f601388bf5124b9`. This is rendered Game evidence,
not a physical GPU, full Windows desktop or graphical exported-EXE acceptance.

## Component animation fields

[Timeline controls](../../../manual/images/animation-component-fields.png) is the
complete1080×1300 actual Timeline render captured locally on2026-10-28 from
unassigned development work based on4df8137. Reproduce it with
`forge_animation_property_editor_tests <scratch> --capture`. It calls production
Timeline/source/history owners with an admitted native struct/array test component,
actual ImGui, pinned SDL software rendering and Lato. Real pointer input opens
Component fields and selects a saved key. The image shows indexed Settings leaves:
weight0.5/cubic/outgoing0.6, a Boolean toggle, signed-min64, an exact unsigned-max64
struct leaf, Reverse and declared array flag controls. The complete canonical PNG
is copied unchanged with no painted controls or overlays. Its SHA256 is
`72f1930a4faf93ec8aeb943b8a3a5e7eeee0c652d335993dcba7784d0e8dd045`.

Actual input tests cover root/nested/indexed typed values, enum/flag choices,
curves, Undo/Redo, publication and Play locks at100/150/200 percent in two window
sizes; copied sequence preview samples without project callbacks. This is a local
SDL software capture, not full Windows editor, physical GPU or exported graphical
acceptance. The shared SDL/ImGui fixture also reproduces the existing Sprite
Inspector PNG byte for byte.

## Sprite frame filtering

[Controls](../../../manual/images/sprite-mip-filtering-controls.png) is the complete
760×1000 native Inspector render from `tests/sprite_editor_tests.cpp --capture`,
captured locally on 2026-10-28 from work based on d33c848. It calls the production
Inspector with actual reflected fields, ImGui, the pinned SDL software renderer
and Lato. An unassigned Material isolates the controls in this fixture. Separate
input tests exercise mip selection, Undo/Redo and live authored isolation at
100/150/200 percent. The PNG is copied unchanged from the actual renderer.

[Game image](../../../manual/images/sprite-frame-filtering.png) is the complete
640×360 Gallery99 Game output from `tests/sprite_mip_gallery_render_tests.hpp`
through Engine/Scene, RuntimeSimulation, resource workers, FrameRenderer and the
actual display pipeline. The pinned Diligent Vulkan backend used local llvmpipe;
portrait 360×640 output was also visually reviewed. Conversion from the original
P6 capture to PNG preserves every RGB byte. No overlays or controls were painted
onto either image. These captures establish local software rendering and control
layout; Windows/full editor, physical GPU and graphical export acceptance are
recorded separately.

## Distance fog

[Controls](../../../manual/images/distance-fog-controls.png) is the complete
900×360 actual Scene lighting control render from `tests/hdr_bloom_editor_tests.cpp`
with `--fog`, captured 2026-10-28 from local distance-fog work based on4038249.
The fixture calls the production control function with real ImGui/SDL software
rendering and Lato; separate real input tests exercise the full Scene lighting
owner, scaling, scrolling, history, rejection/recovery, timestamped Problems and
Play locks. This is not a full Windows editor screenshot or hardware acceptance.

[Game image](../../../manual/images/distance-fog.png) is the complete 640×360
Gallery98 Game output. Its original Scene loads through Engine/Scene/
RuntimeSimulation, FrameRenderer and MeshResourceHost with the existing shared
Material. Pinned Diligent Vulkan/DXC on llvmpipe LLVM15 software rendering supplies
actual pixels. The source P6 is losslessly encoded to PNG with identical RGB;
there are no pixel edits. The separately reviewed 360×640 output retains the
fixed16:9 camera's complete view with black letterboxing. All five complete
fog/exposure images are compared with a relocated source-free runtime package.
The same native gate checks custom-wrapper alpha/diagnostic pixels, graph source
compilation and independent numeric radial fog. It does not load D3D12 Shader
cooks on Vulkan or establish physical performance. Each image has one canonical
manual asset; local validation records exact hashes, source and review evidence.

## Packed Sprite fills

[Packed fills](../../../manual/images/packed-sprite-fills.png) is the complete
640×360 actual Game output of original Gallery97, captured2026-10-28 from local
work based onf15bc6f. `tests/sprite_fill_gallery_render_tests.hpp` uses normal
Engine/Scene/RuntimeSimulation realization, FrameRenderer, MeshResourceHost and
the ordinary original/packed Texture/Material assets. GCC12 Release/Linux and
pinned Diligent Vulkan/DXC run on llvmpipe LLVM15 software rendering. Top arrows
use Sliced fill; bottom markers use Tiled fill with both logical flips. Originals
are left, trimmed/clockwise versions right. Fixed16:9 camera aspect preserves the
same complete artwork with letterboxing in the separately reviewed portrait image.

The whole image is compared against an independently composed all-original-image
reference with identical geometry and fill values. Wider native tests cover both
filters, all flips, rotated/trimmed/clear canvases, border compression, partial
tiles, direct draws and mixed two-instance data. Source-free relocation uses
logical Scene IDs rather than original source filenames. The P6 RGB capture is
encoded losslessly as PNG with exact decompressed RGB verification, without crop,
scale, retouch or inserted controls. The canonical PNG is copied unchanged and
visually reviewed; exact source/build/image hashes belong in the local receipt.
This is Game output, not an editor-window capture or Windows/physical acceptance.

## Automatic exposure controls and Game adaptation

[Controls](../../../manual/images/automatic-exposure.png) is a complete900×900
native FORGE Scene-lighting control capture from2026-10-28, based onf6bb9ce with
the automatic-exposure batch applied. `tests/hdr_bloom_editor_tests.cpp` renders
the real owner with pinned Dear ImGui/SDL3 software rendering, the product Lato
font and GCC12 Release/Linux. The actual input suite separately covers all fields,
percent conversion, Undo/reset, rejected-value recovery, timestamped Problems,
Scene Save ownership and Play locks at1/1.5/2 scales and two window sizes. This
focused control fixture is not a full Windows Main window or physical acceptance.

[Transition](../../../manual/images/automatic-exposure-transition.png) and
[settled image](../../../manual/images/automatic-exposure-settled.png) are complete
640×360 actual Game images from original Gallery96 through FrameRenderer,
MeshResourceHost and its normal held light-intensity clip/runtime simulation.
`tests/auto_exposure_vulkan_probe.cpp` and its gallery header use the pinned
Diligent Vulkan/DXC path on llvmpipe LLVM15 software rendering. The first is the
bright-light transition; the second follows two seconds of exposure adaptation
with the bright state held. Relocated source-free content produces exactly the
same three initial/transition/settled RGB captures as the source project.

All controls and pixels were visually reviewed as complete images. The control
PNG is copied unchanged. Game P6 RGB is encoded losslessly as PNG with exact
decompressed RGB verification, without crop, scaling, retouch, inserted controls
or generated screenshot content. Canonical assets live only in `manual/images`.
These captures do not establish Windows/D3D12 execution, physical GPU performance,
physical input acceptance or a new numbered delivery. Exact hashes/build commands
and review evidence are retained in the local automatic-exposure validation receipt.

## Sprite atlas options and review

[Atlas options](../../../manual/images/sprite-atlas-options.png) and
[Atlas review](../../../manual/images/sprite-atlas-review.png) preserve complete
1080×800 native SourceImport modal captures from the local frame-metadata work
based on7d5db0e6c62bc09d223117952ef2eaf339d6caaf, captured2026-10-28.
`tests/sprite_atlas_editor_tests.cpp` uses GCC12 Release/Linux with pinned Dear
ImGui and SDL3 software rendering. The actual mouse controls enable transparent
trimming and stored quarter turns, then prepare original Badge/Panel images
through FORGE's supervised worker and normal source-import owner. Checks cover
1/1.5/2 scaling, review/Back, lock, copy/publication, clipboard and cancellation.
The review is a wire layout, not a color image preview. Complete PNG bytes are
copied without crop, resize, retouch or generated controls. Final hashes and
separate visual-review evidence are recorded in the local validation receipt.
- sprite-atlas-options.png SHA256: `77edaa7447e5dad893772df45492ceed7157e6c983ad999c68ba3580f940789a`.
- sprite-atlas-review.png SHA256: `9ceaeb7b5bb2fc98ef4869098826820cc6b6e21a332eaf043a6747027a7a98a7`.
- sprite-trimmed-frames.png SHA256: `8f3688d0d598f41a3c887d2025e8952241ff2f767fb3aeefa3b6d31cfd14a711`.

These are actual editor-control fixtures, not full Windows Main, physical GPU/
input acceptance or a numbered delivery.

[Trimmed canvases](../../../manual/images/sprite-trimmed-frames.png) is the complete
640×360 Game output from original Gallery95 through the actual FrameRenderer/
MeshResourceHost and published ordinary Texture/Material selections. The source
is `gallery95-frame0-wide.ppm` from the final `forge_sprite_vulkan_probe` run,
GCC12 Release/Linux, pinned Diligent Vulkan and llvmpipe LLVM15 software rendering.
The P6 RGB capture was encoded as PNG with exact decompressed RGB equality; no
crop, resize, pixel retouch, UI insertion or generated scene art was applied to
the capture. Scene source PNGs are original deterministic FORGE fixtures. Separate
direct/instanced original-versus-packed pixel checks cover all flips, both filters,
transparent canvases and stored orientation. Source-free content relocation and
last-good material/bounds checks are separate evidence. This image is Game output,
not an editor-window capture or physical GPU/Windows acceptance.

## Local controller assignments and sensors

[Assignments](../../../manual/images/gamepad-assignments.png) and
[Motion sensors](../../../manual/images/gamepad-motion.png) are complete600×450
native FORGE GameInput captures from2026-10-27, based on
7bf6b3acf2a5992263339b1cf6bcfb2d7e89ad6f with the local raw-motion batch applied.
`tests/multiple_gamepads_ui_tests.cpp` uses GCC12 Release, Linux, the pinned
Dear ImGui SDL3 software renderer and four actual SDL virtual controllers with
native sensor capabilities/rates. The same real widgets pass mouse sensor toggle,
Tab/Space keyboard toggle, occupied-slot assignment and1/1.5/2-scale layout.
The validation/capture receipt records exact fixture/owner hashes and images.
These are isolated editor-control fixtures, not full Windows Main or physical
controller/GPU acceptance. Original/canonical PNG SHA256 values:

- gamepad-assignments.png: `f4ab28ab2f41807f8123aa9bbcd7bd4267631709e376d75542a113e510d2d5eb`
- gamepad-motion.png: `56c497f3120183359449c98913a1db8ea48680f5c3dc432106ed3e6faef6e6b8`

Full original PNG bytes/dimensions are preserved, with no crop, resize, retouch,
generated controls or duplicate canonical image asset.

## Ordered Sprite frame regions

[Frame regions](../../../manual/images/sprite-frame-regions.png) preserves the full
640×360 actual Game render of original Gallery87. Captured2026-10-27 with
`tests/sprite_vulkan_probe.cpp` and `tests/sprite_gallery_render_tests.hpp` from the
local frame-bank work based onbbf949976dc4a260bb3187c20ea088882aeed8b9. The exact
validation receipt records the final source hashes. Configuration: GCC12 Release,
SDK OFF, Linux software Vulkan/llvmpipe LLVM15.0.7; the full8 landscape/portrait
frames and the real Image/Material/FrameRenderer path passed. Three representative
wide/tall images were visually inspected; other images have numerical checks.
Original readback `gallery/gallery87-frame0-wide.ppm` SHA256:
`c6d46fcaecda8f526e56d59dcf8526a0aed06e9edc5a1db69d3a452ccad5c0ce`. Canonical PNG SHA256:
`2329caf8a6e249faaa7427bbf0d8247eff28352345a005cf6c66e57cc7dae83a`. Lossless container conversion
preserves RGB bytes/dimensions; no crop, resize, retouch or generated content.
This image explains the Game result, not native Inspector controls. Windows/Main,
physical GPU/input and an authentic new Frame regions Inspector capture remain
pending. No numbered package or physical acceptance is implied.

## C++ 2D movement

[Movement](../../../manual/images/2d-movement-runtime.png) preserves the complete 1280×720 exported-game
capture from source `dfe0a6555beec36c4d25014be16909325f7decac`,
[Build261022-000204](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/39761476803),
artifact `FORGE-Physics-Loads-Example` (`12422715584`), entry
`movement-2d/movement-2d-jump.ppm`. Original PPM SHA256:
`9a7f427d9839d2c9705eda3f7307440132cbc519ece5d3de77686ce52a9434c3`.
The fixture observes the actual exported GameSession and injects SDL keyboard
events. It does not write body state or advance a test clock. The authored copy
and temporary runtime kit were deleted before production startup and relocated
game execution. Walking, painted support, a 1.822136 m jump, held-jump suppression
and all three rest/jump/landed captures pass; those images were visually reviewed.
The PNG is lossless, with no pixel or dimension edits. D3D12 WARP execution is
separate from physical GPU performance or manually operated desktop acceptance.

## Native Tile Map render

[Tile Map gallery](../../../manual/images/tile-maps.png) is a 1280×720 native Game rendering of
`Scenes/74-tile-maps.scene.json` using Linux software Vulkan. It shows the two
actual map instances, shared original arrow image, reflected transform and layer
markers. `tests/tile_gallery_render_tests.hpp` captures this output and checks
projected cell colors at two resolutions. The PPM readback was encoded as lossless
PNG with its pixels and dimensions preserved. Windows execution is a separate gate.

## Native Tile Map authoring controls

[Tile Map authoring](../../../manual/images/tile-map-authoring.png) preserves the complete 1280×800
Windows D3D12 WARP capture from source
`0b9397cf1852fa1db71f4ddcc9040fdc2984365f`,
[full editor audit](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/39519971030),
artifact `FORGE-Editor-Source-Audit` (`12314486767`), entry
`grid-test-images/tile-editor/tile-document-100.ppm`.
The fixture draws the real Tile Map document, native preview and Inspector using
original Gallery74 data; it stages those two panels rather than recording a
manually operated desktop session. The PPM was encoded as lossless PNG with all
pixels and dimensions preserved. Captures at 100/150/200 percent were visually
reviewed for visible controls, canvas and complete cell-dimension labels.

## Windows editor captures

These are actual FORGE Windows editor captures. The original full-window pixels
and dimensions are preserved; PPM captures were encoded as lossless PNG without
cropping, retouching or generated interface elements.

## Capture sources

**Editor workflows:** source
`1465a511d61257096ae39afc39fecb92e2780c63`, successful
[Windows source audit](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/39084122081),
artifact `FORGE-Editor-Source-Audit` (`12121764898`). These workflows are also
included in the fully verified Build261018-000190.

**C++ editor and live properties:** source
`84553c800d3a6dbf636532a9fad99d840b3dc1f3`, successful
[Build261018-000190 validation](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/39095070659),
artifact `FORGE-Editor-SDK-Acceptance` (`12126140875`).

| Screenshot | Original artifact entry |
| --- | --- |
| [Scene editor](../../../manual/images/scene-editor.png) | `grid-test-images/editor/editor-default-scene-inspector.ppm` |
| [C++ editor](../../../manual/images/cpp-editor.png) | `onboarding with spaces/editor-55-cpp-code-selected.ppm` |
| [Material graph](../../../manual/images/material-graph.png) | `grid-test-images/editor-workflow/editor-258-material-graph-workspace.ppm` |
| [Animation states](../../../manual/images/animation-states.png) | `grid-test-images/animation-root-motion/editor-animation-state-graph.ppm` |
| [Mesh sculpting](../../../manual/images/mesh-sculpting.png) | `grid-test-images/editor/editor-editable-mesh-sculpt-symmetry.ppm` |
| [Camera sequencing](../../../manual/images/camera-sequencing.png) | `grid-test-images/editor-workflow/editor-562-camera-shots-timeline-100.ppm` |
| [Live Inspector](../../../manual/images/live-inspector.png) | `onboarding with spaces/editor-136-cpp-rotator-live-tuned.ppm` |

The captures use D3D12 WARP software rendering. Status-bar timings and diagnostic
counts belong to the validation fixture and its current test stage; they are not
physical graphics-card performance measurements. Some fixture stages retain
diagnostics from intentional failure checks. The small scenes demonstrate editor
workflows rather than finished game artwork. Sample-content attribution remains
in the [Feature Gallery](../../../samples/projects/FeatureGallery/README.md).

## 2D physics Game render

[2D physics](../../../manual/images/2d-physics-runtime.png) is a lossless PNG encoding of
`grid-test-images/physics-2d/gallery75-tick180-1280.ppm` from full Windows run
39559873949, source `984ce7d61b24c2a70abf79666b0b5be5f2d6bf04`. The actual Game
renderer's six 2D captures passed pixel/pose checks. That candidate's Scene guide
fixture and a separate image-export walkthrough failed, so the whole run is not
accepted. This image documents the qualified Game render only. Original PPM
pixels are unchanged; no generated art, overlays or semantic edits were applied.

## Painted Tile Map collision

The retained Inspector capture below displays the old **Solid layer** label.
The current selector is **Collision layer** and supports Static/Kinematic solid
and sensor Maps. The old Inspector image is omitted from the current manual and
README; keep its original pixels as historical provenance. A matching native
Windows recapture is pending in the canonical agent review queue. The Game capture
still represents unchanged Gallery77 and remains in use. No image was retouched.

[Game](../../../manual/images/painted-collision-game.png) and [Inspector](painted-collision-inspector.png)
preserve the complete 1280×720 and 1280×800 captures from source
`27638989a1e506b9766dd1604a62aed38d1ce186`,
[Windows audit39698636222](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/39698636222),
artifact `FORGE-Editor-Source-Audit` (`12396948531`). Original entries:

- `grid-test-images/physics-tiles/gallery77-tick180-1280.ppm`
- `grid-test-images/physics-tiles/editor/physics2d-editor-100.ppm`

The native `editor_render_physics-tiles` and `physics_tile_map_editor` cases passed.
All six Game captures at ticks 0/60/180 and widths 640/1280, and the three Inspector
captures at 100/150/200 percent, were visually reviewed. The actual Game renderer
simulates Gallery77; the Inspector fixture stages production controls and the
readonly published-layer outline without creating an authoring solver.
These are software WARP checks, not physical GPU or manually operated desktop
acceptance. This audit failed its separate Save-image and sequence-frame input
walkthroughs, so these images qualify the Map captures only. The original PPM
pixels and dimensions are unchanged in their lossless PNG encodings.

The corrected [full source audit](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/39712829120)
at `375b9415df5bd70df7ed129481d9922f23e67b82` passes all 326 editor checks,
including both walkthroughs, and two standalone-only checks. Rendering, Inspector
and Gallery code are byte-unchanged from the captures attributed above.

## Native 2D character feedback

[Inspector](../../../manual/images/2d-character-inspector.png) preserves the complete 1280×800 capture from
source `0d8489c13ba9cd7acbedc642695ac28d55b6d785`,
[Build206](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/39822638412), artifact
`FORGE-Viewport-Checks` (`12451549767`). Original entry:
`grid-test-images/physics-2d/character-editor/physics2d-editor-100.ppm`.

The fixture uses the actual RuntimeWorld/controller before rendering the Scene and
production Inspector controls. Ground, clipped velocity and blocked placement are
observed after real fixed movement and a genuinely rejected placement. All three
100/150/200% captures were reviewed, alongside three actual source-free Character2D
game rest/jump/landing captures. The complete gate passes 326 editor checks and the
combined-package C++ workflow. This is a native fixture using software WARP, not
physical GPU or a manually operated whole desktop. PNG encoding is lossless.

## Sprite image regions

`sprite-image-regions.png` is a lossless conversion of
`grid-test-images/sprites/gallery/gallery78-frame0-wide.ppm` from the successful
[focused Windows shader/native Sprite audit39857502853](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/39857502853).
Source50e311ada199b2dbfb257d8092513dc53ce5e704, Windows/D3D12WARP,640×360.
This is a rendered Game capture using original FORGE Gallery78 content, not a
whole-editor interaction screenshot or physical GPU performance evidence. No AI
image generation or retouching. PNG SHA256: `ded497df30638e030ad255eb89077325535d36ec3b54011cacc301ddb2c109c4`.

## Manual workflow illustrations

The manual shares the existing canonical PNGs in `manual/images` with the README.
The following additional complete Windows editor captures come from compiled source
`8200738a7055decfafae14b0705c1406cb8ef20f`, Build261024-000213,
[run40083004201](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/40083004201),
artifact `FORGE-Viewport-Checks` (`12558961977`). All336 editor checks passed.
These are D3D12 WARP fixture captures, not physical GPU measurements. Test content
and diagnostic examples belong to the fixture. PPM-to-PNG encoding preserves every
pixel and the full dimensions. No generated or retouched interface content is used.

| Manual image | Original artifact entry | PPM SHA256 |
| --- | --- | --- |
| [project-settings](../../../manual/images/project-settings.png) | `grid-test-images/editor/editor-project-settings.ppm` | `9503e66cbc3bd46ce4ba04057f5cd4b53d508a3d543c1e1ddea898dc7dcf1de4` |
| [prefab-source](../../../manual/images/prefab-source.png) | `grid-test-images/editor/editor-prefab-source.ppm` | `8cb4849c6ff65c39e2f435222284fbc79e5a69347860a405ed7fa8f2a4642c35` |
| [texture-import](../../../manual/images/texture-import.png) | `grid-test-images/editor/editor-texture-import.ppm` | `f57e75aa247cfaa7a6370b515fb4d7b6f0fea83636c4da3ca20a3f9328dd5cff` |
| [scene-lighting](../../../manual/images/scene-lighting.png) | `grid-test-images/editor/editor-scene-lighting.ppm` | `23bc7a69e6a623f72a7d26f6d8af8e688813f05bf90db91e3f543ea2c567bc17` |
| [audio-preview](../../../manual/images/audio-preview.png) | `grid-test-images/audio-preview/editor-audio-clip-preview.ppm` | `1adeeede962bb3ef853f2fa11339406982756131758ec5cbd988b3d1a6d92fc4` |
| [material-editor](../../../manual/images/material-editor.png) | `grid-test-images/editor/editor-material-editor.ppm` | `d3dcfbd6fd1a7855dd1b0cfb97904dc195c033b7522fcc68940e026c0f97427e` |
| [model-import](../../../manual/images/model-import.png) | `grid-test-images/editor/editor-model-preview.ppm` | `a79a1ec790281f2f3c2bf2c7c4aec037d45bbff14a3e747cc62af415b414e9c6` |
| [physics-body](../../../manual/images/physics-body.png) | `grid-test-images/editor/editor-physics-body-motion-tuning.ppm` | `c4458f895264a2d7f43623bb197e3b81f6b13fcb7a29ea6f45fc0a7c7fe6a281` |
| [shader-import](../../../manual/images/shader-import.png) | `grid-test-images/editor/editor-surface-shader-import.ppm` | `fd4fe2f7d75e71312f019f98921c3d79e63e029c55457ecb01c4bcd8b4a883a4` |
| [flecs-script](../../../manual/images/flecs-script.png) | `grid-test-images/editor/editor-flecs-script.ppm` | `cb5f1a310771aad11263d032c42863b7a028621a13f76c47126292ec006186f7` |
| [loaded-resources](../../../manual/images/loaded-resources.png) | `grid-test-images/editor/editor-loaded-resources.ppm` | `badc86ce95ab4ae2831d3c07814f378515b17fe2307160144aa3d24634b4f1ec` |
| [problems](../../../manual/images/problems.png) | `grid-test-images/editor/editor-problems.ppm` | `94a26da0a1dc52e1f490d88225397afa3ca839241f82411cd5b52815a9966fa1` |
| [command-palette](../../../manual/images/command-palette.png) | `grid-test-images/editor/editor-command-palette.ppm` | `7b54192728c94fa640b055db4b1a798d5cd0ee1520ed07549e2d1849ffd7788e` |
| [timeline-curves](../../../manual/images/timeline-curves.png) | `grid-test-images/editor/editor-timeline-curves-view.ppm` | `6f8d5bfd45cb62a55319f11854e3a211c1267b277a0a7f05799ed4f2a6f48621` |
| [content-browser](../../../manual/images/content-browser.png) | `grid-test-images/editor/editor-content-thumbnails.ppm` | `492bed9388a325e1face971c6075b6984791c17d9e3753192903f8c50afc6f21` |
| [ecs-statistics](../../../manual/images/ecs-statistics.png) | `grid-test-images/editor/editor-ecs-statistics.ppm` | `1def0de1df8afe16bf861ac2714084ae0095c08ab892ac8399c018fa0295cf6f` |
| [component-inspector](../../../manual/images/component-inspector.png) | `grid-test-images/editor/editor-custom-component-inspector.ppm` | `0afb88f80d538b38ac158091ca56f6387f5bee840592381833717fa4bc59c522` |
| [animation-blend-space](../../../manual/images/animation-blend-space.png) | `grid-test-images/animation-root-motion/editor-animation-blend-space.ppm` | `dc24f15c9d782777c8bcd3742a8ac9b91bce1d91c1c85e3a118813986ad438ea` |
| [animation-rigging](../../../manual/images/animation-rigging.png) | `grid-test-images/animation-root-motion/editor-two-bone-ik.ppm` | `cd3db9d6d6d58607a4302643d7dbc19563d8300ec0553d0c9eb803de2b3d172d` |
| [animation-root-motion](../../../manual/images/animation-root-motion.png) | `grid-test-images/animation-root-motion/editor-animation-root-motion.ppm` | `361806dd5d7cc3f28e1f4482bf7fcba764ad30a0332df0b571ff35014af9b638` |
| [animation-playback](../../../manual/images/animation-playback.png) | `grid-test-images/animation-root-motion/editor-live-animation.ppm` | `dca2c0531320e46c0e395052e21d40ebee0918c2eafa9c2d6998c1361245a2fd` |
| [input-contexts](../../../manual/images/input-contexts.png) | `grid-test-images/editor-workflow/editor-622-input-context-authoring.ppm` | `7499b4994803f38b7d644711b7fe3b59cc14a92b781a0cec58000f2eacd7e035` |

## Animated runtime UI

[The canonical game UI image](../../../manual/images/runtime-ui-animation.png) is
the unretouched800x600 output of tests/ui_render_tests.cpp and
ui_animation_render_tests.hpp using Gallery100's actual RML/RCSS, the existing
UiPresenter/Diligent adapter and pinned Lato font. The Linux forge_ui_vulkan_probe
uses software Vulkan/lavapipe; source branch forge/ui-property-animation, based on
54afb6a. The displayed tick24/paused model belongs to the fixture; CPU owner tests
separately prove paused/zero-rate gameplay isolation. This is an actual game UI
render, not a full editor/Windows/physical-GPU acceptance screenshot.

The source frame is ui-animation-800-late.ppm at presentation time0.75s. Lossless
PNG encoding preserves all RGB pixels and dimensions. Wide/portrait early/late
captures and pixel-readback checks are recorded in local validation; only one
canonical image is published. PNG SHA256: `37ff672c13d5ae76fbdea5f6ccf4cba0492db2ed6277e68adecfff9a4796836f`.

## Tile collision Inspector

[The canonical Tile Set image](../../../manual/images/tile-collision-profile.png)
is the complete unretouched760x1000 PNG produced by
`forge_tile_set_editor_tests <capture-directory> --capture`, using
`tests/tile_set_editor_tests.cpp`, the actual TileSetEditor/Inspector,
`ImGuiSoftwareCapture`, pinned ImGui/SDL and the ordinary Lato font. The staged
Source2 fixture contains a named convex ramp with three unit-cell XY/up points.
Source branch forge/tile-collision-shapes is based on76c589c. Independent input
fixtures cover Apply/Discard, invalid contours and Undo/Redo at three scales and
two layouts. This Linux software-rendered Inspector capture does not establish
full Windows editor, physical GPU or OS-input acceptance. No controls or pixels
were retouched. PNG SHA256: `20f28ca05402bad13ae62da6ed2819a63b98de483f0938a9a8888c83e67e0c0d`.

[The canonical collision course image](../../../manual/images/tile-collision-gallery.png)
is the1280x720 starting frame from `tile_gallery_vulkan`,
`tests/tile_gallery_render_tests.hpp`, Gallery101 and the actual FrameRenderer.
Software Vulkan/lavapipe renders the admitted published Sprite/Tile resources at
their saved starting poses. Separate CPU owner tests run the physical course and
cold recovery; this frame itself does not show a performed physics walkthrough.
Both640x360 and1280x720 readbacks check amber ramp interiors, their transparent
halves against the rendered clear background (one UNORM8 step tolerance), the
cyan floor, complete cell count, camera and runtime Map selection. Lossless PNG
conversion preserves every RGB pixel from `gallery101-1280.ppm`; no crop or
retouch was applied. This is not Windows/full editor/physical GPU/graphical EXE
acceptance. PNG SHA256:
`e7d04c02b55101ac78cc97a4810d3cd927c2c1d4b3dc189b6aea39306995d9ce`.

## Depth of field controls

[The canonical focus controls](../../../manual/images/depth-of-field-controls.png)
are the complete unretouched900x440 PNG from
`forge_hdr_bloom_editor_tests <capture-directory> --focus`, using the production
`scene_focus_controls` widget, pinned ImGui/SDL software renderer and the ordinary
Lato font. Source branch forge/depth-of-field starts from9b38df2. The Scene owns
the displayed enabled10m/5m/8px settings. Actual input tests exercise all fields,
reset/Undo, rejection/recovery and authored locks at three scales/two sizes.
This is an authentic widget capture, not full Windows/Main, physical GPU or
OS-input acceptance; no controls or pixels were retouched. PNG SHA256:
`fe7f3bd35aa57c353947d50a0fcc83a74c8105ee228daf4c356a2125b500ec1c`.

[The canonical focus Gallery image](../../../manual/images/depth-of-field.png)
is the complete640x360 RGB readback from `depth_of_field_vulkan`,
`tests/depth_of_field_scene_tests.hpp`, RuntimeSimulation and the actual shared
FrameRenderer. Original Gallery103 has192 colored cells at three depths. Wide
and tall captures compare enabled/disabled views, preserve alpha and verify that
the middle focus plane remains exact while outer planes change. Lossless PPM to
PNG conversion preserves all RGB bytes; no crop/retouch. Software Vulkan/lavapipe
source evidence does not establish full Windows editor, physical GPU or graphical
exported-game acceptance. PNG SHA256:
`75c2d61a8b749c0bb8001f221205929714571cb54b07e00caf914c01096ac0bb`.

## Motion blur controls and Gallery

[The canonical controls](../../../manual/images/motion-blur-controls.png) are the
complete unretouched900x400 PNG from
`forge_hdr_bloom_editor_tests <capture-directory> --motion`, using the production
`scene_motion_controls`, pinned ImGui/SDL software renderer and ordinary Lato font.
Scene owns enabled/shutter0.5/trail32. Input tests separately exercise all fields,
Undo/reset, rejection/recovery and Play locks at three scales and two layouts.
Source branch forge/motion-blur is based on287d0e6. This authentic widget image
does not establish full Windows Main, physical input or GPU acceptance. PNG SHA256:
`3e7a15983d903f7165f0f9647ed4f62b9333eb991d498e9c8a50dfc40c6aa818`.

[The canonical Game image](../../../manual/images/motion-blur.png) is the complete
640x360 readback from `motion_blur_vulkan`, `tests/motion_blur_gallery_tests.hpp`,
RuntimeSimulation and shared FrameRenderer with original Gallery104. The left
checker panel moves in front of the stationary panel; compatible surface detail
softens horizontally. Wide and tall enabled/disabled captures check changed detail
with exact alpha and clear background. The tall output retains the authored fixed
camera aspect. Lossless PPM-to-PNG encoding preserves every RGB byte and dimension;
no crop or retouch. These software Vulkan/lavapipe renders are distinct from
Windows/full editor, physical GPU/input and graphical exported-game acceptance.
PNG SHA256: `9b3804cb3ed5c0252c42233a05b13647c72eb76ffa0d96e9adf1fa5d7630c014`.

## Ambient occlusion controls and Gallery

[The canonical controls](../../../manual/images/ambient-occlusion-controls.png)
show the actual `scene_occlusion_controls` widgets rendered by the rebuilt
`forge_hdr_bloom_editor_tests <capture-directory> --occlusion` fixture. The complete
900×460 SDL software-rendered window uses the actual font/style, with the optional
Normal influence authored to0.7;
controls, labels and pixels are not fabricated or retouched. The interaction
fixture checks three scales and two extents, Undo/reset, rejection/recovery,
Play locks and the real timestamped Problems/document owner.

[The canonical Game image](../../../manual/images/ambient-occlusion.png) is the
complete 640×360 output from shared FrameRenderer and RuntimeSimulation using
Gallery105, Scene `d4b6e7a5-5c2a-4ee0-852d-cb53ffc7319a`. The blue wall, three
amber posts and lower ledge use five explicit lit Mesh renderers with typed
object-local base colors, a built-in white cube environment and direct light.
Shadow maps are disabled. The Scene and all entity identities remain unchanged
when correcting the fixture from implicit unlit blockout to explicit lit surfaces.
The runtime-content test verifies those values after source-free relocation.

Source branch `forge/screen-space-occlusion` starts at `5e10997`. The native
`ambient_occlusion_vulkan` fixture writes wide640×360 and tall360×640 enabled/
disabled RGB images and checks visible contact changes with exact current alpha
and clear background. PNG conversion preserves every source RGB pixel without
crop, annotation, compositing or retouch. The full wide image is the single manual
asset; unchanged controls and captures remain attributable to their owner tests.

Capture uses software Vulkan through the explicit lvp ICD and SDL software UI.
This is authentic implementation/fixture evidence, not a Windows editor desktop,
physical GPU, performance measurement or graphical exported-EXE acceptance.
The bounded spatial sample pattern can be visible near contacts; no temporal
occlusion history or hidden/off-screen geometry is implied. The updated controls
capture comes from branch `forge/normal-aware-occlusion`, based on `26053ad`;
the older Gallery105 image remains a geometric-mode example. Material-normal
refinement changes directions only, not geometry. Full Windows/FXC/cooked D3D12 and physical acceptance remain queued.


## Screen-space reflection controls and Gallery

[The canonical controls](../../../manual/images/screen-space-reflections-controls.png)
are the complete 900×520 output of `forge_hdr_bloom_editor_tests <capture-directory>
--reflection`, using the production `scene_reflection_controls`, actual Lato font,
ImGui style and SDL software renderer. Three scales and two extents exercise all
fields, decimal minimum thickness, Undo/reset, rejection/recovery, Play locks and
the real timestamped Problems/document owner. No controls or pixels are fabricated.

[The canonical Game image](../../../manual/images/screen-space-reflections.png)
is the complete 640×360 shared FrameRenderer/RuntimeSimulation readback of original
Gallery106, Scene `120a1fb2-f8af-4fad-a644-eb598988ffab`. Four explicit lit Mesh
renderers use object-local color/metal/roughness, a built-in white Cube environment
and direct light. Amber and blue blocks reflect on a smooth metal floor; the rough
wall retains environment reflection. Shadow maps are disabled. Wide/tall enabled
and disabled comparisons check visible changes, exact alpha and clear background.
The tall image retains the authored fixed camera aspect and its bars. PPM-to-PNG
encoding preserves source dimensions and every RGB byte, without cropping,
annotation, compositing or retouch. Both workflows were visually reviewed.

Source branch `forge/screen-space-reflections` starts at `5604270`. These authentic
software Vulkan/lvp and software UI captures do not establish full Windows Main,
MSVC/FXC/cooked D3D12, physical GPU/input/DPI, performance or graphical EXE acceptance.
The single-ray visible-depth profile can show gaps near thin edges and misses;
no hidden/off-screen geometry, probes or roughness convolution is implied.

PNG SHA256, controls: `163c7c1acc859ebbf28fae5d2d44ba978ed9822faffde48239c908403ee4f16b`.
PNG SHA256, Game: `9fd4e00824c1b5353dc3c71ffbbb0536002b0224e137ab9999c568163233697e`.

## Local reflection probe controls and Gallery

[The canonical controls](../../../manual/images/local-reflection-probe-controls.png)
are the complete 760×520 output of `forge_reflection_probe_editor_tests
<capture-directory> --capture`. They use the actual Component Inspector, Lato font,
ImGui style and SDL software capture. Actual input checks at three scales and two
extents cover creation, typed map assignment, scalar precision and native history.
No controls or pixels are fabricated.

[The canonical Game image](../../../manual/images/local-reflection-probes.png)
is the complete 640×360 FrameRenderer/RuntimeSimulation readback of Gallery107,
Scene `1f955498-5eaa-46a7-9958-9678072dc24b`. Three lit metal spheres and a metal
floor receive two original imported warm/cool room panoramas. The maps use the
Windows/desktop/D3D12 content target; this capture realizes their texture data on
software Vulkan. Direct light is shared and shadow maps are disabled. Wide/tall
on/off checks preserve current alpha and background. The tall view retains the
authored fixed aspect and its bars. Four final readbacks match lossless PNG RGB
bytes without cropping, annotation, compositing or retouch. The controls and
wide/tall comparison were visually reviewed.

The source branch is `forge/local-reflection-probes`, based on `b421f66`.
These are software UI/Vulkan captures, not Windows/MSVC/cooked D3D12 execution,
physical GPU/input/DPI or performance acceptance. Imported maps do not imply
runtime capture/baking, diffuse irradiance volumes or per-Mesh probe masks.

PNG SHA256, controls: `ebc128da3fb19dd9e3d789c25c3a2afce0347847b74242958aedb00bae832776`.
PNG SHA256, Game: `a4c1778eb28a333456b98b7374162598f10e5da85f95212b75f068b9dc6dbb8c`.


## Material-normal ambient occlusion Gallery

[The canonical mapped Gallery image](../../../manual/images/ambient-occlusion-material-normals.png)
is the complete640×360 FrameRenderer Game output from Gallery108, Scene
`277f080c-b97b-4202-bd31-5cded7e4dd18`, on branch
`forge/normal-aware-occlusion` based on `26053ad`. The test uses the original
procedural RippleNormals PNG, its real imported Windows/desktop/D3D12 texture and
built-in Material cooks, five existing Mesh geometries and saved Normal influence1.
The `normal_occlusion_export_vulkan` fixture invokes the real content exporter and
shared runtime renderer. Its four wide/tall normal/geometric RGB readbacks match
exactly after source-free relocation. Editable normal PNG and Material JSON are
absent from that package; selected cooked content supplies runtime resources.

PNG conversion preserves the complete source PPM RGB bytes, without crop,
compositing, annotation or retouch. Wide normal/geometric and tall normal images
were visually reviewed. The spatial pattern near contacts is visible and remains
a documented limit. The current controls capture shows the actual Normal
influence widget at0.7. The existing geometric Gallery105 PNG remains unchanged.
Both captures use software rendering, not physical-hardware performance,
Windows/D3D12 execution or exported graphical-EXE acceptance. Those reviews remain
queued separately. No temporal AO history or hidden-surface reconstruction is
implied.

## Roughness-filtered reflection Gallery

[The canonical Game image](../../../manual/images/rough-reflection-filtering.png)
is the complete640×360 FrameRenderer/RuntimeSimulation readback of Gallery109,
Scene `03d7f430-77ba-4c78-b75a-ff3a638ea3d9`, on branch
`forge/rough-reflection-filtering`, based on `12ab11f`. Eleven original alternating
amber/blue columns form a contiguous source face above a metal floor with
roughness0.38. The saved Scene enables Roughness filtering and uses existing
built-in assets; it adds no texture or Material cook. The production operator
builds view-owned radiance/coverage and depth-range levels after local probes.

The filtered and point wide captures, filtered tall capture and current controls
were visually reviewed. Filtering visibly softens the reflected column boundaries.
Conservative depth fallback and level selection can leave horizontal bands; this
is bounded screen-space source averaging, not physical reflection convolution.
The tall view preserves the authored fixed aspect and its black bars. Lossless PNG
conversion preserves all source PPM RGB bytes without crop, compositing, annotation
or retouch. The existing controls PNG now includes the real Roughness filtering
checkbox, captured by `forge_hdr_bloom_editor_tests <capture-directory> --reflection`
using actual ImGui, Lato and SDL software rendering. Default filtering is unchecked.

The `rough_reflection_export_vulkan` consumer invokes the existing exporter and
renderer on authored and source-free relocated content. Eight wide/tall filtered,
point and effect-on/off images must match. These are local software Vulkan/UI
captures, not Windows/MSVC/FXC/cooked D3D12, physical GPU/input/DPI, performance or
relocated graphical-EXE acceptance. Those reviews stay queued separately.

PNG SHA256, Game: `16dd7e56b82e34044c2023428d1238eb328aaec8fd964469452e9c5021b73573`.

PNG SHA256, Controls: `71966f3656fb4db8e4ad67f5cb2ab7359e0cecbbbd696f06e36329413b736f2e`.

## Reflection capture controls and import review

The complete1080×900 [capture settings](../../../manual/images/reflection-capture-settings.png)
and [generated source review](../../../manual/images/reflection-capture-review.png)
come from `forge_reflection_capture_editor_tests <asset-worker> <scratch>` on
2026-10-29, local development based on9667417. Actual production Inspector,
ViewImageTask, ReflectionCaptureTask and SourceImport controls are rendered by
the pinned ImGui/SDL software backend with Lato. Pointer input opens settings,
starts capture and reaches the real generated-source review. The test exercises
actual encoding, source copying, worker publication, Scene history and save at
100/150/200 percent; only its native pixel-delivery boundary is recorded.
The separate native capture/workflow checks exercise real software-Vulkan HDR
rendering and source-free relocation. These PNGs demonstrate controls, not a
full Windows desktop, physical display/input or graphical exported-EXE acceptance.
No controls were painted, retouched, cropped or rearranged after capture.

- `reflection-capture-settings.png` SHA256: `7bcdbc13450f2bdfef7cfc9bec8615cc40a68f9d848b42b4dd79f690414ce0f5`.
- `reflection-capture-review.png` SHA256: `c32c1bd0c4ccef13383f1295d6b0a1d775e9e2e42bd162763e4aa11ceed2f56b`.

Capture input source hashes (the normal UI gate passed before this documentation):

- `tests/reflection_capture_editor_tests.cpp`: `3e7cfee72efc1742da102c94dce59032105d98abb29e2d0dd126af1537d7ec97`.
- `src/editor/component_inspector.hpp`: `43929ee879cba949de73bab6035993a2f251b55e7ead45990074cb382a5e1fdc`.
- `src/editor/reflection_capture_task.hpp`: `ed407190a39c6c4b63692d3b5429470297ffcdc2d35ee1db211104765e31a8e3`.
- `src/editor/view_image.hpp`: `c460cd7333b5bdf0b8cdd94bcafb3e9158122b826047c8b23976137be32dd13c`.

## Captured reflection-map Gallery

[Game image](../../../manual/images/reflection-map-capture.png) is the complete
640×360 Game rendering of Gallery110, captured locally on2026-10-29 from development
based on9667417. `forge_reflection_capture_workflow_vulkan_probe`'s
`--prepare-gallery` fixture duplicates the FORGE-owned Scene through SceneDocument,
creates the authored receivers/source panels, and uses the production image-task
native driver and source import worker to capture/import a128square six-face HDR
map. Capture mask1 includes room geometry and emissive panels; Game mask2 hides the
panels and displays the metal spheres. No other engine source or artwork is used.
The original306catalogrecords remain exact; only one Scene and one Texture are
added. This preparation input is the Gallery before110was added; the completed
sample's persistent identities are retained in the repository.

The actual shared Game renderer produces both on/off640×360 and360×640 images,
with35940and11158changedpixels respectively. A source-free export relocates and
reproduces all four images exactly. The source sky check retains0.18HDR radiance
with its display sky hidden. The source-shadow check changes22928HDRpixels. The
PPM-to-PNG conversion preserves every RGB byte and the complete dimensions;
no controls, objects or pixels are retouched. This demonstrates software Vulkan
rendering, authoring and packaging, not Windows/D3D12 execution, a full editor
window, physical GPU/input or graphical exported-EXE acceptance.

Canonical PNG SHA256: `751d0630bf31b8d1a597fc57cc572ee379194f2df50df09ff0c98ac6bbacaa1d`.
Scene identity: `177b3d5b-f5a7-41df-a1f8-f36ab10ac6e0`; map identity: `cb50dced-5d4b-4e7f-9d06-f13fa65ea8fc`.

## Contact-hardening shadows

`manual/images/soft-shadow-light-controls.png` is the production ComponentInspector
rendered by `forge_reflection_probe_editor_tests --capture-light`,1080×900, SDL
software/dummy. `manual/images/contact-hardening-shadows.png` is the actual shared
FrameRenderer output for Gallery111 at640×360, software Vulkan/lavapipe; lossless
PPM-to-PNG conversion preserves every RGB byte. Capture inputs are the local
contact-hardening-shadow batch based onab801e63; the batch commit contains the
Scene, shaders and fixtures. Original captures and receipts remain in local
AgentFiles/build/phase190-contact-hardening-shadows, including
light-controls/soft-shadow-light-controls.png and
gallery-native/1bfdbec6-6000-4ed0-aa86-85930cc09351/RelocatedImages. The controls
capture shows the actual Light labels and the Game capture shows the authored
soft-shadow comparison. Neither is a full Windows editor or physical GPU capture;
no controls/pixels were retouched.

## Rectangular and spherical direct lights

`manual/images/rectangle-area-light-controls.png` and
`manual/images/sphere-area-light-controls.png` show the production
ComponentInspector through `forge_reflection_probe_editor_tests --capture-rectangle`
and `--capture-sphere`,1080×560, SDL software/dummy. The smaller capture window
contains the actual shape-specific controls; no controls or pixels were retouched.

`manual/images/area-light-gallery.png` is the shared FrameRenderer Game output for
Scene112 (`19f69809-0eea-4a0b-a360-dd4b7c90f2be`),640×360, software Vulkan/lavapipe.
Lossless PPM-to-PNG conversion preserves every RGB byte and the full dimensions.
The rectangular highlight on the left and spherical highlight on the right come
from the authored emitters. The sources themselves have no visible mesh.

Capture inputs are the local area-light batch based on2d06b35; its commit contains
the Scene, shader and fixture inputs. Local AgentFiles/build/phase191-area-light-integration
retains the two Inspector captures, gallery-native/05d0c264-c4a2-48ec-b298-83230a542e7b
preparation receipt and four exact authored/relocated wide/tall readback pairs.
These images demonstrate actual controls and software rendering, not a full
Windows desktop editor, D3D12/FXC, physical GPU/input or exported graphical EXE
acceptance. Physical performance is not inferred from software timings.

## Weighted Mesh transparency

`manual/images/weighted-transparency-controls.png` is900×240: the production
Scene lighting Transparency helper rendered by `forge_hdr_bloom_editor_tests
<capture-directory> --weighted`, actual ImGui/Lato controls with SDL software/dummy.
The fixture isolates this section; it is not a full desktop editor capture.

`manual/images/weighted-transparency-gallery.png` is640×360 from the shared
FrameRenderer Game output for Scene114 (`46a2f490-bb77-4a85-92aa-e8d7055fa921`),
software Vulkan/lavapipe with independent blending disabled. Lossless PPM-to-PNG
conversion retains every RGB byte and full dimensions. No controls or pixels were
retouched. Both images were visually reviewed for labels/content and clipping.

Capture inputs are the local weighted-transparency batch based on5074320; its
commit contains the Scene, shaders and fixtures. Local evidence in
AgentFiles/build/phase193-weighted-transparency retains capture-ui-run.log,
gallery-candidate.json, gallery-promotion.json, captures/ and the native regression
logs. The Gallery candidate's four authored/relocated display pairs match exactly
with sources removed. These captures do not establish Windows/MSVC/FXC/cooked D3D12,
physical GPU/input/DPI, full editor or exported graphical EXE acceptance. Software
timings are not physical performance evidence.
