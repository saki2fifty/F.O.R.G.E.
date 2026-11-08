# F.O.R.G.E.

[![CHANGELOG — Browse daily updates](changelog/badge.svg)](changelog/README.md)

**Flecs-Oriented Runtime & Game Editor**

Native ECS game editor under development. Windows/D3D12 is the first editor target; the portable core can be built and tested on Linux.

## Technology stack

| Layer | Technology | Role in FORGE |
| --- | --- | --- |
| Engine and gameplay | C++20 | Native engine, editor, and project Components and Systems through the matching FORGE SDK. |
| Internal codec adapters | C17 | Private image codec boundaries. |
| Entity component system | Flecs | Entities, components, queries, reflection, relationships and scheduling. |
| Platform | SDL3 | Windows, input events, dialogs, process pipes and application paths. |
| Editor interface | Dear ImGui docking | Editor panels, inspectors, workspaces and controls. |
| Rendering | Diligent Engine and selected DiligentFX utilities | Shared Scene/Game renderer, PBR materials and GPU resources; D3D12 editor with Vulkan validation probes. |
| Shaders | HLSL | Surface shaders and shader generation from FORGE material graphs. |
| Game interface and fonts | RmlUi and FreeType | Runtime HUD documents, input, text and bundled-font rendering. |
| Physics | Jolt Physics and Box2D | Jolt supplies 3D bodies, queries, constraints and character movement; private Box2D supplies XY bodies, colliders, joints, sensors, queries, painted Tile Map collision and native 2D character movement. |
| Skeletal animation | Ozz Animation | Animation sampling, blending and glTF animation conversion. |
| Navigation | Recast and Detour | Navigation mesh generation, static collision collection, dynamic obstacle updates, authored links, path queries and local steering. |
| Audio | miniaudio | WAV/FLAC/MP3 decoding, resident or bounded streamed playback, spatial sound and mixing. |
| Serialization | nlohmann/json | Authored JSON documents and process messages. |
| Mesh processing | meshoptimizer and Draco | Geometry processing and glTF compression decoding. |
| FBX model parsing | ufbx | Private supervised FBX parsing, coordinate conversion, triangulation and animation baking feeding the shared model cooks. |
| Texture and image processing | KTX Software/Basis, libpng, libwebp and stb | Texture import, image decoding, lossless PNG/WebP and opaque JPEG still output, bounded silent WebP sequence animation, stored8/16-bit PNG texture export, decoded BC texture still output, bounded Radiance HDR output with labelled PNG inspection and cooked texture processing. |
| Build tools | CMake and Ninja | Dependency configuration, incremental builds and packaging. |
| Compilers | MSVC and GCC | Windows builds and portable core validation. |
| Tests and automation | CTest, Python and GitHub Actions | Build automation, runtime checks, native editor validation and delivery gates. |

Exact revisions, enabled dependency features and license details are recorded in the [dependency baselines](docs/dependencies.md).

## Editor screenshots

Real Windows editor captures using small validation scenes. Click an image to open it at full size.

[![Scene authoring with the Hierarchy, transform tools, Inspector and unified Content browser](manual/images/scene-editor.png)](manual/images/scene-editor.png)

| [C++ components and systems](manual/editor/native-gameplay.md) | [Material graphs and shader preview](manual/editor/material-graphs.md) |
| :---: | :---: |
| [![Integrated C++ editor showing a rotation system and project code in Content](manual/images/cpp-editor.png)](manual/images/cpp-editor.png) | [![Material graph connecting a color parameter to surface output with a rendered sphere preview](manual/images/material-graph.png)](manual/images/material-graph.png) |
| **[Animation states](manual/editor/animation-states.md)** | **[Mesh editing and sculpting](manual/editor/editable-meshes.md)** |
| [![Animation state graph with connected Idle, Walk and Run states](manual/images/animation-states.png)](manual/images/animation-states.png) | [![Editable mesh workspace showing a wireframe sphere and sculpt brush symmetry controls](manual/images/mesh-sculpting.png)](manual/images/mesh-sculpting.png) |
| **[Sequence camera cuts and transitions](manual/editor/animation-sequences.md)** | **[Live Inspector tuning](manual/editor/play-mode.md)** |
| [![Animation sequence timeline with camera shot intervals and camera selection](manual/images/camera-sequencing.png)](manual/images/camera-sequencing.png) | [![A running game with C++ component properties adjustable in the Live Gameplay Inspector](manual/images/live-inspector.png)](manual/images/live-inspector.png) |

