# Asset source file operations

**Phase7 implementation in progress.** The private application operations described
here have portable tests and Content menu/job integration. Full source-format
coverage and native Windows acceptance remain in progress. This is not a public plugin ABI or scene Undo system.

## Identity and typed preparation

`prepare_asset_file_operation` captures exact source, sidecar and catalog bytes for
Move, Duplicate or Delete. Moves retain the root and complete member identity family.
Duplicate allocates a new root/member family through `duplicate_subasset_identity`,
remaps known catalog dependency edges and retains unknown payloads unchanged. It does
not copy a selected cooked binding as if it belonged to the new asset. Imported
copies require their own validated publication before runtime use.

Scene copies use the existing `duplicate_scene_asset` remapper with a reserved NEW
AssetId, allocating new EntityIds and remapping understood intra-scene references.
Prefab copies use `PrefabDocument::duplicate`; Material/Shader copies preserve their
opaque source fields while replacing the known source-owned identity. No arbitrary
UUID-looking string search/replacement is performed. Source format adapters must
validate their own identity and locator conventions; absent adapters fail clearly.

Plans report catalog graph dependents. The asynchronous file service additionally
inspects unopened JSON sources, Material base/texture bindings, structured prefab
instances, the project startup scene, and a detached current scene draft. Component
references follow the copied native Meta projection recursively through structs,
arrays and vectors; EntityRef records the target scene AssetId. Opaque data is listed
as uninspected, never searched/replaced as UUID-looking strings. This is deliberately
not a claim of complete opaque/plugin reference coverage.
Directories must exist; destinations/sidecars must be absent. An imported member is
operated on through its owning source. Source suffix changes are not format conversion.

## Durable source/catalog coordination

`AssetFileTransaction` uses the same durable IO extracted from `AssetPublisher`.
It runs on its owning asset-operation worker while retaining the project writer
lease. The caller must exclude competing editor writes for that operation. Prepared
sources/sidecars precede one exact `forge.assets.json` commit point. Every input is
rechecked against its captured byte digest before mutation; each affected path is
checked again immediately before its write. This is not a sandbox against unrelated
host processes changing the filesystem concurrently.

The private recovery record is `.forge/asset-file-operation.json`. Verified old/new
blobs and an operation manifest are staged under `.forge/asset-file-operations/UUID`.
Before the catalog commit, interrupted work restores old files. After the commit,
recovery retains the complete new state. Conflicting external bytes or corrupt backup
blobs stop recovery without being overwritten. A successful commit may report a
cleanup diagnostic separately. Delete callers retain the backup folder rather than
irreversibly erasing source data. Automatic scene Undo is not implied by retained files.

Limits: 8,192 affected locators, 512 MiB per source/blob, 2 GiB aggregate prepared
before/after bytes, 4 MiB recovery metadata and the existing 64 MiB catalog limit.
These are data bounds, not a measured process-RSS guarantee. Reads, hashing, staging
and durable writes belong on the asset-operation worker. An import refuses to start
while unresolved file-operation recovery exists. Recovery runs before ordinary import
publication recovery; simultaneous recovery authorities are rejected as a conflict.

Tests interrupt real processes at journal/source/destination/catalog boundaries,
cover cancellation and exceptions, stale review inputs, new scene/member identities,
retained backups, corrupt blobs and external conflicts. Windows additionally tests
an open file handle that prevents deletion; physical Windows validation is pending.

## glTF source relocation

The source importer and relocation adapter share GLB admission and URI decoding.
Relocation rebases only known buffer/image relative URIs through `ProjectPaths`,
including percent-encoded filenames and moves to the project root. It keeps data
URIs, opaque JSON fields, BIN payloads and unknown GLB chunks unchanged. GLB JSON
padding and total/chunk lengths are rebuilt when needed. A same-folder rename
retains the exact source bytes. This is source preparation, not full mesh admission
or successful reimport; ordinary importer validation still applies afterward.

Evidence: exact selected Khronos glTF specification commit
`c18432787e6d545a1218c1926ccdcfaffd4c116b`, URI and GLB chunk sections; no dependency
pin was changed. The shared FORGE profile still rejects unsupported absolute/network
URI forms rather than broadening source access for a move operation.


## Job and editor ownership

`AssetFileService` runs preparation, reference scanning and durable IO on one joined
worker at a time. It exposes Preparing/Review/Committing and terminal receipts to its
owning application thread. Review is a real pause before source mutation. Commit
re-scans JSON inputs and refuses changed/new/removed reference sources, then uses the
transaction's exact source/sidecar/catalog checks. A successful receipt is prepared
before the commit point so later catalog IO cannot misreport it as a rollback.

Reference scan limits: existing source-discovery bounds, 64 MiB per JSON source,
256 MiB aggregate JSON source bytes, 1,048,576 inspected nodes, depth32 and 65,536
matching references/opaque coverage entries. Documents are inspected individually;
unknown malformed JSON is reported as uninspected. Malformed recognized authored
sources block preparation. These are payload limits, not a process-RSS guarantee.

A discovered Scene/Prefab may be explicitly indexed as a precursor using its existing
validated source UUID. Cancelling the later file review does not undo registration.
It does not allocate an identity or modify source bytes. Current file operations
require existing destination directories; backup restore UI and additional source
adapters remain outstanding.

Content suspends and cancels/drains automatic reimport before starting its worker.
It excludes other editor writes through the shared file-busy state, rejects pending
conversion/build jobs and unresolved source drafts, and blocks project switch/quit
until the operation is closed. Scene Save and recovery autosave honor that exclusion.
Catalog removal prunes queued automatic imports. On resume the watcher rescans.
Moving the active scene adopts its new locator without resetting history or invoking
Save As identity duplication. A dirty draft writes recovery at the new locator before
its old recovery snapshot is removed; a failed recovery write retains the old snapshot. Deleting the active scene requires opening another scene.
Independent external programs are still outside cooperative project writer ownership.

Atomic storage stages bytes in a uniquely named sibling (`.UUID.pending`) before
replacement. It does not append to the destination basename: valid hashed blob
names and deep project roots must not acquire avoidable staging-name length.
Exclusive creation, file flush and same-directory replacement remain required.
The Windows regression includes a valid destination near its traditional path
bound; broader long-path behavior still depends on the particular platform API.
