# Runtime UI

The standalone host can request a presentation-only FPS panel through
`UiPresenter::frame_statistics`. Empty text hides it; the default creates no
diagnostic document. The text is validated UTF-8 capped at128bytes and inserted
as a literal text node, without RML parsing or model binding. One private document
per existing context uses the same resource admission, font and renderer. It
stays outside authored document counts, source provenance and gameplay protocol
models, and takes no input/focus. Live/staged sets keep their ordinary lifetime.
See [standalone presentation statistics](standalone-host.md) for launch and timing.

Phase 6F adds screen-space game UI using RmlUi 6.3. Dear ImGui continues to own the editor. The [user manual](../manual/editor/runtime-ui.md) describes creating a HUD without coding.

## Ownership and composition

`forge.ui` uses EngineModule schema registration and a runtime-only UiService. Authoring worlds register `UiDocument` without initializing a UI service or RmlUi. Headless runtimes omit the service by default (`--ui off`). `forge_runtime` links no RmlUi, FreeType, Diligent or ImGui.

During editor Play, the separate runtime process owns gameplay, authored UI configuration, copied model production, command validation and the fixed clock. The reusable `forge_ui_presenter` owns RmlUi and its documents, copied bindings, fonts and input state. `forge_ui_diligent` owns the graphics adapter. They do not depend on ImGui or an authoring WorldContext. The editor hosts them beside its existing viewport renderer; gameplay DLLs remain outside that process.

The standalone `forge_game` executable composes gameplay, Diligent and the same presenter in one process. The private bridge does not require permanent IPC. See [standalone host](standalone-host.md). Native RmlUi/presentation faults share the editor's failure domain; process isolation protects against gameplay worker faults, not every native fault.

## Authored state and assets

`UiDocument` contains a typed `AssetRef<UiDocumentAsset>`, `enabled`, `visible`, and `layer` (0–255). One entity displays one document. Only these fields are authored. The builtin participates in reflected editing, structured prefab inheritance, explicit equal-value property overrides, Revert, scene Undo/Redo and save/reopen. No DOM, focus, GPU handle, hover, copied model or text layout is serialized.

The catalog records a schema-1 `ui_document` asset with an AssetId and project-relative `.rml` locator. Renaming a catalog locator preserves references. Supporting RCSS, PNG/JPEG/TGA and font files remain contained source resources. Phase7 optionally registers them as logical Content assets for identity, inspection and dependency/referrer workflows; the presenter continues to use admitted file copies. Registration and the generated HUD example are UI-independent operations in `forge_ui_assets`. These asset writes are outside scene Undo. No generic importer, cooker, HTTP loader or Apply to Prefab is added.

## Private copied-value protocol

Protocol version 1 carries a runtime session, generation, monotonic snapshot revision and up to 16 document descriptors. Each has an entity/asset ID, document incarnation, visibility/layer, scalar model and allowed command names. RmlUi retains copies, never ECS storage or gameplay DLL callbacks. Native RmlUi data binding supplies read-only variables; no scripting language is enabled.

Model snapshots admit the project catalog only when an enabled document has an
assigned asset. Empty scenes, disabled documents and unassigned documents require
no UI asset lookup. Each snapshot with active documents still admits a fresh
catalog, including hidden documents, and retries after a previous failure. A failed
catalog is attempted once for that snapshot; each affected document retains its
own diagnostic. Live file availability and document incarnations remain distinct.

Scene supplies copied entity IDs, Prefab flags and UI components through its
existing effective projection and sibling ordering. The UI read retains unresolved
Prefab member intent and required source hydration. Its per-entity output reads
skip unrelated components and spatial evaluation. Full Scene views, authored saves and history retain their existing
serialization. This adds no stored UI model or alternative entity registry.

The runtime publishes `tick` and `paused`. The experimental exact SDK's real `ui_probe.cpp` consumer registers `DecreaseHealth`, publishes a numeric `health`, and polls commands inside its fixed system. Health remains an ECS component, not a UI value authority. These private callbacks change the exact SDK fingerprint, not ABI1. The transient sample Health component is not a new general native-state recovery contract.

