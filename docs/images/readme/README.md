# README screenshots

## Signed Texture image output

[Signed Texture export](../../../manual/images/signed-texture-export.png) shows
the actual720×650 production TextureViewer with Data selected through pointer
input. The original Gallery SignedPair BC5 SNORM pattern, PNG16 mapping note and
Export image button are unretouched. The same fixture clicks the native export
control and verifies16-bit saved samples, absent sRGB and the mapped completion
status across both signed formats and twelve wide/compact100/150/200% layouts.
PNG SHA256: `857609693cd1e70006262734fda2a24c04687d9f8551a84535d4739b9a32cb92`.

Personally reviewed local unassigned source based on88f3947. Original capture:
`texture-signed-image-editor-vulkan/signed-pair-export-wide-100.png`; exact source,
binary and PNG provenance is in
`AgentFiles/build/phase246-signed-block-images/native-capture.json`.
Software Vulkan/ImGui capture is distinct from Windows/full Main, physical
GPU/input/DPI, graphical EXE, external-client or performance acceptance. Preview
filtering/display mapping is independent from the exported numerical samples.

## Foliage wind

[Wind Inspector](../../../manual/images/foliage-wind-inspector.png) and
[Live Wind](../../../manual/images/foliage-wind-live.png) show the actual 720×840
production ComponentInspector in SDL/ImGui software rendering. The authored
controls use native nested-value metadata; the colored Live gameplay panel tunes
the runtime copy. The capture consumer also exercises six layouts at 100/150/200%
and actual Keep Values, Apply to Scene, Undo and Redo input.

[Wind Game](../../../manual/images/foliage-wind-game.png) is an exact, unscaled
640×360 RGB transcode of the production FrameRenderer at simulation time0.25s in
Gallery157 (`053092de-0138-4df7-89bc-fe4fbd22b394`). The left patch has64 moving
plants; the right patch has24 still plants. Plants share the existing original
blade Mesh; only their saved object values differ. Scene/Game software Vulkan
checks include shadows, instancing, deformed picking, frozen/borrowed/explicit
time, temporal AA, mode changes, failed-value retention and four exact displays
after source-hidden export relocation.

All three canonical captures were personally reviewed and are unretouched. Local
unassigned source is based onbc75030 with the Wind batch. Exact executable,
source, original readback and PNG hashes are retained in
`AgentFiles/build/phase245-foliage-wind/native-ui-captures.json` and
`native-rendering.json`. These captures do not establish full Main, Windows /
MSVC/D3D12, physical input/DPI/GPU, graphical EXE or performance acceptance.

## Navigation behavior trees

[Tree editor](../../../manual/images/navigation-behavior-tree.png) is the
unretouched1180×1280 production NavigationBehaviorEditor in SDL/ImGui software
rendering. Real pointer input selects Patrol in the published original Gallery139
tree. All eight nodes, parent/child branches, Sequence type and ordered child
controls are visible. Source and Scene values remain unchanged by capture.
PNG SHA256: `049c35e7b3e5a6bf34573f4a1fddde0a6362439d15816af3791f654e7c1d0a19`.

[Tree Game](../../../manual/images/navigation-behavior-tree-game.png) shows the
unretouched640×360 production FrameRenderer after 90 fixed ticks in Gallery139
(`e363aa80-78a1-43c6-975a-6cbdc39dfc2b`). Cyan follows the tree's navigation intent;
orange is the animated target and green marks the patrol destinations. Four
visibly moving source/source-hidden relocated export display pairs match exactly
on software Vulkan. PPM-to-PNG conversion preserves every RGB pixel.
PNG SHA256: `df4a0c6719043a911f00e2d8bba605d1475b8a3d3efa99e024bd7d8762401e54`.

Both images were personally reviewed. Local unassigned-source captures are based
on8972d9b. Exact source/executable/image hashes and original paths are recorded in
`AgentFiles/build/phase218-navigation-behavior-trees/tree-captures.json`.
These captures do not establish full Main, Windows/MSVC/D3D12, physical input/DPI/GPU
or graphical EXE acceptance.

## Named navigation build profiles

[Profile Game](../../../manual/images/navigation-profile-gallery.png) shows the
unretouched640×360 production FrameRenderer after 90 fixed ticks in Gallery138
(`9105eaa8-0920-4e64-bf3d-9bd2bf8f7392`). The small cyan agent uses default clearance;
the large amber agent uses its independent Large asset. Four exact moving
source-hidden/relocated export displays pass. Lossless PPM-to-PNG conversion
preserves RGB pixels. PNG SHA256:
`618fa17c898e242e65edffb920dc5e747bdb2c0686f7118025c0651f2d693572`.

[Profile controls](../../../manual/images/navigation-profile-controls.png) is the
unretouched1180×760 production ComponentInspector and NavigationTools in SDL/ImGui
software rendering. Actual mouse input selects Large; its saved radius1.8m and the
agent's named NavMesh and Shared group1 are visible. Scene/catalog bytes remain unchanged. PNG SHA256:
`c24405a82bbec654a0b8d4e9770a154f7cdf48fad82dac25a604b0cad256db3a`.

Both images were personally reviewed. Local capture source is based onbae089e
with unassigned build identity. Exact source/executable/frame receipts are in
`AgentFiles/build/phase217-navigation-build-profiles/game-capture.json` and
`controls-capture.json`. These software captures do not establish full Main,
Windows/MSVC/D3D12, physical input/DPI/GPU or graphical EXE acceptance.

## Selected collision navigation sources

[Collision-source Game](../../../manual/images/navigation-collision-gallery.png)
is the unretouched 640×360 production FrameRenderer after 90 fixed ticks in
Gallery137 (`13e8001b-0b7c-4fcd-b8aa-c5dbc3dcf129`). Cyan routes around configured
Circle/Capsule footprints on the left; amber routes around the selected Map layer
on the right. `navigation_collision_gallery_vulkan` checks visible motion and four
exact source/source-hidden relocated display pairs. Lossless PPM-to-PNG conversion
preserves all RGB pixels. PNG SHA256:
`a582497f4c9b9db948e4586b8b466e446e5f7940eee62c85a1bb8df741fcb5b3`.

[Collider Inspector](../../../manual/images/navigation-collider-inspector.png)
is the unretouched 1000×900 production ComponentInspector at 1x after actual mouse
selection of **Shape source → Collider 2D**. Unused rectangle/Points controls are
hidden. PNG SHA256:
`b387f4274e1f64032d7cd9ebc39043149ead1041f5137c33376d43e4d645d47d`.
The same SDL/ImGui fixture checks conversion, source selection/history, outlines,
Map islands, picking/framing and source isolation at 100/150/200% and two sizes.

These current unassigned-source captures are based on 2d5a37f. Exact executable,
image, Scene and original-frame receipts are in
`AgentFiles/build/phase216-navigation-collision-sources/game-capture.json` and
`inspector-capture.json`. Both canonical images were personally reviewed. Game
uses software Vulkan; Inspector uses SDL/ImGui software. This does not establish
full Main/editor, Windows/MSVC/D3D12, physical input/DPI/GPU, performance or a
relocated graphical EXE acceptance.

## Polygon navigation regions

[Polygon Game](../../../manual/images/navigation-polygon-gallery.png) is the
unretouched640×360 production FrameRenderer Game after90fixed ticks in Gallery136
(`99ee18f7-3644-46f1-94ed-610f76e4583b`). Cyan follows the concave floor and amber
routes around the orange obstacle. `navigation_polygon_gallery_vulkan` verifies
visible motion and four exact source/source-hidden relocated display pairs.
Lossless PPM-to-PNG conversion preserves all RGB pixels. PNG SHA256:
`cc6d0361a6696511c18074a2f9097c842cdb3b1d4ba68f87669a32de09625afe`.

[Polygon Inspector](../../../manual/images/navigation-polygon-inspector.png) is
the unretouched1000×900 production ComponentInspector at1x, with Points expanded
through actual mouse input. Six entries and normal collection/conversion controls
are visible. `forge_navigation_region_editor_tests <scratch> <captures>` also
checks100/150/200% at420×640and1000×900. PNG SHA256:
`9a7ac9d2e0d764a8277dcbe9b3323f4ce5f1386128615e4a8d7b6e8dcc03fddd`.

The Game image reuses unchanged attributable source based on 733a50f, with its
original receipt in `AgentFiles/build/phase215-planar-navigation-polygons/captures.json`.
The Inspector image was recaptured from current source based on 2d5a37f to show
the Shape source field; its receipt is the current `inspector-capture.json` above.
Both use the software paths and acceptance limits described above.

## Custom polygon one-way boundaries

[Custom one-way Game](../../../manual/images/custom-one-way-gallery.png) is the
unretouched 640×360 FrameRenderer Game after 90 fixed ticks at 60 Hz in Scene
`f01d3e31-4094-4eaf-bc45-031717a6e13b`. Amber, cyan and rose have landed on the
ring roof, hole floor and island; violet passed upward through the ring to the
ordinary ceiling. The `polygon_one_way_gallery_vulkan` test checks passage/landing
pixels and four exact authored/source-free relocated display pairs.
Original output: `f01d3e31-4094-4eaf-bc45-031717a6e13b-images/authored/90-640.ppm`.
PNG conversion preserves RGB pixels. SHA256:
`e633bfe77caf196376b7e4e1813aa8157506afc549e44890a1f274d3cfe3ed54`.

Capture source is local development based on 459378b with custom-boundary work
and an unassigned build identity. Executable and affected input hashes are in
`AgentFiles/build/phase204-custom-polygon-one-way/captures.json`. Graphics SDK:
`c670461de1435a5ae9a2ce038ac4463bb270e05905eafed7bd7573ed718689f4`.
Both 60/90-tick captures were visually reviewed. This uses Linux software Vulkan;
it does not establish Windows, physical GPU/input/DPI, full-editor or graphical
EXE acceptance.

## Dynamic Tile Map bodies

[Dynamic Map Game](../../../manual/images/dynamic-tile-map-gallery.png) is the
unretouched 640×360 FrameRenderer Game after 30 fixed ticks at 60 Hz in Scene
`c1559f95-5d8d-49ee-9e7f-9616021c8c80`. It shows three independently falling
compound Maps, including reflected and rotation-locked instances. The
`dynamic_tile_gallery_vulkan` test checks moving ring/island/void/oriented-bar
pixel witnesses and four exact authored/source-free relocated display pairs.
Original output: `c1559f95-5d8d-49ee-9e7f-9616021c8c80-images/authored/30-640.ppm`.
PNG conversion preserves its RGB pixels. SHA256:
`c24875d394196eb6a02a4dcd5956337599307cb91adf0d9b19e6cb3e44914847`.

Capture source is local development based on fd73682 with Dynamic Map work and
an unassigned build identity. Exact executable/input provenance is recorded in
`AgentFiles/build/phase203-dynamic-tile-map-bodies/captures.json` and the final
validation ledger. The graphics SDK remains
`e70620514062bb53671217da074ce9208c575362b5cd8a06663872598b85ef72`.
The Game image uses Linux software Vulkan and was visually reviewed. It does not
establish Windows, physical GPU/input/DPI, full-editor or graphical EXE acceptance.

## Tile collision contours

[Contour controls](../../../manual/images/tile-contour-controls.png) is the
unretouched 760×1280 production TileSetEditor Inspector at 1x through the SDL/ImGui
software harness: `forge_tile_set_editor_tests <scratch> --capture-contours`.
It shows the actual contour/loop selectors, numbered boundary, orange hole,
separate island, coordinate table and staged Apply/Discard controls. SHA256:
`b259645cc0e92ee6c4d18fa3310662bf8255b456d822620e108551e88ac8be64`.
Its exact main-cache SDK is
`cc71b03f70d0d6ab33f0255a2e1079a0d26d775119e883edd3b849338419ef76`.

