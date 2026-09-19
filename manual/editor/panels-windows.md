# Panels and windows

FORGE uses dockable panels. Drag a tab to move it or dock beside another panel; drag a divider to resize the split.

## Default workspace

- **Hierarchy**, left: scene entities and structural organization.
- **Scene / Game**, center tabs: authored scene versus isolated runtime presentation.
- **Inspector**, right: the selected entity, asset or prefab-member context.
- **Content / Problems / Console / Gameplay Code**, bottom tabs: assets, actionable issues, chronological messages and native compilation.
- The permanent one-row bottom status bar shows FPS, frame time, editor CPU/RAM, VSync state, runtime state, entity/selection counts, problem count and build activity. At narrow widths, **Details** exposes telemetry that does not fit.

Application menus sit above the global action bar. The bar provides Save, Undo/Redo and Play/Pause/Step/Stop. the **More** (three-dot) button retains access to actions hidden at narrow widths. Narrow windows or large UI scales group application menus under **Menu**.

## Recover a panel or layout

Use **Window** to show a closed panel. **Window → Reset layout** restores the default arrangement. Panel visibility and docking are saved in personal preferences, separately from project data.

Existing custom layouts are preserved. Scene tabs include filename and dirty marker; the stable docking identity survives filename changes. Prefab source initially joins the central document workspace; saved custom placement remains intact. Reset layout is deliberate; it does not happen merely because you upgraded.

At crowded sizes, widen Inspector, tab it with another panel, hide panels through Window, or use Ctrl+Minus. Ctrl+Plus increases readability and Ctrl+0 restores 100%.

## Window size and scale

Use 1440×900 or larger at 100% for the full default workspace. At 150–200%, a 1920×1080 or larger window gives more room; widen or tab the Inspector as needed. A 960×640 window is supported for reduced tasks at 100%. At 200% in that small window, use Menu/More, hide or tab panels, or reduce zoom: the complete workspace cannot fit at once. Docking remains under your control.

Fresh/reset layouts show Content in the bottom workspace when there is sufficient
height. At crowded sizes the bottom workspace starts folded. **Workspace +** in
the status bar, **Window → Expand bottom workspace**, or **Ctrl+Space** restores it.
**Workspace -** folds Content, Problems, Console and Gameplay Code together without
changing which of those panels you have chosen to show. The folded state is saved
in personal preferences. Existing saved layouts keep their active tabs.

Ctrl+Space is suspended while typing, manipulating, using a popup or capturing
Game input. The Window menu and status button remain discoverable alternatives.

## Independent tasks

**Prefab source** opens from Content or an instance's Inspector, independently of whether Content remains visible. **Tools → Project Settings** opens project configuration. Both retain unpublished drafts and guard close/switch with Save or Publish, Discard, and Cancel.

The action bar's **Save** indicator identifies which task Ctrl+S saves. Scene Undo/Redo is disabled while an independent draft owns the active task. There is no cross-document Undo.

## Advanced tools

Tools contains [Command palette](commands.md), [Scene diagnostics / Component schema](diagnostics.md), Performance, Project Settings and [Local automation](live-automation.md). Ordinary authoring does not require an automation connection.

## Layout file problems

If migration cannot replace a locked or inaccessible workspace file, FORGE preserves the original, reports the path, uses the readable layout where possible and disables layout saving for that session. Restart after resolving the file issue. Scene/project files are separate.

The Scene tool row uses labeled-by-tooltip vector icons: Add, Select, Move, Rotate, Scale, Snap and View. The active manipulation tool is highlighted; Snap is an independent toggle. Tools wrap at crowded sizes rather than disappearing.