Buttons use `data-event-click="command('Pause')"` (or another registered command). Actions take **no arguments** by default. An exact-SDK module may explicitly register an action that accepts one bounded string value; reserved Pause/Resume/Step remain argument-free. Host-reserved `command('TimeScale','0.5')` sets simulation speed through the existing clock, accepting a complete finite numeric string in0..4. Zero holds motion while controls remain responsive. The snapshot permits four host actions plus all29 custom actions. See [gameplay services](gameplay-services.md). Runtime validates the current document incarnation, visible/enabled configuration, typed asset availability and allowlisted command. Host controls execute at the control boundary. Gameplay actions queue for the module to poll in its fixed system or standalone control callback. Control callbacks can operate menus while paused or at zero rate without advancing simulation.

Commands have session/generation and sequential positive IDs. Acknowledgements correlate the same scope and ID. The runtime keeps 64 receipts: exact retries return the prior result, conflicting reuse and expired IDs reject, and sequence gaps reject. The editor sends one outstanding command over its reliable process pipe; it performs zero automatic retries and never replays uncertain commands after recovery. Queues cap at 128. Snapshot size caps at 64 KiB; requests at 4 KiB. Models allow 32 scalar values per document (including reserved values), 1 KiB UTF-8 strings, finite numbers and a 16 KiB aggregate custom-model budget. Lists, objects, pointers, DOM and draw commands do not cross this bridge.

Successful runtime replacement/native reload increments the generation; recovery changes the process session. The host discards the old context and command state, then reconstructs current presentation. Disabling/removing/replacing a document creates a fresh incarnation on reactivation. UI hover, unsent interaction and text-field contents are transient.

## Time, input and density

RmlUi Update/layout/render uses monotonic **presentation time**, separate from fixed simulation. Pause leaves mouse, typing, hover and UI controls responsive. F7 advances exactly one simulation tick.

The SDL adapter reuses the pinned RmlUi SDL3 key/modifier and IME translation. Routing is reserved editor controls (Escape/F6/F7), runtime UI, then gameplay for unconsumed input. Runtime UI interaction requires Capture gameplay input. Matching UI-owned key/button releases are consumed even after hover/focus changes. UI capture sends gameplay neutralization; focus loss, document replacement, Stop and recovery clear ownership. Clicking outside the viewport releases capture. This deliberately clears all held gameplay controls when UI acquires input; held keys may need release/repress afterward.

Viewport coordinates map to render-target pixels. RmlUi dimensions match that target. Density uses SDL display scale multiplied by the target/viewport ratio, bounded to 0.5–4; RCSS `dp` uses it, `px` remains target pixels. ImGui Ctrl+Plus/Minus is independent. Mixed-monitor physical IME/DPI behavior still needs desktop acceptance.

## Resource admission and replacement

ProjectPaths mediates every resource read. Styles/images are document-relative. RmlUi 6.3 passes `@font-face src` directly to FileInterface without JoinPath; FORGE scopes those font paths to the project root during candidate admission. Absolute paths, escape paths, remote URLs, query/fragment locators and arbitrary resource extensions reject. Engine Lato bytes come from the packaged resource root, independent of the current working directory.

Bounds include: 256 KiB per RML/RCSS, 32 resources / 32 MiB per candidate context, 32 levels / 8192 RML tags, bounded RCSS expressions, finite bounded numeric styles, fonts up to 4 MiB with an admitted SFNT table directory, and PNG/JPEG/uncompressed 24/32-bit TGA up to 2048×2048. Image sources cap at 16 MiB + 274 bytes; native decoded pixels cap at 16 MiB, including 16-bit PNG samples. Generated GPU textures share a 64 MiB/512-object budget; geometry shares 64 MiB/8192 objects. Font variants cap at 32 / 16 MiB per presenter lifetime. Font parsing uses pinned FreeType; directory validation does not claim to prove every internal font table safe.

