# Panels and windows

FORGE uses dockable panels. Drag a tab to move it or dock beside another panel; drag a divider to resize the split.

## Default workspace

- **Hierarchy**, left: scene entities and structural organization.
- **Scene / Game**, center tabs: authored scene versus isolated runtime presentation.
- **Inspector**, right: the selected entity, asset or prefab-member context.
- **Content / Problems / Console / Gameplay Code**, bottom tabs: assets, actionable issues, chronological messages and native compilation.
- The permanent bottom status bar shows FPS, frame time, editor CPU/RAM, VSync state, runtime state, entity/selection counts, problem count and build activity.

Application menus sit above the global action bar. The bar provides Save, Undo/Redo and Play/Pause/Step/Stop. **More...** retains access to actions hidden at narrow widths. Narrow windows or large UI scales group application menus under **Menu**.

## Recover a panel or layout

Use **Window** to show a closed panel. **Window → Reset layout** restores the default arrangement. Panel visibility and docking are saved in personal preferences, separately from project data.

Existing custom layouts are preserved. New Game and Problems panels join the existing Scene and Console docks on first use. Reset layout is deliberate; it does not happen merely because you upgraded.

At crowded sizes, widen Inspector, tab it with another panel, hide panels through Window, or use Ctrl+Minus. Ctrl+Plus increases readability and Ctrl+0 restores 100%.

## Independent tasks

**Prefab source** opens from Content or an instance's Inspector, independently of whether Content remains visible. **Tools → Project Settings** opens project configuration. Both retain unpublished drafts and guard close/switch with Save or Publish, Discard, and Cancel.

The action bar's **Active** label identifies which task Ctrl+S saves. Scene Undo/Redo is disabled while an independent draft owns the active task. There is no cross-document Undo.

## Advanced tools

Tools contains [Command palette](commands.md), [Scene diagnostics / Component schema](diagnostics.md), Performance, Project Settings and [Local automation](live-automation.md). Ordinary authoring does not require an automation connection.

## Layout file problems

If migration cannot replace a locked or inaccessible workspace file, FORGE preserves the original, reports the path, uses the readable layout where possible and disables layout saving for that session. Restart after resolving the file issue. Scene/project files are separate.
