# Runtime content packages

The Phase7 content tool selects cooked assets for a target without copying their
original glTF, image, WAV, material source, HLSL, or import sidecars. It does not
create a standalone visual game executable. Scene/Prefab document closure is implemented in the current Phase8 source
checkpoint. Legacy Ozz, Script, and RmlUi runtime families still need packaging
adapters; selecting one fails rather than silently producing an incomplete package.
This source checkpoint has not been included in a numbered Windows delivery.

## Identity and dependency ownership

`forge.runtime-content.json` version1 records sorted logical roots, the target,
per-artifact profile/format/version, and every packaged file's size and SHA256.
`forge.assets.json` retains the existing catalog representation and AssetIds,
asset types, selected revisions/generations, and typed Runtime dependencies.
There is no second logical asset database, persistent handle, or identity scheme.

A selected imported member brings its owning Model and the complete immutable
family into the closure. Shared revisions copy once. Engine asset references
retain their reserved IDs and are supplied by the engine. Missing/removed,
wrong-type, or revision-mismatched required dependencies reject the candidate.
Build/Source/Optional edges are omitted; the current supported cooked families
have explicit Runtime edges for every required binding. Optional future runtime
resource semantics require an adapter rather than silently treating Build edges
as shipped dependencies.

The runtime catalog's source locator addresses its packaged immutable artifact
manifest. It does not resolve a development filename. Model member/owner locators
remain equal, and the existing complete-family loader checks all identities,
bindings, member formats, animation provenance and selected revisions.

The Phase8 navigation adapter copies the already baked, admitted `.fnav` envelope
to `runtime/navigation/ASSET_ID.fnav`. It retains the exact Recast revision, Detour
layout, geometry/settings/identity provenance and byte digest. The build-only
source scene is excluded; runtime stale-geometry checks still use the loaded scene.
Its CPU data has no graphics-backend binding. No Recast build worker or source
geometry is needed to load it. The existing native loader validates the envelope
and tile before returning a usable resource.

## Authored scene and prefab closure

The shared native-Meta reference inspector now has an unfiltered collection mode.
It records expected asset types, including unresolved IDs, nested component values,
partial prefab overrides, structured prefab source references, explicit spatial
EntityRef scene scopes and scene environment textures. Ordinary deletion/move
impact reviews retain their target-filter semantics. UUID-looking strings remain
ordinary strings; incompatible or opaque component envelopes are reported.

For reachable scenes/prefabs, export prepares a detached copy of the existing
AssetCatalog. It refreshes `document:` Runtime edges through that catalog's graph,
retains separately declared dependencies and traverses the same graph used for
cooked assets. It does not save the source catalog or construct a second graph.
An unknown reference-bearing field rejects export, as do missing or wrong-type
assets and untyped legacy dependency lists. The built-in schema is the default;
a caller supplying custom metadata is responsible for obtaining that detached
schema through the existing exact-module admission boundary.

Bounded validated document bytes are copied to `runtime/scene/ASSET_ID.json` and
`runtime/prefab/ASSET_ID.json`. `forge.runtime_document` metadata records version1,
byte SHA256 and the reference-schema digest. The existing content-manifest version1
contains these files in its inventory. Older readers without these asset adapters
reject them as unsupported; this is not forward compatibility with older runtimes.
Documents retain their independent local TRS ownership and explicit prefab override
intent. No prefab values are flattened into scene overrides. The original authored
scene/prefab formats and identities are unchanged. Loading uses `load_game_scene`
and the existing prefab realization path, with no source-project fallback.

At most64MiB per document and256MiB of reachable document inputs are admitted.
This is content packaging only: the standalone startup/distribution manifest,
module/DLL collection, update promotion and editor export operation remain open.

## UI dependency completeness boundary

A successful RmlUi preview is **not** a complete export dependency declaration.
Exact pinned RmlUi6.3 instantiates conditional decorators lazily. A hover-only image
can be absent from both the initial FORGE resource observation and native texture
source list, then fail when hovered. Its public stylesheet API does not enumerate
all inactive rules/media blocks. The existing observed-resource catalog must not
be relabeled as complete. Full UI export remains unavailable pending the dependency
declaration/admission contract; no private CSS parser, upstream patch, full-project
copy or silent missing-resource omission has been introduced.

## Contents and bounds

