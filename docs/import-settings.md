# Importer registry and settings

**Phase7 infrastructure; production importer/editor integration is in progress.**
These APIs do not currently register a shipped Model/Texture/Shader importer or
expose an import dialog. Native model preparation is described separately in
[glTF admission](gltf-admission.md). The [asset foundation](asset-foundation.md)
still governs worker, publication and runtime ownership.

## Registration and selection

`AssetImporterRegistry` owns immutable importer declarations and shared provider
lifetimes. Register at owner startup, then seal before querying source candidates.
There is no removal/hot-unload operation. A selected job retains its provider even
if the registry owner shuts down; the job must finish/join before its native code
can be unloaded. This C++ tooling interface is tied to the exact source/SDK build;
it is not a stable external plugin ABI or an ABI1 extension.

Declarations include stable ID and implementation revision, source suffixes/kinds,
produced logical types, output format/version, settings schema, determinism,
platform/backend/profile combinations, diagnostic version and execution budgets.
Each provider supplies source probing, dependency/build-plan discovery,
decode/process/cook, and artifact validation operations. Those operations have
separate responsibilities even though decoding/processing/cooking are encapsulated
inside one native provider call; no generic runtime object format is imposed.
Registry registration does not invoke build operations or mutate a catalog.

Probe prefixes are at most64KiB. Probes are trusted lightweight recognition code,
not a place to run full third-party decoding. Suffixes are ASCII case-insensitive;
source admission still validates actual content. Candidates are listed by stable
ID, independently of registration order. Multiple matching providers require an
explicit choice, including when their confidence differs. An explicit unavailable
or unsupported provider fails rather than silently selecting another.

Declarations are bounded to256 providers,128 suffixes/provider,256 source/output
identifiers and256 target combinations. Wildcards apply only when explicitly
declared in a target combination. Worker declarations default to120seconds,
1GiB process memory,512MiB output and4096 files. Registry validation bounds these
requests; the actual supervisor must enforce them before executing a provider.
This registry alone is not evidence of process isolation or budget enforcement.

## Typed importer configuration

`ImportSettingsSchema` describes configuration values for importers. It is not an
ECS/gameplay reflection system; Flecs Meta remains authoritative for components.
Supported setting types are boolean, integer, finite number, text, choice,
string list and string map. Each rule has a label, contextual help, default and
applicable numeric/length/count constraints. Native cross-field validation runs
for defaults and effective candidates. Project-path containment is enforced by
the actual importer/application boundary using ProjectPaths, not by treating every
text value as a path.

Limits include256 fields,1MiB schema labels/choice text and64KiB per document or
effective settings object. Integers stay within±(2^53−1), keeping numeric bound
comparisons exact across platforms. String rules reject embedded NUL. List/map
values have explicit entry/length limits; there is no implicit string-to-number or
number-to-boolean coercion.

## Persistence, intent and migration

`forge.import-settings` envelope version1 stores importer ID, independent
`settings_version`, explicit overrides and preserved unknown envelope fields.
Settings schema versions belong to their importer and are unrelated to build IDs.
This serialization API returns a document value; asset-owner persistence and
transactional filesystem publication remain separate responsibilities.

- Effective values are defaults plus validated overrides.
- Setting a value equal to the default still records explicit override intent.
- Resetting one field removes that override; resetting all removes all overrides.
  These are explicit operations, not automatic cleanup of user data.
- Invalid edits/parses return failure without modifying the old document. An
  invalid known field can be explicitly repaired with a valid replacement.
- Unrecognized overrides remain preserved but block application; they are not
  silently interpreted, discarded, or included as effective settings.
- A mismatched importer/version requires explicit migration. Downgrade and
  importer retargeting are rejected. The provider's migrator must preserve unknown
  envelope data and return a fully validated candidate for the new version.

The effective-settings digest includes importer ID, settings schema version and
canonical values. An equal-value override changes authored intent but does not
change effective build identity. Actual changed values do. The complete artifact
key must also include implementation/source/dependency/output/platform/profile
identity through AssetBuildInput; this settings digest is not a replacement for it.

## Current validation

Portable tests cover rejection, enum/range/type/size constraints, default and
cross-field validity, equal-value intent, reset/repair, unknown retention,
version migration, failed-parse preservation, canonical digest behavior,
registration ambiguity/order, sealed lifecycle and shared provider lifetime.
Production importer selection, settings UI/persistence, actual worker execution,
DDC publication and end-to-end reimport are still integration work.

## Shared worker supervision

The existing asset supervisor now accepts bounded per-job memory, individual-file,
aggregate-output, file-count, wall-clock and CPU-time limits. Existing converter
callers retain their default512MiB memory,16MiB file,32MiB aggregate and30second
wall-time profile. The Import command uses a pre-created flat `output/` directory;
input manifests remain outside that output budget. Symlinks, nested output
folders, special files, excessive entries and oversized output are rejected.
Aggregate disk usage is polled and rechecked at exit; this is not a filesystem
quota or a hostile-native-code sandbox. Independent format/hash admission is
still mandatory after process success.

Commands are fixed; executable and working-directory paths are passed separately,
without shell interpolation. An already cancelled request never launches. Import
jobs may request a bounded cooperative grace period: the owner atomically creates
`cancel.request` as a marker directory, which workers check at safe boundaries.
Cancellation still rejects a successful late exit; after grace, supervision
terminates the worker. Resource/time violations terminate without waiting for
cooperation. Stale cancellation staging is refused.

Windows uses a kill-on-close Job Object, one active process, process-memory and
user CPU-time limits. Linux uses process groups plus address-space/file/CPU
rlimits; these OS counters differ from Windows committed memory/user time. Linux
configures a parent-death kill signal, including a parent-ID race check, and
terminates the process group while its exited leader remains waitable before
reaping it. These are worker lifetime/resource controls, not permission isolation.
The supervisor must retain exclusive child-wait ownership.

The policy fixture verifies actual child OS limits, cooperative/uncooperative
cancellation, late-success rejection, time/file/aggregate/count bounds and flat
outputs. The legacy converter timeout regression remains active. Production
import job manifests, executable/provider dispatch, result provenance, publication
and startup staging recovery are still integration work.
