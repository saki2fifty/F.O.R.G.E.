# Asset source file operations

**Phase7 implementation in progress.** The private application operations described
here have portable tests; Content menus/job integration and full source-format
coverage remain in progress. This is not a public plugin ABI or scene Undo system.

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

Plans report catalog graph dependents and explicitly warn that this is not complete
reference coverage of unopened authored documents or opaque plugin data. Full editor
delete-impact scanning and confirmation remain required before exposing these actions.
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