[Contour Game](../../../manual/images/tile-contour-gallery.png) is the complete
640×360 FrameRenderer Game after 90 fixed ticks at 60 Hz in Scene
`ca73e628-15fc-4b3c-b246-2af9852eefb8`. The original transparent atlas matches the
ring, hole, island, disconnected bars and catch-floor collision. The
`tile_contour_gallery_vulkan` test verifies analytic landscape pixel witnesses
and four exact authored/source-free relocated display pairs. Portrait auto aspect
crops the actors and retains the course. PNG conversion preserves original RGB
pixels. SHA256:
`60a2a346c7b56e806d26d809c74b06cedad028cd4d77a9376b682884315487e6`.
Its exact graphics-cache SDK is
`e70620514062bb53671217da074ce9208c575362b5cd8a06663872598b85ef72`.

Both use local development source based on 77892e8 with contour work and an
unassigned build identity. Capture inputs, executable hashes and limitations are
recorded in `AgentFiles/build/phase202-tile-collision-contours/captures.json`.
Game uses Linux software Vulkan; Inspector uses SDL/ImGui software. These are not
Windows/full editor/physical input-DPI-GPU, performance or graphical EXE acceptance.

## Tile neighbor rules

[Neighbor controls](../../../manual/images/neighbor-tile-controls.png) is the
unretouched 760×620 production TileSetEditor Inspector at 1x, scrolled to the
neighbor section through the SDL/ImGui software harness:
`forge_tile_set_editor_tests <scratch> --capture-neighbors`.
Targets, compass conditions, priority and Apply/Discard are actual production
controls. SHA256:
`b816c21c5d9ac2e31d44671cbf6402ce55f4916196ddd7ec5ba8cfabbc89d207`.

[Neighbor Game](../../../manual/images/neighbor-tile-gallery.png) is the complete
640×360 FrameRenderer Game after 90 fixed ticks at 60 Hz in Scene
`437021c5-7fd9-480c-bcb0-ce3a7a350c7a`. It reuses FORGE's original arrow atlas.
The `neighbor_tile_gallery_vulkan` test checks four exact authored/source-free
relocated display pairs. PNG conversion preserves every original RGB pixel.
SHA256: `305483bff88147170c8480a8514dcaead696d4e2d11178bafcdf431dcd7a23ab`.

Both are local development source based on 018d3fa with neighbor rules, unassigned
build identity, exact SDK `ce28b6243d30e40f9377d1e47894022fa73cd774be85941326e0e0f71f008d11`. Capture receipts are retained in
`AgentFiles/build/phase201-autotile-neighbor-rules/captures.json`. Game uses Linux
software Vulkan; Inspector uses software SDL/ImGui. Neither is a Windows editor,
physical input/DPI/GPU, performance or graphical EXE acceptance claim.

## Animated tile sequences

[Animation controls](../../../manual/images/animated-tile-controls.png) is the
unretouched 760×1000 production TileSetEditor Inspector at 1x through the SDL/ImGui
software harness: `forge_tile_set_editor_tests <scratch> --capture-animation`.
The enabled four-frame table, individual durations and Apply/Discard controls are
actual production drawing. SHA256:
`5c3a3390b15a59b37b244aa8a0f16794e8beb2681e87bad77075f1f1f029e4ff`.

[Animated Game](../../../manual/images/animated-tile-gallery.png) is the complete
640×360 FrameRenderer Game after 90 fixed steps at 60 Hz in Scene
`3380e046-21be-4161-82b1-edf2e898ac8d`. Forward, reverse, variable-duration and
static rows reuse FORGE's original arrow atlas. The `animated_tile_gallery_vulkan`
test checks the lane frame order and four exact authored/source-free relocated
display pairs. PNG conversion preserves all original RGB pixels. SHA256:
`0ccd5567ef18172442234699798572beae353c46080479253b870c7d8c6d4288`.

Both are development source based on a86a2e8 with the animated tile extension,
unassigned build identity, exact SDK
`d634f73b60fe724a141a29494e87c70ed98110f98d5148fe8dc83270484db430`.
Capture provenance and hashes are retained locally in
`AgentFiles/build/phase200-animated-tiles/captures.json`. Controls are the shared
Inspector harness; Game uses software Vulkan/lavapipe. These do not establish
full editor, Windows/MSVC, physical DPI/GPU or graphical exported-EXE acceptance.

## Concave tile outlines

[Concave controls](../../../manual/images/concave-tile-controls.png) is an
unretouched760×1100 production TileSetEditor Inspector capture at1x using software
SDL/ImGui. Polygon(decomposed) is applied with six numbered L-shaped vertices and
the actual editable coordinate table. Separate actual selector/typed-input/history
checks cover the existing six viewport/scale pairs.
PNG SHA256:`b2ea44534f89861807e293a5239cc3da21cf655868273a616511ff47df2b1498`.

[Concave Game](../../../manual/images/concave-tile-gallery.png) is the complete
640×360 FrameRenderer Game display after90fixed60Hzsteps, Scene
`0dd80adc-f80b-45a7-81e7-22ca6fb7b6c7`. The original L/U atlas matches the
source collision contours. Amber/rose rest on upper edges; cyan/green rest inside
the notches. PPM-to-PNG conversion preserves every RGB pixel. Four source/relocated
display pairs match exactly with the source offline. The portrait crop has nine
distinct colors, so its scoped flat-color check requires seven; the older textured
course retains its21-color requirement and passes the shared capture helper.
PNG SHA256:`a3505d4e8dd9352ac6608623d12e21b10774f68cb99a73128b68d821ac0c449b`.

Captured31October2026, GCC12Release, local source based onff0033a plus the
concave-tile extension. Game rendering uses software Vulkan/lavapipe. Exact private
source/executable/pixel receipts are in
`AgentFiles/build/phase199-concave-tile-collision`. These are actual shared Inspector
and Game captures, not full docked-editor, Windows/D3D12, physical input/DPI/GPU or
graphical exported-executable acceptance.

## Moving one-way platforms

[Moving one-way controls](../../../manual/images/moving-one-way-controls.png) is
an unretouched820×1060 production ComponentInspector/Meta capture at1x using the
software SDL/ImGui backend. It shows MotionKinematic, Sensoroff, One-way collision
on, margin0.02m and a4×0.2m Box. The source uses shared authoring operations and
Inspector drawing; separate actual selector/Undo/Redo checks cover100/150/200%.
PNG SHA256:`f15e6e6e87a4b5b7a737a0c24f8192b68ec1ca87c488b0d861f61e4f90d2a6a0`.

[Moving one-way Game](../../../manual/images/moving-one-way-platforms.png) is the
complete640×360 FrameRenderer display of Gallery119 after90fixed60Hzsteps. Scene
`39a8c3c8-012d-45e8-9a8c-4510b1a00879` uses the original painted course and
Platform transform clip with a separate inline ledge. Amber is supported above
the moving ledge; reversed-gravity Cyan has passed both surfaces. PPM-to-PNG
conversion preserves every RGBpixel. Four actual source/relocated display pairs
match exactly with source offline. PNG SHA256:`4b3623fe07f61d5695d084563c26806a15608a194eb91ac609e220185b52caff`.

Captured31October2026, GCC12Release, local source based on1d0b306 plus the moving
one-way extension. Game rendering uses software Vulkan/lavapipe. Exact private
source/executable/pixel receipts are in
`AgentFiles/build/phase198-kinematic-one-way-platforms`. These are real shared
Inspector and Game captures, not full docked-editor, Windows/D3D12, physical input /
DPI/GPU or graphical exported executable acceptance.

## Local diffuse irradiance

[Diffuse controls](../../../manual/images/local-diffuse-irradiance-controls.png) is
the complete unretouched1080×900 production ComponentInspector/Meta capture from
`forge_reflection_probe_editor_tests <scratch> --capture-irradiance`,31October2026,
software ImGui/SDL1x. It shows independent Intensity0 and Diffuse intensity1 with
the same map/volume controls. Actual input/history/save/live and diffuse-only guide
checks separately pass six size/scale pairs. PNG SHA256:`4208d463cfaea2e3d2c63a7070a5391bb65a1c0b91171137d2bcf7a113c0871d`.

[Diffuse Gallery](../../../manual/images/local-diffuse-irradiance.png) is the complete
640×360 FrameRenderer display for Gallery118,Scene`180d3ed7-99b8-48ff-a849-40e12ebf04b9`. Existing WarmRoom
and CoolRoom maps illuminate three rounded receivers, two rotated boxes and the
floor without direct lights/global environment. Both probes have separately checked
visible effects; the Scene/two-Texture closure has four exact source-free relocated
display pairs. PPM-to-PNG conversion preserves allRGBpixels. PNG SHA256:
`3e0f9fc41bed523dacd5f2a4df6f88f09149b342eef6273598a511c023459190`.

Both captures use GCC12Release, local source based on257b89b and current diffuse
lighting changes. Exact source/executable/capture receipts are recorded in local
`AgentFiles/build/phase197-local-diffuse-irradiance`. Renderer capture uses software
Vulkan/lavapipe. The first flat-camera composition was replaced in source and
recaptured; pixels were never retouched. Full desktop/Windows/D3D12/input/DPI and
physical GPU acceptance remain pending.


## Light images

[Light image controls](../../../manual/images/light-image-controls.png) is the
complete unretouched 1080×900 ComponentInspector capture from
`forge_reflection_probe_editor_tests <scratch> --capture-light-image`, captured
31 October 2026 using the production Meta/property controls and software ImGui/SDL
fixture at 1x. The picker, directional dimensions and tiling are visible. Actual
input/history/save/live-availability checks separately run at 1x/1.5x/2x and two
fixture sizes. This is not a full docked-editor or physical input/DPI acceptance.
PNG SHA256: `4cdbeb9a79a81c89653edac22051da4803b45298035d53ce6ca711fac9646b40`.

[Light image Gallery](../../../manual/images/light-image-gallery.png) is the
complete 640×360 FrameRenderer display for Gallery 117, Scene
`3c9dbfd3-b805-4dbb-809c-e300b069629a`. It uses existing arrow-sheet, panel and
captured-Cube Textures. Each image has a separately checked visible effect. The
`forge_decal_gallery_tests <Gallery> <scratch> --light-images` fixture checks the
Scene/three-Texture export closure and four exact authored/source-free relocated
display pairs. PPM-to-PNG conversion preserves every RGB pixel; no controls,
geometry, colors or light patterns were retouched. PNG SHA256:
`c12461b09809fd873329ff1cfe9d851cfd4503f77661dffeba3ba91af6c15a38`.

Both captures use GCC 12 Release and local light-image source based on 625e43b.
Exact source/executable/capture hashes are recorded in the local
`AgentFiles/build/phase196-light-textures` evidence. Renderer capture uses software
Vulkan/lavapipe; Windows/D3D12, full graphical editor and physical GPU acceptance
remain pending. The earlier checker/badge composition was replaced in source and
recaptured after visual review; its pixels were not edited.

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
complete 640×360 FrameRenderer display capture for Gallery115,
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

[Gallery image](../../../manual/images/decal-gallery.png) is the complete 640×360
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
native FORGE GameInput captures from2026-11-03, based on
1873f61470623bb8f319c8f8593e3b2c164a97aa with the sixteen-slot batch applied.
`tests/multiple_gamepads_ui_tests.cpp` uses GCC12 Release, Linux, the pinned
Dear ImGui SDL3 software renderer and sixteen actual SDL virtual controllers
with native sensor capabilities/rates. The popup is constrained to the viewport;
these images show its visible top portion and scrollbar. Actual input tests scroll
to Player16, change its assignment, toggle sensors by mouse and Tab/Space, and
transfer an occupied Player16 slot at1/1.5/2-scale layouts. Exact fixture/owner,
executable and image hashes are recorded in the local capture receipt. These are
isolated editor-control fixtures, not full Windows Main or physical acceptance.
Original/canonical PNG SHA256 values:

