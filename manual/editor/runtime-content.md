# Package cooked content

A cooked content package contains the imported data needed to load selected assets
without the original authoring files. This command-line tool is useful for checking
content delivery. It does not yet create a playable standalone game executable.

Game-runtime foundation work now includes separate save/settings storage and scene
session ownership. These internal services do not add an Export Game command or
a player Save/Load menu yet. The packaging steps below still describe the supported
cooked-content tool.
An opt-in [standalone development host](../getting-started/standalone-runtime.md)
now exists in source; it is separate from this incomplete export workflow.

## Choose the assets

Import your models, textures, materials, audio clips or shaders first. In a terminal,
use `forge_tools --assets query PROJECT` to find their AssetIds. Include the assets
you want as roots; their required asset dependencies are included automatically.
Selecting a mesh from an imported model includes its complete model family.

The current tool supports imported Model, Texture, built-in Material, AudioClip,
compiled Shader assets, baked NavMesh assets, and registered Scene/Prefab files
in the current source checkpoint. Build260923-000066 does not include this new
Scene/Prefab packaging yet. Legacy source-based animation, Script and game UI
packaging are not yet supported by this command.
An unsupported selection reports an error.

For navigation, bake the navigation mesh first (see [Navigation](navigation.md)),
then include its NavMesh AssetId. The package contains the baked navigation data;
it does not include its authoring scene. The game must load the matching level:
existing checks still report stale navigation when its source geometry changes.

## Include a scene and its prefabs

With a tool built from the current source, choose the saved scene's AssetId as a
root. The tool follows its known asset references, including prefab members and
instance overrides. It reports missing references and unknown plugin component
data instead of guessing which files to include. Save your changes before running
this command; it reads files on disk, not unsaved editor changes.

Scene and prefab IDs are preserved. Prefab instances keep their inherited values
and independent overrides. The output remains a content folder, not a standalone
EXE. Current source builds can include admitted game UI and standalone animation
archives. UI export needs the adjacent `forge_ui_inspect` worker and its packaged
default font. A working UI preview alone does not prove
that all images used by hover states or other UI conditions have been discovered.

## Create a package

This example uses PowerShell7. Replace the project, destination and example UUID
with your own values. The destination's parent folder must exist, and the destination
itself must be new.

```powershell
$target = '{"platform":"windows","backend":"d3d12"}'
$roots = '["12345678-1234-4123-8123-123456789abc"]'
.\forge_tools.exe --assets package "C:\Projects\MyGame" "C:\Builds\GameContent" $target $roots
```

Before copying files, the tool registers newly discovered static UI resources in
the project catalog. Their AssetIds remain the same on later exports. This metadata
preparation is separate from publishing the game content folder.

The result is JSON. `ok: true` and exit code0 mean the package passed validation
and was written. The tool never overwrites an existing package. Use a new destination
for another build.

The selected cooked data must match your target. If the command reports a target
or stale dependency mismatch, rebuild the affected imports for that target first.
The tool does not silently recook or relabel artifacts. Linux headless CPU imports
use `{"platform":"linux","backend":"none"}`.

## Verify after copying

Copy the complete package directory to another location, then run:

```powershell
.\forge_tools.exe --assets verify-package "D:\CopiedGameContent" $target
```

Verification checks every file, its hash, the asset dependencies, target profiles
and cooked formats. It needs neither the original project directory nor a shader
compiler. Keep the package intact: deleting files or adding unrelated files makes
verification fail.

## What is included

The package contains its manifest, a compact asset catalog and the selected cooked
artifacts, plus selected scene/prefab documents. It keeps AssetIds and asset references. UI RML/RCSS/fonts/TGA resources and legacy Ozz archives are also included when required.
It excludes unused raw images, glTF,
WAV and HLSL, import sidecars, unrelated cache entries and editor notes. Bounded
recipe settings and digests remain as artifact provenance.

The initial package limit is2GiB, with at most16,384 asset identities and32,768
files. A failed attempt leaves existing packages unchanged. If packaging is
interrupted by a process crash, a sibling `.forge-package-UUID` temporary directory
may remain; the final destination is published only after candidate validation.

See [Asset command-line tools](asset-tools.md), [Models](models.md),
[Textures](textures.md), [Materials](materials.md), [Audio](audio.md), and
[Shader import](shaders.md).

## Declare content chosen at runtime

These controls are in current source builds; Build260923-000066 does not include them.

1. Select the owning UI document or scene in **Content**.
2. Open **Runtime Dependencies** in the **Inspector**.
3. Review **Automatic dependencies**. You do not need to enter ordinary scene, prefab or material references again.
4. For conditional content, choose **Asset type**, then use **Resource** to search or drag an asset from Content. Enter a **Reason / group**, such as “hover images”.
5. Choose **Add dependency**, then **Save declarations**. Every declared resource is required for export.

Use **Selection owner** to associate a gameplay-selected resource with a configured
native module. Keep these declarations on a scene or asset included by the game.
A changed module build requires reviewing its declarations again.

**Observed resources** lists files seen in preview. Use **Add to Runtime Dependencies**
to confirm a conditional resource explicitly. Preview observations are not automatically
shipped: a button can look fine before hovering and still need another image afterward.
Declare finite alternatives even if you have never shown them in preview.

**Remove** edits the draft. **Discard draft / refresh** reloads saved declarations.
The editor keeps a draft when you select another asset and blocks project/scene
switching or closing until you save or discard it in Inspector.
These catalog saves are separate from scene Undo. Missing resources, wrong types,
changed source revisions and catalog conflicts appear as diagnostics. Fix the cause,
review the list and save again. In a packaged game, an undeclared resource request
is rejected; FORGE does not look in the original project to fill the gap.

## Export a standalone game

Save your scenes and asset drafts first. In **Project Settings**, choose **Use
saved current scene as startup**, then **Set up game defaults**. Set your game
name, window mode and resolution, VSync, audio volume and mouse sensitivity. The
**Application ID** is the stable key for player saves and settings: keep it the
same across later builds of the same game. Click **Save Settings**.

Open **Run > Export Game** (also available in the Command Palette). Choose a
**Destination** outside your source project. The supplied **Runtime kit** contains
the matching game executable, DLLs and default font. A project using native SDK
modules uses the matching shared kit in `runtime-kits/shared-native-sdk` and also
needs **Module kits**: the folder holding the matching deployment
folder for each configured module. The SDK's deployment helper prepares these;
the exported game does not include the SDK or compiler.

Click **Export / Rebuild**. FORGE validates the complete declared runtime content
closure and stages a new game folder. Progress shows the current stage. **Cancel
export** preserves the previous completed export; once final promotion starts,
FORGE finishes or recovers it before returning. Errors appear in the task and
Problems. Correct missing dependencies or stale imports/builds, then retry.

After success, **Reveal output** opens the game folder. Copy the whole folder to
the target Windows machine and run `forge_game.exe`. Keep its content, DLLs and
manifest together. Player saves, settings and runtime logs live in the OS user-data
location, independently of the installation folder.

Rebuilding replaces only an existing validated FORGE export. An unrelated folder
is refused. Source project files and scene Undo are unchanged; newly discovered UI
resource registrations are a separate saved catalog update. Export packages saved
content and current cooked artifacts; it does not compile arbitrary gameplay code.
