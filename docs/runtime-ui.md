# Runtime UI

The standalone host can request a presentation-only FPS panel through
`UiPresenter::frame_statistics`. Empty text hides it; the default creates no
diagnostic document. The text is validated UTF-8 capped at128bytes and inserted
as a literal text node, without RML parsing or model binding. One private document
per live/staged main context uses the same resource admission, font and renderer. It
stays outside authored document counts, source provenance and gameplay protocol
models, and takes no input/focus. Live/staged sets keep their ordinary lifetime.
See [standalone presentation statistics](standalone-host.md) for launch and timing.

Phase 6F adds screen-space game UI using RmlUi 6.3. Dear ImGui continues to own the editor. The [user manual](../manual/editor/runtime-ui.md) describes creating a HUD without coding.

## Ownership and composition

`forge.ui` uses EngineModule schema registration and a runtime-only UiService. Authoring worlds register `UiDocument` without initializing a UI service or RmlUi. Headless runtimes omit the service by default (`--ui off`). `forge_runtime` links no RmlUi, FreeType, Diligent or ImGui.

During editor Play, the separate runtime process owns gameplay, authored UI configuration, copied model production, command validation and the fixed clock. The reusable `forge_ui_presenter` owns RmlUi and its documents, copied bindings, fonts and input state. `forge_ui_diligent` owns the graphics adapter. They do not depend on ImGui or an authoring WorldContext. The editor hosts them beside its existing viewport renderer; gameplay DLLs remain outside that process.

The standalone `forge_game` executable composes gameplay, Diligent and the same presenter in one process. The private bridge does not require permanent IPC. See [standalone host](standalone-host.md). Native RmlUi/presentation faults share the editor's failure domain; process isolation protects against gameplay worker faults, not every native fault.

## Authored state and assets

`UiDocument` contains a typed `AssetRef<UiDocumentAsset>`, `enabled`, `visible`, `layer` (0–255), and optional typed `render_texture` output reference. One entity displays one document. Only these fields are authored. The builtin participates in reflected editing, structured prefab inheritance, explicit equal-value property overrides, Revert, scene Undo/Redo and save/reopen. No DOM, focus, GPU handle, hover, copied model or text layout is serialized.

The catalog records a schema-1 `ui_document` asset with an AssetId and project-relative `.rml` locator. Renaming a catalog locator preserves references. Supporting RCSS, PNG/JPEG/TGA and font files remain contained source resources. Phase7 optionally registers them as logical Content assets for identity, inspection and dependency/referrer workflows; the presenter continues to use admitted file copies. Registration and the generated HUD example are UI-independent operations in `forge_ui_assets`. These asset writes are outside scene Undo. RML and supporting raw resources have no UI cooker or HTTP loader. Typed localization tables use the source/import/cook owner described below. Component edits retain ordinary Scene/Prefab history rules; resource registration remains separate from Scene Undo.

## Private copied-value protocol

Main-only snapshots retain private protocol1; a positive output assignment requires protocol2. Both carry a runtime session, generation, monotonic snapshot revision and up to 16 document descriptors. Each has an entity/asset ID, document incarnation, visibility/layer, nullable output, scalar model and allowed command names. Protocol1 rejects a positive output, so a legacy presenter cannot silently draw routed UI on main. Command receipts retain their request version. RmlUi retains copies, never ECS storage or gameplay DLL callbacks. Native RmlUi data binding supplies read-only variables; no scripting language is enabled.

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

Buttons use `data-event-click="command('Pause')"` (or another registered command). Actions take **no arguments** by default. An exact-SDK module may explicitly register an action that accepts one bounded string value; reserved Pause/Resume/Step remain argument-free. Host-reserved `command('TimeScale','0.5')` sets simulation speed through the existing clock, accepting a complete finite numeric string in0..4. Zero holds motion while controls remain responsive. An unlocalized snapshot permits four host actions plus all29 custom actions. A localized snapshot adds the reserved forge.locale action without removing a custom slot. See [gameplay services](gameplay-services.md). Runtime validates the current document incarnation, visible/enabled configuration, typed asset availability and allowlisted command. Host controls execute at the control boundary. Gameplay actions queue for the module to poll in its fixed system or standalone control callback. Control callbacks can operate menus while paused or at zero rate without advancing simulation.

