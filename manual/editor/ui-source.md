# Edit a game's UI source

Use **UI Source** to edit a game's RML document or RCSS stylesheet inside FORGE.
RML describes the widgets and their bindings; RCSS controls their appearance and
layout. The **Elements** view edits the saved document structure through the same
source draft. View the running result in **Game**.

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
They do not provide a drag-and-drop layout canvas or a live preview.

## Edit the source and styles

![UI Source text view with source editing and Save controls](../images/ui-source-text.png)

Use Source for RML text and RCSS styles; both views share one draft and history.

Open **hud.rcss** from Content to change the example's colors, spacing or sizes.
Only one UI source is open at a time. Switching to another file asks you to save,
discard or keep editing an unsaved draft. RCSS has no element outline.

Incomplete markup may remain in the text draft. The Elements view explains why
it cannot inspect that draft, and Save rejects invalid source. Correct the source
before saving. Current admission limits include 256 KiB of source, 8,192 tags and
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
