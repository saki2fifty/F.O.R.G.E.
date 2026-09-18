# Runtime UI

Runtime UI is the interface shown **inside your running game**: HUDs, menus, buttons and text fields. FORGE uses RmlUi for it. The editor's own panels still use Dear ImGui.

## Create your first HUD

1. Open a writable project. In **Content → Runtime UI**, click **Create HUD example**.
2. Create or select an entity in the World panel.
3. In **Inspector → Runtime UI**, click **Add UI Document**.
4. Use the **Document** asset picker to select the new `Assets/UI/HUD-…/hud.rml` asset. Keep **Enabled** and **Visible** checked.
5. Save, then press **Play**. The Scene viewport shows a styled panel, image, simulation tick, pause state, buttons and a text field.
6. Turn on **Capture gameplay input** in the Scene controls to interact with the HUD.

The example creates three files: `hud.rml` for content, `hud.rcss` for appearance, and `badge.tga` for the image. You do not need to enter an asset UUID or write C++.

## Try the buttons and text field

Click **Pause**. The tick counter stops, but the HUD stays interactive. Click the text field and type; gameplay keyboard actions should not fire while the field owns your input.

Click **Step** while paused: the counter advances by one. Click **Resume** to continue. **F6** pauses/resumes, **F7** steps while paused, and **Escape** releases game input so you can use the editor again.

UI capture clears held gameplay controls to prevent stuck movement. Release and press a movement key again when returning from a field to gameplay.

Resize the viewport and editor window. The HUD remains positioned within the game image. Ctrl+Plus/Minus changes the editor interface independently; it is not a game HUD zoom control.

## Use your own document

Put an `.rml` file and its resources inside the project. Enter its project-relative path in **Content → Runtime UI → RML in project**, then click **Register RML**. Assign it through the Document picker.

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

FORGE supplies the data model automatically; omit `data-model` attributes. `tick` and `paused` reflect gameplay state. Registered gameplay commands and additional values require gameplay code; the included pause/step example works without it. Events request changes from gameplay. A text field's local contents are transient and do not automatically change game state.

## Styles, fonts and images

Use document-relative local resource paths. The supplied **Lato** font is packaged with FORGE, so it does not depend on installed system fonts. You may load project `.ttf`/`.otf` fonts using an RCSS `@font-face` declaration, with suitable redistribution rights. Use explicit `font-family` and `font-size`; font shorthand is not supported in this integration. When replacing a font using the same family name, restart Play to clear its font cache.

Images currently use uncompressed true-color **TGA**, 24 or 32 bits, at most 2048×2048. PNG, JPEG, SVG, remote URLs, templates and stylesheet imports are not supported yet. Link multiple stylesheets individually. Filters, shader gradients, offscreen effects and shadows are also deferred.

`px` means game render-target pixels; `dp` follows display density. The editor's interface scale remains separate. Physical mixed-monitor DPI and IME behavior can vary and still need desktop testing.

Font `src` paths are relative to the **project root**, as a FORGE policy for RmlUi 6.3 font declarations. For example, place `game.ttf` at that root:

```css
@font-face { font-family: Game; src: "game.ttf"; }
body { font-family: Game; }
```

## Edit and reload

Edit the RML or RCSS file using your text editor, save it, then click **Reload UI** in the Scene controls during Play. FORGE prepares the replacement first. If it fails, the previous usable HUD remains visible and an error explains the failure.

Reloading clears hover/focus and local text-field contents. It does not restart gameplay. UI asset creation and file editing are outside scene Undo/Redo; use your text editor or version control for those files.

## Multiple documents and prefabs

Add UI Document to more than one entity to show multiple documents. **Layer** controls their starting draw order; larger values draw later. Equal layers follow scene order. RmlUi handles normal focus and element ordering within the UI context. Up to 16 documents can be active.

**Enabled** controls whether a document participates. **Visible** hides or shows it without removing the authored component. A hidden document cannot send gameplay commands.

UI Document works with [prefabs](prefabs.md). The document reference, visibility and layer can inherit from the source or be overridden. Property Revert follows the prefab again; scene Undo/Redo restores these edits. **Remove / Revert UI Document** removes local configuration/overrides. UI DOM and local input state are not saved into the prefab.

## When something does not appear

- Confirm Document points to a registered `.rml` asset and Enabled/Visible are checked.
- Read the Runtime UI message in the Scene controls. Check balanced tags, linked styles and resource paths.
- Keep resources inside the project; absolute machine paths and web URLs reject.
- Use the packaged Lato family first to distinguish font issues from layout issues.
- Large documents, deep nesting, too many files or oversized images/fonts reject with limits.
- After fixing a file, use Reload UI. If the first document could not load, stop and restart Play.

Stopping Play destroys the live UI. Starting again rebuilds it from the saved configuration and current runtime state. Recovery recreates the UI; unsaved text-field contents and hover state are not restored.

## Short acceptance test

Create the example, assign it, Play, capture input, Pause, type, Step once and Resume. Resize the viewport, press Escape, Stop, then save/reopen the scene. Finally create a prefab from the UI entity and try a visibility override and Revert. No C++ changes are needed.

See also [Play mode](play-mode.md), [Gameplay input](input.md), [Prefabs](prefabs.md), and [Content browser](content-browser.md).
