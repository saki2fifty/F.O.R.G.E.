# Runtime UI

Runtime UI is the interface shown **inside your running game**: HUDs, menus, buttons and text fields. FORGE uses RmlUi for it. The editor's own panels still use Dear ImGui.

## Create your first HUD

1. Open a writable project. In **Content → Create / Register → Runtime UI**, click **Create HUD example**.
2. Create or select an entity in the Hierarchy panel.
3. In **Inspector → Components**, click **+ Add Component → Runtime UI / UI Document**.
4. Use the **Document** asset picker to select the new `Assets/UI/HUD-…/hud.rml` asset. Keep **Enabled** and **Visible** checked.
5. Save, then press **Play**. The Game viewport shows a styled panel, image, simulation tick, pause state, buttons and a text field.
6. Turn on **Capture gameplay input** in the Game controls to interact with the HUD.

The example creates three files: `hud.rml` for content, `hud.rcss` for appearance, and `badge.tga` for the image. You do not need to enter an asset UUID or write C++.

## Try the buttons and text field

Click **Pause**. The tick counter stops, but the HUD stays interactive. Click the text field and type; gameplay keyboard actions should not fire while the field owns your input.

Click **Step** while paused: the counter advances by one. Click **Resume** to continue. **F6** pauses/resumes, **F7** steps while paused, and **Escape** releases game input so you can use the editor again.

For slow motion, a button can use `data-event-click="command('TimeScale','0.5')"`.
Use `1` for normal speed or `0` to hold motion while keeping the HUD interactive.
Values must be numeric strings between 0 and 4. This changes simulation speed; it
keeps existing sound at normal speed. The original Gallery58 Scene includes six
working speed buttons and can be exported without a C++ gameplay module.

UI capture clears held gameplay controls to prevent stuck movement. Release and press a movement key again when returning from a field to gameplay.

Resize the viewport and editor window. The HUD remains positioned within the game image. Ctrl+Plus/Minus changes the editor interface independently; it is not a game HUD zoom control.

## Use your own document

Put an `.rml` file and its resources inside the project. Enter its project-relative path in **Content → Create / Register → Runtime UI → Project UI file**, then click **Register / Refresh**. Assign it through the Document picker.

RML resembles HTML; RCSS resembles CSS, but this is not a web browser. Link each stylesheet from the document:

```xml
<rml>
<head><link type="text/rcss" href="hud.rcss" /></head>
<body>
  <p>Tick: {{tick}} — Paused: {{paused}}</p>
  <button data-event-click="command('Pause')">Pause</button>
</body>
</rml>
```

A simple stylesheet:

```css
body { font-family: Lato; font-size: 18px; color: #edf3fa; }
button { padding: 8px 12px; background-color: #34597b; }
button:hover { background-color: #477da9; }
```

FORGE supplies the data model automatically; omit `data-model` attributes. `tick` and `paused` reflect gameplay state. Add [UI Values](sequence-ui.md) on the same entity to supply named numbers without C++. Project C++ can publish additional scalar values and register commands. The included pause/step example works without it. Events request changes from gameplay. A text field's local contents are transient and do not automatically change game state.

For translated menus, use a [UI Localization catalog](ui-localization.md) and
`{{ 'message_key' | tr(forge_locale) }}`. Its language command works without
project C++, including while paused. This catalog stores literal messages;
editor translation, full shaping and parameter/plural formatting are unavailable.

For menus and state-driven choices, use [Application States](application-states.md).
Its read-only state bindings, ancestor conditions and revision-checked `forge.state`
buttons work without a project C++ module, including while paused.

## Draw UI in a Camera image

1. Create and publish a [Render Texture](render-textures.md). Assign it to an enabled Camera, and display the image through a Mesh Material.
2. Select the UI entity. In **Inspector → UI Document → Render Texture**, select that same output asset. Leave this field empty for an ordinary main-window HUD.
3. Keep **Document**, **Enabled** and **Visible** assigned as usual. Save the Scene and press **Play**. The document draws over the complete output image after its Cameras, before Materials and other Cameras read the image.
4. Add [UI Values](sequence-ui.md) to bind numbers to its text or styles. These values and the built-in `tick` / `paused` bindings work on the output too.

