# Panels and windows

FORGE uses dockable panels so you can arrange the workspace around your task. Drag a panel tab to dock it beside another panel or group it into a tabbed area. Resize a split by dragging its divider.

## Find the right panel

- **Hierarchy** lists scene entities and their parent/child organization.
- **Inspector** edits the Hierarchy selection.
- **Scene** shows the block preview and camera controls.
- **Content** browses project scene files and opens them through the save guard.
- **Gameplay Code** creates and compiles gameplay source, with compiler output and expandable Compiler setup.
- **Console** displays editor, file, and runtime diagnostics.

The top toolbar holds file, help, edit, play, and tooltip controls. The bottom performance bar remains visible below the docking area.

## Keep a useful layout

Your panel arrangement is saved between editor sessions. The current layout is global to the editor, rather than separately stored for each project. Ctrl+Minus makes controls smaller if a crowded panel needs more space; Ctrl+Plus increases readability.

Tooltips explain controls when you pause the pointer over them. See [Settings and appearance](settings.md) and [Performance and diagnostics](performance.md).

## Inspection tools

The Tools menu opens the [Command palette](commands.md), [Scene diagnostics and Component schema](diagnostics.md). Diagnostics lists informational scene findings; Component schema describes the supported built-in data.

## Local automation

Open **Tools → Automation → Local connection...** to start a read-only or editable connection, copy connection details, inspect its status, or stop it. See [Live automation](live-automation.md) for the included Python example.

## Show, hide, or restore panels

Use **Window** to show or hide a panel. You can also close a panel with its tab's close button. **Window → Reset layout** restores Hierarchy on the left, Scene in the center, Inspector on the right, and Content / Console / Gameplay Code tabs below.

Existing custom layouts are retained when World becomes Hierarchy and Native becomes Gameplay Code. The previous layout file is backed up during that migration. Use Reset layout if you want the new default arrangement. Panel visibility is saved with your personal settings.

Scene controls now fit in a compact toolbar. Its **View** menu holds framing, camera bookmarks, grid settings, flight speed and orientation visibility. Scene name, unsaved-change marker and Edit/Play state appear above it. Content shows the active project; hover that name for its folder.