- gamepad-assignments.png: `e1ad16cb2f7f240e64a86a52bd3609c881137dcacc7962285712ca51bc775016`
- gamepad-motion.png: `1d14b841e40f45fd9f0ae285b2b22271ce375bf4dfe2c3e824161924c89b9a4b`

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
is the complete 640x360 RGB readback from `depth_of_field_vulkan`,
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
is the complete 640×360 FrameRenderer Game output from Gallery108, Scene
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
is the complete 640×360 FrameRenderer/RuntimeSimulation readback of Gallery109,
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

## Reflection capture and rebake controls

The complete1080×900 [capture settings](../../../manual/images/reflection-capture-settings.png),
[generated source review](../../../manual/images/reflection-capture-review.png),
[rebake settings](../../../manual/images/reflection-rebake-settings.png) and
[existing-source update review](../../../manual/images/reflection-rebake-review.png)
come directly from `forge_reflection_capture_editor_tests <asset-worker> <scratch>`
on2026-11-06, local development based on5ef2a5c. Actual production Inspector,
ViewImageTask, ReflectionCaptureTask and SourceImport controls use the pinned
ImGui/SDL software backend with Lato. Real pointer/text input opens settings,
selects the update mode, edits face size and commits the source review.

The native fixture exercises encoding, exact source/settings/catalog guards,
normal worker/publication, retained Texture identity, source-only versus cooked
results, Scene history and save at100/150/200 percent. A deliberate publication
rejection checks that committed source bytes and last-good selection remain
separate. GPU pixel delivery is recorded in this UI fixture. A separate post-write
checkpoint injects a durability diagnostic after the source has committed; it
does not alter successful writes or captured pixels. The separate
software-Vulkan workflow uses actual changed lighting/capture/reimport and exact
source-hidden relocated rendering. These PNGs show controls, not full Windows
editor, physical input/display/GPU or graphical exported-EXE acceptance. Pixels
and dimensions are unmodified; no controls were painted, cropped or rearranged.

- `reflection-capture-settings.png` SHA256: `bb10502161a9e316dfb41ef5a0a4538937dc7953df73ed6af66f30dee0b6888a`.
- `reflection-capture-review.png` SHA256: `4ee5ff710de7c538c8399c916ad4293bf1a3f59ab8317b55aa23d84d5c5dff84`.
- `reflection-rebake-settings.png` SHA256: `7508a1558a5491d64294119916d35ff7a3b94c68586c9f7766b48a3988059cf9`.
- `reflection-rebake-review.png` SHA256: `877bc7f6a28519053a5ca8ef3a60608cee0478192dedb4204f527be85a557d30`.

Capture input source hashes:

- `tests/reflection_capture_editor_tests.cpp`: `391257ac64dfec42beb4bce3db70601c8859a10a5309e5db7dcef5c3938f0955`.
- `src/editor/component_inspector.hpp`: `a62e26065a03b11ace03b75501f5f1502bf5a341f364616f2fdf0b2bbe186968`.
- `src/editor/reflection_capture_task.hpp`: `60972686a84128bf02df4325337c906265e5107840f0e77e5663f93fdc627bd7`.
- `src/editor/source_import.hpp`: `c19d8e285ff5b4bf2f199f01fca6cf87f4c6eeeaedbbf823643ad42dc2184336`.
- `src/editor/view_image.hpp`: `abdb0da569caf49ad718f7d3a7f1cc076d218eeaea35242a04da6edc071bf027`.
- `src/reflection_capture_source.cpp`: `a37e4d97f7a0de869688eb56695fd0dee984acb2cb0a432eb3c66360dcaf56c1`.
- `src/source_copy.cpp`: `0dec5ebfa768abb54055c56aff765b7565fe7637e55d4e7edb371e60b2b831cf`.
- `cmake/mcp_tests.cmake`: `e23daf8bcc05c21e48a0a73f0455c743ceb454e35cd1a2404ad859183fd2ed4f`.


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

## Decoded Tile chunk resource pane

[Loaded resources](../../../manual/images/loaded-resources.png) shows the current
`ResourceInspector` component with a real `MeshResourceHost`, two shared decoded
pages and ordinary Mesh/Material/Texture resources. Source is the bounded Tile
view fixture: five visible cells plus16384offscreen cells, shared by two actual
software-Vulkan renderers. The runtime snapshot supplies logical Map data outside
the presentation Map pool, explaining its zero root-resource counter here.

Capture uses the actual current ImGui component, FORGE style and packaged
LatoLatin-Regular font. An operational rasterizer records its unmodified draw
output at900×740; lossless PPM-to-PNG conversion preserves all pixels. This is a
component layout illustration, not full editor interaction, native UI GPU, physical
Windows/DPI or performance acceptance. No generated or retouched controls/text.
Local capture source, commands, input hashes and limitations are recorded under
`AgentFiles/build/phase205-tile-map-streaming/resource-capture-proof.json` outside
the product tree. The screenshot updates the canonical image used by the manual.

Raw PPM SHA256: `ee49bbfaba823e32e91bf380ef61c37f40e39ac54b0a38200625bc492b02ef43`.
PNG SHA256: `c05a84f04b0aaea18562025492cfc00875bfd2020563181cb872c72973ee5581`.

## Tile chunk residency Gallery

`manual/images/tile-chunk-residency-gallery.png` is the unmodified640×360 Game
capture at tick0 of Gallery126 from `tile_residency_gallery_vulkan`. The actual
FrameRenderer/Scene Viewport check visits five regions and loops back, confirms
Scene retained redraw/picking/full logical framing and two coalesced chunk pages
per stopped view, and compares all12Game/Scene images exactly after relocation
with source files offline. Current source Scene explicitly excludes the camera's
legacy primitive; the published Map/Clip and all prior asset IDs remain unchanged.

The canonical PNG is shared by README and manual. This is actual software Vulkan
rendering of original FORGE content, not a full-editor interaction capture,
physical Windows/GPU/DPI acceptance or performance result. No retouching or image
generation. Local log: `gallery-camera-native-final-test.log` in the active batch
evidence; its receipt and source/input hashes remain outside the product tree.

PNG SHA256: `116b8247bc58e31d9253838bf77eb55ffa0f43cd936561eb1f8f59894b670f21`.

## Maintained Sprite atlas source and Gallery

`manual/images/source-aware-sprite-atlas.png` is the unmodified640×360 Game capture
at tick75 of Gallery127, Scene `ed3c5415-1ee7-49c2-9b74-78b20e9fb6f7`. The actual
FrameRenderer/Viewport regression checks four animation samples, all three mip
modes, retained Scene redraw/picking/framing and eight exact authored/source-free
relocated display pairs. This is software Vulkan rendering of original FORGE
images. It is not physical Windows/GPU acceptance or a performance measurement.

PNG SHA256: `ca97e93382b1451d303e8f47376bc36731bebbeba02a8ee834de3ce515cd7d1a`.

`manual/images/sprite-atlas-source-editor.png` is an actual920×850 ImGui component
capture through SDL's software renderer and the canonical Lato font. It opens the
Gallery's real `.atlas.json` source with `SpriteAtlasEditor`; no synthetic controls
or text are substituted. The source editor, source input hashes, capture commands
and limitations are recorded in the local batch's `manual-capture-proof.json`.
It demonstrates the recipe controls and does not establish Windows/DPI acceptance.

PNG SHA256: `71cb85699675ca896a63b7e4c2cb064ea7d8c659ad6f48b17657123daf599c9c`.

Both canonical images are shared by the README/manual where used. No retouching,
image generation, duplicate image assets or hardware performance claim.

## Sprite graph output controls

`manual/images/sprite-material-graph.png` is an authentic 1180×820 capture of the
actual MaterialGraphCanvas, rendered through the existing SDL/ImGui software
capture backend by `material_graph_geometry_editor`. The fixture displays the
complete default Sprite graph with Sprite Output selected. Its primary image
chooser shows the human-readable label while retaining the stable binding key.
The same fixture checks real controls, history, read-only state and save/reopen at
100/150/200% scale. It is a control capture, with no GPU material preview; it does
not establish actual Windows editor, shader cooking or physical GPU acceptance.

Source base: `b25186ce8fe7260e786558da877fc62865a21e2b` plus the programmable Sprite
material work in the current source tree. Capture: the `sprite-material-graph.png`
output of `forge_material_graph_geometry_editor_tests`. Current capture was
personally viewed at its original dimensions, with no image retouching.

PNG SHA256: `5562d10cee54d53f12b2d100b55a20dba7771be7e134aa2cf447d2cf4841be45`.


### Sprite and Tile bloom

`manual/images/sprite-tile-bloom.png` is an unretouched 640×360 FORGE Game display
from the original Gallery 128 Scene. Captured with the actual FrameRenderer,
MeshResourceHost, RuntimeWorld, ordinary Texture/Material/Tile resources and existing
HdrBloom on local software Vulkan. The matching authored/source-free relocated
captures are byte-identical. It is not an editor-window mockup, physical GPU
measurement or Windows acceptance. Capture provenance and image hashes are retained
in the local Sprite/Tile bloom validation evidence. The shared Scene controls are
unchanged; the manual uses this one canonical Game image for the visual result.

Capture source: `tests/sprite_bloom_gallery_render_tests.hpp`, exercised by
`forge_sprite_bloom_gallery_vulkan_probe` / CTest `sprite_bloom_gallery_vulkan`.
Renderer baseline: `482811d` plus the local shared bloom Gallery fixture; no numbered
release identity was assigned. Adapter: `llvmpipe (LLVM 15.0.7, 128 bits)`.
Scene AssetId: `41785133-5f53-4669-987e-ad39f7b4dfe3`.
Canonical PNG SHA256:
`6ad949e3bc25b1b06430ba408305b58580c108d3c53f9e9fa935d2e74b5b79fd`.

## Dynamic navigation controls

`manual/images/navigation-dynamic-controls.png` is an unretouched 1180×760 capture
of actual ComponentInspector and NavigationTools ImGui draw data, rendered through
SDL's software backend. The ready Gallery129 Scene supplies the genuine obstacle,
clip and v4 navigation selection. Actual mouse input collapses the unrelated
Transform Animator to focus the task controls; no image/control was edited.
Capture source: `tests/static_mesh_navigation_editor_tests.cpp --capture-all`,
local work based onbae089e with named profiles. The current default Build profile
selector is visible. Exact receipt:
`AgentFiles/build/phase217-navigation-build-profiles/dynamic-controls-capture.json`.
SHA256: `335d773f14b91441c659d802204934385a5bd4b7f1077cf7f5e84eecfbada670`. The capture was personally reviewed. The same fixture checks
100/150/200% controls, stale/revert behavior and Play locks. It is an authentic
editor-owner fixture, not a launched Windows editor or physical display/GPU/DPI
acceptance. CPU source-free Gallery tests verify routes and render submissions;
this image does not claim graphical standalone execution.


## Navigation behavior graph and Game captures

Captured from local development based on f1c9ed2 with the bounded navigation
behavior implementation. The canonical images are in manual/images; other guides
link to those assets rather than maintaining edited copies.

| Asset | Capture source | SHA-256 |
| --- | --- | --- |
| navigation-behavior-graph.png | navigation_behavior_editor_tests --capture; real document owner, original published Patrol graph and real edge selection, SDL software ImGui renderer,1180×1040 | 76aada2117303fb854edd441b1558b46e0bfcc47aaac136ea3d841e46a5909f3 |
| navigation-behavior-patrol.png | navigation_behavior_gallery_vulkan_probe; Gallery131, actual Game frame at tick90,640×360 | 32c3a97d403e2f7b1ea9bc8e2d894e8b63cdb77e51ad85ed615de712ff6fac0c |
| navigation-behavior-chase.png | same native consumer; Gallery132, actual Game frame at tick90,640×360 | e656555c4646897194b6211d6df3801ad502a672148b7073e81f38addc25a895 |