Commands have session/generation and sequential positive IDs. Acknowledgements correlate the same scope and ID. The runtime keeps 64 receipts: exact retries return the prior result, conflicting reuse and expired IDs reject, and sequence gaps reject. The editor sends one outstanding command over its reliable process pipe; it performs zero automatic retries and never replays uncertain commands after recovery. Queues cap at 128. Snapshot size caps at 64 KiB; requests at 4 KiB. Models allow 32 scalar values per document (including reserved values), 1 KiB UTF-8 strings, finite numbers and a 16 KiB aggregate custom-model budget. Lists, objects, pointers, DOM and draw commands do not cross this bridge.

Successful runtime replacement/native reload increments the generation; recovery changes the process session. The host discards the old context and command state, then reconstructs current presentation. Disabling/removing/replacing a document creates a fresh incarnation on reactivation. UI hover, unsent interaction and text-field contents are transient.

## Game UI localization

[ADR029](decisions/029-runtime-ui-localization.md) defines the ownership boundary.
The optional `UiLocalization { AssetRef<UiLocalizationAsset> catalog; std::string
locale = "en"; }` component is the Scene's language authority. One configured
non-Prefab controller serves its active UI context. No active UI Document means
no localization resource demand. Independent Worlds retain independent values.
Existing UI Documents and the C SDK host table retain their layout and meaning.
Native users require the exact matching SDK, including this component's string
construction/copy/destruction support. Ordinary runtime recovery uses the runtime
Scene snapshot; prepared SDK Play continues to return no recovery checkpoint.

The `.localization.json` source uses `format:"forge.ui-localization"`, version1,
AssetId, `default_locale`, and `locales:{tag:{label,messages:{key:text}}}`.
The existing AssetSourceDocument, ImportService and selected derived cache own
history, publication and persistence. Importer `forge.ui.localization` publishes
one canonical `localization.json`; shared admission verifies its identity,
selection, manifest, recipe, hashes and metadata for runtime and export.
Scene/Prefab references and ordinary content closure own dependencies. Duplicate
changes the asset identity while preserving literal text and opaque extensions;
UUID-looking message text is never an asset reference.

Bounds: eight locales, 128 default keys, 256 KiB source, 1 KiB/message,
64-byte labels and 48-byte ASCII identifier keys; JSON depth8/events12000.
Locale tags are an ASCII hyphenated subset, 2..35 bytes, with a 2..8-letter
primary segment and 1..8-alphanumeric subtags. Case-fold collisions reject.
Selection tries exact tag, primary language, then default. Other locales may omit
default keys; absent translations use the default message. Unknown keys show
`[[key]]`. Invalid UTF-8, NUL, duplicate JSON fields and non-round-trippable values
reject. There are no plural, parameter or arbitrary international-format rules.

RML uses `{{ 'menu_play' | tr(forge_locale) }}`. The context-wide pure transform
returns literal text through native expression DataViewText/SetText. It does not
use the translation hook that can precede markup parsing. `forge_locale` and
`forge_resolved_locale` are read-only native getters from copied component
settings; custom model collisions reject for localized contexts. Locale-only
updates dirty bindings without replacing the context, input ownership or focus.
Controller/catalog/native-entity scope changes renew document incarnations and
use the normal complete-candidate gate. Failed table admission retains the
complete predecessor; it cannot mix new metadata with the old table.

UiResources retains one immutable typed table under its existing combined
32-resource/32-MiB candidate budgets. `source_count()` still counts raw UI files;
`resource_count()` includes the table. The table is excluded from raw
UiAssetSnapshot sources. No source read/import occurs while drawing or ticking.
Reload UI admits new saved content through the same owner; font-family cache
rules remain unchanged.

Localized snapshots add an optional controller/asset/locale descriptor to private
UI protocol1. Their `forge.locale` command accepts one valid locale tag and writes
the same Scene runtime property owner as live Inspector tuning. It operates under
Pause or zero simulation rate without advancing a tick, using normal correlation,
visibility and incarnation checks. Localized documents alone admit the 34th
command slot; legacy documents retain 33 and may still register a custom action
named `forge.locale`. A localized configuration rejects that action collision.
The component remains the sole authority; the command introduces no model override
or durable player-preference store. Stop restores authored values unless the
existing Keep values workflow deliberately retains a supported live change.