[Capture sources](docs/images/readme/README.md) · [Implementation and delivery status](docs/status.md)

[Smooth Camera Shot transitions](manual/editor/animation-sequences.md#camera-shots)
move between adjacent camera views and lenses while preserving animated camera rigs.
Gallery153 includes an incoming blend, a return blend and an overview gap.
[Nested playback rates](manual/editor/animation-sequences.md#place-a-sequence-inside-another)
let child sections run faster or slower while one root controls the arrangement.
Gallery186 includes direct, morph and mapped animation with fast/slow/nested timing.
[Eight clips per track](manual/editor/animation-sequences.md#arrange-up-to-eight-clips-on-one-object)
share the same sequence clock with adjacent overlaps, held gaps and one-step Timeline edits.
[Layer overlapping clips](manual/editor/animation-sequences.md#layer-more-than-two-overlapping-clips)
opts into up to eight simultaneous contributors, with complete live influence
inspection. Gallery190 demonstrates locomotion, morph and mapped poses using the
same saved clips and source-hidden game export.
Gallery187 includes direct, morph and mapped arrangements, with the same saved content in exported games.
Nested controllers can also repeat through their parent with **Loop in parent**.
Gallery188 demonstrates fast, morph and nested mapped sections; the root retains
playback control. See [child sequence loops](manual/editor/animation-sequences.md#place-a-sequence-inside-another).
Gallery189 adds up to eight independently timed placements of one child on its
parent, with finite sections, held gaps and per-placement rates/loops. Timeline
edits share one Scene Undo operation; live controls show the selected placement.
See [repeated controller placements](manual/editor/animation-sequences.md#repeat-a-controller-at-separate-times).


## Texture painting

Author Color or Data pixels with brush, erase, fill, published-image copy and
stroke Undo/Redo. Paint on a selected Mesh in Scene through its displayed pose
and UV0, using the same source and publication workflow. Save / Publish prepares
an ordinary Texture for Materials, Sprites and game export.

[![A painted Texture shared by a rotating cube and Sprite](manual/images/texture-painting-game.png)](manual/editor/texture-painting.md)

See [Paint a texture](manual/editor/texture-painting.md) for controls and limits.

[![Paint Texture controls and the same published stroke on a bent Mesh in Scene](manual/images/mesh-texture-painting.png)](manual/editor/texture-painting.md)

## Live camera images

[Render textures](manual/editor/render-textures.md) display a Camera's live scene
image on a Mesh. Gallery152 combines two Cameras into one split-screen image and
relays it through a second Camera to another monitor.

[![Two monitor meshes display red and cyan camera feeds, including a same-frame camera relay](manual/images/render-texture-gallery.png)](manual/editor/render-textures.md)

Production Game renderer capture on software Vulkan. Saved outputs, Material
bindings, source controls, layer masks and export are explained in the guide.

[Native UI filters](manual/editor/runtime-ui.md#apply-blur-and-color-filters)
include bounded blur, grayscale, brightness, contrast and opacity. Gallery155
compares the same shapes and exposes paused Inspector blur tuning.

[![Native Runtime UI reference and filter samples with Inspector blur guidance and pause controls](manual/images/ui-filter-gallery.png)](manual/editor/runtime-ui.md#apply-blur-and-color-filters)

Actual GamePresentation capture on software Vulkan. Filters use the same source,
model, reload and package owners; Windows/physical-device acceptance is pending.

[Runtime UI in camera images](manual/editor/runtime-ui.md#draw-ui-in-a-camera-image)
adds a document after the producer Cameras and before Materials sample the output.
Gallery154 includes upright telemetry on direct/relayed monitors, paused value
editing and a separate main-window guide.

[![Telemetry UI appears on both camera-image monitors with an independent main Game guide](manual/images/camera-output-ui-gallery.png)](manual/editor/runtime-ui.md#draw-ui-in-a-camera-image)

Actual GamePresentation capture on software Vulkan. Output surfaces are display-only;
source-hidden relocated exports match the original pixels.

[Interactive scene UI](manual/editor/runtime-ui.md#interact-with-ui-on-a-scene-surface)
adds native focus, text and pointer capture to a matching output Material. Use
Geometry **Quad** for the built-in plane, or **Mesh** for retained triangle UV0
with supported skin/morph poses. The Feature Gallery includes separate scenes for
both workflows; source authoring, automation/MCP and exports use the same owners.


## Projected material color

[Decal Projectors](manual/editor/decals.md) project color, normal detail and roughness onto lit Mesh
surfaces with editable boxes, opacity, angular fade, layers and stable priority.
Gallery115 compares normal-only, roughness-only and combined projections; Gallery113 supplies mirrored/tinted badges and overlapping floor paint.

[![Normal detail on the left cube, a roughness patch on the right and both channels on the floor](manual/images/decal-surface-gallery.png)](manual/editor/decals.md)

Shared Game renderer capture on software Vulkan. The guide explains source materials,
live Inspector controls, saving/export and the bounded color profile.

## Tile Maps

Local source includes camera-driven loading of decoded Tile chunks through the
shared Scene/Game resource owner. Gallery126 visits five regions of one complete
sparse Map, retaining two visible pages per stopped view. See
[loading and export](manual/editor/tile-maps.md).

[![The cyan first region in the camera-driven Tile chunk Gallery](manual/images/tile-chunk-residency-gallery.png)](manual/editor/tile-maps.md)

[Tile Set and Tile Map tools](manual/editor/tile-maps.md) provide palette selection,
layered painting, stroke Undo/Redo and Inspector settings. These are the actual
Windows document controls rendered by the native validation fixture:

[![Tile Map document with arrow palette, painting controls, layered canvas and cell-size Inspector](manual/images/tile-map-authoring.png)](manual/images/tile-map-authoring.png)

The editable Gallery scene uses two layers and a mirrored instance of the same
map. Native Game rendering on Linux software Vulkan:

[![Two arrow courses rendered from one Tile Map, including a smaller mirrored instance](manual/images/tile-maps.png)](manual/images/tile-maps.png)

Tile definitions also support ordered neighbor rules. One painted tile can choose
edge images, animation and collision while its saved cell key stays unchanged.
Gallery122 includes a hole, a flipped L and animated connected interiors.

[![Neighbor-selected arrow edges around a hole, a flipped L and an isolated cell](manual/images/neighbor-tile-gallery.png)](manual/editor/tile-maps.md#choose-tiles-from-their-neighbors)

This is the shared Game renderer on Linux software Vulkan. Inspector controls,
saving, publishing and export are covered in the guide.

## Maintained Sprite atlases

Create an atlas recipe from published images, then use stable frame keys on
Sprites. Input changes rebuild the packed Texture through the existing source
pipeline. Gallery 127 includes editable sources and held frame animation; the
[source workflow](manual/editor/sprites.md#maintain-an-atlas-from-its-source-images)
uses the same editor and MCP document history.

[![Gallery 127 showing original image selections and animated stable atlas keys](manual/images/source-aware-sprite-atlas.png)](manual/images/source-aware-sprite-atlas.png)

## Sprite image regions

Sprite Renderer can select part of an existing image, animate frames inside it,
resize its borders or repeat the selected area. Gallery78 contains editable
examples. Build208 includes this workflow and the matching C++ SDK. The native
Windows validation capture shows the actual whole-image, cropped and filled objects:

[![Gallery78 with whole and cropped arrows, an animated strip, a bordered panel and mirrored repeated arrows](manual/images/sprite-image-regions.png)](manual/images/sprite-image-regions.png)

## 2D physics

Local source supports moving one-way Kinematic Boxes and full-cell Tile Maps,
including optional character support transport. Editable Gallery119 demonstrates
an amber box landing while a cyan circle passes through moving surfaces. This
development Game capture uses software Vulkan; Windows acceptance is pending.
See the [2D physics guide](manual/editor/physics-2d.md) for setup and limits.

[![Amber box resting on a moving one-way ledge while the cyan circle has passed both moving surfaces](manual/images/moving-one-way-platforms.png)](manual/images/moving-one-way-platforms.png)

Build206 adds the editable [C++ Character2D project](samples/projects/Character2D/README.md).
Its native 2D controller provides grounded movement, sliding, floor support and
checked placement. Project C++ supplies gravity and jumping. The actual Windows
Scene/Inspector capture shows copied ground, speed and blocked-placement feedback:

[![Native 2D capsule supported by a moving platform, with ground and velocity feedback in the Live gameplay Inspector](manual/images/2d-character-inspector.png)](manual/images/2d-character-inspector.png)

The [complete Build206 gate](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/39822638412)
passes the editor, matching C++ SDK and source-free exported game checks. These
captures use software D3D12 WARP; physical GPU performance remains unmeasured.

The original [C++ 2D Movement project](samples/projects/Movement2D/README.md) adds
editable walking/jumping on painted and ordinary floors, using capsule support
sweeps and the existing Dynamic body. Speed and jump settings are Inspector
properties. Build204 includes the prepared project and matching compiled C++.
Its [Windows acceptance](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/39761476803)
checks both Play hosts and the relocated exported game with native keyboard input.
The actual exported game's jump, rendered with D3D12 WARP:

[![Amber player jumping through a translucent sensor above painted platforms and a blue stepping stone](manual/images/2d-movement-runtime.png)](manual/images/2d-movement-runtime.png)

The editable [2D physics gallery](manual/editor/physics-2d.md) combines falling
Sprite shapes, a moving Kinematic platform and a pass-through sensor. This is an
actual Windows Game render using the D3D12 WARP software adapter:

[![Three boxes, a circle and a capsule rest inside a 2D physics playground below a green sensor and blue moving platform](manual/images/2d-physics-runtime.png)](manual/images/2d-physics-runtime.png)

### Painted collision

Gallery77 uses a painted Tile Map layer for static floors and platforms. Four
Dynamic shapes land on the green tiles; the blue arrows remain decorative.
Select **Collision layer** in the Inspector and see its collision boxes in Scene.
Gallery81 adds an animated Kinematic painted course;82 uses painted sensor regions
above a solid receiver. See the [Tile Maps guide](manual/editor/tile-maps.md).
Gallery101 adds per-tile **Full cell**, **None** and **Convex polygon** profiles,
with mirrored ramps and decorative tiles. Edit normalized vertices in the Tile Set
Inspector, publish the Set and Map, then test Static, Kinematic or Dynamic collision.
Gallery120 adds **Polygon (decomposed)** for3–8 vertex simple concave outlines,
with falling actors landing on upper edges and inside L/U notches. Contours / holes supports bounded voids and separate
solid regions. Gallery124 adds World-bound Dynamic compound Maps with density or
mass override, reflection and rotation locks. All pieces share one body and the
usual C++ controls, cell edits and export workflow.

Gallery125 demonstrates Static/Kinematic nonsensor custom one-way boundaries:
actors land on original roof, hole-floor and island edges while passing through
from the back. Decomposition diagonals never become one-way surfaces.

[![Actors resting on a ring roof, its hole floor and island, with a violet actor above the ring at a ceiling](manual/images/custom-one-way-gallery.png)](manual/editor/tile-maps.md#make-painted-platforms-one-way)

[![Three compound Tile Maps falling toward a receiver, including mirrored and rotation-locked instances](manual/images/dynamic-tile-map-gallery.png)](manual/editor/tile-maps.md#make-a-falling-or-tumbling-map)

These extensions are local development source;
Windows and physical GPU verification remain pending.
This actual Windows D3D12 WARP Game capture demonstrates the original static course:

[![Four colored shapes resting on painted green platforms while blue arrows remain decorative](manual/images/painted-collision-game.png)](manual/images/painted-collision-game.png)

## User manual

Read the [FORGE User Manual](manual/README.md) for feature explanations and how-to guides. Windows packages include an offline edition under **Help → User Manual**. Technical implementation documents remain in `docs/`.

## Game modes and playable menus

[Application States](manual/editor/application-states.md) provides menus, playing
modes, parent states, subordinate choices and computed results. Gallery134's
**Signal Lock** puzzle uses the source editor, game UI and shared runtime controls
without a project C++ module.

[![Signal Lock puzzle with Cyan and Right selected and an open connection](manual/images/application-states-game.png)](manual/editor/application-states.md)

**State Condition** connects an object's stock activity to current state
membership and structural parent conditions. Gallery135 **Activity Lab** combines
animation, physics, particles, source audio and a computed UI badge, with controls
that also work while paused. Existing enable fields stay authored; each subsystem
retains its normal disable/re-enable lifecycle. Project C++ systems use explicit
execution conditions.

[![Activity Lab native controls and computed state badge in a narrow game view](manual/images/state-conditions-game.png)](manual/editor/application-states.md)

Actual native Game UI on software Vulkan. Source-free relocated exports match the
same views; physical Windows/full editor and graphical EXE review remains pending.

## Current implementation

**Windows Build 213:** [native MCP authoring tools](manual/editor/mcp.md) connect AI
clients to the editor's existing project, scene, asset, C++, Play, capture and
export tools. Separate grants control native execution and output folders.
The [packaged Windows check](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/40093162001)
creates a C++ cube project, compiles and plays it, captures the Game view, and
verifies its saved C++ behavior in the exported game. Automated rendering uses
D3D12 WARP and offline audio; physical hardware and AI application testing remain
separate.
See the [capability checklist and validation limits](docs/mcp.md).

- Flecs scene state, stable authored IDs, independent local translation/rotation/scale, derived world transforms, structured prefabs, reflected properties, atomic scene replacement and saves, undo/redo, and unknown-component preservation.
- Runtime-owned fixed clock, unscaled C++ timers during Pause/zero speed, Pause/Step/Resume, derived hierarchical pose interpolation and a separate Play process with protocol 2. Project C++ gameplay registers Flecs components and systems through the exact matching SDK.
- Built-in C++ source editing, Component/System creation, incremental CMake builds and isolated candidate validation. Code iteration uses Stop → Save → Build → Play; changed source is never silently run or exported from an older module.
- Restart-bound plugin package validation/staging library. Native editor-plugin loading is not implemented.
- Windows editor source with SDL3, Dear ImGui docking, persistent workspace/tooltips, Hierarchy/Inspector/Content/Scene panels, CPU performance diagnostics, and shared Diligent Scene/Game rendering with imported models, PBR materials, cameras, lights, skins and morphs. Windows compilation is validated in GitHub Actions; the initial editor and subsequent UI controls have been exercised on a Windows desktop.

- [Structured prefab assets and single-base variants](manual/editor/prefabs.md) with stable member identities, local additions, retained removal/Restore, parent/spatial/order overrides, component suppression and reviewed base changes. Native and MCP source editing share candidate validation; Gallery206 is editable and exportable.
- [Skeletal animation](manual/editor/animation.md): validated glTF conversion, Animator playback, shared Scene sequence tracks with independent clip/morph phases, fixed-clock recovery and debug bones.
- [World sprites and image sheets](manual/editor/sprites.md): ordinary image Materials, sheet frames, size/pivot/flip/tint controls, geometric picking and step-valued Timeline animation. Original Gallery scenes71/72 demonstrate still images and animated signals.
- Exact-version shared-Flecs C++ gameplay SDK, [Jolt physics](manual/editor/physics.md), and [Audio sources/listeners, import and bounded streaming](manual/editor/audio.md).

- Model/texture/material/shader importing, cooked resources and dependency-aware, relocatable game export with a standalone SDL/Diligent/RmlUi executable.
- [Live Camera images](manual/editor/render-textures.md): saved output sizes, shared-camera composition and dependency-ordered Mesh Material sampling. Gallery152 includes split and relayed outputs with rotating scene sources.
- [Sequence-driven UI](manual/editor/sequence-ui.md): edit named numbers in Inspector, animate keyed values with the Scene clock and bind them to RML text, styles and classes. Gallery151 works without project C++; native CSS motion retains presentation time.
- [Animated UI shadows](manual/editor/runtime-ui.md#animate-a-panel-shadow): native numeric/color keyframes, hover transitions, inset switching and live named values share the existing source, MCP and export workflow. Gallery173 includes editable and exportable samples.
- [Navigation](manual/editor/navigation.md), with static collision collection, authored directional links and local source for [patrol, wait, chase graphs and behavior trees](manual/editor/navigation-behavior.md), shared editor/MCP authoring and ready Gallery131/132/139/140/141 examples; [runtime UI](manual/editor/runtime-ui.md) and the [asset workflow](manual/editor/content-browser.md).
- [Application States](manual/editor/application-states.md): authored direct/hierarchical/computed/subordinate modes, source and Scene history, runtime-only Game/UI controls, native conditions/events, matching SDK and MCP access, explicit stock activity conditions, runtime-created root lifetimes, and playable Gallery134/135.
- [Localized game menus and HUDs](manual/editor/ui-localization.md): one Scene language controller, shared source/editor/MCP catalog authoring, literal UTF-8 text, deterministic fallback and paused language controls. Original Gallery133 includes English, French and Spanish. Full shaping and editor translation are separate unsupported capabilities.
- [Phase8 runtime foundations](docs/game-foundation.md): resource-gated scene sessions, separate versioned player saves/settings, OS user-data storage, collision assets and a Jolt character controller.
- [Gameplay services](docs/gameplay-services.md): input contexts, player rebinding, bounded gamepad vibration and queued session/save requests through the exact SDK. The [reference game](manual/reference-game.md) exercises menus, first-person movement, interaction, transitions and save/relaunch/load in the standalone host. Editor Play invokes project gameplay through its separate runtime session.

Advanced authoring systems and a broad public plugin SDK remain planned. See [implementation status](docs/status.md) for validation and the current delivered build; source implementation and downloadable delivery are recorded separately.

## Explore features

The [Feature Gallery](manual/getting-started/feature-gallery.md) is an editable sample project with focused scenes for transforms, lighting, projected material color, physics, cameras, animation and world sprites. Open a scene in the editor, change its authored components, then select it as the startup scene and export a standalone game. The [reference game](manual/reference-game.md) remains the combined gameplay example.

The separate [AudioImpulseResponses project](samples/projects/AudioImpulseResponses/README.md)
includes original pulse/response audio, selected resident audio, a Camera and
project bus settings for comparing dry and processed playback. Its project profile
is independent of the Gallery. See [the impulse-response guide](manual/editor/audio.md#process-a-bus-with-an-impulse-response)
for import, saving, limits and export.


[Runtime reflection capture](manual/editor/lighting.md) updates local specular and
diffuse lighting from moving scene sources in Scene, Game and camera outputs.
Gallery176 demonstrates simulation-time refresh and a saved fallback; Inspector,
live tuning and automation/MCP share the capture controls.

Scene lighting includes independently authored [fast approximate anti-aliasing](docs/fast-approximate-antialiasing.md)
for Scene/Game display color. Gallery 143 demonstrates the saved workflow and normal
standalone export; temporal AA and post-filter Game UI remain independent.

Flat Mesh creation includes simple concave outlines through **Triangulated outline (XY)**.
Up to16 boundary corners become ordinary editable triangles with +Z fronts and
bounds UVs, using the same Source/native/MCP owners. Gallery202 demonstrates L shapes
from opposite windings and a concave arrow with source-hidden export proof. See
[flat Mesh creation](manual/editor/editable-meshes.md).

The Scene foliage brush paints bounded strokes on published Mesh or heightfield
surfaces. Pointer markers preview sampled centers; release places ordinary objects
with one Undo. Gallery201 contains two editable saved strokes, and automation/MCP
use the same surface preparation and Scene command. See
[Paint foliage in Scene view](manual/editor/foliage.md#paint-foliage-in-scene-view).

Foliage placement creates bounded seeded groups of independent Mesh Renderer
objects through **Tools > Foliage placement...** and the same automation/MCP command.
Spacing, yaw/scale variation, complete layout checks and one-step Scene Undo use the
normal object and asset owners. Gallery 144 includes an original editable blade Mesh
and two patches for saving, Play and export. Gallery147 adds published-heightfield
projection. Optional world-space slope ranges keep a subset of the seeded candidate
pool through the same native and MCP preparation. Optional slope alignment tilts
the placement frame while preserving relative template rotation. Optional sphere clearance rejects candidates against existing authored 3D colliders through the same editor/MCP task. Gallery 156 provides a saved meadow around a box collider; the result remains ordinary Scene objects. See [the guide](manual/editor/foliage.md).

Gallery157 adds bounded static-Mesh wind with Inspector/live controls, fixed-clock
Pause/Step, shared shading/shadows/picking and source-free export. Its moving and
still patches use the same original blade asset; no project C++ is required.
See [Wind](docs/mesh-wind.md) and the illustrated [workflow](manual/editor/foliage.md#add-wind).

![Wind-enabled meadow beside a still comparison patch](manual/images/foliage-wind-game.png)

![Green and amber foliage patches in the Game view](manual/images/foliage-placement-game.png)


## Build and test
Requires Git, CMake 3.30+, Ninja, Python 3.10+, and a C++20-capable toolchain. Windows requires an MSVC developer shell and Windows SDK.

```sh
cmake --preset core
cmake --build --preset core
ctest --preset core
```

The presets place local builds and dependency working directories in the workspace sibling `AgentFiles`. For another layout, configure explicitly:

```sh
cmake -S . -B /absolute/path/to/work/build -G Ninja -DFORGE_BUILD_EDITOR=OFF
cmake --build /absolute/path/to/work/build
ctest --test-dir /absolute/path/to/work/build --output-on-failure
```

Windows editor (requires D3D12-capable hardware to run):

```sh
cmake --preset windows-editor
cmake --build --preset windows-editor --target forge_editor forge_runtime
../AgentFiles/build/windows-editor/forge_editor.exe /absolute/path/to/game-project
```

The editor opens the project manifest startup scene, with legacy `main.scene.json` folders supported. See [Projects](manual/editor/projects.md) and [Scenes](manual/editor/scenes.md). User layout/settings are stored through SDL's application preferences directory.

See [native iteration](docs/native-modules.md), [scene format](docs/scene-format.md), and [dependency baselines](docs/dependencies.md).

## Editor controls

- **Ctrl+Minus**: reduce interface size to fit more controls on screen.
- **Ctrl+Plus** (or **Ctrl+Equals**): increase interface size.
- **Ctrl+0**: reset to 100%. Keypad plus/minus/zero are supported; zoom is saved between launches.
- **Play**: preview an isolated copy of the current authored scene.
- **Restart**: replace the play world with a fresh copy, including your latest authored edits.
- **Stop**: return the viewport to authoring. Inspector edits and Save always affect the authored scene.

Use **Build → C++ setup → Set up C++ code** for a new project. Find and create Components and Systems in **Content → Code**, edit them in **C++ Sources**, then choose **Build Project Code** and Play. Build on Save is optional. See [C++ gameplay](manual/editor/native-gameplay.md) for the Windows developer launcher and tool requirements.