The Game consumer uses Diligent/Vulkan on llvmpipe LLVM15.0.7/API1.3.255.
It verifies actual movement and eight exact authored/source-free relocated display
pairs, including portrait framing. PNG conversion preserves the original readback
RGB pixels; controls and image contents are not retouched. The document and Game
captures were personally inspected. This is software-backend evidence, not
physical GPU, Windows, full editor or exported graphical EXE acceptance. Windows
review remains queued. Exact input/capture evidence is retained in the local
navigation-behavior batch record. Offline manual layout and package inclusion are
qualified separately at the batch gate.

## Game UI localization catalog and native menu

The canonical `manual/images/ui-localization-editor.png` captures the real
UiLocalizationEditor/SourceDocumentPanel owner with the published original
Gallery133 catalog. `forge_ui_localization_editor_tests --capture` renders actual
ImGui draw data through SDL's software renderer at1180×1020. The complete source
window, language/key controls, literal text, Save / Publish and Scene attachment
are visible. No authored values or history are changed during capture.

`manual/images/ui-localization-game.png` is the1000×800 French native Game UI
readback from `ui_render_tests.cpp` and `ui_localization_render_tests.hpp` through
UiDiligentRenderer, selected RmlUi6.3/FreeType and Linux software Vulkan/lvp. Real
pointer commands select five requested locales in landscape and portrait while
paused. The ten authored displays match their source-free relocated exports
exactly. French/Spanish accents, literal markup/braces and default-message fallback
use the packaged Lato font. The PNG encodes the readback's unchanged RGB pixels;
there is no crop, resize, UI insertion, retouch or generated screenshot content.

Capture source is local development based on7ca81bf with localization changes,
unassigned build identity. Original artifacts are
`ui-capture-final/ui-localization-editor.png` and
`native-gallery-images/ui-localization-1000-french-authored.ppm` in the local
localization validation record. Both canonical captures were personally reviewed.
The same record retains exact source/artifact hashes and pixel-pair receipts.

- `ui-localization-editor.png` SHA256: `cc4adb8497121cc881b32f50c8830e4a12f1bf0ca652f6c3c19d1645803e0762`.
- `ui-localization-game.png` SHA256: `6aaeda25b2926ab2fd2b35b4af75826fc4e45fc797099347b54de19e1242d17c`.

These are actual source-owner and native Game UI captures. They do not establish
full Windows editor, MSVC/D3D12, physical GPU/input/DPI or graphical EXE acceptance.
The Windows review queue retains those unexecuted checks. Offline manual and
package tests verify local navigation and unchanged image inclusion separately.

## Application States source, controls and Signal Lock

These captures use local application-state development based on0cabd47, with an
unassigned build identity. Canonical image assets live only in manual/images.

| Asset | Actual capture source | SHA-256 |
| --- | --- | --- |
| application-states-editor.png | Real ApplicationStatesEditor native gestures / SDL software, 1200x900 | 689091e18d277dffb3ef824cc7de34ce84840556e8f103bd482061f0db235c6c |
| application-states-controls.png | Actual Game state selector owner / SDL software, 800x460 | 04dc5c362a3ccb46dc132645a89f297304a942e314c3a5905878db95a766e78e |
| application-states-game.png | Native UiDiligentRenderer/RmlUi/FreeType software Vulkan/lvp Game UI, 1000x900; unchanged RGB format conversion | c69074e7e927b4b4fb8596b1d7927ca098af960bf1419270d18cdf879a0e1c8e |

The source capture comes from `application_states_editor_tests --manual-capture`:
native editing/history/publication/attachment gestures render through SDL software.
Its displayed computed domain is a real edited definition. The Game-controls
capture uses the exact selector owner called by Main, with copied core state; it
is a focused owner fixture, not a full editor desktop screenshot. Separate pointer
tests exercise all controls at100/150/200% scaling.

The game image is `ui-states-1000-open-authored.ppm` from the native renderer fixture.
The actual GameSession reads the selected typed definition; native pointer commands
choose the signal/route while paused. Twelve desktop/portrait views match full
Scene/state/RML/style exports after relocation and editable-source removal. PNG
encoding preserves every readback RGB pixel. There is no crop, resize, inserted UI,
retouch or generated screenshot content. Desktop/portrait game views and source/
selector captures were personally reviewed. Exact inputs, format conversion hashes
and pixel-pair receipts remain in the local application-state validation record.

These prove the stated owners and software rendering only. Full Windows editor,
MSVC/D3D12, physical input/DPI/GPU and relocated graphical EXE acceptance remain
queued. Offline manual layout and package inclusion are validated separately.

## State Conditions and Activity Lab controls

`manual/images/state-conditions-game.png` is the actual native Activity Lab UI at
420x720 with Playing selected. Source is local State Condition development based
on bb8f5e8, with an unassigned build identity. The original artifact is
`ui-state-conditions-420-playing-authored.ppm` from
`application_state_conditions_ui_vulkan`. It uses UiDiligentRenderer, RmlUi,
FreeType, the packaged Lato font and software Vulkan/lvp.

The fixture runs the real GameSession and state/UI owners. Native pointer controls
choose Run, Rest, Hide all and Step; each state control leaves the clock unchanged,
while Step advances it once. Ten desktop/portrait views match a relocated full
Scene export with editable sources offline. The 1000x900 and 420x720 controls and
badge, including the scrolled clock controls, were personally reviewed.

The screenshot shows the UI texture and its background. It is not a capture of
the 3D world or full editor desktop. Unchanged RGB pixels were encoded as PNG
without cropping, resizing, inserting controls or retouching. Canonical PNG
SHA-256: `b48bc88ea039aec053fcc2bd907964ecfb27b74d38383a3df55c06482e2d487f`.
The local State Condition validation record retains exact source/artifact inputs
and pixel-pair receipts. Physical Windows/MSVC/D3D12, input/DPI/GPU and graphical
EXE acceptance remain queued; offline manual/package checks are separate.


## Authored navigation links

`manual/images/navigation-link-inspector.png` is the real ComponentInspector for
Gallery140's upper two-way source. It was captured from local
`forge/authored-navigation-links` work based on6462f3e, with unassigned build
identity, using the packaged font, ImGui and SDL software renderer. The fixture's
`--capture` route opens a copied ordinary project/Scene, renders the actual
controls, checks essential controls are visible and verifies no authored change.
The original860x560 PNG is preserved; no cropping, scaling or retouching was used.
Canonical SHA-256: `7863d809007f687f0af885bff3308946e893ad00224cfef46be1e10947b6730e`.

`manual/images/navigation-links-game.png` is actual Gallery140 Game output after
90fixed ticks on software Vulkan/lvp. Cyan and orange are in their first straight
crossings. The existing Game/FrameRenderer fixture verifies four byte-exact moving
source/source-hidden relocated-export display pairs, including portrait output.
The original `authored/90-640.ppm` RGB pixels were losslessly encoded as640x360 PNG,
without cropping, scaling or inserting controls. Canonical SHA-256:
`828baa2d3dad876db010ca3b62a0b4d661aa270ec8f1982182592f5ffabf1993`.

Scene `0c9cd880-2808-4310-bf1d-9eb426ad5400` source SHA-256:
`cf01ce5e80540e86ba84e1a57976fb6f8bd8494b13184527fd0e91d576e9336c`. It reuses the existing original Patrol behavior with
separate dynamic Loop/two-way and static OneWay navigation assets. Local capture
receipts retain executable/source provenance, exact inputs, environment and
source/export image comparisons. These captures establish neither a full editor
desktop nor Windows/MSVC/D3D12, physical GPU/DPI, graphical EXE or external-agent
acceptance. Those reviews remain queued separately; offline manual/package
inclusion is verified by the normal documentation gate.

## Static navigation collision collection

`manual/images/navigation-collection-controls.png` records actual NavigationTools
controls for Gallery141's saved Physical profile. The fixture opens a private
ordinary Gallery copy, selects that profile, renders the real Content controls
through ImGui/SDL software rendering and checks visible enabled collection/layer
controls, ready status and unchanged authored Scene. The860x600PNG is preserved
without resizing, cropping or retouching. Capture command uses
`forge_navigation_link_editor_tests --capture-collection <Gallery> <output>`.
PNG SHA256: `746032e6c122cc0455a87f674d429704daa43060aa49e5a821f774ce24cce4d9`.

`manual/images/navigation-collection-game.png` is production Game/FrameRenderer
output after60fixed ticks on software Vulkan/lvp. The cyan patrol approaches the
physical wall between green destination markers. Four moving source/source-hidden
relocated-export display pairs match exactly, including portrait output. The
640x360RGB pixels from `authored/60-640.ppm` were losslessly encoded as PNG;
no visual element was added or retouched. PNG SHA256:
`2de78ed45379e2a386c5466e2ea86344e6d45d4ac2cd8df302f578ca2e07f13e`.

Both captures use local `forge/navigation-collision-collection` work based on
cc6b813 with unassigned build identity. Scene
`a4c29e00-c3d6-4534-83c2-87146bcc5fbb` source SHA256:
`063e5085eb4e6fd3143bb9629c3449166faac109b0d8cd3d642cec6223769c94`.
Local receipts bind the source/executables, rendering environment and exact image
pairs. Both original images were personally reviewed. These are actual control /
Game fixtures, not full editor desktop, Windows/MSVC/D3D12, physical GPU/input/DPI
or graphical exported-executable acceptance. Offline manual/package inclusion
remains the documentation gate's responsibility.


## Single-base Prefab variants

`manual/images/prefab-variant-source.png` is the complete1180×1520 capture of the
production PrefabEditor for Gallery142's published AmberBeacon source. It shows
the reviewed base, name intent, inherited transform channels and partial Animation
Sequence override at Playback Speed1.5. The normal source window is rendered via
ImGui/SDL software/dummy; `forge_prefab_variant_editor_tests --gallery-capture
<Gallery> <scratch> <PNG>` uses a private Gallery copy. Six additional actual-input
layouts cover100/150/200percent scale, narrow/wide panels, equal-value ownership
and Revert. PNG SHA256: `17d422f18823e95d6441f2e7795fd14401b522accc0fd75b3adf1d8ee126d374`.

`manual/images/prefab-variants-game.png` is actual 640×360 production FrameRenderer
output after90fixed ticks on software Vulkan/lvp. Five independently animated
beacons show the base, amber, blue, chained pink and repeated blue instance. Four
moving source/source-hidden relocated-export display pairs match exactly. The
complete `authored/90-640.ppm` RGB pixels were losslessly encoded as PNG with no
resizing, crop, annotation, compositing or retouch. PNG SHA256:
`4565ca2848cff3c8c5ec81e35ccb3c96aa187c12464902b8bd0d2cd3d6d61743`.

Both canonical images use local `forge/prefab-variants` work based onada326d with
unassigned build identity. Scene source SHA256:
`55e7e74570c81d4293b5cc5627dfb04bf079d3affbd1dec1b74bf08183a10652`. Local receipts bind source, executables, pixel proof and
rendering environment. Both images were personally reviewed. These are control /
Game fixtures; full Main, Windows/MSVC/D3D12, physical input/DPI/GPU and graphical
exported-EXE acceptance remain separate pending reviews. Offline manual/browser
rendering and package inclusion are checked independently.


## Fast approximate anti-aliasing