`UiPresenter::localization_state()` reports the actual active table, requested and
selected locale, locale fallback and default-message coverage. Editor Game controls
and automation `play.ui.presentation_localization` borrow this observation; it is
null without a live displayed presenter. Runtime settings remain separately visible
in `play.ui.value.localization`. A retained last-good presentation can therefore
be distinguished from a rejected new request.

The source document, Content, CLI and automation/MCP share the existing authoring
owners. See the [localization guide](../manual/editor/ui-localization.md).
Gallery133 supplies original English/French/Spanish messages, regional/default
fallback, literal accented text and paused controls. This is game UI localization;
editor translation, full shaping and complete international input remain absent.

## Camera output composition

The existing UiPresenter owns one main context and lazily created contexts per
positive output within each live/staged Set. Documents, model values, commands,
localization, application-state transforms, resource snapshots and preparation
remain under that same presenter. Complete preparation/cancel/activation and
retirement include every context. No output registry or second presenter exists.
Layer sorting is stable within each group. Native input/focus and private FPS
statistics use main only; explicit semantic commands still identify their document.
Changing the saved output renews the runtime document incarnation and rejects its
old command immediately, before the next copied snapshot.

Hosts update shared presentation time once before Game. CameraRenderTextures
borrows an optional overlay callback after its complete producer composition and
before copying/publishing the current image for Material/relay consumers. The
shared UiDiligent draw_output lays out that context at the pass dimensions and
renders it without advancing simulation or another clock. A failed composition
cannot publish stale current pixels. Main UI draws after the Game display as before.
Density is1 for outputs; main density retains the existing window policy. An output
without an enabled producer has no draw pass. No pointer/raycast surface input,
world-space layout, stopped Scene overlay or stopped Timeline UI export is added.

The graphics adapter retains RGBA8 display-encoded blending. Its RGBA16_FLOAT
variant unpremultiplies combined UI color, decodes straight RGB through the pinned
DiligentFX SRGBToLinear function and premultiplies for linear HDR blending. The
camera image remains opaque; the consuming view owns exposure/tone mapping.
Meshes retain their authored UV conventions, including any intentional flips.
Clipping scratch images remain renderer-owned: at most5 sizes and256MiB logical
D24S8 bytes, with least-recently-used eviction and ordinary Diligent lifetimes.
These are allocation bounds, not physical-memory or hardware-performance claims.

Generated runtime kits advertise engine.runtime_ui_protocol from the current
header. Export/distribution admission reads the existing cooked document graph,
including Prefab overrides, and requires matching support for a positive UI output
edge. A main-only project can still use a legacy kit. Scene/Prefab default admission
keeps missing outputs null; exact C++ modules require the updated header fingerprint.

## Time, input and density

RmlUi Update/layout/render uses monotonic **presentation time**, separate from fixed simulation. Pause leaves mouse, typing, hover and UI controls responsive. F7 advances exactly one simulation tick.

The SDL adapter reuses the pinned RmlUi SDL3 key/modifier and IME translation. Routing is reserved editor controls (Escape/F6/F7), runtime UI, then gameplay for unconsumed input. Runtime UI interaction requires Capture gameplay input. Matching UI-owned key/button releases are consumed even after hover/focus changes. UI capture sends gameplay neutralization; focus loss, document replacement, Stop and recovery clear ownership. Clicking outside the viewport releases capture. This deliberately clears all held gameplay controls when UI acquires input; held keys may need release/repress afterward.

Viewport coordinates map to render-target pixels. RmlUi dimensions match that target. Density uses SDL display scale multiplied by the target/viewport ratio, bounded to 0.5–4; RCSS `dp` uses it, `px` remains target pixels. ImGui Ctrl+Plus/Minus is independent. Mixed-monitor physical IME/DPI behavior still needs desktop acceptance.

## Resource admission and replacement

ProjectPaths mediates every resource read. Styles/images are document-relative. RmlUi 6.3 passes `@font-face src` directly to FileInterface without JoinPath; FORGE scopes those font paths to the project root during candidate admission. Absolute paths, escape paths, remote URLs, query/fragment locators and arbitrary resource extensions reject. Engine Lato bytes come from the packaged resource root, independent of the current working directory.

