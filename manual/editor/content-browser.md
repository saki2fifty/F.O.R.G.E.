# Content browser

Content is the project's asset browser. It lists registered scenes, prefabs, models, materials, shaders, audio, animation, navigation, texture and Runtime UI assets from the existing project catalog. Saved scenes are also discovered by their scene identity.

In a short panel or at high interface zoom, **Actions** contains Create / Register,
Import files, Refresh and Source updates. Search, Filters, View and Folders stay
visible beside it so the results retain useful space.

## Find and inspect an asset

1. Open **Window → Content**.
2. Search by words in the name, path, extension, type or status. Open **Filters**, then use **All types** and **All states** to narrow results. Matching is case-insensitive.
3. Click an asset to inspect its type, source and availability in Inspector. This replaces entity selection; it does not change an entity's properties.
4. Double-click a scene to open it, or use **Open scene** in its context menu. Resolve any unsaved scene or draft prompts first.

**Refresh** rescans immediately; the visible browser refreshes approximately every five seconds. Empty results explain how to clear filters or create/register content. **Reveal source folder** opens the containing folder in your operating system. Advanced **Asset details** shows identity and dependency metadata.

## Folders, views and selection

Use the folder tree or **Folders** menu to choose a location. Inside **Folders**, **Project** returns to the root; clicking a breadcrumb opens that ancestor. **<** and **>** return to previous/next locations. Search normally includes subfolders. Turn off **View → Include subfolders** for the current folder only.

**View → List** shows names, types and source states. **View → Grid** uses tiles; **Tile size** adjusts their width. In a short panel, previews shrink vertically so the first row's names and types remain visible. Published Texture, Model, Mesh and Material assets show rendered thumbnails as visible tiles prepare. Other types and unfinished previews show a type icon. List rows use the same icons as Inspector asset pickers. Grid/list, tile size, folder-tree visibility and subfolder preference are saved as personal editor preferences. Narrow Content panels use **Folders** when there is insufficient room for the tree.

- Click to select one item; **Ctrl-click** adds/removes an item.
- **Shift-click** selects a range; **Ctrl+A** selects the filtered results.
- **Escape** clears Content selection when the results have focus.
- Inspector follows the primary item. Background refresh retains selection by AssetId; unimported source selection is temporary and follows its path.
- Selected items hidden by a filter remain selected. The selection count and **Reimport selected** are in the results-background context menu; check them before reimporting.
- Dragging sends the individual asset under the pointer. It preserves an entity Inspector for assigning that asset; it does not assign an entire multi-selection.

Right-click offers **Copy AssetId** for registered assets and **Copy source path** for either kind of row. Generated model members show their imported display names, with the owning path in their tooltip and Inspector.

### Thumbnail behavior

Thumbnails show the last successfully published asset, not unsaved import settings
or Material drafts. Textures retain their aspect ratio; models and meshes use the
same materials and transformed geometry as their viewers. Imported materials use
a sphere. Thumbnails do not animate or run gameplay.

Only visible grid items request work. The editor shares scratch viewers, caches up
to 128 completed images (at most 32 MiB), and retires least recently used offscreen
entries. Published dependency changes refresh affected images. Unrelated catalog
changes recheck identity without redrawing unchanged content.

A failed refresh keeps the previous image. Hover the tile for its preparation or
error message; right-click **Refresh thumbnail** to retry. Missing/unimported and
removed assets show a fallback. Thumbnail viewing does not import files, change
scene history or save new assets. Closing/switching projects discards this cache.

### Read asset state

**Registered** means a logical asset is indexed. **Published** means it has a selected import revision; it does not imply a GPU resource is loaded. **Not imported** is a discovered source awaiting registration/import. **Source changed** means observed source/dependency bytes differ from the selected revision. **Source missing** comes only from a complete source scan. **Removed member** identifies a retained subasset mapping whose member no longer exists. **Queued**, **Importing**, and **Import error** reflect the owning importer job; generated members share that state. Browser state can lag by a background refresh. **Source updates** and Problems provide detailed errors and progress.

### Reimport a selection

Select registered assets and choose **Reimport selected**. FORGE queues their existing importer routes and handles a shared source owner once. Dirty source drafts keep their guard; resolve them before publication. An unsupported/unregistered selection rejects the request before queuing any of that batch. Reimport may reuse validated cached outputs when inputs are unchanged. It does not alter scene Undo history.