Open **Scenes/154-camera-output-ui.scene.json** in the Feature Gallery for a
working example. Both monitors show the telemetry panel; the second monitor reads
it through another Camera during the same frame. The guide stays on the main Game
view. Pause, select **Telemetry - UI in Split camera output**, and change
**Live gameplay → UI Values → amount** between `0` and `1` to fade the panel.
Use the ordinary Keep runtime values review if you want to retain a live change.

![Upright telemetry UI on two camera-image monitors, with a separate main Game guide](../images/camera-output-ui-gallery.png)

The same telemetry panel reaches the direct monitor and its camera relay.

Each output uses its published width and height. RCSS `px` and `dp` both correspond
to output pixels, independently of editor/window scale. Higher **Layer** values
place documents above lower ones on the same output. One UI Document chooses one
output; use another entity to display another copy.

Output images are display surfaces. Clicking a Mesh does not send pointer,
keyboard or focus events to its UI. Put interactive controls on the main Game HUD.
The output must have an enabled Camera producer; assigning UI alone creates no
image. Runtime UI draws during Play and standalone gameplay; it is not drawn in
the stopped Scene view or stopped animation frame exports.

Save the UI resources, Scene and output/Material publications before exporting.
Use the runtime kit packaged with your editor; an older kit without output-UI
support is rejected. The exported game includes the reachable RML resources and
recreates the image. UI focus, animation progress and live values are not saved
unless an existing save/review workflow explicitly retains them.


## Animate your HUD

Open **Scenes/100-ui-animation.scene.json** in the Feature Gallery and press **Play**.
The blue bar changes width and opacity, and the yellow marker moves horizontally.
Capture gameplay input and click **Pause**: the tick counter stops while both
animations continue. Click **Resume** to change the state panel's color smoothly.
Hover a button to see its color transition.

![Animated HUD with a blue width/opacity bar, yellow translating marker and pause controls](../images/runtime-ui-animation.png)

The gallery HUD keeps moving while the paused tick counter holds.

RCSS motion belongs to the document's stylesheet. It uses presentation time,
so gameplay pause and simulation speed do not control it. It is separate from
Scene objects' Animation Timeline. To animate a bound number with sequence time, use [UI Values](sequence-ui.md). Edit the gallery's **Assets/UI/Motion/hud.rcss**
in a text editor to change the duration, endpoint values or colors. Save, then use
**Reload UI** in the Game controls. A successful reload restarts the document's
animations; an invalid replacement keeps the previous usable UI and shows a
diagnostic. Gameplay state stays intact.

For a repeating width and opacity animation:

```css
#meter { display:block; height:24px; background-color:#48b6d5;
         animation:1s linear-in infinite alternate meter-motion; }
@keyframes meter-motion {
  from { width:40px; opacity:0.2; }
  to   { width:240px; opacity:1; }
}
```

For a state-driven transition, add `data-class-paused="paused"` to an RML element:

```css
#state { background-color:#285a74;
         transition:background-color 0.4s linear-in; }
#state.paused { background-color:#986026; }
```

Use RmlUi timing names such as **linear-in**, **cubic-in-out** and **sine-in-out**;
plain `linear` is not an accepted timing name. The gallery demonstrates width,
opacity, color and translation. Keep animated sizes inside the viewport and avoid
having multiple animations control the same property. UI animation is intended
for visual feedback, not precise gameplay timing: long presentation gaps advance
an animation by at most 0.1 seconds per update.

Save the Scene and select it as startup before exporting. The game includes the
required RML and RCSS through the normal content package. This example needs no
C++ module or compiler. Transient animation progress is not saved in the Scene.

## Apply blur and color filters

![Six samples compare reference shapes, grayscale, brightness, contrast, opacity and live blur](../images/ui-filter-gallery.png)

The Game view keeps the sample labels sharp while each panel filters its own shapes.

