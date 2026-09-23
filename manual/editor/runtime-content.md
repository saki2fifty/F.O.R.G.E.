# Package cooked content

A cooked content package contains the imported data needed to load selected assets
without the original authoring files. This command-line tool is useful for checking
content delivery. It does not yet create a playable standalone game executable.

Game-runtime foundation work now includes separate save/settings storage and scene
session ownership. These internal services do not add an Export Game command or
a player Save/Load menu yet. The packaging steps below still describe the supported
cooked-content tool.

## Choose the assets

Import your models, textures, materials, audio clips or shaders first. In a terminal,
use `forge_tools --assets query PROJECT` to find their AssetIds. Include the assets
you want as roots; their required asset dependencies are included automatically.
Selecting a mesh from an imported model includes its complete model family.

The current tool supports imported Model, Texture, built-in Material, AudioClip,
compiled Shader assets, and baked NavMesh assets. Scene/Prefab files and legacy
source-based animation, Script and game UI packaging are not yet supported by this command.
An unsupported selection reports an error.

For navigation, bake the navigation mesh first (see [Navigation](navigation.md)),
then include its NavMesh AssetId. The package contains the baked navigation data;
it does not include its authoring scene. The game must load the matching level:
existing checks still report stale navigation when its source geometry changes.

## Create a package

This example uses PowerShell7. Replace the project, destination and example UUID
with your own values. The destination's parent folder must exist, and the destination
itself must be new.

```powershell
$target = '{"platform":"windows","backend":"d3d12"}'
$roots = '["12345678-1234-4123-8123-123456789abc"]'
.\forge_tools.exe --assets package "C:\Projects\MyGame" "C:\Builds\GameContent" $target $roots
```

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
artifacts. It keeps AssetIds and asset references. It excludes raw images, glTF,
WAV and HLSL, import sidecars, unrelated cache entries and editor notes. Bounded
recipe settings and digests remain as artifact provenance.

The initial package limit is2GiB, with at most16,384 asset identities and32,768
files. A failed attempt leaves existing packages unchanged. If packaging is
interrupted by a process crash, a sibling `.forge-package-UUID` temporary directory
may remain; the final destination is published only after candidate validation.

See [Asset command-line tools](asset-tools.md), [Models](models.md),
[Textures](textures.md), [Materials](materials.md), [Audio](audio.md), and
[Shader import](shaders.md).