### Inspect dependencies

Select an asset and expand **Dependencies and references** in Inspector. **Uses assets**, **Uses source files**, and **Referenced by assets** show the catalog's typed links. **Select asset** follows a link. This view covers catalog edges; it is not a complete scan of references in unopened scenes or unknown plugin payloads. Deletion performs its separate reviewed reference scan.

## Assign an asset

Select the destination entity and find its component field in Inspector. Drag a compatible Content row onto the field. Starting the drag preserves the entity Inspector. A regular click selects the asset instead.

Alternatively open the field's picker, search, and choose a compatible asset. **None / Clear** removes the reference; **Reveal in Content** selects the referenced asset without modifying the scene. Fields validate expected asset type. Assignment is a scene edit and supports Undo/Redo; asset creation and external file edits do not.

Search and **None / Clear** stay above the scrolling results. Opening a field with
a selection far down a large list keeps those controls visible at every interface scale.

## Drop content into the Scene

Drag a **Model**, **Mesh** or **Prefab** from Content onto the Scene image. Placement uses a camera-facing plane through the current view target, so it also works in front and side views. It creates fresh entity identities, selects the new root and records one scene Undo step. Save the scene to keep the placement.

Model drops use the source's default scene, or its only scene, without autoplay. If a model has several scenes and no default, open [Model import](models.md) and choose **Source scene** before using **Place model**. Models prepare in the background; **Cancel placement** appears in the Scene while work is pending. A changed scene, cancelled task or failed preparation leaves the scene unchanged.

A Mesh drop assigns that typed mesh to a new Mesh Renderer. A Prefab drop creates a linked instance and overrides only its root translation; inherited rotation and scale remain inherited. A **Scene** drop opens the ordinary scene workflow, including the prompt for unsaved changes.

Other asset types belong on their compatible [Inspector fields](inspector.md). A texture or material dropped on empty Scene space does not create an unrelated object.

## Bring files in from your computer

1. Stop Play and choose **Import files** in Content, or drag source files from your desktop/file manager onto Content.
2. Review the selected files. Use **Remove** for any entry you do not want.
3. Choose an unused **New project folder** below `Assets`; its parent folder must already exist.
4. Choose **Prepare import**. FORGE reads the files and finds the model's required buffers and images without copying anything yet.
5. Review the file count, total size and destination. Leave **Import with defaults after copying** enabled to prepare assets immediately, or disable it to adjust each source's settings later.
6. Choose **Copy sources**. Each selection gets its own `Source-1`, `Source-2`, and so on inside the new folder.
7. Check each result, then choose **Close**. Successfully imported assets appear in Content; copied-only or failed sources remain available through **Open import / source**.

This workflow accepts supported glTF models, texture sources and mono/stereo WAV audio. It copies glTF buffers and images beneath the selected model's own folder while retaining their relative paths. A model that refers outside that folder must first be organized into a self-contained source folder, or copied into the project manually for the existing model import workflow. Network references and symbolic links are rejected.

Existing destinations are never replaced. If source bytes change after review, prepare again. FORGE does not copy project catalogs, import sidecars or authored Scene/Prefab/Material/Shader identities through this workflow; use their own creation or duplication actions.

**Cancel batch** stops pending work. Completed copies and successfully published assets remain. Each asset imports independently: a failure in the second source does not undo the first successful import. Source copying and asset publication do not belong to Scene Undo. For different import settings, disable automatic import, then open each copied source through its own [Model](models.md) or [Texture](textures.md) settings.

Limits are 256 selected sources, 8,192 copied files, 512 MiB per file and 2 GiB total, together with the existing format-specific import limits. Closing FORGE or losing power during copying can leave a hidden `.forge-import-*` staging folder. It is not imported content; do not treat it as a completed destination. No automatic cleanup of interrupted source-copy staging is claimed.

## Create or register supported content

Open **Create / Register**:

- **Import model...** opens [Model import](models.md) for glTF settings, identity review, scene/animation selection and placement.
- **Import texture...** opens the [Texture import](textures.md) document for image/container settings and safe reimport.
- **New collision...** creates a reusable [Collision asset](physics.md) with its own source document, shape settings and Save/history. A selected Mesh also offers **Assets → Create Collision from Mesh...**.
- **New material...** creates a reusable source in the central [Material editor](materials.md).
- **Import shader...** opens [Shader import](shaders.md) for an existing project program.
- **New scene** creates an untitled scene through the save guard.
- **Import audio...** opens the central [Audio clip](audio.md) import document. Enter the project-relative WAV path, choose **Review settings**, then **Import / Reimport**. Select an imported clip to inspect its duration, channels, sample rate and format.
- **Prefabs** contains Create from selection, Instantiate, Edit source and Duplicate asset.
- The animation, navigation and Runtime UI sections expose their existing conversion, build and registration tools.

See [Audio](audio.md), [Animation](animation.md), [Navigation](navigation.md), [Runtime UI](runtime-ui.md) and [Prefabs](prefabs.md). Stop Play to create/register assets; browsing remains available.

## Current limits

Content combines registered assets with recognized source files. Texture and glTF sources have supported import/cook workflows. Model documents provide explicit placement into the Scene. The source operations below cover supported formats; unsupported formats still require their existing registration workflows. A failed registration leaves existing good assets intact. Problems retains errors; the operation also displays its error locally.

Scene discovery skips `.forge`, `.git` and symbolic links. Optional scene discovery is bounded to 64 directory levels, 110,000 directory/file entries and 64 MiB of JSON reads, with an 8 MiB per-file limit. Registered non-scene sources, the catalog, project metadata and import sidecars are excluded from those JSON reads. Use **File → Open scene...** if a scan exceeds these limits. Package metadata and project manifests are not scenes.

The default bottom panel keeps search and filters compact so asset rows remain visible. Prefab assets come from the project’s existing prefab library and retain their AssetIds. Selecting a newly created prefab resolves it even when Content is hidden. **Reveal in Content** brings that tab forward. Closing a prefab source returns its selection to the asset, rather than leaving a closed member draft selected.

Double-click a Scene or Prefab asset to open its registered editing workflow. Scene opening retains unsaved-change guards; Prefab opens the independent source task. Texture assets open their texture preview and import document; generated model textures open a read-only Texture viewer. Model assets open their preview, import and placement document. Mesh assets open read-only mesh inspection. AudioClips open the Audio clip import document; imported metadata also appears in Inspector.

Project Material assets open their source editor; imported model Material members open a read-only Material preview with an Open source import action. Shader assets open compilation settings. Each source task has its own history/publication rules and close guard.


## Asset catalog compatibility

The asset catalog can now retain typed dependency information for imported content.
Existing version-1 catalogs still open. On the next catalog save, FORGE keeps the
original file as `forge.assets.json.v1.backup` before writing version 2. Keep the
catalog and its backup with the project when upgrading. Older FORGE builds cannot
read version 2. This does not change scene or prefab identities.

## Refreshing the list

Content discovers saved scenes and registered assets in the background.
**Refreshing...** means a scan is running; you can continue using the existing
list. Repeated **Refresh** clicks are combined. A failed catalog scan reports its error and keeps the last good results. If only optional scene discovery fails, registered assets still refresh while the last complete discovered-scene list is retained. Opening another project clears the old list
and discards any unfinished results belonging to the previous project.

## Source files and automatic updates

Copy source files into the project, then choose **Refresh**. Recognized sources
that have not been registered have the **Not imported** state; choose it in the
state filter to see them separately. Selecting a source shows its relative path, size
and kind in Inspector. **Open import / source** opens supported Model, Texture,
Material, Collision or Shader workflows. A source row has no AssetId and cannot be dragged
into a typed asset field until it has been imported.

FORGE checks project source contents in the background approximately every two
seconds after the previous check completes. Changes to registered Model, Texture,
Material and Shader sources or their import settings schedule reimport. New files
require an explicit import; discovery alone does not assign identities.

Choose **Source updates** to see the current scan, queued updates and active job.
**Rescan / retry failed** checks again and retries failed imports. Errors also
appear in Problems. A failed import keeps the last published asset usable. A
missing source does not erase its asset identity or existing cooked output.

Unsaved import settings or Material edits delay automatic replacement for that
asset. Save or resolve the draft first. Successfully published changes refresh
clean source documents and dependent materials, and request safe scene-resource
updates. This does not change scene Undo history. An incomplete scan pauses new
automatic work until a complete scan succeeds.