Open **Scenes/155-ui-filters.scene.json** in **Examples/FeatureGallery** and press
**Play**. Compare the reference shapes with grayscale, brightness, contrast,
opacity and blur. Pause, select **Filter lab HUD**, and change **Live gameplay →
UI Values → blur** between `0` and `16`. The blur sample updates while the fixed
gameplay tick stays held. The small soft pulse keeps animating on presentation time.
Stop returns the live value to its saved setting.

Edit **Assets/UI/Filters/Lab.rml** through the Content source workflow or a text
editor. Filters belong in an element's RML inline style or linked/embedded RCSS:

```css
#panel { filter: blur(2px) contrast(1.2); }
```

Filters apply to the element and its descendants in the written order. Put a label
outside that element if its text should stay sharp. A named UI Value can control a
filter with `data-style-filter="'blur(' + blur + 'px)'"`. Use a saved **UI Values**
component for the initial value and the live Inspector to try changes during Play.
Avoid having a binding and an animation control the same filter property.

- `blur(2px)`: standard deviation from `0` to `16` resolved pixels.
- `opacity(0.5)`: `0` to `1`.
- `grayscale(1)`: `0` to `1`.
- `brightness(1.5)`: `0` to `4`; `1` keeps the original brightness.
- `contrast(1.5)`: `0` to `4`; `1` keeps the original contrast.

Chains support up to eight filters and four nested filtered layers. Blur softens
pixels within the target image; content beyond the image edge is unavailable.
Large viewports and nested effects use more temporary image memory. Unsupported
filters, excessive values or resource limits report a problem. **Reload UI** keeps
the previous usable UI when a replacement fails. Correct the source and reload.

Filters also work in UI assigned to a camera image. Save the sources and Scene,
select the Scene as startup, and export with the runtime kit supplied with this
editor. An older kit without filter support is rejected before replacing an
existing export. No C++ module is needed. Drop shadows use the workflow below;
backdrop filters and linear gradients use the workflows below. Masks, box shadows
and other shader decorators are unavailable.

## Add a drop shadow

Open **Scenes/162-ui-drop-shadows.scene.json** in **Examples/FeatureGallery** and
press **Play**. Compare the reference, hard, left-offset, soft, stacked and live
samples. Shadows follow the rendered alpha of the element and its descendants.
The original content stays above its shadow.

![Original UI shapes with hard, left-offset, soft, stacked and live shadows](../images/ui-shadow-gallery.png)

The small shadow pulse animates on presentation time, including while Pause holds
the fixed gameplay tick.

1. Pause, then select **Shadow lab HUD** in Hierarchy.
2. Under **Live gameplay → UI Values**, change **blur** between `0` and `16`. Watch the **Live** sample soften. `0` gives a hard shadow.
3. Stop to restore the saved value. Set **UI Values → blur** in Edit and save the Scene to keep a new initial value.
4. Edit **Assets/UI/Shadows/Lab.rml**, save it, and use **Reload UI** during Play to try source changes.

A filter uses color, horizontal offset, vertical offset and blur sigma:

```css
#badge { filter: drop-shadow(#20408080 8px 6px 2px); }
```

This casts a partly transparent blue shadow, eight resolved pixels right and six
down, with sigma two. Negative offsets move left or up. Each offset must stay
between `-128` and `128`; sigma must stay between `0` and `16`. Color includes its
own alpha. A transparent shadow adds nothing. Keep model-bound values in range.
This is the `filter` effect; the `box-shadow` property is unavailable.

Chain filters in the desired order. A second shadow receives the result of the
first, including its shadow. Opacity before or after a shadow can therefore give
different results. Filtered layers share the nesting, memory and image-edge
limits above. Content and blur tails outside the target image are unavailable.

Drop shadows also work in UI assigned to a camera image. Save the sources and
Scene, select it as startup, and export with the runtime kit supplied with this
editor. An older incompatible kit rejects before replacing an existing export.
The same saved UI Values and source animation run in the exported game.

## Filter the background behind a panel

Open **Scenes/163-ui-backdrop-filters.scene.json** in **Examples/FeatureGallery**
and press **Play**. The cards compare reference colors, backdrop blur, grayscale,
contrast, a foreground filter and a live backdrop. Text above a backdrop filter
stays sharp. The **Foreground** card applies its blur to its own text and surface.

