# Runtime content packages

The Phase7 content tool selects cooked assets for a target without copying their
original glTF, image, WAV, material source, HLSL, or import sidecars. It does not
create a standalone visual game executable. Scene/Prefab document closure is implemented in the current Phase8 source
checkpoint. Legacy Ozz and bounded RmlUi source adapters are implemented in the current source.
Unrecognized runtime types still reject export. Standalone distribution manifests, runtime/module-kit collection and the shared export
operation are implemented in current source; final Windows graphical acceptance
remains in progress.
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
The low-level content packager is separate from the standalone assembly operation
described below. Its new-directory publication contract remains unchanged.

## Declared runtime closure

The package contains and validates the complete **declared runtime dependency
closure**. This does not prove every request arbitrary gameplay code could make.
Automatic reflected/cooked edges and explicit `declared:` Runtime edges share
AssetCatalog's existing graph. Declarations are finite, typed and required; there
are no wildcard or optional-runtime semantics. Missing/wrong-type selections,
unsupported adapters and stale reviewed sources reject export.

`forge.runtime_declarations` version1 stores reviewed source hashes only, not a
second dependency list. Saving declarations checks the expected **persisted** catalog revision,
re-admits any prepared UI source snapshot and atomically saves one catalog. UI
inspection and failed declaration validation do not publish an intermediate catalog.
Reimport preserves author-owned declarations; it does not silently refresh their
reviewed source hashes. A scene/document may own module-selected resources using
`declared:module/MODULE_ID/REASON`; these additionally track project settings and the
selected module binary. The owner must be reachable from the exported roots.

### UI resources

Pinned RmlUi6.3 lazily instantiates decorators; an initial successful preview cannot
certify inactive styles/media rules. `ui.observed` edges remain advisory even if an
older catalog marked them Runtime. They are excluded from export closure.

Before immutable packaging, `prepare_runtime_content_catalog` registers newly
found UI sources with persistent UUIDv4s in the authoritative project catalog.
This metadata preparation is separate from package promotion; it does not modify
scene/prefab bytes or publish their temporary `document:` edges. Repeating it keeps
identities unchanged. The low-level packager refuses unregistered UI discoveries
rather than allocating different logical identities for each export.

A disposable `forge_ui_inspect` worker uses native RmlUi DOM/style loading with
bounded lifetime/memory/output. It does not evaluate gameplay data bindings. Linked
RCSS/fonts and literal `<img src>` attributes (including hidden images) form the
reliable `ui.automatic` subset. Native sprite precedence is preserved. Conditional
images/decorators, data-selected URLs and finite theme alternatives require explicit
declarations. There is no private RCSS/RML parser or completeness flag.

Supported RML/RCSS/font/TGA bytes retain relative paths for native resource URLs.
Their exact admitted hashes/types are verified. A Texture used by both the renderer
and UI carries its cooked selection and its independently admitted raw UI source.
Package reads require manifest membership and a matching digest; undeclared paths
produce `package.resource.undeclared`. Gameplay resource subscriptions enforce
catalog membership and never refresh immutable packages from project discovery.

### Legacy Ozz archives

Standalone Skeleton/AnimationClip records retain exact converter/settings/source
hash provenance, skeleton identity and artifact digests. Strict archive admission
runs before native Ozz load; clip/skeleton compatibility is validated and sampling
is probed. Archives relocate to `runtime/animation/ASSET_ID.ozz`. Original glTF/source
records remain provenance, not runtime files. Development source checks stay intact;
an admitted package does not require its original conversion project.

## Contents and bounds

Supported roots are imported Model families, Texture bundles, built-in Material
bundles, cooked AudioClip, compiled Shader programs, baked NavMesh assets, and
registered Scene/Prefab documents, legacy Ozz archives and admitted runtime UI
resources. Other source-only or unknown families are rejected. Existing cooked parsers and selected-resource loaders
validate the candidate, including texture variants, material bindings and shader
reflection/provenance. Packaging links no Diligent device, HLSL compiler, glTF
source importer or native audio decoder. The UI adapter uses its existing bounded
TGA/font/text admission, independently of graphical GPU validation.

The default/hard initial bounds are 16,384 logical identities, 32,768 files,
256MiB per file, 512MiB per artifact family, and 2GiB total package bytes including
its manifest. Callers can lower those budgets. Processing releases each copied
family before reading the next; this is not a claim of constant memory independent
of the admitted family size. Every selected family is validated before publication.

Unused raw sources, authoring sidecars, source-dependency catalog entries, unrelated cache
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
This describes loading cooked custom Shader assets. The shared renderer still
asks Diligent to compile its embedded engine/UI shader source when creating
pipelines, including in the Development standalone host. Those strings ship in
the executable; they do not resolve editor/project HLSL files. The Windows host
uses the platform shader-compiler runtime for that path, not an installed FORGE
SDK or development shader-worker executable. Do not describe the entire host as
performing no runtime shader compilation.

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

## Standalone assembly and recovery

The UI-independent `export_standalone_game` operation is shared by Run > Export
Game and `forge_tools --assets export-game PROJECT OPTIONS_JSON`. Options select
`destination`, `runtime_kit`, and an optional `module_kits` map from configured
module ID to deployment folder. Export reads saved project configuration; editor
unsaved authoring drafts must be saved or discarded first. It rebuilds the package
from current selected cooked artifacts, without rebuilding arbitrary C++ projects.
Missing/stale artifacts require Reimport or rebuilding the affected module first.

