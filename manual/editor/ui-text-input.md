# Edit whole UI characters

Enable **Edit whole UI characters** to move and delete a complete Unicode
character with plain arrow and deletion keys in game UI text fields. A character
can contain several code points, such as a letter with a combining accent or an
emoji sequence. This option works independently of [Shape UI text](ui-text-shaping.md).

![Game HUD containing a local editable text field](../images/ui-text-shaping-game.png)

The UiTextShaping example includes a text field below the Pause and Resume buttons.

## Enable the option

1. Stop Play and open **Tools → Project Settings**.
2. Choose **Set up game defaults** if game defaults are absent.
3. Expand **Game defaults** and enable **Edit whole UI characters**.
4. Choose **Save Settings**, then start Play and select **Game**.

Existing projects retain native editing until this option is enabled. Saving
settings is separate from Scene Undo. Restart Play after changing the option.
Text entered in the running HUD stays local to that UI; it does not save a new
RML value or modify the authored Scene.

## Try the included field

1. Open **Examples/UiTextShaping**, then **Scenes/TextShaping.scene.json**.
2. Start **Play**, select **Game**, and click the text field. Both text options are enabled in this project.
3. Select the field's text with **Ctrl+A** and paste `áb`. Here, the accent after `a` is a separate combining code point.
4. Press **End**, then **Backspace** to delete `b`. Press **Backspace** again: the accented `a` disappears together.
5. Paste the same text again. Press **Home**, then **Right**: the caret moves past the accented `a` in one step. Press **Left** to return before it.
6. From **Home**, press **Delete**: the accented `a` disappears and `b` remains.

Use a font containing the characters you test. Missing glyph boxes indicate font
coverage, even when an emoji sequence is edited as one character.

## Controls and limits

- **Plain Left / Right:** move by a whole character in logical text order.
- **Plain Backspace / Delete:** delete the adjacent whole character.
- **Partially selected character + a plain key above:** expand to its whole character boundary before the native action.
- **Shift, Ctrl, Alt or Meta modified keys:** existing native behavior.
- **Active IME composition:** existing platform/native behavior.

Text inputs, password inputs and textareas support the option. Native disabled
controls and ordinary maximum-length restrictions still apply. Pointer selection,
vertical movement, word navigation and modified selection retain native behavior;
their caret can be inside a multi-code-point character. The next plain key above
uses the complete character boundary.

The option covers fields up to **64 KiB of UTF-8** and **16,384 code points**.
Larger fields retain native editing without being truncated. Font rendering has
its own limits. This is not complete international text editing, paragraph
bidirectional layout, accessibility or a complete IME implementation. Test your
target keyboards, input languages and displays. The native `readonly` attribute
is unavailable; use supported disabled controls when editing must be blocked.

## Save and export

Save the UI source, Scene and Project Settings, and test Play before exporting.
Use the runtime kit packaged with the matching editor. A kit lacking the enabled
whole-character editing profile is rejected. Players need no compiler or
machine-installed font. See [Export a game](runtime-content.md).