Bounds include: 256 KiB per RML/RCSS, 32 resources / 32 MiB per complete candidate Set, 32 levels / 8192 RML tags, bounded RCSS expressions, finite bounded numeric styles, fonts up to 4 MiB with an admitted SFNT table directory, and PNG/JPEG/uncompressed 24/32-bit TGA up to 2048×2048. Image sources cap at 16 MiB + 274 bytes; native decoded pixels cap at 16 MiB, including 16-bit PNG samples. Generated GPU textures share a 64 MiB/512-object budget; geometry shares 64 MiB/8192 objects. Font variants cap at 32 / 16 MiB per presenter lifetime. Font parsing uses pinned FreeType; directory validation does not claim to prove every internal font table safe.

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

RmlUi colors/generated textures are already premultiplied. TGA source pixels are premultiplied once during decoding. The blend is ONE / INV_SRC_ALPHA for color and alpha. The existing display-encoded UNORM path applies no extra gamma conversion. The camera-output RGBA16 variant uses the explicit premultiplied linear transfer described above. Text and images follow the same convention.

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

Native RCSS keyframes and transitions run through the existing RmlUi context and
monotonic presentation clock. Supported examples cover width, opacity, background
color and translation. State classes use ordinary copied scalar model bindings;
hover/focus use native input. Gameplay pause/zero time scale do not freeze these RCSS animations.
Scene UI Values can drive existing RML expressions through component-field tracks;
see the named-value profile below. There is no second animation player or persistent
DOM/property registry. Native animation progress is transient and restarts on
successful document replacement. The selected dependency caps each animation
advance at0.1s; this is visual presentation, not a fixed-tick gameplay timer.
Use the pinned parser's timing names (`linear-in`, `cubic-in-out`, etc.); plain
`linear` is not accepted. Multiple animations targeting one property are outside
the supported profile. Existing filter/layer/resource limits apply inside keyframes.

Gallery100 provides editable RML/RCSS and a camera-only Scene, without native
gameplay code. The ordinary native inspection/catalog/package owners retain its
document/stylesheet closure. Runtime loading uses packaged raw UI resources;
relocation does not require the original authoring directory.

Editor **Reload UI** and automation `play.ui.reload` share RuntimeUiHost's reload
operation and the existing presenter's complete-candidate gate. It releases input,
validates saved resources, and only publishes a usable context. Failure retains
the previous documents/resources and reports the native diagnostic. Automation
requires the current Play launch/session/generation, revision/sequence and
`play.execute`. Raw source writes retain their Edit-only `assets.write` policy;
agents can Stop, edit and restart. Reload can also admit a user's external saved
RML/RCSS edit during Play. Neither operation introduces Scene Undo for transient
presentation state or access to arbitrary DOM/script/engine files.

Adopted: native RML/RCSS layout, styled text/buttons/forms, copied scalar expressions, local text editing, multiple ordered documents, PNG/JPEG/TGA images, project fonts, transforms, scissor and stencil masks, explicit reload. Documents sort by layer then stable incoming scene order; RmlUi manages normal DOM focus/z-order within that context.

Deferred: browser JavaScript, Lua, debugger UI, SVG/Lottie/HarfBuzz, RCSS imports/templates, world-space UI, collections/two-way gameplay binding, public plugin APIs and a visual UI designer. Standalone gamepad navigation now routes through input actions to native RmlUi focus traversal and activation. Link stylesheets individually. Bounded blur/color filters use transient renderer-owned layers as described below. Masks, shader decorators/gradients and box shadows remain unsupported and diagnosed. Drop-shadow and backdrop filters use the profile below. Use explicit font-family/font-size rather than shorthand. This is the first screen-space integration, not a visual UI designer.

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

## Application state presentation and commands

The [application-state contract](application-states.md) defines the optional copied
state descriptor, readonly `forge_state_<domain>`/`forge_rev_<domain>` bindings and
`state_is` ancestor transform. Runtime UI validates the current admitted controller
and derives these observations from the core owner. The full editable definition
and choices are not copied into the UI descriptor. Its limit is 16 domains/24 KiB,
within the unchanged 64 KiB snapshot cap.

A configured `forge.state` action takes `domain:state:revision` and shares native
state validation and the existing UI session/generation/receipt/document gates.
It adds one reserved action slot; a localized stateful UI can have 35 actions
(29 custom, four clock controls, locale, state). The older 33/34-slot cases retain
their limits. Custom action/model collisions reject only configured stateful UI;
unconfigured projects retain their existing behavior. State changes preserve the
native context/focus, while controller/definition/incarnation changes re-admit a
complete set. `UiPresenter::application_states_state()` copies only the displayed
set's observations. See the [user guide](../manual/editor/application-states.md).

