# Edit a game's UI source

Use **UI Source** to edit a game's RML document or RCSS stylesheet inside FORGE.
RML describes the widgets and their bindings; RCSS controls their appearance and
layout. **Source**, **Elements** and **Preview** share one source draft and Undo
history. Preview an unsaved RML draft while Play is stopped; use **Game** to test
its gameplay bindings and interaction.

## Open an example

1. Copy **Examples/FeatureGallery** to a writable location and open its project.
2. Open **Scenes/210-ui-source-authoring.scene.json** and press **Play**.
3. Open **Game**. The panel shows text, a tick counter, Pause, Resume, Step and a text field. Capture gameplay input to interact with it.
4. Stop Play. In **Content**, find **Assets/UI/SourceAuthoring/hud.rml** and choose **Edit UI source**. Double-clicking the registered document also opens it.

The example includes a Camera and uses the supplied Lato font. No Light or
project C++ module is required. Register your own RML or RCSS files through
**Content → Create / Register → Runtime UI → Register / Refresh** before opening
them from Content. Keep editable sources under **Assets**, **Sources**, **UI** or
**Shaders** inside the project.

## Edit widgets with Elements

![UI Source Elements view with a selected paragraph and text controls](../images/ui-source-elements.png)

Select a widget in the source outline to change its attributes, text or position.

1. Select **Elements**. Expand the body and select a paragraph, button or container.
2. To change an attribute, enter its **Attribute** name and **Value**, then choose **Set attribute**. For example, use `style` and `color:#73d7bd;` to set text color. **Remove attribute** removes the named attribute.
3. For a leaf containing text, edit **Text** and choose **Set text**. Containers with child widgets or comments protect their contents from this replacement.
4. Select a container, choose **New child**, then **Add child** to append a div, span, paragraph, button, input, image or line break.
5. Select a widget and use **Move up**, **Move down** or **Remove element** to change its position among siblings or remove it.

Each successful operation is one source Undo step. The outline is derived again
after an edit; select the element again before another structured operation.
Element positions describe the current source, rather than permanent widget IDs.
Use an `id` attribute to name a widget for styles and bindings.

Text and attribute values escape markup characters such as `<` and `&`. RML
bindings keep their source meaning: `Tick {{tick}}` displays the runtime counter.
Use **Source** to write child markup, resource declarations or other RML syntax.
The Elements controls protect document structure and style/resource declarations.
Use **Preview** for native rendering and the supported pixel layout controls.

## Preview and adjust layout

For a ready-to-edit example, open **Scenes/211-ui-layout-preview.scene.json** in
the Feature Gallery. In Content, choose **Edit UI source** on
**Assets/UI/Layout/layout.rml**. Its blue panel already has a unique ID and an
inline pixel layout.

1. Open a registered RML document and stop Play.
2. Select **Preview**. Set **Size (px)** to the desired physical viewport width and height. **Density** controls density-independent units such as `dp`.
3. Click **Refresh preview** to reload the current draft and saved linked resources. RML edits appear without saving. Linked RCSS, fonts and images use their saved bytes.
4. Click an element in the image, or select it in **Elements** and return to Preview. A unique `id` connects the native element to its source. Blue outlines identify editable layouts; an amber outline and explanation identify unsupported layouts.
5. Drag an editable element to move it. Drag its bottom-right handle to resize it. Release to commit one source Undo step, or press **Escape** to cancel.
6. Alternatively, enter **Left / Top / Width / Height (px)** and choose **Apply layout**.

![UI Source Preview with the blue panel selected, pixel fields and its resize handle](../images/ui-source-preview.png)

The selected panel shows its authored pixel values and a blue border with a
bottom-right resize handle. The image fits the available editor area while
**Size (px)** retains the preview's physical viewport dimensions.

Canvas editing requires a quoted inline `style` with `position:absolute` and
plain pixel values for `left`, `top`, `width` and `height`. For example, add this
child inside the document's body in Source:

```xml
<div id="layout-demo" style="position:absolute;left:24px;top:400px;width:260px;height:80px;background-color:#285d88;pointer-events:auto;">Move this panel</div>
```

Give the element an ID used only once. Existing percentages, `dp`, automatic
sizes and responsive rules remain available in Source. The source-authoring
example's main panel uses responsive constraints, so edit that panel in Source
or create a separate pixel layout element for the canvas.

Move and resize replace the four existing values and retain unrelated source,
comments and styling. The source stays unchanged during a drag; Save and source
Undo/Redo wait until the gesture finishes or is cancelled. Closing the source,
switching source views or starting a blocking task cancels an active gesture.

Preview is static: it does not execute gameplay, evaluate gameplay models or
provide runtime form interaction. Test those behaviors in Play. A failed source
or resource load keeps the last usable image, marks it stale and prevents layout
edits against that old geometry. Correct the error and refresh.

After replacing an existing font family or changing the project's text shaping
or whole-character editing settings, save any draft you need, close UI Source and
reopen it. This starts the preview with the current font and text settings.

Preview supports dimensions from 1 to 2,048 pixels per axis and density from 0.5
to 4. Native observations cover at most 512 IDs of up to 128 UTF-8 bytes each.
Canvas sizes must be at least one pixel; values must stay within 65,536 pixels.
Transforms, animation, transitions, scrolled ancestors, min/max size constraints,
right/bottom constraints, duplicate properties, entity-encoded inline styles and
data-bound ancestors use Source for layout editing. Sizes clamped by native box
sizing also use Source. An RML draft must have a
registered, contained source in the editable project. To preview an RCSS
stylesheet, open the RML that links it.

## Edit the source and styles

![UI Source text view with source editing and Save controls](../images/ui-source-text.png)

Use Source for RML text and RCSS styles. All views share the same draft and history.

Open **hud.rcss** from Content to change the example's colors, spacing or sizes.
Only one UI source is open at a time. Switching to another file asks you to save,
discard or keep editing an unsaved draft. RCSS has no element outline.

Incomplete markup may remain in the text draft. The Elements view explains why
it cannot inspect that draft. Save checks the bounded source structure and supported
static profile; native styles, properties and resources are checked when loading
the HUD. Correct source errors before saving and test the result in Play. Current admission limits include 256 KiB of source, 8,192 tags and
32 levels of nesting. See [Runtime UI](runtime-ui.md) for supported resources,
bindings and styling limits.

## Save, run and export

Choose **Save source**, or use the Save shortcut while UI Source has focus.
**Undo** and **Redo** act on this source, independently of Scene history. Saving
does not clear source history. Save retains a file's uniform Windows line endings.

Save checks that the file on disk still matches the version you opened or last
saved. If another program changed it, Save reports a conflict without overwriting
that file. Preserve any draft text you need, then choose **Reload from disk** and
explicitly discard the old draft to load the current file. Reload starts fresh
source history. Closing an unsaved source offers **Save**, **Discard** and
**Keep editing**.

Save or discard all drafts before starting Play, switching projects or exporting.
UI Source is read-only during Play. To test a native edit, Stop, edit and save,
then start Play again. A user's externally saved change can use **Reload UI**
during Play; unsuccessful reload keeps the last working HUD. Reload clears local
focus and text-field contents. Restart Play after replacing a font family.

Saving source does not register it, refresh dependency metadata, assign a UI
Document component or reload the running HUD. Those actions stay explicit. Use
the [Runtime UI registration and resource workflow](runtime-ui.md) to record
linked styles, fonts and images after a successful load.

For export, save the Scene and choose it as the project's startup Scene. Export
through **Export Game** with a matching runtime kit. Export follows the registered
document and its supporting resources; the native source editor and its draft
history are not part of the exported game. The example's buttons and bindings use
the existing runtime UI commands.

See also [Content](content-browser.md), [Undo and redo](undo-redo.md),
[Runtime UI](runtime-ui.md) and [Export a game](runtime-content.md).