`manual/images/fast-approximate-aa-controls.png` is the complete 900×340 production
Scene lighting control capture from `forge_hdr_bloom_editor_tests <output> --fast-aa`.
The fixture uses the normal Scene authoring owner with enabled filtering, smoothing
0.5 and 12 search steps, ImGui/SDL software rendering and the existing packaged font.
It resizes the actual fixture window; no image crop, control retouch or annotation
is applied. PNG SHA256: `77bccadd59a9219927d9e97c86b6e59ea4d370a1688784daa24a2b42d001b2ab`.

`manual/images/fast-approximate-aa-game.png` is actual 640×360 FrameRenderer output
for Gallery 143 after 90 fixed ticks on software Vulkan/lvp. The pink foreground bar
moves against original thin cyan/amber edges and tilted cubes. Four moving source /
source-hidden relocated-export display pairs match exactly. The complete
`authored/90-640.ppm` RGB bytes are losslessly encoded as PNG without resizing,
compositing or color changes. PNG SHA256:
`4c0b7e3a3b0e658c9f4600475104239c6708039b23ff9f49a23fb1ecd7a93f7f`.

Both canonical images come from local `forge/fast-approximate-antialiasing` work
based on d57e25c, with unassigned build identity. Scene source SHA256:
`c00d8100fb35c684184889f3ce113d494d357ac8ae066dbc638fbf1234d08086`. Local receipts bind source/executable hashes,
rendering environment and exact pixel proof. Both images were personally reviewed.
These are authentic control/Game fixtures, not full Main desktop, Windows/MSVC /
FXC/D3D12, physical input/DPI/GPU or graphical standalone acceptance. Offline manual
layout and package inclusion are checked separately.


## Foliage placement

[Placement controls](../../../manual/images/foliage-placement-controls.png) and
[heightfield controls](../../../manual/images/terrain-foliage-controls.png) are
unretouched 720×1340 captures of the production FoliagePlacement window, rendered
with actual ImGui and SDL software output. Real pointer/keyboard input selects
Gallery147's independent blade template and published layered terrain, enters
128 candidates and checks the layout without changing Scene intent or Problems.
Heightfield controls include inclusive Minimum/Maximum slope, checked Align to
terrain slope and the retained/candidate status. The shown default0..90range keeps all128 origins.
All controls, status and explanatory text are visible. Capture command:
`forge_foliage_editor_tests --capture-gallery <Gallery> <private-output>`.
The production owner is also exercised with real input at 100/150/200 percent
in full/compact layouts. These GPU-independent component captures do not establish
full Main, physical Windows input or display/DPI acceptance.
Plane PNG SHA256: `7c88bd4a1a64880247ff1ba6123ad4c7d4d325a000a29494820d6b630ead1469`.
Heightfield PNG SHA256: `310d7ef492bf22e53d4c9ab3579990eab5a20abf1342e9ba356afd51852cc8ca`.
Current capture consumer SHA256: `852c8dede984208f97445a5a9753f2203a201ab38a22f5286af3a1001597e711`.
Local unassigned source; current control captures replace the earlier captures in
their canonical assets. Game images below retain their separate unchanged proof.

[Meadow Game](../../../manual/images/foliage-placement-game.png) is the unretouched
640×360 production FrameRenderer after 90 fixed ticks in Gallery 144
(`dd33660e-3750-4ef4-8c5e-bb90d85f7448`). Original base-pivot blades form 128 green
and 64 amber ordinary placements, beside the independent green template and a
moving pink marker. Four moving source/source-hidden relocated-export pairs match
exactly under software Vulkan. The Scene View independently renders 195 instances
with active batching and verifies individual template picking. Draw counts depend
on existing queue order and compatible numeric material values; this is not a
physical GPU performance claim. Capture uses `forge_foliage_vulkan_probe <Gallery>
<private-output>`, and PPM-to-PNG conversion preserves every RGB byte.
PNG SHA256: `fa06996e668cb5b257d0bb5a77d9b53380cb633cb333d0c92e1ebc0f6846e160`.

Both captures use local foliage source, original FORGE-authored geometry and existing
pinned dependencies. No controls/pixels are retouched, no temporary capture fixture
is shipped in runtime content, and no Windows/D3D12/full-editor/physical acceptance
is implied. Exact local source/command/image proofs are retained by the capability
batch's canonical validation owner.

