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

![UI Source Elements view with a selected paragraph, ID prefix, duplication and text controls](../images/ui-source-elements.png)

Select a widget in the source outline to change its attributes, text or position.

1. Select **Elements**. Expand the body and select a paragraph, button or container.
2. To change an attribute, enter its **Attribute** name and **Value**, then choose **Set attribute**. For example, use `style` and `color:#73d7bd;` to set text color. **Remove attribute** removes the named attribute.
3. For a leaf containing text, edit **Text** and choose **Set text**. Containers with child widgets or comments protect their contents from this replacement.
4. Select a container, choose **New child**, then **Add child** to append a div, span, paragraph, button, input, image or line break.
5. Select a widget and use **Move up**, **Move down** or **Remove element** to change its position among siblings or remove it.

Each successful operation is one source Undo step. The outline is derived again
after an edit; most operations clear the selection. Duplication selects the new
root, and inline style edits keep the edited element selected. Select an element
again after Undo or Redo.
Element positions describe the current source, rather than permanent widget IDs.
Use an `id` attribute to name a widget for styles and bindings.

Text and attribute values escape markup characters such as `<` and `&`. RML
bindings keep their source meaning: `Tick {{tick}}` displays the runtime counter.
Use **Source** to write child markup, resource declarations or other RML syntax.
The Elements controls protect document structure and style/resource declarations.
Use **Preview** for native rendering and the supported pixel layout controls.

## Edit an inline style property

1. Select one widget in **Elements** and expand **Inline style**.
2. Choose a property from **Common**, or enter its **Property** name, such as `color`, `width` or `font-size`.
3. Choose **Read inline** to load its authored value. If the property is absent, the field is blank. It does not show the computed value from stylesheets or inheritance.
4. Enter a **CSS value**, such as `#77bdf7`, `24px` or `50%`, and choose **Set property**. The same widget stays selected for your next property edit.
5. To remove that declaration, choose **Remove property**. Stylesheets, inheritance and bindings then determine its appearance.
6. Each source change is one **Undo** step. Open **Preview** to check native styling, then **Save source** and use Play to check gameplay bindings.

![Inline style controls with an authored color value and property set/remove buttons](../images/ui-source-inline-style.png)

The selected paragraph has a `color` override. These fields edit authored source,
not a computed runtime value. Existing model/event bindings remain intact and can
override the authored value during Play. Other declarations and surrounding
comments keep their original bytes. Removing the only declaration keeps the
`style` attribute and any surrounding comments. An identical value or absent-property removal
creates no source Undo step.

Use a lowercase property name beginning with a letter, up to 64 letters, digits,
`_` or `-`. Values are limited to 8,192 UTF-8 bytes and the whole opening tag must
still fit its source limit. Use one quoted inline style with plain property names.
Duplicate matching declarations, entity-encoded styles, extra declarations in a
value, markup and conflicts with the attribute's quote character use **Source**.
For example, use CSS single quotes inside a double-quoted attribute. Source
admission checks structure and resource limits; Preview and Play check native
CSS behavior. These controls do not provide a complete theme or responsive designer.

## Duplicate a static control

1. In **Elements**, select a static control or container.
2. Leave **ID prefix** empty for an automatic name, or enter a name such as `settings`. Use a lowercase letter first, followed by lowercase letters, digits, `_` or `-`, up to 64 characters.
3. Choose **Duplicate subtree**. The copy appears immediately after the original and becomes selected. Its source remains unsaved.
4. Adjust the copy's text, attributes and layout. A copied absolute-positioned panel initially overlaps the original, so use **Preview** or Source to move it.
5. Use **Undo** to remove the entire copy, or **Save source** when ready.

The copy includes the selected element and its children. Existing source and IDs
stay unchanged. A prefix of `settings` changes copied `id="panel"` to
`id="settings-panel"`; a root without an ID receives `id="settings"`. With no
prefix, FORGE tries `copy`, then `copy2` through `copy64`. Choose another prefix
if those names collide. Copied IDs must fit within 128 UTF-8 bytes.

