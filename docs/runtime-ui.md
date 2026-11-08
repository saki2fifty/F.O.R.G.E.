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

Native RML/RCSS authoring uses `UiSourceEditor` in DocumentWorkspace. One source
widget owns its draft/history; the Elements view derives byte ranges through the
existing `validate_text_profile` parser's optional collector. Detached structured
edits validate the whole result and preserve unrelated source bytes. Runtime
admission calls do not allocate an authoring outline. RCSS remains source-only.
Save shares `ProjectTextSource` containment, UTF-8 validation, baseline checks and
atomic replacement with raw automation, preserving uniform CRLF. It does not
publish catalog metadata or reload presentation. Raw automation cannot overwrite
an open native-owned source. MCP borrows the same editor; no agent-only authoring
or runtime DOM writer exists. See [ADR048](decisions/048-ui-source-authoring.md).

Text/attribute editing calls the pinned `Rml::StringUtilities::EncodeRml` API.
Markup escapes do not suppress `{{model}}` bindings or attribute expressions;
those retain ordinary RML source meaning. Leaf text containing child markup or
comments and document/resource structural changes use Source instead. Native
fields hold at most8192bytes; larger leaf text stays available in Source and
disables compact text replacement. Existing admission caps the entire opening
tag at8192bytes, keeping valid attributes within the field capacity. The runtime source
profile remains256KiB/8192tags/32levels. No UI protocol or kit requirement changes.

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
Layer sorting is stable within each group. Private FPS statistics use main only.
Native input can also reach an explicitly enabled scene surface through the
existing presenter; semantic commands continue to identify their document.
Changing the saved output renews the runtime document incarnation and rejects its
old command immediately, before the next copied snapshot.

Hosts update shared presentation time once before Game. CameraRenderTextures
borrows an optional overlay callback after its complete producer composition and
before copying/publishing the current image for Material/relay consumers. The
shared UiDiligent draw_output lays out that context at the pass dimensions and
renders it without advancing simulation or another clock. A failed composition
cannot publish stale current pixels. Main UI draws after the Game display as before.
Density is1 for outputs; main density retains the existing window policy. An output
without an enabled producer has no draw pass. Scene-surface interaction uses the
bounded profile below. Free-space3D layout, stopped Scene overlay and stopped
Timeline UI export remain unavailable.

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

## Interactive scene surfaces

`UiSurfaceInput` is optional authored data on a Mesh entity: typed nullable
`AssetRef<RenderTextureAsset> render_texture`, `bool enabled=true` and
`uint32 geometry=0` (`Quad`0, `Mesh`1). Missing geometry defaults to Quad through
native member-default metadata. Reflected
Scene/Prefab authoring, references, history, schema and MCP use the builtin
component owner. Detached RenderScene stores bounded surface-to-mesh indices
within that same snapshot; it introduces no persistent identity or separate
registry. At most64 admitted surfaces are supported. An absent output, disabled
component or invisible/disabled node/Mesh supplies no surface.

Geometry **Quad** uses the immutable Engine / Quad, one complete ordinary unlit
Material part, and an undeformed pose. Geometry **Mesh** uses admitted indexed or
nonindexed triangles from the retained mesh, its selected rendered LOD and current
pose. Morph and skin placement reuse the selection evaluator; supported static-mesh
wind uses that same evaluator too. Mesh input reads no source models and keeps no
second geometry or pose cache. Every part/LOD needs float2 UV0 inside[0,1],
nondegenerate UV triangles and a complete supported Material selection.

Both modes require opaque/two-sided/depth-tested/depth-writing ordinary unlit
Materials with exactly one Color2D UV0 `baseColorTexture` matching the component's
Render Texture. Identity or complete per-axis flips (scale -1, offset1) are
admitted; rotation, crop and custom programmable surfaces are unsupported. Mesh
input interpolates original vertex UVs using perspective-correct barycentrics,
including triangles clipped before division by W. The existing texture-transform
rows then map authored UV0 to image UV. Renderer validation reports
`render.ui_surface.profile` against the component. Existing last-good resource/pose
selection remains authoritative; interaction requires that exact selected image
to have current completed producers.