Reload UI creates a fresh context/resource snapshot, binds copied values, loads documents, updates layout and prepares initial geometry/textures through a non-drawing render gate. Only a successful candidate replaces the previous context. Failure retains the prior usable presentation and reports an error. RmlUi deferred document destruction is completed by destroying the candidate/old context while its callbacks and resources are still alive. Global styles are cleared for each candidate; old documents retain their own live references. Font family caches are global to the presenter: change the family or restart Play when replacing a font with the same family.

## Layout defaults

RmlUi is not a browser with an HTML user-agent stylesheet. At the pinned revision,
`StyleSheetSpecification.cpp` registers `display: inline` as the default; the
upstream `Samples/assets/rml.rcss` explicitly makes `div`, `p` and `h1` block elements.
Game documents must declare their intended layout, for example
`body, div, h1, h2, p { display: block; }`. FORGE does not inject a global style
reset into authored UI assets. Bound menu widths/heights to the viewport and check
actual element geometry as well as command navigation and rendered captures.
Scrollable documents must also style native `scrollbarvertical` with an explicit
width; otherwise its auto-sized box can consume the menu content width. The
reference stylesheet includes a visible track and draggable thumb.

## Rendering

RmlUi geometry uses immutable Diligent buffers, a dynamic transform/screen constant buffer, scissor rectangles, transformed geometry and stencil clip masks. UI draws over the freshly rendered scene into its RGBA8 UNORM target. The viewport already redraws during Play, so transparent HUDs do not accumulate over a retained scene.

RmlUi colors/generated textures are already premultiplied. TGA source pixels are premultiplied once during decoding. The blend is ONE / INV_SRC_ALPHA for color and alpha. The existing display-encoded UNORM scene path is preserved; no sRGB decode or extra gamma conversion is applied. This is not a new linear-light/HDR rendering pipeline. Text and images follow the same convention.

Context/geometry/textures retire before the render adapter and Diligent device. All presenter calls run on one owning thread, with one active presenter per process. No callback/job can outlive it.

## Bitmap normalization

The existing FORGE bounded C codec boundaries are shared by texture import and
Runtime UI through `forge_image_codecs`. Selected libpng/stb/zlib revisions stay
unchanged. Minimal headless builds populate those selected sources and build only
the software codecs; they do not require a graphics device. Runtime and Native SDK
installation retain their codec notices.

PNG expands palette/grayscale/transparency to native channels. 8-bit samples are
retained; host-endian 16-bit samples round to display bytes with `(value+128)/257`.
Grayscale replicates into RGB; absent alpha is opaque. JPEG uses the existing
bounded grayscale/RGB decoder; CMYK and embedded orientation/profile transforms
are outside this subset. PNG gamma/profile chunks also do not alter sample values.
The adapter treats stored RGB as display-encoded values and premultiplies once for
native RGBA8_UNORM upload. This preserves the previous TGA convention; the separate
3D Texture cook can retain its own color-space/semantic/mip policy.

Resource extension and signature must agree. Full decode, byte/extent/layout
admission precede copied-byte publication; invalid reloads retain the prior native
context, textures and source observation. Snapshots use header metadata from those
already admitted immutable bytes instead of decoding pixels again. Rendering reuses
native texture handles; there is no per-frame file read or bitmap decode.

An imported Texture used by UI keeps its existing AssetId, cooked selection and
opaque authoring extensions. Export ships both its required cooked selection and
its admitted raw UI source through the existing content access/closure owner.
Relocating the game does not require the original project directory.

## Supported subset and deferred features

Adopted: native RML/RCSS layout, styled text/buttons/forms, copied scalar expressions, local text editing, multiple ordered documents, PNG/JPEG/TGA images, project fonts, transforms, scissor and stencil masks, explicit reload. Documents sort by layer then stable incoming scene order; RmlUi manages normal DOM focus/z-order within that context.