Supported roots are imported Model families, Texture bundles, built-in Material
bundles, cooked AudioClip, compiled Shader programs, baked NavMesh assets, and
registered Scene/Prefab documents. Other source-only or unknown families are rejected. Existing cooked parsers and selected-resource loaders
validate the candidate, including texture variants, material bindings and shader
reflection/provenance. Packaging links no Diligent device, HLSL compiler, glTF
source parser, image codec, or native audio decoder.

The default/hard initial bounds are 16,384 logical identities, 32,768 files,
256MiB per file, 512MiB per artifact family, and 2GiB total package bytes including
its manifest. Callers can lower those budgets. Processing releases each copied
family before reading the next; this is not a claim of constant memory independent
of the admitted family size. Every selected family is validated before publication.

Raw sources, authoring sidecars, source-dependency catalog entries, unrelated cache
revisions, private metadata, thumbnails and diagnostic logs are not copied. The
small `forge.import` and format-specific selection metadata remain. Content-addressed
artifact manifests retain their bounded original recipe inputs and digests because
existing loaders verify the build key from those inputs. Recipe inputs may include
relative source labels and effective settings; those are provenance, never paths
opened by the cooked loader. Rewriting these manifests would invalidate the selected
revision. A future compact artifact storage format must preserve equivalent identity
and admission evidence explicitly.

## Targets and backend neutrality

Target matching uses the artifact's declared platform/backend and retains its
own recipe profile. The existing `windows-x64` shader recipe and `windows` desktop
recipes share the Windows platform label for packaging. Backend names are never
aliased. A Linux CPU or Vulkan cook cannot be relabeled as a D3D12 cook.

Logical Mesh/Material/Texture/Shader asset identities remain backend neutral.
Compiled custom Shader assets currently use the admitted D3D12 FXC5.1 artifact
adapter. Packaging ships its compiled bytecode and copied reflection; loading it
needs no HLSL source or full compiler. That does not claim a Vulkan/Metal/WebGPU
custom-shader cooker is implemented. See [backend validation](render-backends.md).
The editor's development shader compilation remains a separate behavior.

## Publication, loading and failure

The output directory must not exist. The tool creates a sibling staging directory,
copies and validates bounded immutable bytes, builds a trimmed catalog, verifies
the complete package, then rechecks the source catalog before renaming the candidate.
There is no overwrite/update mode and no cross-document Undo claim. Failed validation,
cancellation, stale source selection, or an existing destination leaves previous
packages untouched and removes that attempt's staging directory. A process crash
may leave an unpublished `.forge-package-UUID` directory for inspected cleanup.
This is not a claim of durable multi-file replacement after sudden power loss.

`open_runtime_content` verifies paths, the inventory, hashes, the exact closure,
profiles, and formats before returning the ordinary AssetCatalog. Extra files,
redirected paths, and cooked binding mismatches fail. Integrity hashes are not
signatures or authentication. Verification can be repeated after relocation.

Immutable selected DDC reads no longer create/acquire the writer lock file.
Runtime packages can be mounted read-only. Import publication, repair, pruning
and cache bookkeeping retain their writer locks. A concurrent eviction can make a
selected read fail; owned bytes are admitted before becoming usable, and existing
resource replacement semantics preserve the previous good resource.

## Windows file I/O

A cache key adds 64 path characters; the unique staging directory can make a
valid final output exceed the traditional Windows path limit during preparation.
The private asset I/O adapter supplies normalized absolute extended-length wide
paths at Windows stream, cache-lock, flush and directory-publication calls.
Catalog locators, identity, path admission and serialized values retain their
existing spelling. Read/write diagnostics report the original path. This follows
[Microsoft's extended-length path contract](https://learn.microsoft.com/en-us/windows/win32/fileio/maximum-file-path-limitation)
and does not require changing the machine registry. Other subsystems' external
converters and filesystem APIs retain their own validation requirements.

## Validation

`runtime_content_package` covers deterministic output, dependency closure, source
removal and relocation, actual material/texture loading, PCM admission, writer-lock
absence, long output/staging paths, target and hash mismatches, path rejection, extra files, budgets,
cancellation and previous-package preservation. Model pipeline tests additionally
package an actual imported member and verify its complete source-free family.
Native Windows shader-worker tests package real compiled bytecode and reload it
from a relocated directory. Native execution results belong to the corresponding
build's validation record; merely declaring these tests is not acceptance.

The navigation regression additionally packages an actual baked obstacle course,
relocates it with the original project unavailable, queries a path and advances
a runtime agent, and rejects a corrupted packaged envelope.

See the [user instructions](../manual/editor/runtime-content.md).
