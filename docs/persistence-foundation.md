# Persistence, scripting and runtime identity boundaries

Architecture contract, preserving current [scene](scene-format.md), [prefab](prefabs.md),
[transform](transforms.md) and [timing](runtime-timing.md) formats. No format migration,
Apply, nested prefab asset, save-game or networking implementation is introduced here.

## Frozen authored identity

EntityId and AssetId are persistent UUIDv4. Scene AssetId is its authored-document identity;
EntityRef=(Scene AssetId,EntityId), AssetRef<T> validates expected type. PrefabMemberId is a
source-member identity with stable instance-member EntityId mapping. Independent owned/inherited
LocalTranslation/LocalRotation/LocalScale remain separate Flecs components; normalized quaternion
rotation is canonical. WorldTransform stays transient derived data. Equal-value and supported
property-level prefab intent remain explicit. Revert removes that intent and belongs to scene
Undo. Unknown payloads are preserved but not interpreted or rewritten by unavailable plugins.
Versioned candidate migration preserves ownership, identity, order and unknown data before
publication. Native world JSON is not a replacement authored format.

## Future prefab transactions

Nested prefab assets reference AssetId plus source-member binding, with dependency-cycle
rejection and namespaced member paths. A variant references a base revision plus explicit
component/property intent; it does not copy a flattened instance as new source truth.
Missing/deleted sources keep unresolved references and diagnostics. Content deletion must show
reverse dependents; no automatic conversion or legacy scene-local prefab migration.

Future Apply is a coordinated prefab+scene operation owned by an application transaction
coordinator. It captures expected source/scene revisions, prepares both candidates, validates
reconciliation, then durably records a transaction intent before selecting either candidate.
Publication has one logical commit marker; startup completes committed selection or restores
pre-commit selections. Individual file atomic saves are not sufficient for two documents.
History references both before/after revisions as one operation. If either document subsequently
changes, Undo must validate expected revisions and report a conflict rather than overwriting
unrelated edits. Dirty/untitled scenes must first obtain a durable identity/location or use an
explicitly recoverable document staging record; Apply cannot pretend a later manual Save is
part of its transaction. Source publication from the prefab editor remains its separate history
owner. No cross-document record encoding or binary format is frozen here; executable interruption,
conflict and Undo proofs are required before enabling Apply. The semantic owner/order/recovery
boundary is frozen, not a speculative generic transaction implementation.

## Entity categories

| Category | Identity / save eligibility | Play visibility / recovery |
| --- | --- | --- |
| Authored scene entity | Persistent EntityId in scene AssetId; normal authored Save | Authoring selection remains distinct from runtime inspection; supported runtime state uses existing checkpoint rules |
| Authored prefab instance member | Mapped EntityId plus PrefabMemberId/source revision; scene stores instance/intent | Runtime realization is a separate world; source edit/reconciliation does not silently alter a running copy |
| Runtime-spawned entity | Native world handle; no authored EntityId by default | Runtime inspection only; future game-defined spawn identity for save/replication; no automatic Scene Save |
| Script preview entity | Disposable preview-world handle owned by managed source | Not scene hierarchy/authored content; failed worker retains last-good inspection result |
| Recovery candidate entity | Reconstructed privately from compatible checkpoint and authored base | Not externally visible until validated/adopted; never saved as authored entity merely because recovery succeeded |
| Other editor preview entity | Preview world/session identity | No authored Save; explicit promotion command required |

Simultaneous additive scene copies will need transient SceneInstanceId/world-local scope for
runtime resolution. Keep persistent EntityRef semantics unchanged; ambiguity cannot choose the
first loaded copy. Subscenes/streamed sections reference scene AssetIds with explicit load bounds
and lifecycle; partition metadata is a separate future asset, not another persistent DocumentId.
World partition, additive loading and large-world streaming start only when their actual runtime
consumer and unloading/reference tests are authorized.

## Three data domains

Authored files describe project intent. Recovery is ephemeral same-build/session restoration
with provider-specific compatibility and reconstruction validation. A save game is a deliberate,
versioned game-defined durable snapshot: stable game/spawn keys, selected opt-in component fields,
asset revision policy, migrations and missing-content behavior. It never dumps raw Flecs IDs,
resource handles, function pointers or checkpoint bytes. Custom schemas may later separately
opt into save-game fields and migration; authorable/serializable does not imply save-game safe.

Networking similarly needs connection/session authority, spawn/replication identity, field
replication policy, ownership, sequencing/ticks and validation. EntityId can identify authored
origins but is not enough for dynamic copies/spawns. Flecs ranges/generations are local storage
mechanisms; fixed ticks do not guarantee deterministic cross-platform physics or native code.
No wire format, rollback netcode or replication service is frozen prematurely.

## Scripting layers

| Layer | Behavior/component/services/state | Authoring / persistence / reload / packaging |
| --- | --- | --- |
| Native exact SDK | Trusted C++ registers native Flecs types/systems and calls declared EngineModule providers; host owns fixed phases/lifetime | Opt-in bounded custom authoring contract; exact rebuild fingerprint; startup-bound registration with Stop/build/Play; package matching runtime+modules later |
| ABI1 gameplay | Restricted versioned C host callbacks and supported reflected state | Existing constrained build/probe/migration/rollback, separate from rich C++ registration; preserve previous artifact on failure |
| Flecs Script | Native language for preview now; explicit procedural generation, recipes and opt-in runtime configuration/world setup direction | Roles below; no automatic durable entity promotion or arbitrary C++ callback unloading |
| Future visual scripting | Graph asset compiles/interprets through a deliberate VM/backend; ECS service bindings and owned runtime state | Same asset/document/dependency model; versioned debug/state/reload contract required before runtime consumer; not a material/VFX evaluator reused by analogy |

### Flecs Script role matrix

| Role | World/ownership/identity | Persistence and update policy |
| --- | --- | --- |
| Preview — implemented | Disposable Preview world; managed native script owns generated entities; source AssetId | Save saves source only. Apply evaluates candidate worker, preserves last-good result on error; no scene/prefab/save-game/recovery promotion |
| Procedural generation — designed | Detached candidate world; source AssetId and explicit stable output-key mapping to asset/entity IDs | Explicit authoring transaction publishes validated outputs. Scene/prefab Save stores published references/intent; dependency digests include includes/settings/seed. Runtime recovery uses published artifacts, not rerunning nondeterministic source |
| Entity recipes/templates — designed | Native templates supply candidate content; target authoring command owns final IDs | Instantiate through validated authoring operations; prefab creation is explicit. Updating recipe source never silently overwrites existing authored instances |
| Runtime configuration/world setup — designed | Runtime module-owned managed script in its runtime world; transient generated handles unless game declares spawn keys | Run at declared startup/tick boundary. Failure rejects startup/candidate; compatible updates require explicit state/ownership migration, otherwise restart runtime. Save games need opt-in schema; no implicit checkpoint support |

All roles use contained native includes and structured error capture. Exact SDK can register
trusted native functions/methods, but that does not make arbitrary editor-process execution safe.
Headless execution uses the same candidate validators without UI. Generation intended for caching
or replay must declare seed/RNG state and deterministic inputs. Default native RNG makes no
replay guarantee; nondeterministic output is explicitly marked and cannot impersonate a shared
reproducible artifact. Managed updates own only their generated entities. Future runtime reload
must account for side effects/services; native update alone is not a transaction.