A `GameRuntime` CMake installation provides `forge.runtime-kit.json`, exact engine
profile/fingerprint/build provenance, the game executable, its resolved native
runtime dependencies, fonts and dependency notices. It is separate from the SDK.
The pinned CMake4.4.3 native `GET_RUNTIME_DEPENDENCIES` resolver inspects actual built binaries;
FORGE does not maintain a second PE dependency parser. The current kit is Windows
D3D12 Development; logical asset formats and content closure remain backend-neutral.

Shared-SDK consumers call `forge_install_gameplay_runtime(target NAME module.id)`
and install component `GameplayRuntime` into a fresh directory. One child folder
per name contains `forge.module-kit.json`, the exact module and its resolved DLLs.
`EXTRA_LIBRARIES` supplies a finite list for explicitly loaded native libraries that
cannot be inferred from import tables. Native code/content requirements remain
trusted declarations, not proof of arbitrary future LoadLibrary or asset requests.
Export checks source-module bytes against the kit and inspects module compatibility
in the disposable SDK worker. Conflicting DLL filenames with different bytes fail;
identical shared Flecs dependencies are copied once beside the executable.

`forge.standalone.json` version1 records Development profile, target, engine build,
startup Scene AssetId, validated game defaults, derived asset/revision/dependency
inventory, native module provenance and every physical file hash/size. The existing
AssetCatalog remains the sole logical graph. Startup admits this manifest and its
content before opening the game window or loading gameplay modules. Runtime
profile, executable inventory membership and native deployment provenance are
cross-checked against the actual delivered configuration and file digests. Recognized
linked engine module declarations require no external library. Exported project
configuration is read-only; saves/settings/logs use OS user-data storage.

Output must be separate from the source and all selected kits. A private sibling
control directory holds a cooperative writer lock, staging and the narrow export
journal. A valid existing output is never modified in place. Once the candidate
passes admission, a durable journal precedes renaming the previous output aside
and promoting the candidate. Recovery restores the previous output if promotion
was interrupted before the new destination appeared; completed promotion remains
committed. This is recoverable directory replacement, not a claim that two renames
are one atomic filesystem operation. Unrecognized or externally modified output
is preserved with a diagnostic. Cancellation ends at the promotion boundary.

The user-selected output root is canonicalized at admission, consistently with
ProjectPaths, so Windows short-name aliases resolve to the same destination and
recovery journal. A destination that is itself a symbolic link is rejected.
Internal control, candidate and metadata paths retain unredirected-path checks.

Local service tests use a synthetic executable to validate assembly, configuration,
relocation, cancellation, corrupt-kit rejection, unrelated-directory protection and
interrupted replacement. They do not establish native DLL or graphical acceptance;
Windows runtime-kit/shared-SDK/relocation and captured editor workflows remain
separate required delivery gates.

## Current runtime-family dispositions

| Family | Export representation |
| --- | --- |
| Scene / Prefab | Validated authored bytes and reflected typed closure; no flattening |
| Model / Mesh / imported Skeleton / AnimationClip | Whole selected immutable model family, stable subasset IDs and provenance |
| Material | Selected cooked material and required texture bindings; shader preparation data embedded in the cooked revision |
| Texture | Selected cooked variant(s); admitted raw TGA additionally when used by UI |
| Shader | Selected target-labelled DXBC artifact; authoring HLSL/includes stay build inputs |
| Legacy standalone Skeleton / AnimationClip | Strict admitted Ozz archives, converter provenance and skeleton compatibility |
| Navigation | Admitted baked fnav envelope; build-only source geometry excluded |
| AudioClip | Selected decoded runtime clip envelope; original WAV/import tools excluded |
| UiDocument / stylesheet / font / UI image | Admitted native RML/RCSS/font/TGA sources, automatic plus required declarations |
| Engine primitives/materials/fallback textures | Reserved engine identities supplied by the matching compiled runtime |
| Default runtime UI font | Runtime-kit resource and license, included in physical inventory |
| Flecs Script | Authoring input, not an authorized runtime script-file loader; an explicit runtime edge to unsupported script content rejects |
| Unknown/opaque component data | Retained source data; export requires the admitted owning module/schema/adapter and declared references |
| Any other type | Explicit unsupported-type rejection, never silent omission |

These are finite current formats. Runtime render-resource requests still support
their existing typed subset; packaging a family does not invent a generic loaded
resource API for all subsystems. Standalone target support remains Windows/D3D12.

The standalone acceptance fixture also writes an explicitly validated game save
slot and user audio preferences through GameStorage, then restarts after a second
installation move and verifies both. Saves, settings and runtime.log must remain
outside the installation. This checks the existing host storage API; it does not
represent gameplay-module session/save access as implemented.

Runtime/module kit installation selects CMake CMP0207 NEW to normalize Win32 path
separators before filtering System32 dependencies. This packaging helper requires
CMake4.3 or later (the validated tool is4.4.3). Missing application DLLs remain
errors; operating-system libraries are left to the supported Windows installation.
The exact upstream behavior is documented by
[CMP0207](https://cmake.org/cmake/help/v4.4/policy/CMP0207.html).

The editor's Content catalog may include read-only discovered Scene/Prefab records.
That view is not a persisted revision token. Declaration drafts capture the actual
saved index and may supply requested discovered documents to the shared operation.
It validates their bounded authored bytes and existing identity, reconstructs clean
records, rechecks their hashes and publishes registrations plus declarations in one
catalog save. Unrelated discoveries are never copied wholesale; failed/stale requests
preserve the saved catalog. Existing registered records remain authoritative.