Mesh reverse projection searches the selected LOD's UV triangles across parts.
Shared-edge matches at the same posed world point are equivalent; overlapping UV
islands at different world points reject input. In-image UV holes have no target.
A transient capture anchor holds only mesh asset/revision/variant and LOD/part/
primitive data, with no native resource handle or persistent identity. Changed
mesh revision, LOD or geometry mode cancels the anchor. Animation evaluates the
same triangle against the current retained pose. Outside-image dragging continues
on its last triangle plane through the existing plane projection helper.
Validation, UV lookup, detailed picking and geometric blockers share the existing
1,048,576-unit query budget; exhausting it rejects the complete query, including
any earlier candidate. Diagnostics use the same bounded geometry profile.

FrameRenderer borrows its last Game snapshot, retained Mesh draw/pose and Game
Camera views for input queries. Shared camera-relative plane math supports
perspective/orthographic projections, reflections, shear, rank-two XY planes and
finite/infinite far. New uncaptured hits choose the nearest eligible surface and use
the existing bounded geometry query for Mesh/Sprite/Tile blockers, ignoring editor
selectability while honoring visibility and Camera layers. This is geometric
occlusion, not a rasterized alpha/culling/particle hit mask. Separate camera
viewports are supported; a point covered by overlapping Game Camera viewports is
ambiguous and rejected. Producer Cameras are not Game input views.

UiPresenter owns focus, pointer capture, keys/buttons, navigation, text and IME
through its existing native contexts. Shared host callbacks query FrameRenderer
at call time; there is no native-context pointer in a hit, secondary input queue,
world mutation or second presentation clock. Main UI takes uncaptured pointer
precedence. Capture retains surface/output/view identity and permits local XY
outside the Quad until release; loss of geometry/output/view cancels safely.
Normalized UV maps into current output pixels. Reverse camera-relative projection
maps native text caret anchors into main physical pixels. Resizing or replacing
the main presentation releases native input, and native output resizing updates
pointer coordinates without rebuilding the document. Stationary pointers refresh
against displayed scene motion during the existing presenter update.

Runtime document metadata records `ui_surface_input:1` for present Quad
Scene/Prefab component or property-override intent and2 for Mesh intent. Mixed
documents retain the maximum requirement regardless of row order. Current kits
advertise integer `engine.runtime_ui_surface_input:2`, generated from the same
private profile header, and accept requirements1 or2. A kit advertising1 still
accepts Quad-only documents and rejects Mesh documents; unknown profile versions
are rejected. Ordinary image-only documents add no requirement. The
public typed component changes the exact SDK fingerprint; stable plugin ABI and
Scene envelope versions are unchanged.

