# Runtime UI

Phase 6F adds screen-space game UI using RmlUi 6.3. Dear ImGui continues to own the editor. The [user manual](../manual/editor/runtime-ui.md) describes creating a HUD without coding.

## Ownership and composition

`forge.ui` uses EngineModule schema registration and a runtime-only UiService. Authoring worlds register `UiDocument` without initializing a UI service or RmlUi. Headless runtimes omit the service by default (`--ui off`). `forge_runtime` links no RmlUi, FreeType, Diligent or ImGui.

During editor Play, the separate runtime process owns gameplay, authored UI configuration, copied model production, command validation and the fixed clock. The reusable `forge_ui_presenter` owns RmlUi and its documents, copied bindings, fonts and input state. `forge_ui_diligent` owns the graphics adapter. They do not depend on ImGui or an authoring WorldContext. The editor hosts them beside its existing viewport renderer; gameplay DLLs remain outside that process.

A future standalone visual executable can compose gameplay, Diligent and the same presenter in one process. The private bridge does not require permanent IPC. That executable is not implemented in this phase. Native RmlUi/presentation faults share the editor's failure domain; process isolation protects against gameplay worker faults, not every native fault.

## Authored state and assets

`UiDocument` contains a typed `AssetRef<UiDocumentAsset>`, `enabled`, `visible`, and `layer` (0–255). One entity displays one document. Only these fields are authored. The builtin participates in reflected editing, structured prefab inheritance, explicit equal-value property overrides, Revert, scene Undo/Redo and save/reopen. No DOM, focus, GPU handle, hover, copied model or text layout is serialized.

The catalog records a schema-1 `ui_document` asset with an AssetId and project-relative `.rml` locator. Renaming a catalog locator preserves references. Supporting RCSS, TGA and font files are contained dependencies of that document; they do not acquire separate durable IDs merely for symmetry. Registration and the generated HUD example are UI-independent operations in `forge_ui_assets`. These asset writes are outside scene Undo. No generic importer, cooker, HTTP loader or Apply to Prefab is added.

## Private copied-value protocol

Protocol version 1 carries a runtime session, generation, monotonic snapshot revision and up to 16 document descriptors. Each has an entity/asset ID, document incarnation, visibility/layer, scalar model and allowed command names. RmlUi retains copies, never ECS storage or gameplay DLL callbacks. Native RmlUi data binding supplies read-only variables; no scripting language is enabled.

The runtime publishes `tick` and `paused`. The experimental exact SDK's real `ui_probe.cpp` consumer registers `DecreaseHealth`, publishes a numeric `health`, and polls commands inside its fixed system. Health remains an ECS component, not a UI value authority. These private callbacks change the exact SDK fingerprint, not ABI1. The transient sample Health component is not a new general native-state recovery contract.

Buttons use `data-event-click="command('Pause')"` (or another registered command). Phase 6F actions take **no arguments**. Runtime validates the current document incarnation, visible/enabled configuration, typed asset availability and allowlisted command. Pause/Resume/Step execute at the control boundary. Gameplay actions queue until a fixed tick and cannot silently advance paused simulation.

Commands have session/generation and sequential positive IDs. Acknowledgements correlate the same scope and ID. The runtime keeps 64 receipts: exact retries return the prior result, conflicting reuse and expired IDs reject, and sequence gaps reject. The editor sends one outstanding command over its reliable process pipe; it performs zero automatic retries and never replays uncertain commands after recovery. Queues cap at 128. Snapshot size caps at 64 KiB; requests at 4 KiB. Models allow 32 scalar values per document (including reserved values), 1 KiB UTF-8 strings, finite numbers and a 16 KiB aggregate custom-model budget. Lists, objects, pointers, DOM and draw commands do not cross this bridge.

Successful runtime replacement/native reload increments the generation; recovery changes the process session. The host discards the old context and command state, then reconstructs current presentation. Disabling/removing/replacing a document creates a fresh incarnation on reactivation. UI hover, unsent interaction and text-field contents are transient.

## Time, input and density

RmlUi Update/layout/render uses monotonic **presentation time**, separate from fixed simulation. Pause leaves mouse, typing, hover and UI controls responsive. F7 advances exactly one simulation tick.

The SDL adapter reuses the pinned RmlUi SDL3 key/modifier and IME translation. Routing is reserved editor controls (Escape/F6/F7), runtime UI, then gameplay for unconsumed input. Runtime UI interaction requires Capture gameplay input. Matching UI-owned key/button releases are consumed even after hover/focus changes. UI capture sends gameplay neutralization; focus loss, document replacement, Stop and recovery clear ownership. Clicking outside the viewport releases capture. This deliberately clears all held gameplay controls when UI acquires input; held keys may need release/repress afterward.

