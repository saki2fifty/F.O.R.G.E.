# Editor placement and extension guidelines

This is the standing policy for FORGE editor additions. Place a control by **what it affects**, not by the subsystem that implements it. Describe implemented features in the user manual; this document also defines future direction.

## The shell

Hierarchy is scene structure; the center is the primary document workspace; Inspector edits the selected target; Content finds assets; Problems summarizes actionable issues; Console keeps chronological detail; Gameplay Code owns build/reload work. Preserve docking, personal layouts, and the compact status bar. A subsystem does not earn a permanent default panel merely by existing.

| Scope | Home |
| --- | --- |
| Whole editor/project: files, Save, history, runtime control | Main menu/global toolbar; infrequent configuration in on-demand windows |
| Scene entities: create, rename, organize, reparent, duplicate, delete | Hierarchy, Entity menu and contextual actions |
| Primary content editing | Central workspace document |
| Selected entity/asset/member/document item | Inspector |
| Asset discovery, type/folder search, supported create/register/convert | Content |
| Actionable errors/warnings with location | Problems; local errors also beside their fields |
| Detailed logs/compiler/runtime output | Console or Gameplay Code |
| Transform, snap, camera, grid, overlays and scene editing modes | Viewport-local tools |
| Performance, extensions, project settings, packaging | On-demand tool/task window |

## Entities, components, assets and tools

An **entity** is an instance in a world. A **component** is data/behavior attached to it. An **asset** is reusable project content with AssetId. A **tool/document editor** is the UI for editing or analyzing content. A graph node is not made a Flecs entity just to satisfy editor selection. Flecs remains the authority for gameplay entities/components/relationships/reflection/prefabs; editor selection, document focus, layout and transient previews belong to editor owners.

Expose useful terms such as Entity, Component, Parent, Prefab and Override. Put schema IDs, generations, module ownership and SDK details in collapsed Details or diagnostics. Do not introduce GameObject/Actor classes beside Flecs.

## Central documents, inspection and history

`DocumentWorkspace` is an internal borrowed-adapter registry. It associates a transient editor key with a title, stable ImGui window identity, central docking policy, open/dirty queries, draw ownership, Save, history capabilities, close request and optional local inspection callback. Current adapters cover Scene, prefab source, Project Settings and Flecs Script. Scene/Game rendering remains explicit because it owns render/input resources; registration is not a second renderer or simulation. Prefab source initially docks centrally; saved custom geometry remains authoritative.

`AssetEditors` maps supported asset types to open callbacks. Content double-click/context/Inspector opening uses this registry. Scenes, prefabs and Flecs Script currently have document-open handlers. Do not register nonexistent Material/Graph/UI designers or advertise unsupported asset editors.

Future documents register their own draft owner, validation/publication, close guard and lifecycle. They must participate in project switch/shutdown guards before becoming enabled; the adapter alone is not a generic cross-document transaction framework. ImGui persists dock geometry; each owner defines open/resume policy. Registry callbacks must be removed before their owners die. This is private C++ organization, not a frozen binary plugin ABI.

`EditorSelection` supports a transient document key plus a document-local opaque selection key, separate from entity and asset selection. The registered Inspector callback resolves that key in the owning document; stale/closed targets clear cleanly. Do not persist these keys as EntityId, AssetId or a new DocumentId. Future subelement identities and undo records need their domain contracts first.

Save and Undo/Redo dispatch to the active task's declared capabilities. Current prefab/settings drafts intentionally have no Undo/Redo capability. Scene history remains scene history; source publication is not undone by scene Undo. File/project/quit guards continue to resolve the existing independent drafts before the scene. Never imply cross-document atomicity.

## One action, many entry points

A logical action must share execution and availability across menu, toolbar, shortcut, context menu and Command Palette. `EditorActions` is the presentation adapter; mutations call UI-independent authoring operations or existing application/process controllers. Palette is optional power-user search, never the only discoverable entry point. Friendly command categories and shortcuts are searchable; disabled actions explain their context requirements.

Entity creation uses one `entity_recipes()` catalog and the `entity.create` command with a recipe ID. Scene Add, Hierarchy Add/context, Entity > Create and palette use it. A recipe composes an entity and schema-default components, not a new object class. Validation happens on the existing candidate draft and commits one scene history entry. New component defaults remain reflected-schema-owned. Creation explicitly selects the new entity and focuses the Scene task even when an asset/draft was previously selected. Placement is either World Origin or At View Target, shared and persisted. No implicit parenting or hidden resource creation.

## Viewport interaction ownership

Reuse `SceneTools`, `ModalTransform` and their existing move/modal exclusion. Select/Move is the resting interaction; Rotate/Scale owns the existing modal gesture until commit/cancel. Only the active owner manipulates; camera and orientation handlers honor gesture capture. Toolbar selected state shows that owner. Existing Q/W, R/S then XYZ, Escape/Enter, MMB/RMB/Shift navigation remain unchanged. New terrain/paint/spline tools must join this ownership boundary, not install independent conflicting mouse handlers. Do not invent a second tool system or silently replace established bindings.

## Property grammar

Use shared reflected property drawers: label then value, exact numeric type, units in the label/help, named enum values, validation near the field, disabled help and consistent ownership/Revert. Stack label/value when a narrow panel cannot fit a useful field width. XYZ transformations retain independent local-channel intent and one history entry per drag. Transform utilities belong in the section overflow. **Spatial binding** means Follow parent, World or Explicit attachment, not Local/World edit coordinates.