[Heightfield meadow Game](../../../manual/images/terrain-foliage-game.png) is the
unretouched 640×360 production FrameRenderer after90 fixed ticks in Gallery147
(`e6d70f7c-b8a2-4af3-97b2-5f0834e40e75`). Its128 ordinary blades rest on the
published layered terrain, with an independent template. Four static source /
source-hidden relocated export displays match exactly using software Vulkan.
Scene View checks131 draw instances (including the terrain's two parts), active
batching and individual template picking. Capture command:
`forge_foliage_vulkan_probe <Gallery> <private-output> Scenes/147-terrain-foliage.scene.json`.
Lossless PPM-to-PNG encoding verifies every RGB byte. No pixels are adjusted or
controls retouched. PNG SHA256:
`6f49ac94b2b4563cf21091e567d251d2557b88f3aa93b609e5f6d93bab355cdd`.
This is software rendering evidence, not physical GPU performance or Windows /
D3D12/full desktop/graphical EXE acceptance. Capture source/executable/input hashes
and byte proofs remain in the canonical local capability validation owner.

## Two-clip sequence Timeline

[Sequence crossfade controls](../../../manual/images/sequence-clip-blending.png)
is the unretouched 720×1100 production Animation Timeline, rendered by the existing
SDL/ImGui software fixture at 100% with the project font. Actual pointer input
selects Gallery183's Light track. Both property-only clips, highlighted overlap,
typed clip pickers, pending start fields and supported-track hint are visible.
The same run explicitly samples the light and Sprite at four sequence times,
checking continuous values, the discrete midpoint and unchanged authored Scene
content/history. Frame export is unavailable in this isolated Timeline fixture
because it has no graphical Game host; production Scene/Game rendering and
source-hidden relocated export are validated separately.
PNG SHA256: `fe762a913b93ed6f643ea8b9f0cdb4ea48b4dba76052919472ed106c1b436767`.

Capture uses `forge_animation_sequence_editor_tests --capture-properties <Gallery>
<private-scratch> <png>` from the local property crossfade extension based on
77bf1de, with an unassigned build identity. Exact executable, relevant source,
Scene and image hashes remain with the local capability validation owner. The
canonical PNG is copied byte for byte from that run. This is not full Main,
Windows/MSVC/D3D12, physical input/DPI/GPU/audio, performance or graphical EXE
acceptance.


## Nested sequence Timeline

[Nested sequence placement](../../../manual/images/nested-sequences.png) is the
unretouched 720×1100 production Animation Timeline at 100%, rendered with the same
SDL/ImGui software fixture and project font. Actual pointer input selects the
original Gallery146 child row on its eight-second parent. The green six-second
placement, start field, apply/reset, local navigation and detachment controls are
visible. The capture includes the selected controller name. No authored Gallery
file changes during capture. Frame export is disabled because the isolated
Timeline fixture has no graphical Game host.
PNG SHA256: `0b96320ccb6be139998c71ccf7e1895bbb914fcccad1e9b5d809c58f2311a4bd`.

Capture uses `forge_animation_sequence_editor_tests --capture-nested <Gallery>
<private-scratch> <png>`, source based on 5027e2b and an unassigned build identity.
The same source refreshes the two-clip Timeline image above because its visible
header now identifies the selected controller. Exact executable/source/Scene/image
proofs remain with the local capability validation owner. No pixels or controls
are retouched; full Main, Windows/MSVC/D3D12, physical input/DPI/GPU/audio,
performance and graphical EXE acceptance remain separate.


## Source reverb Inspector

[Reverb controls](../../../manual/images/audio-reverb.png) is an unretouched
720×1400 production ComponentInspector capture through actual ImGui/SDL software
output. A private project fixture uses Gallery's original published FalloffTone
and ordinary AudioSource values. The capture checks all four reverb controls are
visible/enabled, Scene intent is unchanged and no Problem is raised. It is an
authored Inspector capture, without invented live playback status. The same
controls are exercised with real pointer/keyboard input and Undo/Redo at four
scales (0.65,1,1.5,2), plus actual retained-tail Pause/Resume input.
Command: `forge_audio_editor_tests --capture-reverb <Gallery> <private-output>`.
PNG SHA256: `618d326b2770e9dd0dea40c9916c5f3aad8630647687ab4ee7caad4263b8c0ca`. Source, executable and capture inputs remain bound in the
canonical local validation owner. The image was personally reviewed. This is a
GPU-independent widget fixture, not full Main, Windows/MSVC/D3D12 or physical
input/DPI/audio acceptance. Offline layout and package inclusion are checked separately.

## Input Message selectors

`manual/images/input-message-inspector.png` is the unchanged600×500 PNG from the
real ComponentInspector in `forge_input_message_ui_tests`, using SDL's software
renderer. The test chooses Changed, a saved analog action and attached Tint
through native ImGui input; it checks typed values, incompatible-ID retention and
shared Undo/Redo at1/1.5/2 scales. The image was personally reviewed without crop,
resize or retouch. It is an isolated Inspector fixture, not full Main, Windows,
OS DPI or physical hardware acceptance. Capture source, executable/image hashes
and validation attribution are retained in local
`AgentFiles/build/phase248-input-message-triggers/capture-provenance.json`.
PNG SHA256: `edb89aac4c7388cddf623539381603114ae2213c5feda8cfcb0799c3a45d8b2f`.

## UI Values Inspector and Timeline

Canonical assets are `manual/images/ui-values-inspector.png`,
`ui-values-live.png` and `ui-values-timeline.png` (each640×650). They are unchanged
PNG output from the actual ComponentInspector, live tuning and AnimationTimelineEditor
controls in `tests/ui_values_editor_tests.cpp`. The fixture drives native ImGui
input, draft entry creation, numeric edits, Scene Undo/Redo, temporary live updates,
tuning review, keyed builtin track selection and clip key Undo at100/150/200percent.
The three canonical images are100percent captures from the passing local profile
`ui-authoring-integration` (5affected profiles pass52.37s), from working source based
on7493f3d. The fixture source SHA-256 is `5b6f9431c79b424f0e9306a6a47f49e32c65e343dd5b407b856cf3f063648bd0`.

SDL3's software renderer draws the unchanged ImGui output with the packaged Lato
font. All three images were personally viewed before copying. There is no crop,
resize, inserted UI or retouch. These focused production-control fixtures do not
prove a full desktop editor session, Windows, physical DPI/input or GPU acceptance.
Local run/output attribution is retained by the engineering validation owner.

- `ui-values-inspector.png`: SHA-256 `4933e9a460f753434927743c07dfdf1fdb5a51cce9df08659b13c784703ef485`.
- `ui-values-live.png`: SHA-256 `d193b7c1e49bd8b7da7ef620185dc1d0c5d510d24d689a74aafe95c98e04097c`.
- `ui-values-timeline.png`: SHA-256 `9d7f1d6c8ddc1b819b0285c3107527ea778f30facbed370f98c8cc6b39a53bbb`.

## Camera output images

[Render Texture controls](../../../manual/images/render-texture-editor.png) is the
unretouched1080×360 production RenderTextureEditor in SDL/ImGui software rendering.
Actual numeric input, Apply/Cancel, source Undo/Redo, locks and Save / Publish are
exercised before capture. PNG SHA256:
`3b2b6e0b14beb5d38540ed8a3da8666683df0a33426fa687536b0fe21353a158`.

[Two Game monitors](../../../manual/images/render-texture-gallery.png) shows the
unretouched640×360 production FrameRenderer at tick75 in original Gallery152
(`a99b3d47-3fc7-40f5-a10d-0b16aea67cd3`). The left monitor combines red/cyan producer
Cameras; another Camera relays it to the right monitor during the same frame.
Four source/source-hidden relocated animated display pairs match exactly on
software Vulkan. Lossless P6 RGB-to-PNG conversion retains every source pixel.
PNG SHA256: `fc1e7005f0ffa3ef36b767f7be322ca3f0ea61fe80057b5dfdc7889343c4550a`.

Both captures were personally reviewed. Local unassigned source is based on57ed673
with the render-texture implementation. Exact executable/image/source hashes,
original capture paths and native receipts are recorded in
`AgentFiles/build/phase232-render-texture-authoring/manual-captures.json`.
These captures do not establish full Main, Windows/MSVC/D3D12, physical GPU/DPI,
external MCP client or graphical EXE acceptance.

## Smooth Camera Shot controls and view

[Incoming shot controls](../../../manual/images/camera-transition-editor.png)
shows the production Timeline with Gallery153's incoming shot selected and **Shot
blend in** editable. The unretouched720×1400 PNG comes from the actual SDL/ImGui
software capture in `tests/sequence_clip_capture.hpp`.

[Transition Game view](../../../manual/images/camera-transition-gallery.png)
shows the production FrameRenderer at tick150 in Gallery153 on software Vulkan.
The animated camera rig is halfway through the incoming view/lens transition.
Four source/source-hidden relocated display pairs match exactly; the stopped
Timeline's three exported PNG samples also match independently read display pixels.
Lossless P6 RGB-to-PNG conversion retains every source pixel.

Both canonical images were personally reviewed. Local unassigned source is based
on f076f8d with the smooth camera-view implementation. Exact executable/image/source
hashes and capture receipts are in
`AgentFiles/build/phase233-camera-shot-transitions/manual-captures.json`.
The captures do not establish full Main, Windows/MSVC/D3D12, physical GPU/DPI,
external MCP client or graphical EXE acceptance.


## UI in camera images

[Camera-output UI](../../../manual/images/camera-output-ui-gallery.png) is an
unretouched640×360 actual GamePresentation/GameSession capture of Gallery154 on
software Vulkan. Both monitor Materials show upright telemetry text; the main
guide remains separate. The paused tick is0. Lossless P6-to-PNG conversion preserves
every RGB pixel. The original source and source-hidden relocated export displays
match exactly, including transparent/visible model states and main resize.
PNG SHA256: `cdc54d78d4a6823adfe641e3dc50fb4ecf9ddd27d1484f39e5d047dc6d934c01`.

Personally reviewed from local unassigned source based on2b05874. Exact inputs,
executable and capture provenance are recorded in the local234 manual-captures
receipt. This does not establish full Main/editor, Windows/MSVC/D3D12, physical
input/DPI/GPU, performance or graphical-EXE acceptance.


## Native UI filter samples

[UI filter lab](../../../manual/images/ui-filter-gallery.png) is an unretouched
640×360 actual GamePresentation/GameSession capture of Gallery155 on software
Vulkan. The same original shapes compare reference, grayscale, brightness,
contrast, opacity and model-driven blur. All three pause controls fit the tested
landscape and portrait Game targets. Paused model updates and RCSS filter animation
change actual pixels without advancing fixed simulation. Source-hidden relocated
content renders the same four captures, including changed blur and portrait.
Lossless P6-to-PNG conversion retains every RGB pixel.
PNG SHA256: `7abc2a2e2a7c7bb2b7a145a605d07ee5e256ed34e7f67d19b9ef695dc4ea8c90`.

Personally reviewed from local unassigned source based on b24db7d. Exact native
executable, source/export captures and limits are retained in the local235
manual-captures receipt and final source freeze. This is test-fixture/native
software-rendering evidence, not full Main/editor, Windows/MSVC/D3D12, physical
GPU/input/DPI/performance, external MCP client or graphical-EXE acceptance.


## Texture image export

[Byte texture export](../../../manual/images/texture-export.png) and
[HDR texture export](../../../manual/images/hdr-texture-export.png) are authentic
720×650 local source fixture captures, not complete Windows editor or physical-
device acceptance images. They show the actual production TextureViewer with the
published original Gallery Badge and Reflection cube, with both editable sources
absent. The HDR preview uses the existing display tone mapper; the HDR file writer
consumes separate linear source pixels.

The native fixture uses the same pinned Diligent ImGui renderer as the editor,
real ImGui input and a software Vulkan offscreen target. Independent RGBA readback
is directly encoded with the existing PNG writer, with no cropping, resizing,
retouching or fabricated controls. Both pictures show the current Export image
command; the prior texture-import image teaches the separate import-settings flow.

Source test: `tests/texture_image_editor_vulkan_probe.cpp`,
`texture_image_editor_vulkan`, local unassigned build. Source image entries:
`texture-image-editor-vulkan/texture-export-wide-100.png` and
`texture-image-editor-vulkan/hdr-texture-export-wide-100.png`.
Byte PNG SHA256: `1ea102e738c54a12703a345d15eb558f5f970a9276f806588e70bdf3071ebbec`.
HDR PNG SHA256: `84cdfc803db19c63514fe8ef42527947788c6e036a3b280e931b14809dcad5db`.
Native consumer SHA256: `60449ceadf3aed4c6a153c5c30d100dcdf0469561fddd56be7c84004b9bd2174`.
Twelve byte/HDR wide/narrow layout/input cases use1/1.5/2scale. Rebuilt consumers
remain byte-identical after the final private declaration comment, so the exact
attributable native test/capture result is reused. Windows/full Main/MSVC/D3D12,
physical GPU/input/DPI, external clients and graphical EXE remain pending.


## JPEG image export

[JPEG quality](../../../manual/images/jpeg-image-export.png) is an authentic
720×650 local source capture of the production TextureViewer and ViewImageTask
quality modal, showing the published original Gallery WarmRoom texture with its
editable source absent. The selected quality is72. The same dialog serves Scene /
Game still capture and supported byte Texture output; native file picker delivery
is injected at the existing boundary in this fixture.

The original pinned Diligent ImGui renderer draws to a software Vulkan offscreen
target. Independent RGBA readback is encoded directly as PNG, without retouching,
cropping, resizing or fabricated controls. Source test:
`tests/texture_image_editor_vulkan_probe.cpp`, `texture_image_editor_vulkan`, local
unassigned build. Capture entry: `texture-image-editor-vulkan/jpeg-quality-wide-100.png`.
PNG SHA256: `3274666be205ca4d47a693554804de9da4b82e44dd9d8c530277ae4776080ed3`.
Native consumer SHA256: `4370f7968fb294602df6be28706ed82f1caa869a535bbe4bb2d7487df9448c58`.

The complete fixture checks18byte/HDR/JPEG wide/narrow cases at1/1.5/2interface
scale, including actual quality entry and clipped-control guards. JPEG screenshot
readback also requires varied rendered pixels; frame retirement follows the
existing offscreen host protocol. Existing byte/HDR canonical captures above
remain unchanged and retain their original provenance. Windows/full Main/MSVC /
D3D12, physical GPU/input/DPI, external clients and graphical EXE remain pending.


## Normalized16-bit texture image export

[Precision texture export](../../../manual/images/uint16-texture-export.png) shows
the actual production TextureViewer displaying the original Gallery normalized
RGBA16 ramp and its alpha checkerboard. The published source image is absent from
the fixture. The exported full PNG preserves16-bit samples; this8-bit screenshot
and the display alone cannot establish low-bit precision.

This authentic720×650 local unassigned-build image uses the approved pinned
Diligent ImGui renderer, software Vulkan and independent RGBA readback encoded
directly as PNG. No cropping, resizing, retouching or fabricated controls. Source:
`tests/texture_image_editor_vulkan_probe.cpp`, `texture_image_editor_vulkan`;
`texture-image-editor-vulkan/uint16-texture-export-wide-100.png`.
PNG SHA256: `9375188c17a24a921f7406a80f9201d18cc3f7cfd744180620f32ea8e60b4f30`.
Native consumer SHA256: `27146e38b42da28a12f233f82dccd04eb159097d51dfa2b189176bdb4c254ec2`.
The full native suite covers24byte/HDR/JPEG/normalized16-bit wide/narrow cases at
1/1.5/2interface scale, with actual input, chooser hints and exact PNG16 samples.
Windows/full Main/MSVC/D3D12/physical GPU/input/DPI/performance and external-client
acceptance remain pending.


## Compressed texture image export

[Compressed Texture export](../../../manual/images/compressed-texture-export.png)
shows the production TextureViewer and Export image command with the original
published64x32 BC1 sRGB Gallery pattern. The editable DDS is absent from the
fixture. Preview filtering and checkerboard do not alter exported reference
texels. The source supplies two mips, including an opaque second mip for JPEG.

Authentic720x650 local unassigned-build capture from the approved pinned
Diligent ImGui renderer, software Vulkan offscreen target and independent RGBA
readback encoded directly as PNG. No cropping, resizing, retouching or fabricated
controls. Source tests/texture_image_editor_vulkan_probe.cpp;
texture_image_editor_vulkan; texture-image-editor-vulkan/
compressed-texture-export-wide-100.png.
PNG SHA256: 8e50722353f4b52c594357710245b02d1153a59e2646da157fb260009256c583.
Native consumer SHA256: 3f423f5e8bdc11eacd6bbd17424a1e17b0e043d00f1d969a3c5d473816505280.
The native suite covers30 byte/HDR/JPEG/normalized16/compressed wide/narrow cases
at1/1.5/2 interface scale, including actual input, chooser hints and exact exports.
Windows/full Main/MSVC/D3D12/physical GPU/input/DPI/performance and independent
external-client acceptance remain pending. Earlier captures retain their own
unchanged provenance.


## Owned HDR PNG inspection

[HDR inspection](../../../manual/images/hdr-inspection-preview.png) contains exact
unmodified128x128 PNG bytes returned by capture.image through the actual MCP
bridge. Source-hidden published Gallery Reflection.dds face5 was first exported
to owned Radiance.hdr. This CPU image shows the fixed unit-exposure per-channel
c/(1+c), sRGB/nearest8-bit inspection of the quantized written RGBE. It is an
inspection image, not an editor screenshot, full radiance data, native-viewer
display equivalence or physical GPU/Windows acceptance.

Source tests/mcp_image_test.py and forge_mcp_image_host; mcp_image_stdio, current
framing, protocol record339. The retained local transcript has SHA256
85ce385e48825bdf51abb264052c429031cfb65b58701e69117be3fef1556a0d. Native MCP host SHA256:
57c3c726417421b1f74d0cddb414d51558e085233444e5a5d156af247609e6ae.
PNG SHA256: 341f38fa22db31a66b7a72f25ad6b849589b7fa089f0cbde99873731c59d0290.
Local unassigned build. No resizing, retouching, cropping or fabricated controls.
Current/later image equality, explicit preview_profile, exact operation/read/output
grants, damaged-preview artifact retention and both framings have focused proof.
The earlier HDR Texture-viewer screenshot retains its original separate source
and display-transform provenance.


## Sequence export options and movie output

[WebP options](../../../manual/images/webp-sequence-export.png) and
[AVI options](../../../manual/images/avi-sequence-export.png) show the actual
production Additional output selector, retained-PNG explanation, scrollable settings
and Start/Cancel controls. AVI also shows actual JPEG-quality input. Both canonical
images are unretouched480x780 narrow100percent captures through the pinned Diligent
ImGui renderer and offscreen software Vulkan target, with independent RGBA readback
encoded as PNG. The controlled source is7x3; screenshots illustrate native controls,
not physical display or a full Main window. No cropping, resizing or fabricated controls.
Source: tests/webp_sequence_editor_vulkan_probe.cpp. Native fixtures exercise wide/
narrow100/150/200percent, actual format/quality input, cancellation, output metadata
and restoration. PNGs stay exact; JPEG movies are lossy.

WebP capture source: movie-final-graphics-build/movie-final-graphics-check; input
SHA2567954ecee1a2475e471269ed9d9896dc51df14952fbc17efb69ce6d6d0fe58942.
AVI capture source: movie-graphics-probe-build/movie-probe-native-check; input
SHA25641cbb9b08019dc20664975c2f95085f7406364897195965c77b495b6c9c3746a.
WebP PNG SHA256: 2109bf3d481efb7482d78c87d92924e0e329e9d65fc7e532af0312ab052f822d.
AVI PNG SHA256: 30fdab1b583c35db005d10bf51c1f1f11229a6e8e884b8174465e0053c1af903.

[Movie frame](../../../manual/images/sequence-movie-frame.png) is an unretouched
384x216 frame decoded from the actual exported Gallery181 AVI. The native Timeline/
FrameRenderer fixture captures its original Scene, camera transition and existing
motion/material assets. The retained source PNGs match independent display samples
exactly. Existing local FFmpeg7.0.2 independently demuxes/decodes all six native-control
movies and the Gallery movie, checking dimensions/count/order/rational timestamps
and expected lossy image agreement. This frame is output index2 from the Gallery
movie, not an editor screenshot or a physical-platform acceptance rendering.
PNG SHA256: ab4e4dbee81f8eb98f9dd8200fef8f6535e0b6303c1af12683b3428798c0a044.

Canonical local evidence: AgentFiles/build/phase277-sequence-movies/
{capture-provenance.json,independent-movies.json,movie-final-graphics-check.json}.
Windows/full Main/MSVC/D3D12, physical GPU/input/DPI/performance and external-player/
client acceptance remain separate. Runtime gameplay and audio recording are outside
this silent-preview profile.

## Foliage clearance controls

`manual/images/foliage-clearance-controls.png` is the canonical authentic production
ImGui panel capture from `forge_foliage_editor_tests --capture-clearance`, using
Gallery 156 in an isolated project copy. It shows published heightfield projection,
alignment, sphere radius/world offset, layers, sensor/exclusion controls and 88 of 128
retained candidates. SDL software rendering captured the unretouched 720x1850 panel.
The existing plane/terrain control images were recaptured through
`--capture-gallery` at 720x1340 after the additional checkbox entered the workflow.
Actual input fixtures exercise wide/compact panels at 1x/1.5x/2x; these captures do
not establish full Main/Windows/MSVC/D3D12, hardware input/DPI or physical-GPU
acceptance. Capture evidence and hashes are local in
`AgentFiles/build/phase244-foliage-collision-filters/native-ui-captures.json`.

## Imported lightmap graph

`manual/images/imported-lightmap-graph.png` is the canonical unretouched 1360x880
capture of the production MaterialGraphCanvas through
`forge_material_graph_lightmap_editor_tests`. It shows HDR Texture usage and UV3,
Convert, Multiply, a gain parameter and the Lightmap irradiance Surface Output
input. The fixture uses the real ImGui controls and SDL software capture; input
checks cover connecting the port, source history, read-only state and saving at
100/150/200%. The image was personally reviewed after arranging its source nodes.
SHA256: `087c5bead28d7f5785eb1ebbf64669e8201cc970c55c64ee188b219769e185f7`.
Local capture provenance is in
`AgentFiles/build/phase247-imported-lightmaps/capture-provenance.json`.
This graph-only image does not establish a full editor window, Windows cooked
Shader, Game/export, hardware DPI or physical GPU acceptance.

## Native worker shared-write declarations

`manual/images/native-worker-writes.png` is the canonical unretouched1240x760
production Profiler control capture. Its data comes from an actual four-stage
RuntimeSimulation execution in `forge_native_system_access_tests`:256private
rows update once, each native stage observes the same fixed-source address,
and the callback deliberately reads that shared value without writing it.
The query declares shared write access, illustrating a declaration warning
without executing a concurrent shared write. Names and temporary IDs are native
fixture metadata. This is distinct from the SDK ParallelMotion snapshot used
for the access/ordering images below.

The control fixture replays the immutable native observation and drives Worker
writes, filtering and malformed-reference handling at six wide/compact100/150/200%
layouts. SDL software rendering supplies the pixels. Exact source, both profile
origins, binary and image hashes with receipts are recorded locally in
`AgentFiles/build/phase254-native-worker-write-inspection/capture-provenance.json`.
This is actual runtime-data replay in production controls, not full-editor,
Windows, physical GPU/input/DPI, observed-race or performance acceptance.

## Native system ordering targets

`manual/images/native-system-dependencies.png` is the canonical unretouched
1240x760 production Profiler control capture, personally viewed. It shows the
actual original-SDK ParallelMotion system's returned DependsOn target, native
phase tag, owned-pair presence and slot. The fixture replays a real four-stage
runtime observation, drives Ordering links/Show nodes and component filters at
six wide/compact100/150/200% layouts, and rejects malformed copied references.
SDL software rendering supplies the pixels. A separate controlled data oracle
checks legacy absence and system/phase/inherited-pair presentation; it supplies
no manual image. Exact source/profile/binary/module/image hashes and receipts
are recorded locally in
`AgentFiles/build/phase254-native-worker-write-inspection/capture-provenance.json`.
This is measured-data replay in production controls, not full-editor, Windows,
physical GPU/input/DPI or performance acceptance.

## Declared native system access

`manual/images/native-declared-access.png` is the canonical unretouched1240x760
production Profiler capture. The control fixture replays actual original-SDK
ParallelMotion runtime measurements and copied native query declarations at four
stages. The writable translation is owned; its read-only data may traverse.
Six wide/compact100/150/200% layouts and actual input cover worker summaries,
Timeline and declared access/overlap/ordering tabs. A separate controlled copied-data test
covers a positive write/read overlap and malformed-reference rejection; it does
not supply manual images. SDL software rendering supplies the control pixels.
Exact source/profile/binary/image hashes, process/capture receipts and limits are
recorded locally in
`AgentFiles/build/phase254-native-worker-write-inspection/capture-provenance.json`.
This is actual measured-data replay in production controls, not a full running
editor, Windows, physical input/GPU/DPI or performance acceptance capture.

## Native worker trace tracks

`manual/images/native-worker-traces.png` is the canonical unretouched1240x760
production Profiler control capture, personally viewed. The runtime process runs
the original SDK ParallelMotion with four native stages, records real measured
intervals, and the UI fixture replays that immutable status. Its Stage column
shows caller/worker attribution; this is not a fabricated data set or a running
full desktop editor capture. Summary/Timeline input checks cover six wide/compact
100/150/200% layouts. SDL software rendering provides the pixels. Exact profile,
source, binary, module and image hashes, process/capture receipts and limits are
recorded locally in
`AgentFiles/build/phase251-ecs-worker-traces/capture-provenance.json`. No Windows,
physical GPU/input/DPI, external trace viewer or hardware performance acceptance
is established by this control image.

## Fixed ECS worker settings

`manual/images/fixed-ecs-workers-settings.png` is the canonical unretouched
720x720 capture of production Project Settings with four stages saved. The
`forge_runtime_workers_editor_tests` fixture drives real ImGui controls and
the ordinary project Save owner, validation and Play lock at six wide/compact
100/150/200% layouts. SDL software rendering supplies the pixels. Capture source,
binary and image hashes are recorded locally in
`AgentFiles/build/phase250-fixed-ecs-workers/capture-provenance.json`.
This isolated control capture does not establish full-editor, Windows/MSVC,
physical GPU/input/DPI, exported-game or hardware performance acceptance.

## Local Fog Volume Inspector

`manual/images/fog-volume-inspector.png` is the canonical unretouched 720x900
production Inspector capture from `forge_fog_volume_editor_tests`. It shows Sphere
shape, linear RGB, density, local half extents, layer mask and priority. Actual
ImGui input tests cover wide/compact panels at 100/150/200%, live native tuning,
Keep values review/history, detached guides and framing. SDL software rendering
produces the pixels. The image was personally reviewed. Exact source/image hashes
and build/test provenance are in local
`AgentFiles/build/phase249-local-fog-volumes/capture-provenance.json`. This control
capture does not establish full-editor, Windows/MSVC/D3D12, physical GPU/input/DPI,
exported-game or hardware performance acceptance.

## BC6 HDR texture output

`manual/images/bc6-hdr-texture-export.png` is the canonical unretouched720x650
production Texture viewer capture. The original Gallery unsigned BC6 texture is
published; its editable source is absent from the control fixture. Actual pointer
input selects HDR color and the native HDR chooser/file task saves decoded texels.
Twelve unsigned/signed wide/narrow100/150/200% layouts check output values, scoped
operation behavior and completion provenance. Diligent software Vulkan supplies
the viewer pixels; the file follows the independent CPU selected-artifact path.
The canonical image and a signed compact200% capture were personally viewed.
Exact source, executable, image, publication and test receipt hashes are recorded
locally in `AgentFiles/build/phase255-bc6-hdr-output/capture-provenance.json`.
This is an isolated production control capture, not full-editor, Windows/MSVC,
physical GPU/input/DPI, exported-game or hardware performance acceptance.

## Runtime UI drop shadows

`manual/images/ui-shadow-gallery.png` is the canonical unretouched640x360 actual
GamePresentation/RmlUi/Diligent software Vulkan image of original Gallery162.
A 360x640 portrait was also personally viewed. Hard, left-offset, soft, stacked,
model-driven and presentation-time shadows use the production filter owner.
All four rendered checkpoints match source-hidden relocated content pixel for
pixel. PNG conversion preserves the captured PPM RGB bytes exactly; no controls
or pixels were retouched. Capture/source/binary/pair/test hashes are recorded
locally in `AgentFiles/build/phase256-ui-drop-shadows/capture-provenance.json`.
This isolated Game renderer capture does not establish full Main/editor, Windows,
physical GPU/input/DPI, graphical exported EXE or hardware performance acceptance.

## Runtime UI backdrop filters

`manual/images/ui-backdrop-gallery.png` is the canonical unretouched640x360 actual
GamePresentation/RmlUi/Diligent software Vulkan capture of original Gallery163.
The canonical and360x640 portrait were personally viewed. Four model/animation/
resize checkpoints match source-hidden relocated content exactly. The image shows
background blur/color effects, sharp foreground text and an ordinary foreground
filter. PNG conversion preserves captured PPM RGB bytes exactly. Source, binary,
image, pair and test hashes are recorded locally in
`AgentFiles/build/phase257-ui-backdrop-filters/capture-provenance.json`.
This isolated Game renderer capture does not establish full Main/editor, Windows,
physical GPU/input/DPI, graphical exported EXE or hardware performance acceptance.


## Runtime UI linear-gradient gallery

`manual/images/ui-gradient-gallery.png` is the canonical, unretouched640x360
GamePresentation/RmlUi/Diligent capture of original Gallery164. The isolated
software Vulkan probe renders the same saved Scene/UI source as runtime content.
Lossless PPM-to-PNG conversion preserves decoded RGB exactly. Its portrait360x640
capture was also visually reviewed; controls remain inside both layouts. Four
source checkpoints exactly match a relocated package with source files hidden.
Local capture, executable/source hashes and test receipts are recorded in
`AgentFiles/build/phase258-ui-linear-gradients/capture-provenance.json` outside
the product repository. This is local software-rendering evidence, not full
editor, Windows/MSVC, physical GPU/input/DPI or hardware performance acceptance.


## Runtime UI radial/conic gallery

`manual/images/ui-curved-gradient-gallery.png` is the canonical, unretouched
640x360 GamePresentation/RmlUi/Diligent software Vulkan capture of original
Gallery165. Lossless PPM-to-PNG conversion preserves decoded RGB exactly. Its
360x640 portrait capture was also visually reviewed; controls remain inside both
layouts. Four checkpoints from each of Gallery164/165 exactly match relocated
content with sources hidden. The previous linear-gradient canonical pixels are
unchanged. Local source/executable hashes and receipts are recorded outside the
product repository in
`AgentFiles/build/phase259-ui-radial-conic-gradients/capture-provenance.json`.
This is isolated local Game rendering, not full-editor, Windows/MSVC, physical
GPU/input/DPI or hardware performance acceptance. The numeric oracle excludes
exact discontinuous angular repeat seams and other rounded raster edges; it does
not assert device-independent seam antialiasing.


## Runtime UI alpha-mask gallery

`manual/images/ui-alpha-mask-gallery.png` is the canonical, unretouched640x360
GamePresentation/RmlUi/Diligent software Vulkan capture of original Gallery166.
Its360x640 portrait capture was also visually reviewed; controls remain inside
both layouts. Lossless PPM-to-PNG conversion preserves decoded RGB exactly.
Four source checkpoints exactly match relocated content with sources hidden,
including the canonical health PNG required by an explicit Runtime Dependencies
declaration. Snapshot isolation, shared-pool pressure and stale-frame checks are
separate numeric/native tests, not editor screenshots. Local source/executable
hashes and receipts are recorded outside the product repository in
`AgentFiles/build/phase260-ui-alpha-masks/capture-provenance.json`.
This is isolated local Game rendering, not full-editor, Windows/MSVC, physical
GPU/input/DPI or hardware performance acceptance. Numeric rounded-edge/angular
seam limits from the preceding gradient oracle remain in effect.

## Runtime UI box-shadow gallery

`manual/images/ui-box-shadow-gallery.png` is the canonical, unretouched640x360
GamePresentation/RmlUi/Diligent software Vulkan capture of original Gallery167.
The360x640 portrait capture is separately reviewed. Lossless PPM-to-PNG conversion
preserves decoded RGB. Source-hidden relocated content is compared at four
checkpoints: initial, presentation opacity pulse, live blur zero and portrait.
The pulse animates opacity; native box-shadow keyframes/transitions are unavailable.
Local source/executable hashes and receipts live in
`AgentFiles/build/phase261-ui-box-shadows/capture-provenance.json`.
This is isolated Game rendering, not full-editor, Windows/MSVC, physical
GPU/input/DPI or performance acceptance. Native rounded-boundary numeric tests
exclude a1.25-pixel edge band to avoid assuming a particular tessellation convention.

## Morphological anti-aliasing

`manual/images/morphological-aa-controls.png` is the unretouched native Scene
lighting control capture from the actual shared controls on the isolated SDL3
software fixture. It is not a capture of the full editor. The canonical
`manual/images/morphological-aa-gallery.png` is actual 640x360 Game rendering on
software Vulkan; a 360x640 letterboxed portrait is separately reviewed. Lossless
PPM-to-PNG preserves decoded RGB. Four animated source/relocated pairs match with
sources hidden. Hashes, binaries and receipts live in
`AgentFiles/build/phase262-morphological-antialiasing/capture-provenance.json`.
Offline manual pages are checked separately at wide/narrow widths. These captures
do not establish Windows/MSVC/FXC/D3D12, physical GPU/input/DPI, graphical EXE,
full-editor or hardware performance acceptance.


## Current-view occlusion culling

The canonical `manual/images/occlusion-culling-controls.png` captures the actual
shared Scene lighting controls through the isolated SDL3 software fixture; it is
not the full editor. The canonical `manual/images/occlusion-culling-gallery.png`
is actual 640x360 Game rendering of Gallery169 on software Vulkan. A 360x640
letterboxed portrait is separately reviewed. PPM-to-PNG conversion preserves
all decoded RGB bytes without retouching. Four moving source/relocated pairs are
exact with the source project hidden. Receipts, binary/source hashes and the
wide/narrow offline manual captures are recorded in
`AgentFiles/build/phase263-hierarchical-occlusion-culling/capture-provenance.json`.
The offline renderer uses its existing 260915-000006 synthetic test identity; it
is not a new release. These captures do not establish Windows/MSVC/FXC/D3D12,
full-Main, graphical EXE, physical GPU/input/DPI or hardware performance acceptance.

## Audio streaming controls

`manual/images/audio-streaming.png` is an unretouched760x1020 capture of the
production AudioImportEditor, AudioPreviewEditor, shared typed import controls
and Clip details widgets. The isolated fixture uses the pinned ImGui SDL3 software
backend and the original Gallery172 clip. It opens the actual Import settings
header through ordinary input. Companion wide/narrow100/150/200percent captures
and actual runtime Live sound/mix Pause/Resume checks remain in local validation
evidence; they are not additional canonical manual assets.

The canonical capture shows published stereo48000Hz metadata and the `stream`
choice. Later selection of the Windows/D3D12 cook target changes no visible
metadata or widgets. Source/binary/image hashes and limits are recorded in
`AgentFiles/build/phase266-audio-streaming/capture-provenance.json`. These captures
do not establish full Windows Main, physical device playback, physical input/DPI
or hardware performance acceptance.


## Animated UI shadow panels

[Animated shadow gallery](../../../manual/images/ui-shadow-motion-gallery.png)
shows the authentic640x360 shared Game UI renderer in Gallery173 at presentation
time1.1s with gameplay paused (tick0). The Color / offset card has interpolated
shadow controls; the Live card retains its saved blur4. The unretouched RGB PPM
readback is losslessly transcoded to PNG with every pixel and dimension retained.
The640x360 and360x640 layouts were personally reviewed. Four source/source-hidden
relocated export captures match exactly, covering paused motion, live tuning and
portrait resize. Independent144case main/output SDR/HDR/scaled numerical checks
use sharp rectangle/color oracles and ordinary native static-shadow references
for blurred composition.

Local unassigned source is based on5ad842f. Exact source, executable, original
readback, PNG and relocated-pair hashes are owned by
`AgentFiles/build/phase267-ui-shadow-animation/capture-provenance.json`.
This software Vulkan evidence does not establish full Main/Windows/MSVC/D3D12,
physical GPU/input/DPI, graphical EXE, external-client or performance acceptance.


## Interactive scene UI surfaces

[Scene surface controls](../../../manual/images/ui-surface-gallery.png) shows the
actual640x360 GamePresentation output in Gallery174, paused at tick1 after a
native Step click on the left Quad. The right monitor displays the same output
through its Camera relay. The native control focus, tick text, field, guide and
rendered scene are unretouched. The RGB PPM readback was losslessly transcoded to
PNG with every pixel and dimension retained. The capture was personally reviewed.
PNG SHA256: `e9099dc0f237e9f4aebab75af0d04b276f581b15a5d08606d8b1ade8740b7e93`.

Local unassigned source is based on58dfdb6. Exact source, executable, original
readback, PNG and three source-hidden relocated Game image-pair hashes are owned
by `AgentFiles/build/phase268-ui-surface-input/capture-provenance.json`. The actual
GamePresentation fixture verifies one fixed Step, native field text and identical
relocated pixels. Low-level retained geometry tests separately cover UV mapping,
Camera flips/layers, capture, blockers, producer loss and large-world projection.
This software Vulkan capture does not establish full editor Main/Windows/MSVC/
D3D12, physical GPU/input/DPI, graphical EXE, external-client or hardware
performance acceptance. Those platform checks remain queued.

## Runtime reflection capture

The canonical images are `manual/images/runtime-reflection-controls.png` and
`manual/images/runtime-reflection-capture.png`. The first is the actual production
Inspector rendered by the native ImGui/SDL software capture fixture with all
seventeen reflected fields. The second is a 480×270 native FrameRenderer image from
Gallery176 after actual RuntimeWorld transform animation advanced. The game Camera
hides the moving source panels; their changing shapes remain in local reflections.
Only lossless PPM-to-PNG conversion was applied; controls and pixels were not retouched.

`tests/runtime_reflection_gallery_tests.cpp` compares six wide/tall initial,
moving and static-fallback readbacks after runtime packaging/relocation with the
original source root hidden. It also checks paused simulation against changing
presentation time and verifies authored Scene bytes remain unchanged.
`tests/runtime_reflection_vulkan_probe.cpp` covers native HDR faces, convolution,
intervals, staged refreshes, two-probe capacity, inactive probes, failed refreshes,
camera-output borrowing/feedback, physical payload admission and same-time view
resets. `tests/reflection_probe_editor_tests.cpp --capture-runtime` produces the
control image. Native field/history/Save controls run at 100/150/200 percent scales;
installed matching-SDK checks cover the appended values, both hosts and recovery.

Local capture source hashes, image hashes/dimensions, exact source-free receipt and
check attribution are retained in the runtime-reflection-capture evidence owner.
Later changes to validation message prefixes and editor session reset routing do
not change the Gallery's pixel path. Software Vulkan and isolated native control
captures are separate from Windows/full-editor, physical GPU/input/DPI, graphical
executable and performance acceptance. No GPU sanitizer execution is claimed.


## Animated triangle mesh UI input

The canonical `manual/images/ui-mesh-surface-controls.png` is a 1080×320 native
ImGui/SDL Inspector capture from `tests/ui_surface_editor_tests.cpp --capture`.
Geometry Mesh is selected through the actual property dropdown; its output is
intentionally unassigned. The test checks both choices and ordinary Undo/Redo at
100 and150 percent UI scale. The native window is sized for capture; no pixels or
controls are retouched.

`manual/images/ui-animated-mesh-gallery.png` is a 640×360 native GamePresentation
and RuntimeWorld software Vulkan readback from Gallery177 after Step and native
text input. The original glTF contains two parts, one support joint and a Bend
morph. The Gallery test checks the actual morph and joint pose change and compares
three exact readbacks after runtime packaging and relocation with source content
hidden. PPM-to-PNG conversion is lossless and applies no image editing.

The same graphical gate preserves the original Quad example and checks missing
geometry-shader support rejects preparation with its terminal diagnostic. Local
source/check/image hashes and the export receipt belong to the triangle-ui-surfaces
capture evidence owner. These captures do not establish Windows/full-editor Main,
MSVC/D3D12, physical GPU/input/DPI, external-client, graphical executable, broad
performance or native graphics sanitizer acceptance.

## Texture painting on model geometry

`manual/images/mesh-texture-painting.png` is an unedited1220×850 native capture of
PaintTextureEditor beside HDR Viewport from `tests/mesh_texture_surface_vulkan_probe.cpp`.
The real CameraControls/ImGui path drives a brush on an original retained bent
morph mesh. Source pixels, one-stroke history, published display pixels, foreground
blocking and gesture cancellation are checked by the same fixture. It is a
subsystem workspace, without the complete Windows Main host or physical input.

`manual/images/mesh-texture-painting-game.png` captures Gallery178 at tick60 through
RuntimeWorld/FrameRenderer. The independently authored orange/cyan paths use the
shared Mesh/Material UV mapper and paint-source brush. Three moving views and a
portrait view match exactly after source-hidden runtime-content relocation.
PPM-to-PNG conversion is lossless; no controls or pixels are retouched. The existing
canvas image is a current direct capture with Mesh mode off. Each image has one
canonical manual asset; [Texture assets](../../texture-assets.md) records the
capture/test environment and limits. These are Release/GCC12 software Vulkan
captures; Windows, physical GPU/input/DPI and graphical executable review remain
separate.

## Fresh game recording

`manual/images/game-recording-options.png` is the production recording dialog;
`manual/images/game-recording-frame.png` is the recorded Gallery182 Game display.
Both are unretouched canonical PNGs. [Game recording](../../game-recording.md)
records hashes, capture sources, independent movie/audio validation and software
Vulkan limitations. The fixture exercises production scoped recording,
PlayPresentation, runtime UI and six control layouts; complete Windows Main,
physical GPU/input/DPI, external client and player review remain separate.
