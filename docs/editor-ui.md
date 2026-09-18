# Editor UI ownership and presentation

The editor uses the pinned Dear ImGui docking build and SDL3. Runtime libraries remain independent of ImGui. Lato regular (the already packaged SIL OFL 1.1 font) supplies proportional UI text; logs use ImGui's built-in bitmap monospace font. `resources/ui/LICENSE-Lato.txt` retains copyright, reserved-name and license terms. No font file is modified or sold separately.

## Authority

- Flecs/Scene owns entities, components, relationships, prefab realization and authored history.
- AssetCatalog owns persistent asset records. Content caches a catalog view and discovers identity-bearing scene files; browsing does not write another database or mint new scene identities.
- EditorSelection stores only transient None/Entity/Asset/PrefabMember selection. Selecting an asset clears entity selection. Dragging an asset delays click selection so an entity field remains a drop target.
- ProjectSettingsEditor and PrefabEditor each own one transient draft/baseline. ActiveTask selects the Save owner. Each owner validates before publication and retains failed drafts. Scene Undo does not claim to undo either asset publication or project settings.
- EditorActions adapts existing authoring commands and process controllers to menu, toolbar, shortcut, palette and context routes. Availability and execution are shared for the repeated actions. File operations retain their existing EditorFiles controller and unsaved-scene guard.
- ComponentInspector and property_drawer consume registered schema metadata. Friendly labels/categories/enum choices are additive metadata; persistent formats, identity and ABI1 do not change. There is no second authoritative component registration table.
- Problems and Console retain bounded session diagnostics/status transitions. They are presentation history, not project state or a persistent telemetry service.

## Scene and Game

Scene always renders the authored world, including existing transient transform previews. Game renders snapshots from the existing single isolated runtime and hosts the approved RmlUi presenter. Each has a separate viewport render target/cache and editor camera; starting Play seeds the Game camera from Scene. This does not introduce a runtime renderer process or final game-camera system. Runtime UI stays out of the authoring world.

Game input capture releases on Escape, focus loss, hidden Game, or an outside mouse press. The outside press continues into the editor. F6/F7 remain reserved runtime clock controls. Editor zoom remains available independently. A hidden Game tab must not cancel Scene gestures.

## Draft/history boundaries

Ctrl+S routes to the active Scene, Prefab source or Project Settings task. Scene Undo/Redo is unavailable while a separate draft task owns focus. File/project switch and application close resolve independent dirty drafts before invoking the existing dirty-scene guard. A failed candidate or save keeps its draft open. Publishing prefab source retains the existing reconciliation/history policy. No Apply to Prefab, cross-document transaction, new prefab format or automatic migration is introduced.

## Persistence

Personal panel visibility and ImGui docking geometry persist independently of project settings. Stable hidden IDs retain Hierarchy/Gameplay Code arrangements; existing Prefab source/Project Settings names migrate with docking references and a backup. New Game/Problems tabs attach on first use. Reset layout is explicit. Failed workspace replacement retains the original and disables session layout saving, as before.

## Validation boundaries

Portable tests exercise actual ImGui component/draft drawing, integer input, controller validation/history, process isolation and capture. `editor_redesign_render` builds a fixture variant of the actual editor entry point, attaches Diligent to D3D12 WARP, creates disposable project/preferences, drives real controllers and captures rendered windows. The fixture is not packaged as the editor. Evidence covers default/Scene/Inspector, Add Component, Content, Problems, prefab/settings, Play/paused capture, narrow 100%/200% and ultrawide layouts. It is automated rendering, not physical desktop acceptance. Existing shader, viewport, runtime UI and package relocation suites remain required.

## Official implementation references

- [Pinned Dear ImGui public API](https://github.com/ocornut/imgui/blob/b48d1afbe8ee8b238e2961dc363a949dd7304e23/imgui.h): typed InputScalar, fonts, docking configuration, drag/drop and table widgets.
- [Pinned Dear ImGui internals](https://github.com/ocornut/imgui/blob/b48d1afbe8ee8b238e2961dc363a949dd7304e23/imgui_internal.h): existing DockBuilder/viewport-sidebars and selection-on-release behavior. These internal uses require coordinated pin updates.
- [Pinned SDL3](https://github.com/libsdl-org/SDL/tree/fa2c02bb6e21974a89ea9824bc53c9932abe5f9c/include/SDL3): window, input and local-folder URL integration.
- [Pinned Diligent integration](https://github.com/DiligentGraphics/DiligentEngine/tree/a279e5fa8593cbc758ec46ea1eba0b435cbc2f06): separate offscreen targets and existing WARP device attachment/readback pattern.

Deferred: general import/cooking, multiselection, plugin-authored inspectors, cross-document Undo, Apply to Prefab, final game camera/rendering, asset thumbnails/file management and domain editors. This package does not begin Phase 7.
