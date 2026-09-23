# Loaded resources and import cache

Open **Tools > Loaded resources** when you need to understand what the Scene and
Game views have loaded. This is an on-demand diagnostic window; it does not change
your scene or unload objects.

## Read the resource window

The summary shows loaded, pending and retiring Mesh, Material and Texture resources,
plus their CPU payload. GPU mesh buffers, textures and environment maps have separate
payload totals. These totals exclude driver overhead, render targets and independent
asset previews. They are not total graphics-card memory usage.

Expand a resource row to see its AssetId, loaded revision, source, semantic variant
and strong lease count. A strong lease means a consumer still needs that revision.
A **Retiring** revision can remain while an older draw or consumer finishes.

**Requests and errors** shows pending or failed requests. **Previous good revision
retained** means the failed update has not replaced the usable version. Use
**Show in Content** to inspect the asset's import settings and dependencies.
The window refreshes twice a second; **Refresh** requests an earlier update.

This window covers the editor's main scene resource owner. Independent asset-preview
owners and resources inside the separate Play process have their own lifetimes and
are not included in these counts.

## Check the cache inside the editor

Open **Assets > Derived cache...** (also available in the Command Palette). Use
**Statistics** to inspect its size or **Verify** to check manifests, sizes and
hashes. Clearing all derived data requires confirmation; source files and authored
scene data remain. These operations wait for active imports to drain and run in the
background. Scene Undo does not undo cache maintenance. Reimport affected assets
after clearing their cooked data.

## Check the disk cache from a terminal

The import cache contains generated files that FORGE can recreate from your sources.
Close the editor for that project before running these maintenance commands. Each
command acquires the project's writer lock, so it refuses to overlap a running editor.

```powershell
.\forge_tools.exe --assets cache-stats "C:\Projects\MyGame"
.\forge_tools.exe --assets cache-verify "C:\Projects\MyGame"
```

`cache-stats` reports entry count, stored bytes and quarantined entries.
`cache-verify` checks the recorded manifests, sizes and hashes. It does not claim that
every format is supported by your current GPU, and it does not repair or quarantine
files. Actual import and resource loading still perform their own format validation.
A failed check returns a diagnostic and exit code 1.

## Remove unused generated files

```powershell
.\forge_tools.exe --assets cache-prune "C:\Projects\MyGame" 0
.\forge_tools.exe --assets cache-cleanup "C:\Projects\MyGame"
```

`cache-prune` removes older, unselected revisions toward the supplied byte budget;
zero removes all unselected revisions. Currently selected catalog revisions are
protected even if that leaves the cache above the requested budget.

`cache-cleanup` removes abandoned, recognized publication staging directories and
import-worker folders. It first checks that neither an editor nor a surviving
worker still owns the folder. Active jobs are retained; retry after they exit.
The JSON result's `worker_jobs` list shows which import folders were removed and
why others were retained.

Unexpected names, nested contents, redirected or hard-linked files, excessive data
and old worker folders without ownership markers are preserved with diagnostics.
There is no automatic startup deletion of unfamiliar folders. Inspect retained
data before deciding whether to remove it yourself.

## Rebuild a selected asset's cache

Copy the asset's ID from Content and run:

```powershell
.\forge_tools.exe --assets cache-clear-asset "C:\Projects\MyGame" "12345678-1234-4123-8123-123456789abc"
```

This removes its currently selected cooked artifact. An imported model's members
share a family artifact, so other members may be affected too; the JSON result lists
their IDs. Older unselected artifacts can be removed with `cache-prune`.
Reopen the editor and **Reimport** the source to rebuild it. Source files, import
settings, AssetIds, scenes and catalog references remain unchanged.

To remove all recognized cooked entries and quarantined/staging data:

```powershell
.\forge_tools.exe --assets cache-clear-all "C:\Projects\MyGame"
```

Reimport the affected assets before expecting them to load again. Cache maintenance
is separate from scene Undo. It preserves unfamiliar files rather than deleting
arbitrary contents of the project.

See also [Content browser](content-browser.md), [Import tools](asset-tools.md) and
[Cooked content packages](runtime-content.md).