## Scene-owned named values and sequence time

Optional `forge.ui_values` on a UiDocument entity owns up to16 keyed `{name,value}`
entries. The public UiValues type validates unique finite doubles and native
variable names: initial ASCII letter, then ASCII letters/digits/underscore,
maximum64bytes. Native expression keywords reject case-insensitively; tick, paused
and the forge_loading_ prefix reject. Localization/state names reject through
their existing conditional reservation checks.

Scene narrow UI projection includes this component, including effective Prefab
values. UiRuntime merges entries into its ephemeral copied model after copying
existing C++ publications. A duplicate is an explicit failure, never an overwrite
or insertion into the C++ publication map. The existing combined32-entry model
bound includes tick and paused. C++ publication API/name policy remains unchanged.
Native binding dirtiness updates numeric values without document replacement;
adding/removing/renaming a binding uses the existing structure replacement owner.

UI Values explicitly opts into the existing reflected component-track admission
with a builtin schema stamp. Keyed name selectors target its value leaves. Existing
clip interpolation, full candidate, root/nested sequence clocks, seek and ordinary
runtime recovery remain authoritative. Gameplay Pause/Step/time scale affect these
samples. A CSS class expression may also select ordinary native styles/animations;
those CSS animations keep presentation time and have no sequence seeking contract.
Avoid simultaneous native animation/transition and binding writers on one style.

Inspector and the generic property/history/Prefab owners author values. Live
tuning uses RuntimePropertyTune and the normal tuning-review operation; animation
may replace a tuned field at its next sample. Automation/MCP discover the same
component schema, property/source methods and Play controls. Edit Timeline previews
copy sampled values but do not execute/render RML, and sequence frame output omits
Runtime UI. Use real Game Play for visual UI evaluation. No arbitrary DOM path,
new asset, independent clock, CSS player or transient DOM checkpoint is added.
Gallery151 demonstrates a placed child sequence driving width/opacity/class while
an independent CSS pulse continues during Pause.


## Native filter profile

RML inline styles and embedded/linked RCSS may use `filter` chains containing
blur, opacity, brightness, contrast, grayscale and drop-shadow. The native parser resolves
units and interpolation; the shared FORGE profile admits finite nonnegative
values: blur sigma0..16 resolved pixels (Gaussian truncated at3sigma/radius48),
opacity/grayscale0..1 and brightness/contrast0..4. At most8filters per chain,
4active nested layers and256compiled filters across the renderer's live/staged
resources are admitted. Identity blur skips its passes. Authored chain order is
preserved. RCSS keyframes/transitions and `data-style-filter` expressions use the
same profile; their values must remain within these bounds.

UiDiligentRenderer owns its private layer facet, compiled handles and scratch
pool: at most32RGBA8/RGBA16images and256MiB logical image bytes, with inactive
least-recently-used eviction by size/format. These are storage bounds, not actual
VRAM or performance measurements. Each layer starts transparent; separable blur
and color operations preserve premultiplied alpha semantics. Layers use the
current destination domain: display-encoded main RGBA8 or already-linear output
RGBA16_FLOAT. Brightness/contrast can exceed normalized RGB before target-format
storage; HDR values retain their range. No second gamma decode is applied.
Stencil/scissor constrain the final composition; in-place Blend/Replace uses a
scratch copy to avoid sampling the active render target. Geometry clips retain
the existing shared stencil owner. Blur cannot recover pixels outside the target.

RenderGate prepares virtual layers without framebuffer drawing, validates native
compiled filter payloads through the target renderer and retires handles with its
owning candidate context. Renderer callbacks record failures until the native walk
balances its stack; main-view faults remain diagnostics; output faults also throw after Update/Render
to prevent publication of a failed camera image. A failed source candidate preserves the complete prior
presentation. Live binding/animation faults are diagnosed; they do not roll back
arbitrary transient DOM state.

UiResources derives filter requirements during the same bounded source admission
that retains immutable bytes. Declared `filter` properties, including keyframes,
and dynamic style bindings add their required `runtime_features.filters` profile
to the existing
source details/`forge.ui_source` metadata. Inline styles containing character
references conservatively require this profile because native attribute parsing
expands them before interpreting CSS. Other comments and quoted ordinary text do
not add the requirement; ordinary unfiltered sources gain no new key. Catalog preparation independently re-admits
source bytes and evidence; inspection transport and cooked metadata retain it.
This is a conservative declaration requirement, including `filter:none`, not a
prediction of arbitrary future rich-text/DOM injection.