![Colored stripes behind rounded panels with blur, grayscale, contrast and a live backdrop](../images/ui-backdrop-gallery.png)

The colored bar beneath the cards animates its backdrop blur on presentation time.

1. Pause, select **Backdrop lab HUD**, and change **Live gameplay → UI Values → blur** between `0` and `16`.
2. Watch the **Live** card's background soften while its text stays sharp. `0` removes the blur.
3. Stop to restore the saved value. Change **UI Values → blur** in Edit and save the Scene to keep a new initial value.
4. Edit **Assets/UI/Backdrops/Lab.rml**, save, and use **Reload UI** during Play for source changes.

Apply a backdrop filter to a transparent or partly transparent panel:

```css
#glass {
    background-color: #ffffff10;
    border-radius: 6px;
    backdrop-filter: blur(3px) brightness(0.8);
}
```

The panel samples content already drawn behind it in the current target. Its own
background, border, text and children draw above that result. A fully opaque panel
can cover the filtered background. Its border box and rounded corners constrain
the backdrop result; parent clipping also applies. Objects drawn later are not
part of that backdrop.

Backdrop chains use the same supported filters and ranges as ordinary filters.
Use `data-style-backdrop-filter="'blur(' + blur + 'px)'"` for a named UI Value.
Keep bindings and keyframes for a property separate. An ordinary `filter` on the
same panel processes the combined backdrop and panel contents afterward.

Backdrop work consumes temporary layers from the shared image budget. A panel
with both kinds of filter uses more layers; deep combinations can reach the
four-active-layer limit with fewer than four panels. Reduce nesting or effects
when admission reports a limit. Target-edge pixels and blur tails outside the
current image remain unavailable. Correct an invalid source and **Reload UI**;
a rejected replacement keeps the preceding usable presentation.

Save the sources and Scene before export and use the runtime kit supplied with
this editor. Older kits without backdrop support reject before replacing an
existing export. Main Game UI and UI assigned to camera images use this workflow.

## Add a linear gradient

Open **Scenes/164-ui-linear-gradients.scene.json** in **Examples/FeatureGallery**
and press **Play**. The cards show horizontal, vertical, multiple-stop,
transparent, repeating and live-angle gradients. The small bar below them changes
angle on presentation time, including while simulation is paused.

![Six colored UI gradient samples, including transparency and a live angle](../images/ui-gradient-gallery.png)

Apply a gradient with the `decorator` property:

```css
.panel {
    width: 240px;
    height: 80px;
    decorator: linear-gradient(90deg, #e96644 0%, #79e7cf 45%, #4e96df 100%);
}
.stripes {
    decorator: repeating-linear-gradient(90deg, #e96644 0%, #4e96df 25%);
}
```

An angle of `90deg` runs left to right; `180deg` runs top to bottom. Supply two
through sixteen stops, with optional positions. Alpha in a color fades into the
content already behind the panel. Rounded borders and parent clipping apply.
For a two-color alternative, use `horizontal-gradient(#e96644 #4e96df)` or
`vertical-gradient(#e96644 #4e96df)`; these forms separate colors with a space.

In the gallery, select **Gradient lab HUD** and change **UI Values → angle**.
During Play, use its live tuning controls to try `0`, `45`, `90` and `180`.
The source binds that value with:

```html
<div data-style-decorator="'linear-gradient(' + angle + 'deg,#e96644,#4e96df)'" />
```

Live tuning affects the current session. Stop Play and edit the authored value to
keep it, then save the Scene. Source keyframes can animate compatible gradient
angles and stops; keep a binding and keyframes off the same property.
Register or refresh edited RML/RCSS through **Content**, then **Reload UI**.
An invalid replacement keeps the last usable UI and reports a problem.

