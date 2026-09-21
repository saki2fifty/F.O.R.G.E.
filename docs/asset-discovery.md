# Source discovery and observation

`scan_asset_sources` is a bounded, read-only full scan using `ProjectPaths`.
It serves `forge_tools --assets scan` and the editor polling watcher. Automatic
catalog/sidecar move transactions are not connected yet. Scanning does
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
bounded file read/hash is synchronous. File reads/hashing run on the polling worker; the owner thread receives copied
observations and debounced changes. `include_project_root` permits traversal of
project-contained sources outside Assets without making the project root a valid
asset locator. `ProjectPaths::resolve(".")` remains rejected.

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

## Texture import command

Asset-tool builds add `forge_tools --assets import PROJECT SOURCE [OVERRIDES_JSON]`.
SOURCE is a project-relative supported texture source. The optional JSON object
supplies explicit typed setting overrides; unspecified settings retain their saved
intent. The command acquires the existing project writer lease and uses the same
import service/provider as the editor. An editor already owning the project prevents
an unsynchronized CLI write. The authoring stdio protocol remains memory-only.

Success returns the Texture AssetId, build key, verified-cache-hit flag and any
post-commit cleanup diagnostic. Failure returns the existing structured JSON error
and nonzero exit code; prior catalog/sidecar selections remain intact. The process
wait is bounded. The packaged worker is resolved beside the actual running executable,
including PATH launches from other working directories. Projects can be relocated
and rebuilt from sources/sidecars/catalog with an empty disposable cache.

## Editor watch and automatic reimport

`AssetSourceWatch` owns one asynchronous scan, coalesces rescan requests, debounces
through `SourceChangeTracker` and cancels/joins before destruction. The default
interval is two seconds after completion; callers may select 100ms–10min. Before
the initial observation it is incomplete. Failed scans never generate deletions.
A self-write receipt remains pending when an overlapping scan observes another
path first; only an actual matching create/modify is suppressed.

`AssetReimportService` uses the sealed importer routes and existing shared import
service/publisher. It only schedules registered root assets, with one active job
and at most 64 queued candidates considered per owner poll. Build edges order
dependent work. Root/additional-source digests, committed sidecar digest and exact
importer/target revision determine whether observations require rebuilding. Dirty
document guards defer work. New generations cancel superseded work before debounce;
the publisher independently verifies captured inputs again at commit.

Successful publications notify resource/document consumers and enqueue graph
dependents. Failed candidates retain the catalog and cooked selection and report
diagnostics. Manual retry clears failed work; unchanged startup observations do
not needlessly publish new generations. Full scans/hash copies are bounded by
existing scan limits, not a claim of constant-time or production-scale throughput.

Snapshot lookup and self-write acknowledgements use `ProjectLocatorLess`, matching
the project locator contract: ordinal case-insensitive on Windows, exact on POSIX.
A differently cased Windows locator does not imply a missing source or a different
asset. This is an ephemeral index convention, not a rewrite of source paths or IDs.

Content's optional scene discovery is separate from reading the committed catalog.
If a scene is invalid or discovery exceeds its bounded read budget, the browser
keeps the last complete discovered-scene additions and still adopts valid registered
assets. A newly registered locator/identity takes precedence over retained discovery
metadata. This projection creates no new identity and writes no project files.

Windows locator equality compares generic separators before ordinal case folding,
so a caller-created forward-slash path and an OS-normalized backslash path do
not create duplicate source rows or miss publisher acknowledgements.