Runtime kits advertise `engine.runtime_ui_filters:3` from the same profile header.
Known profiles are additive:1 blur/color,2 drop shadows,3 backdrop composition.
The existing compatibility helper admits requirements1..3 covered by a known kit
profile1..3. Static basic filters retain1. Shadow declarations and ordinary dynamic
`data-style-filter` bindings retain2. `backdrop-filter` declarations (including
none), `data-style-backdrop-filter` bindings and encoded inline styles require3.
Encoded styles may expand any supported property before native parsing; their
requirement is conservative. Source read/snapshot/catalog refresh recomputes
this evidence with the same stable logical IDs. A requirement3 rejects kit1/2;
unknown/fractional/noninteger source or kit profiles reject. Unfiltered sources
add no requirement. Camera-output UI protocol2 remains separate. Non-drawing
inspection uses the same typed payload admission; graphical readiness belongs
to the host.

Gallery155 provides original shape/color samples, Inspector-driven blur and a
presentation-time filter pulse. Source editing/registration, live UI reload,
UiValues tuning and export use their existing automation/MCP operations and guards.
No effects database, authored DOM format, new game clock or plugin ABI is added.
Mask/box-shadow/gradient/shader effects, world-space UI and surface input
remain outside this profile.

### Drop-shadow composition

The exact pinned native parser supplies `drop-shadow` with three dictionary
entries: FLOAT sigma, VECTOR2 offset and straight display-space COLOURB color.
Shared `admit_filter` validates exact names/types and `make_drop_shadow` admits
sigma0..16, signed offsets -128..128 resolved pixels and finite straightRGBA0..1.
It reuses the existing normalized Gaussian kernel and radius48 bound. The existing
8chain/4active layers/256compiled filters/32images/256MiB logical image budget
also governs shadows; no second pool or effect database is introduced.

UiFilterLayers retains the incoming chain image active until source-over finishes,
including a preceding scratch result. It applies the existing separable blur,
samples displaced alpha, tints it, and composites the unblurred input over that
shadow. Transparent shadow alpha skips work. A later shadow sees the earlier
composite. Source/destination alias handling and exception retirement remain with
the existing owner. Stencil/scissor still constrain final destination composition;
intermediate Gaussian tails are limited to the target viewport. This is not
recovery of off-target geometry or an unbounded convolution.

The pinned Diligent SRGBToLinear helper converts straight shadow color exactly
once for a linearRGBA16F destination before premultiplication; displayRGBA8 keeps
display channels. Existing blurred layer pixels never undergo another gamma
conversion. Native model/keyframe interpolation, source candidate/last-good,
main/output errors and UI lifetime remain unchanged. Gallery162 supplies original
hard/left/soft/stacked/model/animated comparisons through the same GamePresentation
and source-hidden export test as Gallery155. Source authoring, tuning and reload
continue through existing automation/MCP operations; no effect-specific mutation
API is needed.

### Backdrop filtering

The exact native RmlUi ElementEffects walk supplies ordinary PushLayer and
CompositeLayers callbacks for `backdrop-filter`. UiFilterLayers retains ownership
of all images, kernels, handles, destination domains and alias handling. The
source is the current parent layer, including image content drawn before the
element. Native code supplies an extended read scissor and a final border-box/
rounded clip before drawing the element's own contents. Existing ordinary filters
on that element subsequently process the resulting combined layer. This reuses
the current FORGE adapter; no backdrop framebuffer manager or source database is
added, and no dependency rendering implementation is copied or adapted.

Backdrop chains admit the same bounded native filter payloads, model updates and
keyframes. Native temporary layers consume the existing4active-layer limit as
well as the32image/256MiB pool. Ordinary filters and backdrop work can therefore
reach the cap with fewer authored elements. Target-bound input/blur tails clip;
this is not capture of off-target, later-drawn or hidden geometry. Main encoded
RGBA8 and linearRGBA16F camera output use their existing destination domain.
Opacity, clipping, candidate/last-good, output failure and retirement policies
remain with the existing presenter/resource owners. Gallery163 supplies original
foreground/background and live/presentation-time comparisons, authored and
exported with the same source/model/MCP workflow. Masks, box shadows and shader
decorators remain unsupported.