A copied label whose `for` target is inside the subtree points to the copied
control. External label targets, class names and stylesheet selectors remain
unchanged. Review any ID-based styling and project-specific references yourself.
Use Source for bound or event-driven controls, text expressions, script/template
declarations, fragment links and document/resource structure. Duplication rejects
ambiguous or duplicate IDs, unsupported references and a result exceeding the
source limits; it leaves the draft unchanged.

## Remove several siblings

In **Elements**, hold **Ctrl** and select up to 32 elements with the same source
parent, then choose **Remove selection**. In Preview, a selected group also offers
**Remove group**. Removal uses source ranges and does not require an editable pixel
layout. Protected document/resource elements, mixed parents or nested selections
reject the whole operation. One **Undo** restores every removed subtree. Save
remains explicit.

## Move controls into a container

1. In **Elements**, select one static control or use **Ctrl** to select up to 32 siblings.
2. Expand **Move to container** and choose **New parent**. Containers appear in source order with indentation; giving them unique IDs makes them easier to identify.
3. Set **Placement** to **At end**, or choose **Before** one of that container's existing children.
4. Choose **Move selection**. All selected subtrees move together in their original source order, regardless of the order you selected them.
5. Use **Undo** or **Redo** to restore the whole move. Select the controls again for another structured edit, then **Save source** when ready.

![Two source siblings selected for movement into the target container before its anchor child](../images/ui-source-hierarchy.png)

The move retains each subtree's markup and IDs. Comments inside a moved subtree
travel with it; comments and text between selected siblings stay at the original
location. Styles, inherited properties and parent-relative positions follow the
new hierarchy. FORGE does not rewrite selectors, references or pixel offsets to
preserve the old appearance. Preview and test the result before saving.

Choose an existing container with separate opening and closing tags. The body is
also a valid destination. The chooser excludes selected elements and their
descendants. Invalid placement, mixed or nested selections, protected declarations,
bound/event-driven scopes, expressions, fragment links and relative native label
targets reject the entire move. Named IDs throughout the document must be unique,
literal and at most 128 UTF-8 bytes. Anonymous controls can still move. The full
result must satisfy the source nesting limits. Use Source for unsupported cases.
If the resulting source is identical, its revision and history stay unchanged.
Hierarchy editing does not require the pixel-canvas layout subset and
does not change the Scene hierarchy.

You can also drag a selected row onto a container in the Elements outline.
Dragging a selected group member carries the whole group. A click on an unselected
row selects it alone; **Ctrl** toggles membership. Hold **Shift** while dropping to
place the selection before that row instead of inside it. The same static-source
and placement limits apply. Source does not change during hover; one drop creates
one Undo step. **Escape**, application/window focus loss, a task lock, a source
change or switching views cancels or invalidates the drag. Finish or cancel a drag
before Save or source Undo/Redo. The explicit **New parent / Placement** controls
remain available when you need a precise destination.

## Preview and adjust layout

For a ready-to-edit example, open **Scenes/211-ui-layout-preview.scene.json** in
the Feature Gallery. In Content, choose **Edit UI source** on
**Assets/UI/Layout/layout.rml**. Its blue panel already has a unique ID and an
inline pixel layout. Its teal and purple siblings provide a group-editing example.

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
sizes and responsive rules remain available through Inline style or Source. The source-authoring
example's main panel uses responsive constraints, so edit that panel through those controls
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

## Move, align and space a group