Viewport coordinates map to render-target pixels. RmlUi dimensions match that target. Density uses SDL display scale multiplied by the target/viewport ratio, bounded to 0.5–4; RCSS `dp` uses it, `px` remains target pixels. ImGui Ctrl+Plus/Minus is independent. Mixed-monitor physical IME/DPI behavior still needs desktop acceptance.

## Resource admission and replacement

ProjectPaths mediates every resource read. Styles/images are document-relative. RmlUi 6.3 passes `@font-face src` directly to FileInterface without JoinPath; FORGE scopes those font paths to the project root during candidate admission. Absolute paths, escape paths, remote URLs, query/fragment locators and arbitrary resource extensions reject. Engine Lato bytes come from the packaged resource root, independent of the current working directory.

Bounds include: 256 KiB per RML/RCSS, 32 resources / 32 MiB per candidate context, 32 levels / 8192 RML tags, bounded RCSS expressions, finite bounded numeric styles, fonts up to 4 MiB with an admitted SFNT table directory, and uncompressed 24/32-bit TGA up to 2048×2048. Generated GPU textures share a 64 MiB/512-object budget; geometry shares 64 MiB/8192 objects. Font variants cap at 32 / 16 MiB per presenter lifetime. Font parsing uses pinned FreeType; directory validation does not claim to prove every internal font table safe.

Reload UI creates a fresh context/resource snapshot, binds copied values, loads documents, updates layout and prepares initial geometry/textures through a non-drawing render gate. Only a successful candidate replaces the previous context. Failure retains the prior usable presentation and reports an error. RmlUi deferred document destruction is completed by destroying the candidate/old context while its callbacks and resources are still alive. Global styles are cleared for each candidate; old documents retain their own live references. Font family caches are global to the presenter: change the family or restart Play when replacing a font with the same family.

## Rendering

RmlUi geometry uses immutable Diligent buffers, a dynamic transform/screen constant buffer, scissor rectangles, transformed geometry and stencil clip masks. UI draws over the freshly rendered scene into its RGBA8 UNORM target. The viewport already redraws during Play, so transparent HUDs do not accumulate over a retained scene.

RmlUi colors/generated textures are already premultiplied. TGA source pixels are premultiplied once during decoding. The blend is ONE / INV_SRC_ALPHA for color and alpha. The existing display-encoded UNORM scene path is preserved; no sRGB decode or extra gamma conversion is applied. This is not a new linear-light/HDR rendering pipeline. Text and images follow the same convention.

Context/geometry/textures retire before the render adapter and Diligent device. All presenter calls run on one owning thread, with one active presenter per process. No callback/job can outlive it.

## Supported subset and deferred features

Adopted: native RML/RCSS layout, styled text/buttons/forms, copied scalar expressions, local text editing, multiple ordered documents, TGA images, project fonts, transforms, scissor and stencil masks, explicit reload. Documents sort by layer then stable incoming scene order; RmlUi manages normal DOM focus/z-order within that context.

Deferred: browser JavaScript, Lua, debugger UI, SVG/Lottie/HarfBuzz, PNG/JPEG, RCSS imports/templates, world-space UI, gamepad UI navigation, collections/two-way gameplay binding, public plugin APIs and the generic asset pipeline. Link stylesheets individually. Offscreen layers, filters, masks, shader decorators/gradients and shadows are unsupported and diagnosed; they are not silently advertised as working. Use explicit font-family/font-size rather than shorthand. This is the first screen-space integration, not a visual UI designer.

## Upstream evidence

- [RmlUi 6.3 release](https://github.com/mikke89/RmlUi/releases/tag/6.3), exact `ba95ffe8bfb6370efb2cdcca927eaad4710c5413` (MIT).
- [Pinned RenderInterface](https://github.com/mikke89/RmlUi/blob/ba95ffe8bfb6370efb2cdcca927eaad4710c5413/Include/RmlUi/Core/RenderInterface.h): required geometry/texture/scissor functions, optional transforms/masks/layers, premultiplied texture contract.
- [Main loop](https://mikke89.github.io/RmlUiDoc/pages/cpp_manual/main_loop.html), [font interface](https://mikke89.github.io/RmlUiDoc/pages/cpp_manual/interfaces/font_engine.html), [SDL adapter source](https://github.com/mikke89/RmlUi/blob/ba95ffe8bfb6370efb2cdcca927eaad4710c5413/Backends/RmlUi_Platform_SDL.cpp).
- [FreeType](https://freetype.org/), selected 2.14.3 at `0a0221a1347e2f1e07c395263540026e9a0aa7c7`, FreeType License option. Optional compression/image/shaping dependencies disabled.
