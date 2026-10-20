# README screenshots

## Native Tile Map render

[Tile Map gallery](tile-maps.png) is a 1280×720 native Game rendering of
`Scenes/74-tile-maps.scene.json` using Linux software Vulkan. It shows the two
actual map instances, shared original arrow image, reflected transform and layer
markers. `tests/tile_gallery_render_tests.hpp` captures this output and checks
projected cell colors at two resolutions. The PPM readback was encoded as lossless
PNG with its pixels and dimensions preserved. Windows execution is a separate gate.

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
| [Scene editor](scene-editor.png) | `grid-test-images/editor/editor-default-scene-inspector.ppm` |
| [C++ editor](cpp-editor.png) | `onboarding with spaces/editor-55-cpp-code-selected.ppm` |
| [Material graph](material-graph.png) | `grid-test-images/editor-workflow/editor-258-material-graph-workspace.ppm` |
| [Animation states](animation-states.png) | `grid-test-images/animation-root-motion/editor-animation-state-graph.ppm` |
| [Mesh sculpting](mesh-sculpting.png) | `grid-test-images/editor/editor-editable-mesh-sculpt-symmetry.ppm` |
| [Camera sequencing](camera-sequencing.png) | `grid-test-images/editor-workflow/editor-562-camera-shots-timeline-100.ppm` |
| [Live Inspector](live-inspector.png) | `onboarding with spaces/editor-136-cpp-rotator-live-tuned.ppm` |

The captures use D3D12 WARP software rendering. Status-bar timings and diagnostic
counts belong to the validation fixture and its current test stage; they are not
physical graphics-card performance measurements. Some fixture stages retain
diagnostics from intentional failure checks. The small scenes demonstrate editor
workflows rather than finished game artwork. Sample-content attribution remains
in the [Feature Gallery](../../../samples/projects/FeatureGallery/README.md).
