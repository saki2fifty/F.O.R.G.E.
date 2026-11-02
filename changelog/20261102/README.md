# 2026-11-02

## Navigation behavior trees

Navigation Behavior source version2 adds ordered Sequence, Reactive sequence and
Selector composites, timed Wait, point/follow actions and target conditions.
Existing state graphs and their cooked bytes retain version1 semantics and IDs.
The existing source document owns validation, stable node identity, Undo/Redo and
publication; the same editor canvas and automation/MCP source tools expose both
profiles. Draft forests remain editable; publication requires one connected tree.

Navigation Agent retains movement authority. Flecs cursor continuation handles
priority preemption, actual arrival/failure, fixed time, disable/resume and exact
private recovery. Evaluation has finite node/child/depth/work bounds. The expanded
runtime cursor and its value-support semantics require a matching exact SDK;
native C callbacks and dependency pins remain unchanged.

Both runtime hosts now use the shared unpublished-world resource preparation for
fresh authored scenes, including their time-zero animation sample. This keeps
animated-target decisions aligned before the first fixed tick. Recovery preserves
its validated restore order. Gallery139 supplies a ready chase/patrol tree using
the original target animation, ordinary source-free export and an illustrated
current-use guide. Physical Windows/full editor/graphical EXE review remains pending.

## Named navigation build profiles

A Scene can publish separately named Nav assets for different agent clearances.
Build profile selects saved settings/overlays; New profile starts an unpublished
draft. Agents retain ordinary typed Nav references. Content/pickers show profile
names first, and runtime overlay paths match the selected asset's plane.

A shared derived catalog lookup replaces first-Scene-record selection. The default
keeps legacy IDs/settings/cooked representation; rebuilding or switching plane
replaces only that profile's revision. Names validate before publication and
ambiguous duplicates reject. Automation/MCP status, select and build use the same
owner and permissions. Runtime query-cache, envelope and pinned dependencies remain unchanged.
Optional Shared group preserves default asset isolation and shares same-plane
avoidance between profiles using each peer's baked radius. The appended value
requires matching SDK/module rebuilds; C callbacks and legacy defaults remain. Gallery138 and current-use illustrated
manual instructions exercise distinct small/large clearance and ordinary export.
Physical Windows/full editor/graphical EXE review remains pending.

## Selected collision navigation sources

**Navigation Region 2D → Shape source → Collider 2D** selects a configured Box,
Circle, Capsule or published Tile Map collision layer. Static source/transform
rules, ordinary properties/history/prefabs and the existing navigation build owner
remain authoritative. Source0 retains rectangle/polygon geometry and identity.
Role-conservative native-rounded contours and separate Map islands feed bake,
guides, picking and framing. No physics world or body is created for baking.

Selected Map/Set/neighbor-rule inputs use existing resource owners. Geometry5 and
catalog5 record separate selected Tile revisions/dependency edges; dynamic layers
retain envelope4. Old navigation assets remain readable and rebuilds preserve
logical IDs/prior artifacts. Runtime-edited selected Map instances stop baked-route
movement until Reset. There is no per-tick cell triangulation or general rebake.

Gallery137 provides editable collider and Map courses, with ready navigation and
ordinary export. Inspector pointer/history, actual GameSession movement/reset /
fresh recovery, source-hidden relocated content, both MCP message formats and
software Vulkan displays are covered. The manual includes authentic Inspector /
Game captures. The appended public value field requires a matching C++ SDK/module
rebuild; the C host table and pinned dependencies are unchanged. Full Windows /
MSVC/D3D12/editor/physical/graphical EXE acceptance and delivery remain pending.

## State conditions and Activity Lab

An optional **State Condition** connects stock object activity to a configured
application state or ancestor. Conditions compose through structural parents,
including inherited values and owned overrides. Inspector authoring, Undo/Redo,
live Play tuning and automation/MCP share existing component and Scene owners.
Invalid live references reject before mutation. Authored enable fields stay intact.

Rendering, cameras, lights, physics, source audio, UI, animation, particles and
navigation use the existing runtime owners and disable/re-enable behavior. Paused
state changes retire ineligible physics and source audio without advancing time;
new physics admission waits for normal synchronization. Recovery stays unavailable
until that admission and pending contact events complete. Conditions do not filter
arbitrary project C++ systems or external writes to other objects.

Gallery135 **Activity Lab** provides an editable nested group with Run, Rest and
Hide all controls, a computed badge, animation, a dynamic body, particles and sound.
Its content exports without editable state/UI/clip sources. The manual includes
an authentic native UI capture and control/export instructions. Distributed
Gallery README links now resolve to the offline HTML manual.

The SDK exports the value component, while the activation view remains host-only.
Matching SDK rebuilds are required; the C host table and dependency pins remain
unchanged. Physical Windows/full editor/MSVC/D3D12/graphical EXE review and a
numbered delivery remain pending. This is local development source.

## Polygon navigation regions

Navigation Region 2D accepts simple3–64point convex/concave local XY contours.
Empty Points retains rectangles and their existing geometry identity/cooked bytes.
Inspector construction and structured edits, prefab inheritance, full outline
selection/framing, Undo/Redo and MCP discovery/property operations share current
owners. Polygon mode hides rectangle dimensions/resize handles; world transforms
remain available. Concave masks are applied before agent-radius clearance.

Gallery136 provides two ready routes and a normal source-free navigation export.
The guide covers Points, rectangle conversion, validation and rebuilding. Native
point-vector layout/lifetime changes require a matching exact SDK; the C callback
table and dependency pins are unchanged. Windows review remains pending.
