# Settings and appearance

FORGE uses a slate-colored dockable interface. You can adjust its size and turn contextual help on or off without changing your scene.

## Scale the interface

Press Ctrl+Minus to make text and controls smaller. Press Ctrl+Plus or Ctrl+Equals to make them larger. Ctrl+0 restores 100%. Keypad equivalents are supported.

The supported range is 65–200%. Both text and widget geometry scale, and the value is saved between launches. This does not zoom the viewport camera.

## Control tooltips

Use **Tooltips** in the top Preferences menu to enable or disable contextual help globally. The setting persists between launches.

Pause the pointer over a control to show help after roughly 0.4 seconds. Long help wraps, and placement tries to keep the hovered control visible. Tooltips are suppressed during mouse-button gestures. Disabled controls can still explain why or how they are used.

## Persisted preferences

FORGE remembers interface scale, tooltip state, panel layout, recent/last projects, native tool/build-on-save settings, and the Create menu's At view target setting in its Windows user preferences area. Scene tool preferences also persist: move handles, grid, snap spacing, and flight speed. Use **Save view** in Scene to keep a per-scene camera bookmark. Undo history does not persist.

There is no theme picker or general settings dialog yet. See [Panels and windows](panels-windows.md).


Shared game behavior is configured separately under [Tools → Project Settings](project-settings.md). Layout, scale, tooltips and machine tool paths stay personal.

## Fonts and narrow layouts

The editor uses the packaged Lato font for proportional interface text and a monospace font for logs. Its SIL Open Font License is included with the package. Text and geometry scale together from 65% to 200%. At narrow widths application menus group under **Menu**, and **More...** keeps global actions accessible.
