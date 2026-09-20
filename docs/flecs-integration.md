# Flecs integration contract

Flecs **4.1.6**, exactly `fb55f3c25660425cfe1bc4cf5e6bff8b3f18a9b8`, is the
technical contract. Follow the [dependency evidence policy](dependency-policy.md).
Implemented and verified in **Build 260919-000062**, source
`8bfaa4057660286dd503bd9fc62265dc350a7a84`.
[Clean Windows/Linux validation](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/35445250161)
passed all six jobs. This contract records adopted capabilities, native SDK
availability, measured non-adoption and the approved narrow upstream exception.
It does not claim every native mechanism needs its own editor control. No Phase 7.

## Ownership

| Responsibility | Owner |
| --- | --- |
| Component storage, queries, relationships, inheritance, lifecycle, systems | Flecs |
| Structural reflection, member entities, friendly names/help, units, numeric ranges | Flecs Meta / Doc / Units |
| Reject-before-commit and cross-component/domain rules | FORGE validation |
| Persistent EntityId / AssetId, authored files and unknown payload preservation | FORGE |
| Binary lifetime, supported SDK, role and provider permissions | EngineModule |
| ECS registration beneath EngineModule | Flecs modules |
| Master clock, process isolation, recovery policy | FORGE runtime |
| Physics, audio, animation, navigation, runtime UI and rendering resources | Their existing domain libraries through FORGE modules |

## Reflected authoring metadata

All built-in authored component types explicitly create member entities
(17 with the Phase7 MeshRenderer and ModelSource CPU components).
Their structure and scalar types come from Meta, friendly names and help from
Doc, applicable physical dimensions from Units, and bounds from
`EcsMemberRanges`. Quaternions and scale are dimensionless; quaternion fields
are not mislabeled as angles. Kilograms-per-cubic-meter is a FORGE unit registered
through the native Units API because that exact compound unit is absent from the
standard module.

The shared Inspector, Add Component menu and schema API consume the derived
projection. FORGE annotations retain only engine meanings such as persistent
property IDs, asset types, default values, categories and editable/serialized
policy. Persistence codecs preserve existing authored formats and unknown data.

Detached file validation uses an immutable projection of the same registered
metadata. Normal EngineContext initialization seeds this catalog; ordinary
commands create no validation worlds. A standalone file validator that runs
before an engine exists initializes a short-lived metadata world once. It has
no authored content or gameplay state. Runtime subsystem admission reads the
registered scalar metadata directly, then performs its domain checks.

Ranges are **not mutation vetoes**. Trusted native code can write an invalid
value. FORGE rejects invalid authoring candidates before mutation and rejects
invalid subsystem configurations before realization. Advisory ranges do not
reject supported values: audio gain above 1, up to 4, is valid amplification.
Audio distance ordering, physics ancestry/scale, quaternion normalization,
asset compatibility and other domain rules remain explicit.

### Member-entity choice

Global `FLECS_CREATE_MEMBER_ENTITIES` remains off. FORGE opts in per type so Doc,
Metrics and Alerts have actual member targets. A Linux Debug synthetic probe of
15 reflected types with four scalar members each measured approximately
1,983,174 bytes with explicit registration versus 2,336,594 bytes globally, and
341 versus 434 tables. These are metadata-workload measurements, not editor
performance claims. Global registration also affects upstream/internal types.
Exact SDK types that need member-targeted tooling must explicitly register those
members; ordinary reflection does not require globally enabling the option.

