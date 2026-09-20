# Source discovery and observation

`scan_asset_sources` is a bounded, read-only full scan using `ProjectPaths`.
It serves `forge_tools --assets scan`; the editor/native watcher adapter and
automatic catalog/sidecar move transactions are not connected yet. Scanning does
not assign logical asset identity or claim importer support from a suffix.

## Inventory and limits

Inputs are an explicit project root, relative scan roots (default `Assets`),
relative ignored prefixes, and file/directory/depth/byte budgets. Entries are
traversed in deterministic path order. Every admitted file is hashed with SHA256;
unchanged timestamps and sizes do not bypass content verification. Reads check
size, timestamp, file identity and resolved locator before/after observation.
Filesystem mutation is not globally locked: this is an observation, not a publish
permit. The importer/publisher must recapture its own input revision set.

Only regular files are read. Dot/temporary/backup names and Windows hidden/system
attributes are filtered. Canonical paths must stay in the project. Directory
identities prevent contained symlink/junction cycles and duplicate traversal;
hardlink/file aliases are retained with diagnostics. Roots may overlap. A missing
Assets root is empty; an entry disappearing during enumeration makes the result
incomplete. Diagnostics are capped at256, while the incomplete flag is retained.

Defaults:100k files,10k directories,64 levels,512MiB/file and8GiB total bytes read.
Caller overrides remain bounded. Cancellation is checked between entries; a single
bounded file read/hash is synchronous. No editor-main-thread polling is authorized
by this API. A later watcher should debounce notifications and dispatch scans to
the content worker, with manual/full-rescan recovery.

## Change tracking

`SourceChangeTracker` consumes complete snapshots on its owner thread. It advances
a generation immediately when changed content/paths or scan completeness is
observed, before its default200ms debounce. Duplicate observations do not prolong
the debounce. Drain reports net creates/modifications/removals/move candidates;
failed/partial scans block delivery and cannot imply deletion. Recovery compares
against the last complete delivered baseline. Generation exhaustion rejects.

A move candidate requires a unique OS file identity in both snapshots, unchanged
content, and disappearance of the previous path. Copies with equal content and
ambiguous hardlinks are not matched. Same-content atomic writes refresh identity
evidence without scheduling a content rebuild. OS identities can be reused; they
are session observations, not persistent AssetIds or sole authority for automatic
catalog relocation. Persistent sidecar/catalog reconciliation still owns that
decision. A write acknowledgement suppresses only a matching create/modify digest,
never an intervening different external edit, removal, or move. Generations still
advance so an old import cannot gain permission from a suppressed notification.

## Platform evidence

Windows uses volume plus128-bit `FILE_ID_INFO`, obtained through
[GetFileInformationByHandleEx](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getfileinformationbyhandleex).
Handles permit concurrent read/write/delete and close after inspection. Linux uses
`stat` device/inode pairs. Neither value is written into project metadata or exposed
as persistent logical identity. API verification:2026-09-20. Platforms/filesystems
that cannot provide an observation return a structured incomplete-scan diagnostic.

## CLI boundary

`--assets scan PROJECT [ROOT]`, `query PROJECT`, and `dependents PROJECT UUID`
share product services. They emit JSON, succeed with exit0, fail/incomplete with
exit1, and never acquire a writer or change project files. Query/dependents report
the registered graph only; they do not assert complete authored-reference coverage.
OS file IDs are deliberately omitted from JSON. The existing authoring stdio
protocol remains memory-only and unchanged.

`--assets source-dependents PROJECT LOCATOR` queries primary and additional raw
source edges in the same graph, returning direct consumers and their affected
transitive dependents. Missing source files remain queryable by their locator.