AssetRef fields share current value, searchable compatible picker, clear, missing state, Content reveal and typed drag/drop. EntityRef uses its persistent scene/entity semantics. Future Open affordances use the same asset-editor registry. Custom drawers are allowed for real domain needs, not as a default escape from shared property behavior. Add Component groups actual registered schemas; do not maintain another registration list. Attached-component filtering appears when useful.

## Density and visual language

Retain slate/blue, readable proportional Lato and monospace logs. Prefer alignment, compact headers, secondary text and spacing over oversized headings. Menu/global/viewport/status heights must be measured at 100% and tested through 200%. Every permanent row must justify its space. Stable scene identity and dirty state belong in the Scene tab, not a repeated AUTHORING row. Preferences belongs under Edit.

Use FORGE-owned vector icons with consistent stroke, size, selected state and delayed tooltips containing name/shortcut/purpose. Distinguish exclusive primary tools, toggles and menus. Retain normal menu access for icon actions. Narrow toolbars wrap at complete control boundaries; status details overflow into a popup, not more permanent rows. No silent disappearance of transform tools. High zoom in small windows still requires panel/zoom adjustment; do not claim all panels fit at every size.

## Blockout versus future rendering

Current: `Primitive` shape plus optional opaque Tint; CPU procedural triangles are shared by preview, bounds, picking and navigation. Persisted kinds 0–3 retain their original meaning. New kinds are append-only; **None** is explicit to preserve legacy implicit-Cube behavior when Primitive is absent. Empty/nonvisual recipes have local TRS and explicit None. This is a temporary blockout representation, not general rendering. Older builds reject unsupported new kinds; clients must rediscover schema on upgrade. No automatic authored migration occurs.

Future: a Rendering component references a built-in or imported **Mesh asset** and a **Material asset**. Creating a cube then composes that same model used for characters and imported props. Shape generation does not create a separate renderer-facing type hierarchy. Material/color, topology editing, import settings, UV/tangents, resource lifetime and migration belong to the future asset/rendering work. This pass does not implement that pipeline.

## Future editor placement matrix

These are placement requirements, **not implemented features**.

| Feature | Content / primary surface | Entity/Inspector or supporting tool |
| --- | --- | --- |
| Native/text scripting | C++ project source/external IDE; Flecs Script central source document | Gameplay Code builds/reloads; future behavior attachment |
| Visual scripting | Script Graph asset → central graph editor | Entity behavior reference |
| Materials | Material asset → central Material Editor | Rendering material reference |
| Shaders | Shader asset → central source/graph editor | Material/shader references; diagnostics in Problems |
| Animation | Skeleton/clip → central Animation Editor | Animator and references |
| VFX/particles | VFX asset → central VFX Editor | Emitter recipe/component once implemented |
| Runtime UI | UI document → central UI Designer | UiDocument; distinct from editor ImGui |
| AI/behavior trees | Behavior asset → central graph editor | AI component reference |
| State machines/blend trees | Domain asset → its central editor | Runtime component/reference, not permanent panel |
| Audio mixer | Mixer asset/document → central audio tool | Source/listener Inspector; on-demand monitoring |
| Navigation | Navigation assets in Content | Surface/agent Inspector; Scene overlay/edit mode |
| Physics debugging | On-demand diagnostic tool | Scene/Game overlays; body/collider Inspector |
| Terrain | Terrain assets in Content | Entity/component and Scene sculpt/paint mode |
| Splines | Path resources as domain requires | Entity/component and Scene editing tool |
| 2D/tilemaps | Tileset assets; central Scene/document mode | Relevant entity/component properties |
| Timelines/cinematics | Timeline asset → central sequencer | Target references and contextual Inspector |
| Localization | Tables/assets → central/project tool | Localized references; no global Inspector clutter |
| Input | Project Settings | Runtime input consumers |
| Plugins/extensions | Tools → management window | Future commands/drawers/editors/tools/panels, trusted restart-bound native code |
| Build/package | Run/Build menu → task window | Shared diagnostics and progress |
| Profiler | On-demand performance tool | Compact status telemetry |
| Lighting/environment | Scene/project settings for global state | Local light entities; on-demand bake tools |
| Meshes/models | Mesh/model asset → central preview/editor | Rendering mesh reference |
| Textures | Texture asset → central preview/editor if needed | Typed property assignment |
| Prefabs | Prefab asset → source document | Instance overrides/Revert in Inspector |
| Networking | Future project/runtime tools | Contextual diagnostics; no permanent default panel |
| Version control | Optional tools/status integration | No permanent default panel |

## Reference evidence

Only Unity, Unreal and Godot inform game-editor conventions in this pass. FORGE keeps its own commands, ECS model and keybindings.

- [Unreal viewport toolbar](https://dev.epicgames.com/documentation/en-us/unreal-engine/viewport-toolbar): task groups and viewport-local tools/overflow.
- [Unreal Content Browser](https://dev.epicgames.com/documentation/en-us/unreal-engine/content-browser-in-unreal-engine): assets have a shared discovery home.
- [Godot Inspector](https://docs.godotengine.org/en/stable/tutorials/editor/inspector_dock.html): contextual property inspection and resource editing.
- [Unity positioning tools](https://docs.unity.com/en-us/engine/6000.7/manual/working-with-scenes/scenes-manage-gameobjects/positioning-game-objects): transform modes have toolbar and keyboard access. FORGE preserves its own R/S bindings.

Implementation APIs are checked against the exact pinned upstream sources listed in [Editor UI](editor-ui.md); reference-editor conventions do not authorize dependency or authority changes.
