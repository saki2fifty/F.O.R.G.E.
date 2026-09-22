# Inspect project assets from the command line

Use `forge_tools` to list source files and inspect registered asset references
without launching the editor. The inspection commands below read files only. They do not import
content, assign AssetIds, change the catalog, or alter your scene.

For cooked-data export, see [Package cooked content](runtime-content.md). Importing
assets uses the separate `--assets import` workflow documented under [Models](models.md),
[Textures](textures.md), and [Audio](audio.md).

## List source files

Open a terminal in the extracted FORGE folder and run:

```powershell
.\forge_tools.exe --assets scan "C:\Projects\MyGame"
```

This scans the project's `Assets` folder recursively. To inspect another
project-relative folder, add it at the end:

```powershell
.\forge_tools.exe --assets scan "C:\Projects\MyGame" "Examples"
```

The JSON result lists each source path, byte count, content digest, and recognized
source kind. Recognition uses the filename; it does **not** mean that the file
has been decoded, validated, or imported. Unknown file types remain visible.

Hidden files, temporary files, and backup files are filtered. Sources reached
through hard links or contained symbolic links are reported as aliases. Paths
that lead outside the project are rejected. The command reports an incomplete
scan if a source cannot be read, changes while being read, or exceeds a limit.
Never use an incomplete result as proof that omitted assets were deleted.

The default limits are 100,000 files, 10,000 directories, 64 directory levels,
512 MiB per file, and 8 GiB read per scan. Scans hash file contents, so scanning a
large project can take time. The command does not modify or repair source files.

## Inspect registered assets

```powershell
.\forge_tools.exe --assets query "C:\Projects\MyGame"
```

This reads `forge.assets.json` and reports logical AssetIds, types, source
locations, metadata, and registered dependency edges. A registered asset may still
have a missing or invalid source; this command does not certify it is ready to use.

If a record belongs to an imported source container, `subasset` identifies its
owner and stable mapping key. `removed: true` means the old identity has been
retained to diagnose references to a removed source element. It is not an active
output of the container.

## Find registered dependents

Copy an AssetId from the query result and run:

```powershell
.\forge_tools.exe --assets dependents "C:\Projects\MyGame" "12345678-1234-4123-8123-123456789abc"
```

`direct` lists assets with registered references to that ID. `transitive` also
includes assets that depend on those assets. This uses the catalog graph; it does
not scan arbitrary scene or plugin payloads for additional references.

To find assets affected by a raw source file, such as a model buffer or shader
include, use its project-relative path:

```powershell
.\forge_tools.exe --assets source-dependents "C:\Projects\MyGame" "Assets\Shaders\lighting.hlsli"
```

`direct` lists the registered consumers of that file; `affected` also includes
their dependents. Every registered asset's primary source is indexed. Additional
includes and buffers appear only after their dependency records are registered.

## Use the result in a script

Every response is JSON with `api: 1` and an `ok` value. Success returns exit code
0; invalid arguments, unreadable catalogs, and incomplete scans return exit code
1 with a diagnostic. Commands work from any current folder when the project path
is absolute. On Linux use `./forge_tools` in place of `.\forge_tools.exe`.

See also [Content browser](content-browser.md) and [Headless authoring tools](automation.md).