1. Open Gallery scene **211** and its **layout.rml** source, then select **Preview**.
2. Click the blue panel. Hold **Ctrl** and click the teal and purple panels to add them. Ctrl+click a selected panel removes it. The same modifier works in **Elements**. Select between 2 and 32 elements with the same source parent.
3. Drag any selected member to move the group while preserving sizes and spacing. Release for one Undo step, or press **Escape** to cancel. Resize handles are available when only one element is selected.
4. Choose an **Operation** in the group controls, then **Apply group**. Edge and center alignment use the visible native border boxes, including padding and borders.
5. For **Move group**, enter **Delta X / Y (px)**. Distribution needs at least three elements and spaces them in their native left-to-right or top-to-bottom order, regardless of selection order. Move the outer elements farther apart if there is insufficient room for non-overlapping gaps.
6. Use **Undo** or **Redo** to restore the whole operation, then **Save source** when ready.

![Three sibling panels selected with group layout and Remove group controls](../images/ui-source-group-layout.png)

Every member must satisfy the canvas limits above. One unsupported, stale,
out-of-range or differently parented member rejects the whole operation. No
source changes during a drag; cancellation keeps the original source. Single
selection retains its numeric layout fields and resize handle. Groups do not
support resizing, flow/relative units or reparenting.

## Save a preview image

1. Open **Preview** and check that the current draft has rendered successfully.
2. Choose **Save preview image** and select an output filename. PNG preserves the display pixels; lossless WebP and opaque JPEG are also supported.
3. For JPEG, choose the quality and confirm **Save JPEG**. Confirm replacement when asked.
4. Wait for the image task to report that the image was saved.

The file contains the rendered game UI, without editor controls, selection outlines
or resize handles. Saving an image does not save the source or change the Scene.
The draft and preview must still match when the native image copy starts. Changes
before that point cancel or fail the request; a queued copy finishes independently
of later editing or source closure. Finish the current image task before saving
another image. Stale previews and hosts without a rendered image cannot be saved.

## Edit stylesheet rules

1. Open a `.rcss` file from **Content** using **Edit UI source**.
2. Choose **Rules**. Select a flat rule in **Stylesheet rules**; its selector appears in **Selector**.
3. Expand **Rule properties**. Choose a **Common** property or enter its **Property** name. **Read property** loads that rule's authored value; absence leaves the field blank.
4. Enter a **CSS value** and choose **Set property**, or choose **Remove property**. The selected rule stays selected after a property change.
5. To rename the selected rule's selector, edit **Selector** and choose **Set selector**. To append an empty rule, enter a selector and choose **Add rule**. **Remove rule** removes the selected flat block.
6. Each source change is one **Undo** step. Choose **Save source**, reopen the RML document and **Refresh preview**, or use **Reload UI** during Play.

![Stylesheet Rules with the accent selector, authored color and rule/property controls](../images/ui-source-rules.png)

The selected flat rule has a color override. Other blocks remain listed in source
order; selecting a nested block or at-rule keeps it in Source.

Rules edit the stylesheet source; they do not show computed CSS. Order, specificity,
inline styles and bindings affect the result. Other rules, declarations and
surrounding comments retain their bytes. Identical source results create no Undo
step. After Undo/Redo or a structural source edit, select the rule again.

At-rules, nested blocks and incomplete source stay in **Source**. The outline
supports up to 4,096 top-level blocks. New selectors fit 1,024 UTF-8 bytes and do
not contain at-rules, comments, escapes or additional blocks. Child selectors such
as `.panel > button` are supported. Property names use
the same lowercase 64-byte profile as inline styles; new values fit 8,192 bytes.
Stylesheet values can use either CSS quote form because there is no surrounding
markup attribute. Ambiguous matching declarations use Source. An existing value
too long for the control disables its property buttons; use Source to edit it.

## Edit the source and styles

![UI Source text view with source editing and Save controls](../images/ui-source-text.png)

Use Source for RML text and RCSS styles. All views share the same draft and history.

Open **hud.rcss** from Content to change the example's colors, spacing or sizes.
Only one UI source is open at a time. Switching to another file asks you to save,
discard or keep editing an unsaved draft. RCSS offers Rules in place of the RML
element outline; Source remains available for complete stylesheet editing.

Incomplete source may remain in the text draft. The Elements or Rules view explains why
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