The watcher uses bounded content-hash polling, including sources outside the
Assets folder but inside the project. It ignores hidden, temporary and cache
entries. It does not rely on timestamps alone. Use the reviewed source operations
below for supported renames/moves; supported thumbnail behavior is described above.


## Rename, move, duplicate or delete a source

Right-click the owning asset row in Content. Generated mesh/material/clip members
belong to their source; choose the model/container row to operate on that family.

1. Choose **Rename / Move...**, **Duplicate source...**, or **Delete source...**.
2. For a move or copy, enter a new project-relative filename, including its extension. Choose an existing folder. Existing files are never overwritten.
3. Click **Prepare review**. FORGE pauses automatic imports and checks the files and known references in the background. A discovered scene/prefab may be registered with its existing identity during this step; no source is changed yet.
4. Read the affected-asset count, catalog dependents, and references. **Uninspected data** identifies unknown data that cannot be treated as a typed reference.
5. For deletion, check **I understand the deletion impact**, then click **Confirm file changes**. Otherwise confirm when the reviewed destination is correct.
6. Wait for the result, then **Close** to resume automatic source updates.

A move preserves identities and scene references. Moving the active scene preserves
its unsaved draft and Undo history. A duplicate copies the **saved source**, allocates
new asset identities, and gives a copied scene new entity identities. Imported copies
need their own validated import before use; open the new source and publish it.

Deletion keeps the original files in the backup folder shown in the result. Known
references remain explicit missing references; FORGE does not redirect them to another
file with the same name. These file operations are **not scene Undo**. There is no
one-click backup restore yet. Open another scene before deleting the active scene.

Save or discard open source-document drafts and finish current import/build jobs
before starting. While this dialog owns the source files, Save, project switching,
automation writes and other authoring actions wait. **Cancel job** requests safe
cancellation; an already completed disk commit remains completed. Changed references
or source bytes invalidate the review and require preparing it again.

Current source adapters cover Scene, Prefab, Material, Shader program, glTF model/
animation source, Texture and WAV. Unsupported formats report an error during
preparation. Case-only renames require a different intermediate filename. These
workflows describe the Phase7 source under validation; the final numbered package
has not been delivered yet.


### Existing subsystem sources

The common catalog also lists legacy animation, navigation, UI and Flecs Script
assets. Source status uses recorded file revisions where available, even when the
asset has no general importer. **Source changed** means the recorded source or
supporting file differs; it does not mean the old generated resource was replaced.
Use the asset's existing conversion, bake, preview or reload workflow.

Legacy animation conversion records its glTF/buffer sources and typed
Skeleton/AnimationClip dependencies. Navigation records its source Scene dependency;
rebaking still validates the current scene geometry. UI files and observed
RmlUi dependencies use [Runtime UI's metadata refresh](runtime-ui.md).
Registered Flecs Scripts record their root source revision. Native managed includes
remain the Script preview worker's responsibility; registration does not execute
Script or claim a complete static include graph. Scene and Prefab retain their
own document save/publication/history rules.


## Shared asset commands

The **Assets** menu and Command Palette (**Ctrl+Shift+P**, search **Assets /**)
provide Import files, Open selected, Place selected in Scene, Reimport selected,
Rename / Move, Duplicate source, Delete source, and Derived cache. The matching
Content context commands use the same availability checks and selected asset.
Disabled commands explain what is needed in their tooltip.

**Place selected in Scene** supports Model, Mesh and Prefab assets, using the
creation target (World Origin or View Target) and one scene Undo step. Opening
a Model import document allows its additional scene, clip and variant choices.
File operations show a review before changing files; scene Undo does not undo
source edits, deletion, duplication or reimport publication.

Choose **Assets > Derived cache...** to inspect statistics, verify artifacts or
clear disposable derived data. Verification and clearing wait for imports to drain.
Clearing requires a second click in its review popup, leaves source assets untouched,
and requires Reimport to rebuild artifacts. Already loaded resources can remain
visible while new loads report missing data. This window opens on demand.

The Assets menu uses short operation names; the Command Palette keeps the
**Assets /** category prefix for searching. Both routes use the same actions and
availability rules.