Use finite stop positions in nondecreasing order. Resolved endpoints
use finite coordinates within ±65,536 pixels. Resolved stop positions, measured
relative to the gradient line, are bounded to ±65,536. The line must have nonzero
length, and a repeating gradient needs a nonzero repeat interval. At most 256 compiled gradient records
can be live across UI contexts. Reduce stops or elements if admission reports a
limit. Radial and conic gradients, masks and arbitrary shaders are unavailable.

Main Game UI and UI assigned to camera images use the same gradient source.
Save the sources and Scene, select the startup Scene, then export with the runtime
kit supplied with this editor. An older incompatible kit rejects before replacing
an existing export. No C++ module is needed.

## Layout your document

RmlUi does not supply a browser's default HTML layout. If a menu collapses into a
narrow strip, give its containers block layout in your stylesheet:

```css
body, div, h1, h2, p { display: block; }
```

Set the sizes and spacing your design needs. Use percentage widths with a maximum
width and scrolling for long menus so controls remain reachable in smaller windows.
The reference game's stylesheet provides a working example.
Scrollable documents must also style native `scrollbarvertical` with an explicit
width; otherwise its auto-sized box can consume the menu content width. The
reference stylesheet includes a visible track and draggable thumb.

## Try the image gallery

Open **Scenes/59-ui-png-jpeg.scene.json** in **Examples/FeatureGallery** and press
Play. The HUD shows a transparent PNG health icon and an original JPEG landscape.
Pause, Resume and Step remain usable in small windows. Export the scene through
the normal Export Game workflow; the required images ship with it.

For your own HUD, place images beside the RML document and use, for example,
`<img src="health.png" />`. Register/refresh the document through Content. A UI
image uses the same Texture asset identity as an image imported for a 3D material.
UI displays its source pixels; a material uses its cooked texture settings. Reload
UI applies valid image changes. A bad image keeps the last working HUD and reports
a problem. FORGE does not apply camera orientation tags or image color profiles;
export artwork in the orientation and display colors you want.

## Styles, fonts and images

Use document-relative local resource paths. The supplied **Lato** font is packaged with FORGE, so it does not depend on installed system fonts. You may load project `.ttf`/`.otf` fonts using an RCSS `@font-face` declaration, with suitable redistribution rights. Use explicit `font-family` and `font-size`; font shorthand is not supported in this integration. When replacing a font using the same family name, restart Play to clear its font cache.

Images support **PNG**, **JPEG** (`.jpg` or `.jpeg`) and uncompressed true-color **TGA** (24 or 32 bits), at most 2048×2048. Use PNG for transparent icons and JPEG for opaque artwork. Image bytes and decoded pixels have additional limits; very large or high-precision images can exceed them. SVG, remote URLs, templates and stylesheet imports are not supported yet. Link multiple stylesheets individually. Blur and the color filters described above use bounded temporary layers. Drop shadows use the filter workflow above. Backdrop filters use the workflow above. Linear gradients use the workflow above. Masks, radial/conic gradients, other shader decorators and box shadows are unavailable.

`px` means game render-target pixels; `dp` follows display density. The editor's interface scale remains separate. Check text input and sizing on the displays and input languages used by your game.

Font `src` paths are relative to the **project root**, as a FORGE policy for RmlUi 6.3 font declarations. For example, place `game.ttf` at that root:

```css
@font-face { font-family: Game; src: "game.ttf"; }
body { font-family: Game; }
```

## Edit and reload

Edit the RML or RCSS file using your text editor, save it, then click **Reload UI** in the Game controls during Play. FORGE prepares the replacement first. If it fails, the previous usable HUD remains visible and an error explains the failure.

Reloading clears hover/focus and local text-field contents. It does not restart gameplay. UI asset creation and file editing are outside scene Undo/Redo; use your text editor or version control for those files.

## Multiple documents and prefabs

Add UI Document to more than one entity to show multiple documents. **Layer** controls their starting draw order; larger values draw later. Equal layers follow scene order. RmlUi handles normal focus and element ordering within the UI context. Up to 16 documents can be active.

**Enabled** controls whether a document participates. **Visible** hides or shows it without removing the authored component. A hidden document cannot send gameplay commands.

