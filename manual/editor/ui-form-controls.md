# Form controls

Use checkboxes for independent choices, radio buttons for one choice in a group,
dropdowns for a list of options, and progress for a read-only numeric display.
These are native controls in the running game HUD. The source editor creates and
styles them; use Play to interact with them.

## Try the Feature Gallery

1. Open the packaged **Examples/FeatureGallery** project.
2. Open `Scenes/212-ui-form-controls.scene.json` and press **Play**.
3. In **Game**, choose **Capture gameplay input**.
4. Click **Music enabled** to toggle its checkbox.
5. Choose **Expert**. The **Casual** radio clears because both belong to the `difficulty` group.
6. Open **Display quality** and choose **Detailed**. **Locked example** is disabled.
7. Inspect the progress bar: its authored fixed value is 35%. **Disabled control** cannot be toggled.
8. Press **Escape** to release input, stop Play, then start again. The authored defaults return.

![Native game HUD with a checked Music checkbox, difficulty radio buttons, a quality dropdown and a 35% progress bar](../images/ui-form-controls.png)

The Gallery HUD uses native controls and the bundled font. It needs no project
C++ module or Light. The Scene includes a Camera.

![Native Display quality dropdown showing Balanced, Detailed, Cinematic and a disabled Locked example](../images/ui-form-options.png)

Choose an enabled option from the native dropdown. Disabled options remain visible.

## Create and name controls

Open an RML document from Content with **Edit UI source**. In **Elements**, select
its parent, choose the required **New child** kind and use **Initial text/value**
where applicable. A nonempty dropdown starts with one selected option. Add more
**Dropdown option** children to that dropdown. Choose **Save source** before Play.
See [UI source](ui-source.md) for source selection, attributes, styling and Undo.

Give controls unique `id` attributes when labels or agents must address them.
A label's `for` attribute names its target ID. Give radios the same `name` to make
a group, distinct `value` attributes for its choices, and mark only the initial
choice `checked`. Checkboxes may also start `checked`. A dropdown's option uses
`value` for its data and text for its caption; mark its initial option `selected`.
Use `disabled` to block interaction with a control or option.

The Gallery's `Assets/UI/Forms/forms.rml` shows these attributes together.
`forms.rcss` styles borders, checked/focus/disabled states and the dropdown list.
Appearance is authored: controls do not acquire an editor theme automatically.
For progress, `value="0.35" max="1"` displays 35%. Change the source or connect
appropriate gameplay bindings to drive a display from your game.

## Test controls with an AI agent

Connect through [MCP](mcp.md), enable **Allow C++ build and execution**, then ask
the agent to start Play and inspect the HUD's native form controls. Read-only
connections can inspect state but cannot start Play or change controls.

The agent reads `forge_play_ui_controls` for the current document instance, unique
IDs, checked flags, selected value, progress and presentation revision. To change
a checkbox it calls `forge_play_ui_check` with the desired checked state. To choose
a radio it calls `forge_play_ui_choose`. For a dropdown it reads
`forge_play_ui_options`, then uses the selected row's index and state token with
`forge_play_ui_select`. A changed presentation or option requires a fresh read.

Already-satisfied actions do not focus or activate the control. Native events and
gameplay callbacks can change the result, so the agent reads the returned state.
Text/password/text-area fields have their own [field tools](ui-text-input.md).
Password plaintext is omitted from observations.

## Save, export and limits

Runtime choices are local to the running UI. They do not automatically save game
settings, edit RML, change Scene data or enter Scene Undo. Add your game's binding,
command or save behavior when choices must affect gameplay or persist. Progress
observation does not provide a progress-editing command.

Save UI resources, the Scene and Project Settings before exporting with the
matching runtime kit. Set this Scene as startup to export the Gallery example.
The exported game accepts normal player input; editor MCP tools do not attach to
that process. See [Export a game](runtime-content.md).

Agent actions require unique visible enabled targets, current Play and
presentation scope, and ordinary native focus/input routing. Camera-output input
also needs a current surface hit. Duplicate IDs, missing controls or incomplete
scans cannot authorize changes. Discovery is limited to 4,096 native nodes and
64 form controls. Dropdown pages contain at most 32 options; text/value reads are
bounded and can report unavailable data without truncating it. If a dropdown page
has no state token for an option, that option cannot be selected through MCP.
If a page is not ready, allow a native UI update and read again. Arbitrary DOM/script edits,
IME composition injection and general accessibility automation are unavailable.