Deferred: browser JavaScript, Lua, debugger UI, SVG/Lottie/HarfBuzz, RCSS imports/templates, world-space UI, collections/two-way gameplay binding, public plugin APIs and a visual UI designer. Standalone gamepad navigation now routes through input actions to native RmlUi focus traversal and activation. Link stylesheets individually. Offscreen layers, filters, masks, shader decorators/gradients and shadows are unsupported and diagnosed; they are not silently advertised as working. Use explicit font-family/font-size rather than shorthand. This is the first screen-space integration, not a visual UI designer.

## Upstream evidence

- [RmlUi 6.3 release](https://github.com/mikke89/RmlUi/releases/tag/6.3), exact `ba95ffe8bfb6370efb2cdcca927eaad4710c5413` (MIT).
- [Pinned RenderInterface](https://github.com/mikke89/RmlUi/blob/ba95ffe8bfb6370efb2cdcca927eaad4710c5413/Include/RmlUi/Core/RenderInterface.h): required geometry/texture/scissor functions, optional transforms/masks/layers, premultiplied texture contract.
- [Main loop](https://mikke89.github.io/RmlUiDoc/pages/cpp_manual/main_loop.html), [font interface](https://mikke89.github.io/RmlUiDoc/pages/cpp_manual/interfaces/font_engine.html), [SDL adapter source](https://github.com/mikke89/RmlUi/blob/ba95ffe8bfb6370efb2cdcca927eaad4710c5413/Backends/RmlUi_Platform_SDL.cpp).
- [FreeType](https://freetype.org/), selected 2.14.3 at `0a0221a1347e2f1e07c395263540026e9a0aa7c7`, FreeType License option. Optional compression/image/shaping dependencies disabled.


## Phase7 common asset integration

Exact RmlUi6.3 source `ba95ffe8bfb6370efb2cdcca927eaad4710c5413` rechecked
2026-09-21: `XMLNodeHandlerHead.cpp` records linked styles, `StyleSheetParser.cpp`
uses native `LoadFontFace`, and `FontEngineDefault/FontProvider.cpp` routes font
bytes through the installed FileInterface. FORGE observes its existing
`UiResources` admission cache. It does not add an RML/RCSS dependency parser.
FreeType2.14.3 remains `0a0221a1347e2f1e07c395263540026e9a0aa7c7`.

A successfully prepared presenter set holds a read-only `UiAssetSnapshot` of
root document AssetIds and admitted resource locators/digests/format/size. Failed
candidates preserve the prior observation; reset releases it. The host may copy
it for a later explicit authoring action. No project catalog is mutated from
RmlUi callbacks or runtime presentation. No new scene/ABI/IPC format is introduced.

`forge_ui_asset_catalog` is an authoring-only operation over ProjectLease and the
existing AssetCatalog. Register/Refresh admits one supported source. Refresh loaded
resources re-admits the captured set and compares exact source metadata before
one atomic catalog publication, with an external-index baseline check. Failures
retain previous metadata/identities. Project switches discard the copied observation.
The normal local single-writer/non-adversarial-filesystem contract still applies;
this is not a filesystem-wide lock or cross-document transaction.

Logical types are ui_document, ui_stylesheet, ui_font and the existing Texture
type for PNG/JPEG/TGA images. An image can also use the ordinary Texture importer;
a previously imported Texture retains its identity and cooked selection. Metadata is under `forge.ui_source`; source edges use ui.source and
ui.observed. Documents have typed Runtime edges to supporting resources from their
successfully loaded set. Multiple roots conservatively reference all non-document
resources in that set, avoiding invented document cycles. The graph remains the
one catalog-owned graph. Engine default font, future dynamically requested files,
and parser-free static discovery are not advertised as complete project edges.

Shared Content status compares nonempty recorded source revisions for these and
other legacy families. No automatic UI cook or live font invalidation is claimed.
Reload UI and restart-on-same-family-font-change behavior remain unchanged. Catalog
metadata refresh is separate from scene Undo, UI replacement and native font caches.
The end-user steps are in [Runtime UI](../manual/editor/runtime-ui.md).