UI Document works with [prefabs](prefabs.md). The document reference, visibility and layer can inherit from the source or be overridden. Property Revert follows the prefab again; scene Undo/Redo restores these edits. the component header’s **Remove component / Revert component** removes local configuration/overrides. UI DOM and local input state are not saved into the prefab.

## When something does not appear

- Confirm Document points to a registered `.rml` asset and Enabled/Visible are checked.
- Read the Runtime UI message in the Game controls. Check balanced tags, linked styles and resource paths.
- Keep resources inside the project; absolute machine paths and web URLs reject.
- Use the packaged Lato family first to distinguish font issues from layout issues.
- Large documents, deep nesting, too many files or oversized images/fonts reject with limits.
- After fixing a file, use Reload UI. If the first document could not load, stop and restart Play.

Stopping Play destroys the live UI. Starting again rebuilds it from the saved configuration and current runtime state. Recovery recreates the UI; unsaved text-field contents and hover state are not restored.

## Try the complete workflow

Create the example, assign it, Play, capture input, Pause, type, Step once and Resume. Resize the viewport, press Escape, Stop, then save/reopen the scene. Finally create a prefab from the UI entity and try a visibility override and Revert. No C++ changes are needed.

See also [Play mode](play-mode.md), [Gameplay input](input.md), [Prefabs](prefabs.md), and [Content browser](content-browser.md).


## UI files in Content

**Register / Refresh** also accepts project RCSS styles, TTF/OTF fonts and TGA
images. Each registered file gets a stable identity; refreshing preserves it.
Select the asset in Content to see its admitted format, source size and source
status. This action validates source admission and records metadata. It does not
load a HUD, replace a live font or cook image data. TGA registration uses the
existing Texture asset type, so it can later use the normal texture import
workflow. An already imported Texture keeps its settings and selected output.

To record the files your HUD actually uses:

1. Run **Play** and show the HUD in Game. Resolve loading errors first.
2. If you edited the files, use **Reload UI** and verify the replacement succeeds.
3. **Stop** Play.
4. Open **Content → Create / Register → Runtime UI** and choose **Refresh loaded resources**.
5. Refresh Content and inspect the document's dependencies and their referrers.

This uses the last successful UI load in this editor session. It records the
files RmlUi read, including linked styles, project fonts and images. With several
simultaneous documents, their observed supporting resources are conservatively
shared in the dependency list. This is not a claim to discover every resource
that a later dynamic UI state might request. Engine-provided Lato has no project
AssetId.

A file edited since that load rejects the metadata refresh; the previous catalog
stays intact. Load the edited HUD successfully before retrying. Switching projects
clears the remembered set. Content source status changes when a recorded source
or dependency differs; that status does not automatically reload presentation.
Use **Reload UI** for RML/RCSS/images and restart Play for changed font families.
The generic asset **Reimport** command is for cooked importer routes and does not
replace this UI workflow. Metadata publication is separate from scene Undo.

## Standalone loading state

In current source builds, a game UI can show the host's scene-loading state
without receiving access to the host or graphics objects. These read-only values
are supplied to every RmlUi document:

- `forge_loading_state`: idle, preparing, loading, ready, activated, cancelled, failed or faulted.
- `forge_loading_stage`: current preparation stage.
- `forge_loading_completed` / `forge_loading_total`: completed stages and total; zero total means no measurable progress yet.
- `forge_loading_error_code` / `forge_loading_error`: failure category and readable reason.
- `forge_loading_can_cancel`: whether the unpublished scene can still be cancelled.

For example:

```html
<div>{{forge_loading_stage}} {{forge_loading_completed}} / {{forge_loading_total}}</div>
<div>{{forge_loading_error}}</div>
<button data-if="forge_loading_can_cancel" data-event-click="cancel_loading()">Cancel</button>
```

Cancellation leaves the currently active scene intact. An old Cancel request
cannot cancel a newer scene request. Progress counts preparation stages, not
bytes or an estimated time. The initial host displays a loading title until its
first UI is ready; an already loaded menu/scene can present later transitions.
The game supplies the loading-screen design. These names are reserved for the
presentation host; do not publish gameplay values under the same names.