Pinned evidence: [struct registration](https://github.com/SanderMertens/flecs/blob/fb55f3c25660425cfe1bc4cf5e6bff8b3f18a9b8/src/addons/meta/type_support/struct_ts.c),
[Meta](https://github.com/SanderMertens/flecs/blob/fb55f3c25660425cfe1bc4cf5e6bff8b3f18a9b8/include/flecs/addons/meta.h),
[Units](https://github.com/SanderMertens/flecs/blob/fb55f3c25660425cfe1bc4cf5e6bff8b3f18a9b8/include/flecs/addons/units.h).

## Stable-version exceptions

| Feature | Disposition | Development introduction |
| --- | --- | --- |
| `ecs_type_hooks_t::on_validate` | **UNAVAILABLE IN PINNED STABLE VERSION** | [9617b0d1744da1ee117176457f051dbdda78855e](https://github.com/SanderMertens/flecs/commit/9617b0d1744da1ee117176457f051dbdda78855e) |
| Native Meta map reflection / `EcsMapType` | **UNAVAILABLE IN PINNED STABLE VERSION** | [6791075](https://github.com/SanderMertens/flecs/commit/679107572bc46a3ccadee99265f9422797583562) |
| Script template/struct declaration inheritance | **UNAVAILABLE IN PINNED STABLE VERSION** | [dcccf90](https://github.com/SanderMertens/flecs/commit/dcccf90f2e26065bdb02d93a57dfa64b7dc8d869) |

No upgrade, backport, source patch or substitute API is included. Re-evaluate
when an official stable release includes them. The development validation hook
suppresses `on_set`/`OnSet` after failure; it does not restore the old value.

## Hierarchy, references and prefab ownership

`ChildOf` remains the dynamic structural relationship. Scene roots are native
children of an internal scene membership entity with `OrderedChildren`; that
container is not an authored entity and has no transform or persistent EntityId.
Authored root parent remains empty in scene files/API. Native SDK code must not
interpret every nonzero native parent as an authored parent.

Sibling order comes from native `OrderedChildren`, projected into the existing
entity-array ordering for persistence. `entity.reorder` changes sibling order
without reparenting and participates in scene history. Shift-drag places an
ordinary entity before a sibling. Native `ecs_set_child_order` emits no OnSet in
this pin, so the scene compares an allocation-free derived order fingerprint
when checking its revision. It is not a second mutable order store.

Structured prefabs retain immutable compiled revisions, `Parent` interiors,
`IsA`, member mappings and independent Flecs component inheritance. Published
source member order determines instance member order; dynamic attachments retain
their own order. Candidate publication resolves containers, attachments and
sibling permutations before its durable write. Failure preserves live instances.

`OnInstantiate` policies continue to distinguish inherited authored values,
owned spatial binding, and non-inherited identity/derived state. Native slots
and nested native prefabs remain SDK mechanisms; they do not replace durable
PrefabMemberId mappings, missing-member diagnostics, equal-value override intent
or property-level persistence. Native entity IDs/generations and Doc UUID
metadata do not replace FORGE's EntityId/AssetId.

## Queries, notifications and caches

Transform collection now reuses world-owned queries. The derived WorldTransform
query is cached. The inherited LocalTranslation query is persistent but uncached:
with automatic caching, the stable pin asserted in `query/cache/group.c` during
FORGE scene/prefab teardown. Keeping this query uncached preserves the observed
lifecycle contract without patching Flecs. Re-evaluate the exact reproduction on
a later stable release; do not silently enable caching from live examples.

Native queries own ECS matching, including relationship/variable/traversal
expressions. FORGE keeps spatial transform mathematics, persistent reference
resolution and external resource indexes: these are not alternative query DSLs.
The advanced tool uses native Query Language, paging and JSON. Registered
component insertion uses this world's native ID; arbitrary authored component
names containing literal dots require native escaping in text expressions.

Native change detection requires a cached query with detection enabled. It is
table-level and does not discover raw writes through `get_mut` until the writer
calls `modified`. Sparse-only queries in the pin fall back to uncached matching.
FORGE's revision observers remain necessary for authored changes, inheritance,
relationships and invalidation across command boundaries. They only mark state;
external libraries synchronize at their existing subsystem phases. Existing
render retention already skips unchanged scene/camera work. The isolated scan
benchmark below is not evidence that replacing these broader revision contracts
would improve real editor behavior.

OnAdd/OnSet/OnRemove and wildcard matching drive internal revision invalidation.
Monitors, synchronous/custom events, queued events and yield-existing queries are
available to exact-SDK ECS work. OS events, IPC, import jobs and device callbacks
retain their existing owners and are not repackaged as ECS events. C++ lifecycle
registration continues to supply appropriate constructors, destructors, copies
and moves. Resource providers are not moved into arbitrary component hooks.
`on_replace` is available but is not installed on ordinary mutable authored
components: its mutation-access restrictions differ from candidate validation.

## Storage decisions and measured workloads

The built-in scalar/TRS components remain dense. No built-in requires a stable
component address across archetype moves. World-owned providers and external
resource handles keep their existing ownership. `Sparse` is available to exact
SDK types that need its address/lifecycle contract. `DontFragment` is available
for supported high-cardinality relations; it is not applied to inheritance,
spatial parenting or the low-cardinality scene membership relation merely to
reduce a synthetic table count. Its query/inheritance/monitor restrictions must
be checked against the pin before adoption.

Linux measurements, 2026-09-19: three-run medians, 20,000 entities, optimized probe
linked to the exact **Debug** Flecs library with Script Math enabled, no rendering or external subsystem
work. These are comparative probes, not shipping throughput promises.

| Workload | Tables after churn | Flecs bytes | 100 sequential scans |
| --- | ---: | ---: | ---: |
| Dense three-double component | 558 | 4,526,195 | 2.320 ms |
| Sparse same component | 557 | 4,564,277 | 126.271 ms |
| Dense plus 1,000 relationship targets | 2,557 | 33,619,087 | 21.123 ms |
| Non-fragmenting relationship targets | 559 | 19,460,973 | 2.476 ms |

On dense unchanged data, 100 cached change checks took about 0.010 ms versus
2.320 ms for scanning. Sparse-only change checks are unsupported, not a zero-cost
alternative. Creation of the 20,000-entity non-fragmenting case was slower
(89.137 ms versus 58.585 ms), despite cheaper subsequent scans/churn. Global
trait changes therefore are not justified by table count alone.

### Authored enabled fields

Existing `enabled` fields retain domain/persistence meaning. AudioListener
participates in the exactly-one-listener selection/muting contract. Animator
retains playback state while advancement is disabled. NavigationAgent stops its
controller without removing configuration, and NavigationSurface selects geometry
for a detached bake. UiDocument controls document realization separately from
visibility and preserves its authored settings/recovery contract. None is
silently converted into an unpersisted native toggle, which would lose independent
prefab intent and be invisible to detached snapshots/bake inputs.

Native `CanToggle` is available for SDK components whose only meaning is query
participation. The first toggle installs its native toggle storage; subsequent
enable/disable operations preserve the table. Tests cover query exclusion while
keeping the component value. Built-in Inspector headers do not advertise a
native toggle that their authoring schema cannot persist.

## Systems, stages, threading and time

FORGE retains the runtime-owned fixed clock. Flecs owns the fixed pipeline's
systems, phases, dependencies, iteration and merge points. Runtime orchestration
owns input packets, subsystem provider ordering, process recovery and presentation
publication. Gameplay ECS mutations use Flecs deferring/stages; IPC and asset
queues remain ordinary application services.

Native Timer systems are admitted to the fixed pipeline before input/gameplay.
Intervals, timeouts and rate filters advance with fixed ticks, stop while paused,
and advance once on Step. They do not become another wall-time clock. SDK systems
must join the declared fixed schedule to run; imports alone do not admit arbitrary
systems to the runtime pipeline.

An isolated ECS-only 32-operation numeric workload over 20,000 entities took
1,547.100 ms for 100 ticks with one thread and 412.611 ms with four. At 1,000 entities,
82.811 ms versus 42.120 ms. This supports native multithreading for deliberately
parallel ECS-only SDK workloads, not globally enabling four threads. Diligent,
physics adoption, audio control, animation contexts and navigation provider calls
keep their owner-thread contracts. Every entity in the threaded probe was checked against a sequential numeric reference. No default scheduling policy changes here.

## Modules and process-wide facilities

Built-in ECS registration imports Flecs modules beneath EngineModule. The latter
still validates role, dependency order, permissions and binary lifetime. Providers
stop before worlds and callbacks are destroyed. Shared SDK processes use one
shared Flecs/core host and a fingerprinted build configuration. Script Math is
now compiled consistently in static/shared profiles and exported SDK headers;
its build option participates in the exact SDK fingerprint.

Entity ranges must be reserved before any deletion/recycling in this pin; current host worlds use ordinary allocation. Standalone native-host tests exercise reservation, activation, recycling and manual generations without implementing replication or partitioning. Entity ranges, manual generations, bulk IDs and low-level table/record APIs remain
advanced process-local mechanisms. They must not allocate persistent UUIDs or
bypass authored identity checks. World locking/table locking does not authorize
external subsystem calls on arbitrary threads. Native frame APIs, target-FPS
support and App remain available to standalone native clients; FORGE's SDL editor
loop and fixed runtime clock retain frame ownership.

Flecs logging uses the existing OS API. Tools route diagnostics away from JSON
protocol output; this includes the pin's JSON parser directly writing backtraces
to stdout. Allocator/thread/time hooks remain process-global defaults. Script
workers install a file-open hook before world initialization. No per-world
replacement allocator, dynamic-library loader or application-loop framework is
introduced. Optional Journal/PerfTrace remain off in normal builds.

## Script source and candidate ownership

`.flecs` files are AssetId-backed source assets, opened centrally through the
asset/document registries. Their Save/history/close owner is distinct from the
scene. The worker uses the complete pinned native parser/evaluator, imports native
Script Math, and applies ProjectPaths to relative includes. It accepts no remote
include or arbitrary machine-file paths. Source/total-input/file-count, process
time and memory limits bound evaluation.

Each Apply constructs an isolated Preview world, reconstructs the previous source
when supplied, then calls native managed update. Native update itself is not
transactional: this pin clears managed content before evaluation. FORGE publishes
only a successful inspection result and retains the previous result on failure.
The worker exits afterward. Generated entities, including managed included/template
content, do not become authored scene rows. Includes are read from current project
files for each evaluation; this is not a persistent live simulation or a compiled
asset-revision cache. Native gameplay scripting remains an exact-SDK responsibility.

Simple built-in entity recipes keep their existing registered metadata and
validated scene commands. Flecs templates are the available language for advanced
procedural preview content; there is no second proprietary recipe language.
Future integration of generated content into durable scenes needs explicit
identity/publication/history semantics and is not implied by preview evaluation.

## JSON, diagnostics and development surfaces

ECS inspection uses native entity/query/world JSON. Native world export contains
internal/nonserializable entities and is diagnostic data, not a guaranteed
round-trippable FORGE save. The headless inspection tool's strict import attempts
a separate Preview candidate; a reported deserialization error keeps the previous
inspection world. Successful import stops its previous listener. Scene codecs,
unknown payload preservation and domain validation remain independent.

Stats/Metrics/Alerts register in a consistent order at WorldContext startup, before content allocation. Their C addon tags retain process-global IDs; importing them late in different worlds can collide with existing allocations. Tool definitions, sampling and REST remain explicitly activated. Tools → ECS creates its metric/alert definitions on demand. Native member-range alerts and
a native CounterId authored-entity count provide real diagnostic consumers. Alert instances feed
Problems and are resolved when the native condition clears. Diagnostic systems
receive explicit diagnostic ticks; they do not progress the authoring world's
gameplay pipeline or its timers. Memory/count values include imported diagnostics.

REST is off by default, opt-in, loopback-only and stoppable. A thin read-only
admission callback forwards allowed requests to the native REST dispatcher.
Entity/query/world/component/table/statistics inspection is allowed; mutation,
script execution and command capture are refused. This preserves the normal
FORGE authoring validation/Undo boundary. The official Explorer launch URL selects
this local endpoint. The hosted browser client is not pinned with the executable;
its compatibility and browser local-network permissions must be tested separately.
Neither editor REST startup nor advanced tool windows become shipping game defaults.

The native Metrics interface exposes Gauge, Counter, CounterIncrement and CounterId with validated source/kind choices and session-owned removal. Presence duration uses native Counter semantics, not a separate duration metric type. Native world/system monitor systems receive diagnostic samples and native period reductions for REST; FORGE does not reimplement their aggregation. Authoring inspection does not manually sample/reduce pipeline histories: it never executes the gameplay pipeline, and pinned 4.1.6 performs an invalid zero-length memcpy when reducing an empty pipeline. Runtime pipeline statistics remain native runtime data. The tool plot reads a native statistics ring buffer.

Pinned JSON deserialization consumes native entity member order. The headless protocol retains ordered JSON for native import/export instead of alphabetically sorting keys. Full diagnostic world exports may contain nonserializable internal values and are not promised to round-trip. Tests use a valid ordinary reflected entity and verify that a rejected replacement retains that content.


### Approved pinned managed-include exception (2026-09-19)

**Known bounded upstream buffer leak on failed managed file includes. The minimal
current reproducer leaks 18 bytes.** That is not a maximum: the missed allocation
is the failed included file's loaded source buffer. File size, include count,
total source reads, worker memory and execution-time limits remain enforced.

Pinned version/commit: Flecs4.1.6,
`fb55f3c25660425cfe1bc4cf5e6bff8b3f18a9b8`. The affected path is native managed
file-include evaluation failing inside the filename-based `ecs_script_init`,
which skips its buffer cleanup. Upstream correction
[072f366f6c81f8757b354ff90f6811078a105dad](https://github.com/SanderMertens/flecs/commit/072f366f6c81f8757b354ff90f6811078a105dad)
is outside this stable revision and is not adopted.

FORGE owns the top-level source buffer, captures native evaluation errors and
checks every managed script before publication. One candidate evaluation owns
one disposable process. A failed candidate exports no generated entity snapshot,
does not replace the previous source/result, and terminates; the supervisor waits
for exit and removes temporary staging. Native managed includes remain supported.

The exception applies only to that exact pinned buffer-allocation path. It does
not permit other leaks, crashes, invalid accesses, undefined behavior or memory
corruption. No vendor patch, alternative include parser or LSan suppression is
used. All FORGE-owned and unrelated integration sanitizer tests remain strict.

`flecs_script_known_include` is a separate, explicitly labeled regression. In an
instrumented build it requires the exact native allocation stack, expected file
buffer size and a single leak allocation, checks rejection/publication/exit/cleanup
and subsequent success, and retains the actual sanitizer output under
`Testing/flecs-script-known-include`. It exercises both the minimal file and a
larger included source. Different findings and unexpectedly clean behavior fail
this test. In normal profiles it also exercises the production bounded supervisor.
The instrumented direct-worker fixture bounds resident memory because ASan shadow
mappings cannot fit the production virtual-address-space limit; it is not a claim
that those two memory measurements are equivalent.

Report clean product validation and **EXPECTED pinned upstream LSan finding**
separately. Never describe the combined outcome as globally clean LeakSanitizer.
When a future official stable revision contains the fix, verify that exact source,
require this reproducer to become clean, remove the exception and move its cases
back into the ordinary clean suite. Never carry this exception forward automatically.

## Verified delivery

Windows and Linux static profiles passed **35/35** each; exact shared SDK profiles
passed **44/44** each, including installed/relocated consumers. Windows editor
controllers passed **2/2**, and renderer/subsystem/process tests passed **36/36**.
All **55** prior non-editor render fixtures are byte-identical to Build 61; **23**
actual-editor captures were reviewed, including ECS tools and Script at 200%.

Separate clean local ASan/UBSan/LSan suites passed **34/34 static, 42/42 shared,
and 3/3 editor**. The dedicated managed-include regression produced its **EXPECTED
pinned upstream LSan finding**, at 18 and 4118 bytes for the two source sizes,
with no unrelated finding. No suppression; this is not a globally clean LSan claim.

ZIP integrity, **184 file hashes**, compiled build identities, the matching manual,
dependency licenses, relocated runtime UI/navigation/converter startup and both
SDK manifests (**237 Linux / 246 Windows files**) were verified. Physical Windows
GPU/DPI/input and hosted Explorer/browser acceptance remain separate. Existing
960×640/200% layout clipping is still a documented stress limitation.

## Explorer compatibility contract

Decision D, verified2026-09-19: the hosted official Explorer is **best-effort external
tooling**, not a pinned shipped dependency or guaranteed4.1.6 client. Its source and
REST expectations can drift. FORGE does not falsely version-check a mutable URL.
The native ECS World Inspection tabs and bounded CLI inspection route use the exact
compiled pin and remain the supported inspection path. They do not depend on browser
network policy or the website. Local authoring REST remains opt-in, loopback-only and
read-only; native upstream edit endpoints are not exposed as authoring transactions.

## Explicit member metadata policy

Keep explicit member entities for FORGE's reflected authored types. Fresh measurements
showed lower entity/table/memory overhead than global FLECS_CREATE_MEMBER_ENTITIES,
while native Doc/Units/MemberRanges/Metrics/Alerts behavior passed. This is selective
adoption, not disabling those native capabilities. Member registration preserves
physical layout and verifies offsets/extents before codecs use them; see
[FLECS-003](flecs-known-issues.md#flecs-003--member-entities-drop-explicit-zero-offset-intent).
Debug builds enable native FLECS_EXCLUSIVE_ACCESS automatically through FLECS_DEBUG;
Release does not. Journal and PerfTrace remain disabled unless deliberately configured.
