# Shape game UI text

Enable text shaping when your game needs supported script joining, ligatures or
combining marks. Use a project font containing the characters you want to show.
The setting applies to the game's UI documents throughout the project.

![Game UI with Latin ligatures, combining accents and an Arabic text run](../images/ui-text-shaping-game.png)

The example uses separate Latin and Arabic runs, a project font, native font
effects and a local text field.

## Try the example

1. Open the included **Examples/UiTextShaping** project with **File → Open project...**.
2. Open **Scenes/TextShaping.scene.json**, then start **Play** and select **Game**.
3. Compare the normal and tracked Latin samples. Tracking separates the Latin ligatures. The Arabic sample uses an explicit right-to-left run.
4. Click the text field to enter text. **Pause** and **Resume** use the normal gameplay controls; the field's contents stay local to the running UI.
5. Stop Play. In **Content**, open **Assets/UI/hud.rml** or **hud.rcss** to edit the text and styles. Choose **Save source**, then start Play again.

The example includes a Camera, so it can run in Game without scene setup. Its
font notice is included in the project. See [Edit UI source](ui-source.md) for
source history, conflicts and registration.

## Enable shaping in your project

1. Stop Play and open **Tools → Project Settings**.
2. If game defaults are absent, choose **Set up game defaults**.
3. Expand **Game defaults** and enable **Shape UI text**.
4. Choose **Save Settings**, then start Play again.

Existing projects keep the original text rendering until you enable this option.
Saving changes project settings, independently of Scene Undo. Restart Play after
changing the option or replacing a font family.

## Choose a font and run direction

Put a suitable `.ttf` or `.otf` font inside your project. Font `src` paths are
relative to the project root. For example:

```css
@font-face { font-family: MenuArabic; src: "Assets/Fonts/menu.ttf"; }
#greeting {
    font-family: MenuArabic;
    font-size: 32px;
    -rmlui-language: ar;
    -rmlui-direction: rtl;
}
```

```xml
<p id="greeting">سلام بالعالم</p>
```

Use `ltr` for a left-to-right run or leave direction automatic when appropriate.
Keep runs needing different directions in separate text elements. Language and
direction affect shaping; they do not translate your text. Use a
[UI Localization catalog](ui-localization.md) for translated messages.

`letter-spacing` controls tracking. Nonzero tracking disables supported Latin
ligatures; combining marks stay attached to their native cluster. Existing
shadows and outlines work with shaped glyphs. Separate multiple `font-effect`
values with commas.

## Save and export

Save the UI source, Scene and Project Settings. After a successful Play, use
**Register / Refresh** to publish the UI resource metadata when needed. Choose
the saved Scene as startup and export using the runtime kit packaged with the
matching editor. An older kit without shaping support is rejected. Exported
games include the selected project font through the existing UI resource closure;
players do not need that font installed.

Keep the font's required redistribution notices with your game. The included
example's Cairo font notice also travels in the matching runtime resources.
See [Export a game](runtime-content.md) for the normal export workflow.

## Limits and troubleshooting

- Missing glyph boxes usually mean the selected font lacks those characters. Assign a font containing the script. Fallback chooses a face covering a whole run; arbitrary mixed-script fallback is unavailable.
- Use horizontal text and font sizes from **1–512 px**. A single run supports **64 KiB** of UTF-8 and **16,384 glyphs**, within the normal document limits.
- Font collections, variable axes, color glyphs and vertical text are unsupported.
- Shaping does not provide full paragraph bidirectional layout, joining across separate elements, grapheme-aware selection/deletion, complete international input methods or accessibility. Check text entry on your target systems.
- A failed reload keeps the last usable HUD and reports a diagnostic. If a font or glyph cache limit is reached, simplify the UI or stop and restart Play.
