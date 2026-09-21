# Content browser

Content is the project's asset browser. It lists registered scenes, prefabs, models, materials, shaders, audio, animation, navigation, texture and Runtime UI assets from the existing project catalog. Saved scenes are also discovered by their scene identity.

## Find and inspect an asset

1. Open **Window → Content**.
2. Use **Search project assets...**, **Type** and **Folder** to narrow the list.
3. Click an asset to inspect its type, source and availability in Inspector. This replaces entity selection; it does not change an entity's properties.
4. Double-click a scene to open it, or use **Open scene** in its context menu. Resolve any unsaved scene or draft prompts first.

**Refresh** rescans immediately; the visible browser refreshes approximately every five seconds. Empty results explain how to clear filters or create/register content. **Reveal source folder** opens the containing folder in your operating system. Advanced **Asset details** shows identity and dependency metadata.

## Assign an asset

Select the destination entity and find its component field in Inspector. Drag a compatible Content row onto the field. Starting the drag preserves the entity Inspector. A regular click selects the asset instead.

Alternatively open the field's picker, search, and choose a compatible asset. **None / Clear** removes the reference; **Reveal in Content** selects the referenced asset without modifying the scene. Fields validate expected asset type. Assignment is a scene edit and supports Undo/Redo; asset creation and external file edits do not.

## Create or register supported content

Open **Create / Register**:

- **Import model...** opens [Model import](models.md) for glTF settings, identity review, scene/animation selection and placement.
- **Import texture...** opens the [Texture import](textures.md) document for image/container settings and safe reimport.
- **New material...** creates a reusable source in the central [Material editor](materials.md).
- **Import shader...** opens [Shader import](shaders.md) for an existing project program.
- **New scene** creates an untitled scene through the save guard.
- **Audio / Register WAV** registers a WAV already copied into this project. Enter its project-relative path and choose **Register WAV**.
- **Prefabs** contains Create from selection, Instantiate, Edit source and Duplicate asset.
- The animation, navigation and Runtime UI sections expose their existing conversion, build and registration tools.

See [Audio](audio.md), [Animation](animation.md), [Navigation](navigation.md), [Runtime UI](runtime-ui.md) and [Prefabs](prefabs.md). Stop Play to create/register assets; browsing remains available.

## Current limits

Content combines registered assets with recognized source files. General file management is still being implemented. Texture and glTF sources have supported import/cook workflows. Model documents provide explicit placement into the Scene. General file management and thumbnails are still being implemented. A failed registration leaves existing good assets intact. Problems retains errors; the operation also displays its error locally.

Scene discovery skips `.forge`, `.git` and symbolic links. Scans are bounded to 16 directory levels, 10,000 entries and 64 MiB of JSON candidates, with an 8 MiB per-file limit. Use **File → Open scene...** if a scan exceeds these limits. Package metadata and project manifests are not scenes.

The default bottom panel keeps search and filters compact so asset rows remain visible. Prefab assets come from the project’s existing prefab library and retain their AssetIds. Selecting a newly created prefab resolves it even when Content is hidden. **Reveal in Content** brings that tab forward. Closing a prefab source returns its selection to the asset, rather than leaving a closed member draft selected.

Double-click a Scene or Prefab asset to open its registered editing workflow. Scene opening retains unsaved-change guards; Prefab opens the independent source task. Texture assets open their import settings document; this does not yet provide a GPU texture preview. Model assets open their import and placement document.

Project Material assets open their source editor; imported model Material members open the model's source workflow. Shader assets open compilation settings. Each source task has its own history/publication rules and close guard.


## Asset catalog compatibility

The asset catalog can now retain typed dependency information for imported content.
Existing version-1 catalogs still open. On the next catalog save, FORGE keeps the
original file as `forge.assets.json.v1.backup` before writing version 2. Keep the
catalog and its backup with the project when upgrading. Older FORGE builds cannot
read version 2. This does not change scene or prefab identities.

## Refreshing the list

Content discovers saved scenes and registered assets in the background.
**Refreshing...** means a scan is running; you can continue using the existing
list. Repeated **Refresh** clicks are combined. A failed scan reports its error
and keeps the last good results. Opening another project clears the old list
and discards any unfinished results belonging to the previous project.

## Source files and automatic updates

Copy source files into the project, then choose **Refresh**. Recognized sources
that have not been registered appear as **Source / ...** rows; use **Type → Source
files** to see them separately. Selecting a source shows its relative path, size
and kind in Inspector. **Open import / source** opens supported Model, Texture,
Material or Shader workflows. A source row has no AssetId and cannot be dragged
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
entries. It does not rely on timestamps alone. General rename/move reconciliation
and thumbnail browsing remain under development.


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