GamePresentation and editor RuntimeUiHost install the same callback composition;
standalone button events refresh physical pointer coordinates before dispatch.
Callbacks are released while the geometry provider still exists. Gameplay input
capture, relative mouse and editor focus gates retain their existing owners.
Native UI commands, semantic automation/MCP and output incarnation guards are
unchanged. MCP can author all component/source/value assignments and call the
existing semantic commands; raw pointer/ray injection is not a tool capability.
The [manual](../manual/editor/runtime-ui.md#interact-with-ui-on-a-scene-surface)
describes setup, native focus and current limits.

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

Deferred: browser JavaScript, Lua, debugger UI, SVG/Lottie/HarfBuzz, RCSS imports/templates, free-space3D UI layout, collections/two-way gameplay binding, public plugin APIs and a visual UI designer. Standalone gamepad navigation now routes through input actions to native RmlUi focus traversal and activation. Link stylesheets individually. Bounded blur/color filters use transient renderer-owned layers as described below. Other shader decorators remain unsupported and diagnosed. Box shadows use the extension below. Alpha masks use the profile below. Bounded linear gradients use the profile below. Drop-shadow and backdrop filters use the profile below. Use explicit font-family/font-size rather than shorthand. This is the first screen-space integration, not a visual UI designer.

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
ui.observed. Native reliable static roots/literal image attributes create
ui.automatic Runtime edges; other observed resources remain Optional. Image
and mask-image decorators require explicit Runtime Dependencies declarations
when their files are needed in exported content. Multiple roots conservatively reference all non-document
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

Runtime kits advertise `engine.runtime_ui_filters:7` from the same profile header.
Known profiles are additive:1 blur/color,2 drop shadows,3 backdrop composition,
4 alpha masks,5 box-shadow snapshots,6 luminance conversion,7 shadow-panel motion.
The existing compatibility helper admits requirements1..7 covered by a known kit
profile1..7. Static basic filters retain1. Shadow declarations and ordinary dynamic
`data-style-filter` bindings retain2. `backdrop-filter` declarations (including
none), `data-style-backdrop-filter` bindings require3. Static alpha masks require4;
box-shadow declarations/bindings require5. Luminance declarations and dynamic mask/decorator bindings require6.
`forge-shadow-panel` tags and their six custom property declarations/bindings
require7. Encoded inline styles conservatively require7.
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
Other shader effects, free-space3D UI layout remains outside this filter profile. Box shadows use the extension below.

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
exported with the same source/model/MCP workflow. Other shader decorators remain unsupported; linear gradients and box shadows use the profiles below.


## Native linear-gradient profile

The pinned RmlUi parser owns angle, units, automatic stop positions and native
animation interpolation. FORGE's shared `ui_gradient_detail` admission validates
and copies the resolved `linear-gradient` payload: exactly `p0`/`p1` VECTOR2,
`length` FLOAT, `repeating` BOOL and `color_stop_list` COLORSTOPLIST. Stops must
use NUMBER positions and premultiplied byte colors. No native pointers survive
compilation. Drawing, non-drawing inspection and source candidates share this
admission. Other compiled shader names reject; radial/conic types use the extension
below.

Profile1 accepts2..16 ordered stops, finite pixel-local endpoint coordinates and
normalized stop-position magnitudes<=65536,
consistent line length>=1e-4 and repeating period>=1e-6. Colors must be finite
premultiplied RGBA. Coincident stops are allowed in nonrepeating gradients. A
maximum256 copied compiled records spans live contexts; release retires the row.
Legacy `horizontal-gradient`/`vertical-gradient` use ordinary native geometry.
They share source capability metadata with shader-backed linear gradients.

The existing Diligent adapter owns one352-byte pixel constant buffer and reuses
its geometry, clipping, transform, viewport, texture SRBs and draw path. Existing
96-byte vertex constants carry destination-domain and gradient flags. The original
FORGE shader projects pixel-local UVs, interpolates premultiplied stops and wraps
repeat intervals. Encoded RGBA8 retains display values; linearRGBA16F uses the
existing pinned sRGB transfer helper and premultiplies again before source-over.
No per-gradient GPU resources, arbitrary shader authoring API, parallel renderer
or asset-identity system is introduced.

UiResources derives `runtime_features.gradients:1` in the existing source snapshot
for recognized static linear declarations/factories. The extension below uses
profile2 for radial/conic and dynamic declarations. Ordinary/image-only source does not acquire this flag.
Runtime kits advertise integer `engine.runtime_ui_gradients:2` from the same
private profile header. Known requirements1..2 are additive; static linear
declarations retain1. Radial/conic declarations, dynamic decorator bindings and
encoded inline styles require2. Missing, fractional, boolean, too-old or unknown
versions reject required content before export promotion. Filter profiles1..3 retain their meanings; mask support adds4.
The installed native SDK value-header list, C ABI1, identity and scene schema are
unchanged. Source text/register/refresh, saved models, live Play tuning, keyframes,
reload, automation/MCP, camera outputs and source-hidden packages use their
established owners. No separate gradient mutation tool is necessary.

Gallery164 contains original gradient source and a saved angle model. See the
user manual for the illustrated workflow. Validation/capture provenance records
local software-rendering evidence separately from pending Windows/full-editor
and physical-hardware acceptance.


## Native radial/conic gradient extension

Gradient profile2 extends the existing shared admission and copied record rather
than adding a second effect owner. Native radial payloads have exactly `center`
VECTOR2, `radius` VECTOR2, `repeating` BOOL and `color_stop_list` COLORSTOPLIST.
Conic payloads substitute FLOAT `angle` in radians for radius. The approved native
parser owns shape keywords, relative sizes/positions, automatic stop positions
and angle/length resolution. All variants share copied2..16 NUMBER/premultiplied
stops,256live records, ordered finite positions, nonzero repeat period and release.

Original FORGE radial projection measures Euclidean distance after scaling by
inverse resolved radii. Original conic projection measures clockwise angle from
the top, subtracts the normalized starting angle and wraps one turn. The common
stop interpolation/repeat/color/clip/draw path is unchanged. The shape uses the
existing four floats and vertex mode flags: no352-byte PS or96-byte VS buffer
size change and no per-shape GPU resource. Radius components must be finite and
within1e-4..65536 pixels; centers within±65536 pixels and angle magnitude<=65536
radians. The conic center's undefined direction uses normalized position0 before stop repetition. Exact angular
repeat seams can select either adjacent side after floating-point rounding;
small/frequent bands are not analytically antialiased.

Native models/keyframes, candidate retention/cancel/retirement, main/camera output,
non-drawing inspection, automation/MCP and export retain their existing owners.
Source requirements1/2 and runtime-kit2 preserve static linear1 compatibility;
oldkit1 rejects radial/conic/dynamic2. Existing Gallery164 dynamic metadata is
refreshed through source registration/preparation with unchanged source and IDs.
Original Gallery165 demonstrates the new forms. Alpha masks use the extension below. Arbitrary shaders, visual UI authoring and free-space3D UI layout remain outside this profile. Box shadows use the extension below.


## Native alpha-mask extension

Filter profile4 implements the pinned native SaveLayerAsMaskImage callback. The
native ElementEffects producer creates a mask layer during each Render, draws its
decorators, stores a local compiled mask filter, pops the layer, composites content
with ordinary filters followed by that mask, and retires the local filter. Native
syntax, decorator placement and gradient/image resolution remain dependency-owned.

The existing UiFilterLayers owner copies the current layer into an image from its
32image/256MiB logical pool. A pin on that pool slot prevents reuse or eviction
until ReleaseFilter. The copied snapshot preserves alpha if the original target
is subsequently modified. The existing compiled-filter map and handle issuer own
both typed filters and masks; no mask asset identity or image manager is added.
A frame stamp rejects use after a later begin. End clears transient bindings and
layer activity; outstanding pins retain their charged pool images until release.

An independently authored extension to the existing compositor samples mask alpha
and multiplies all premultiplied content channels by it. Both encodedRGBA8 and
linearRGBA16F retain the destination domain; alpha is not decoded as color and no
luminance conversion is applied. Snapshot/copy/intermediate passes use existing
clipping/composition and buffers, with one additional bound image sampler. The
four-active-layer and eight-filter chain bounds include mask work; saved masks
also count toward256compiled filters and the shared image budget.

Candidate preparation remains non-drawing. Because the pinned mask filter is local
to one native Render, RenderGate uses a typed opacity1 placeholder through the
existing filter owner while preparing. This validates chain counts and native
release without reading/drawing the framebuffer or inventing opaque handles.
Actual rendering calls SaveLayerAsMaskImage. Non-drawing inspection and recording
tests validate admission/lifecycle only. Masks remain presentation effects; no new
DOM mutation, gameplay binding or shader-authoring tool is introduced.

UiResources derives filter4 for mask-image; dynamic mask/decorator bindings and encoded inline styles conservatively require current filter6; ordinary static filters1, shadows2 and backdrops3 retain their
requirements. Gradient mask declarations use the existing gradient1/2 lexer and
dynamic mask bindings conservatively require gradient2. Known kit filter1..6 remains
additive; old1..3 reject required4 before export promotion. Sources, values,
keyframes, reload, automation/MCP, main/camera-output and source-hidden packages
share established owners. Original Gallery166 reuses the canonical health PNG
through the existing declare_runtime_dependencies owner and a required typed
edge. Optional ui.observed edges are not silently promoted; no stylesheet parser
or second dependency owner is added.
Luminance masks use the extension below. Arbitrary shaders, visual authoring and
free-space3D UI layout remains outside this subset. Windows/full-editor/physical review is queued.

## Native box-shadow snapshots

Filter profile5 implements the pinned native SaveLayerAsTexture callback through
UiFilterLayers and the existing renderer Texture handle table. The layer owner
copies the active scissor rectangle into a cropped image in its32-image/256MiB
logical pool. A pin prevents reuse/eviction until native texture release; unlike
mask filters these textures may span frames. At most24 such snapshots remain
live, reserving eight slots for active layers and intermediate work. Existing
four-layer, eight-filter,256-filter and512-renderer-texture bounds still apply.
Snapshot SRVs clamp; ordinary source images retain wrap sampling. The geometry
adapter explicitly handles RGBA8 display/HDR linear snapshot source/destination
domains, retaining premultiplication and HDR values. Shared approved transfer
functions remain authoritative. No framebuffer alias, second resource manager,
asset identity or installed SDK/ABI change is introduced.

At RmlUi6.3 ba95ffe8bfb6370efb2cdcca927eaad4710c5413, the box-shadow callback
returns true after SaveLayerAsTexture even when its returned handle is zero.
TextureDatabase retries an uncaptured callback on the next use. The preparation
Gate returns zero without drawing or faulting; the first actual Render creates
the snapshot. It never publishes a blank proxy texture, globally invalidates
textures or adds a second context renderer. Candidate cancellation/retirement
uses native context/texture lifetimes and the same pool pins.

Native geometry supplies offset, spread, inset, rounded corners and background
composition. FORGE independently implements only the existing blur/copy/texture
rendering callbacks. Native sigma is half the CSS blur radius: sigma0..16 permits
resolved radius0..32. Native box-shadow keyframes/transitions are rejected;
static and model-bound values work through existing presenter/model owners.
Native shadow canvases are intersected with the viewport and can clip when
oversized. No expanded off-target canvas or substitute shape algorithm is added.

Literal and model-bound box-shadow properties require5; encoded inline styles
conservatively require7. Static masks4/backdrops3/drop shadows2/basic filters1
retain their prior meanings. Known kit1..7 is additive; older1..4 reject a
requirement5 before export promotion. Gradient profiles remain1/2. Source
registration/preparation, camera outputs, MCP, live reload and source-hidden
packages use existing owners. Original Gallery167 demonstrates the workflow.
See [capture provenance](images/readme/README.md) for local evidence and
platform limits. Windows/full-editor/physical review remains queued.

## Animated shadow panels

The `forge-shadow-panel` native element derives one ordinary native box shadow
from six registered native styles: `forge-shadow-x`, `forge-shadow-y`,
`forge-shadow-blur`, `forge-shadow-spread`, `forge-shadow-color` and
`forge-shadow-inset`. The first four use native length parsing; color uses the
native color parser; inset accepts `outset` (default) or `inset`. Length defaults
are0px and color defaults transparent. Set `display:block` where a block panel is
intended. The panel owns its `box-shadow` value; author its shadow through these
six controls, rather than assigning `box-shadow` on this element. Ordinary
static/model-bound box shadows on other elements retain the preceding contract.

Native keyframes/transitions interpolate these numeric/color/keyword controls.
Inset switches discretely; color uses the pinned library's gamma-aware
interpolation. Native presentation time, long-frame clamp, pause behavior and
model/class/pseudo-class owners remain authoritative. Avoid simultaneous binding
and animation writers on one control. Each existing candidate/main/output
Context.Update is followed by deriving the shadow and refreshing only this
native element's computed properties before rendering. There is no second
context update, clock, interpolator, stylesheet parser or renderer. Direct
BOXSHADOWLIST keyframes/transitions remain unsupported.

Resolved lengths must be finite and within +/-8192px; blur must resolve0..32px.
Percentages are unsupported. Native font-relative and density-relative units
resolve against the current element/view. A view admits at most24 shadow panels;
all contexts and ordinary shadows still share the24 cached snapshots,
32-image/256MiB pool and existing layer/texture bounds. Mixtures may reach shared
limits earlier. Changed shadows replace native cache references, rather than
accumulating snapshots. Candidate admission performs no framebuffer capture;
failed reload preserves the usable document. Live binding/animation faults are
diagnosed through the existing presenter and do not roll back arbitrary DOM state.
Native oversized-canvas viewport clipping remains unchanged.

Private filter7 is required for the component tag or any of its six styles or
`data-style-*` bindings. It covers preceding profiles1..6; older kits reject a
requirement7 through the existing export compatibility owner. UI source evidence,
inspection transport, catalog refresh, runtime packaging, source/model authoring
and MCP use their existing paths. No public SDK/ABI or Scene format change.
Gallery173 demonstrates keyframes, hover transitions, inset motion and live blur.

## Native luminance-mask extension

Filter profile6 adds an independently authored `luminance(...)` DecoratorInstancer
through the pinned public API. A presenter owns registration from initialization
through native shutdown. A bounded string property delegates one anonymous inner
declaration to the existing native mask-image parser. Instancing admits native
image and supported gradient types, rejects names/lists/nested wrappers before
delegating, and retains ordinary native element data. No dependency code, separate
stylesheet parser, asset identity or image manager is introduced. The inner source
is bounded to4096bytes; existing source/image/gradient limits remain authoritative.
Named outer decorators may set this wrapper's `source` property.

The wrapper compiles a normal internal filter through RenderManager and draws the
inner decorator into one additional shared layer. UiFilterLayers converts encoded
premultiplied SDR RGB back to straight linear RGB, or uses premultiplied linear HDR
RGB directly. Dot weights0.2126/0.7152/0.0722 yield relative luminance times alpha;
saturation bounds coverage, and transparent pixels yield zero. A white premultiplied
coverage result is composed into the native mask layer. Existing alpha snapshot,
frame stamps, pool pins, image budget, compiled-filter map, clipping, composition,
retirement and failure handling stay unchanged. No framebuffer readback is used.

One extra active layer counts against the existing four-layer limit. The normal
wrapper filter counts against256compiled filters, its composition against the
8filter chain bound, and every scratch allocation against32images/256MiB logical
pixel storage. Candidate preparation validates through RenderGate without drawing;
invalid sources or deep combined masks preserve the preceding usable contexts.
Live source bindings/keyframes and main/camera-output UI reuse native presentation.

Static luminance declarations, including named definitions, require filter6.
Static alpha masks4 and box shadows5 retain their profiles. Dynamic decorator/mask
bindings and encoded inline styles conservatively require6; dynamic gradients
retain their gradient2 requirement. Existing runtime-kit negotiation advertises6
and rejects missing/older support before export promotion. Existing source editing,
registration, inspection, runtime-dependency review, revision/history, automation/
MCP, reload and source-hidden packaging owners supply the workflow. Gallery171
shares the canonical health PNG through an explicit required runtime edge.

Local numerical coverage includes28whole-image SDR/HDR cases: independent constant
black/white/primary/gray/transparent and partial-alpha oracles, continuous fades,
local-image placement, child content, named wrappers and unchanged alpha masks.
Recording and isolated native inspection cover admission/lifetime/main/camera-output;
both MCP framings cover authoring/replay/stale/source guards/reload/rejected nesting.
Four animated/live/portrait Game source/export pairs run on software Vulkan.
The authentic `manual/images/ui-luminance-mask-gallery.png` is a lossless encoding
of that native Game capture. Windows/MSVC/D3D12/full Main/physical input/GPU review
is queued; these local fixtures do not establish that acceptance.
