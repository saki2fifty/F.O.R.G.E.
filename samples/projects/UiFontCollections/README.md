# UI Font Collections

Open this project in FORGE, open **Scenes/FontCollections.scene.json**, then
start **Play** and select **Game**. A Camera is already included.

Two styles select different faces from **Assets/Fonts/Menu.ttc**: face **0** is
Lato and face **1** is Cairo. Compare the two Latin samples, the Arabic run and
the text field. The saved **Shape UI text** option enables script joining;
**Edit whole UI characters** enables bounded plain-key character editing.

In Content, open **Assets/UI/hud.rcss**. Each `@font-face` names the same file,
provides a family alias and uses `-rmlui-face-index` to select a zero-based face.
Change the source, choose **Save source**, stop and restart Play. Runtime field
edits do not save to source. Existing scoped MCP UI field tools can exercise the
same input through its native owner.

Save source, Scene and Project Settings before export. Use the runtime kit
packaged with this editor; collection fonts require matching kit support.
The collection travels through ordinary UI resource closure, so the exported
game does not need system-installed fonts. See
[Shape game UI text](../../../manual/editor/ui-text-shaping.md) for selection,
limits and troubleshooting.

Menu.ttc is a collection container built from the included stack's Poppins and Cairo
font data. The fonts retain their original ownership. Their complete notices
are in **LICENSE-Poppins.txt** and **LICENSE-Cairo.txt**. Keep required notices with
redistributions. Variable axes, named variation instances, color glyphs, full
paragraph bidirectional layout and complete IME/accessibility are outside this
example's supported profile.
