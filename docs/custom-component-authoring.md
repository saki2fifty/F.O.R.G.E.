# Custom component authoring contract

Architecture decision, 2026-09-19. This defines the bounded implementation required
before exposing project-defined authored components. **It is not a claim that the
current Inspector or scene codecs already support arbitrary SDK components.**
See [ADR002](decisions/002-custom-components.md).

## Authority and identities

Flecs Meta is the structural reflection authority. Flecs Doc supplies labels/help/
links; Units and EcsMemberRanges supply units and range metadata. FORGE enforces
candidate validation before committing. Range metadata is not a mutation veto.

A project opts a type into authoring using an immutable namespaced type key such as
`com.studio.game.health`. Field keys are immutable within the type and form stable
paths through nested reflected types. Display labels may change independently.
These keys supplement asset/entity identities; they do not replace EntityId,
AssetId, PrefabMemberId or the meaning of EntityRef. Numeric Flecs IDs and C++ RTTI
names are process-local, never persisted identifiers.

An admitted type has a positive authored schema version, canonical structure digest,
owner module identity and declared default values. This small admission record
annotates native Meta; it does not redeclare a competing field/type graph. A reflected
native type without opt-in remains runtime-only. Domain validation may further
restrict a structurally valid value.

## Isolation and supported values

The inspection worker loads the exact SDK module set into a disposable world and
exports copied, bounded metadata and defaults. The editor never loads that code.
It reconstructs admitted native Meta types in its authoring world and copies values
by stable named fields. Runtime admission verifies the same schema version/digest.
Different compiler layouts must not become different authored schemas.

Initial generic admission is limited to fully reflected plain value structures:

| Kind | Contract |
| --- | --- |
| Boolean and fixed-width integers | Preserve signedness and width; reject overflow, never round through double |
| f32/f64 | Finite values only; native units/ranges plus domain validation |
| Enum/bitmask | Native constant/type metadata; reject unknown values/bits unless the schema explicitly preserves them |
| UTF-8 string | Owned value, bounded length; no borrowed pointer semantics |
| Nested struct/fixed array | Complete reflected layout, bounded depth/count, stable field paths |
| Native Meta vector | Bounded admitted element values and lifecycle; no arbitrary STL object admission |
| AssetRef/EntityRef | Existing FORGE typed adapters and durable reference semantics, not native entity IDs |
| Project opaque types, unions, raw pointers, callbacks, resource owners | Not generically authored; require a separately approved explicit value adapter |
| Native Meta map | Unavailable in pinned stable Flecs; do not emulate it merely for coverage |

Admission bounds: maximum nesting8,256 reflected leaves,64KiB canonical component
payload and4,096 total container elements per value. A schema may impose lower
bounds. Reject arithmetic overflow before allocation. Vectors/strings must use
native lifecycle support in the authoring world and be rebuilt from values in the
runtime world. A custom C++ destructor/hook is not permission to execute project
code in the editor. Known reference adapters do not authorize arbitrary opaque types.

Check all reflected field extents, alignment, overlap and the intended typed layout.
Pinned member entities require physical-offset-order registration and verification
([FLECS-003](flecs-known-issues.md#flecs-003--member-entities-drop-explicit-zero-offset-intent)).
Do not trust only `EcsType.partial` or total size: padding and unsupported members
can make size equality insufficient. Reject incomplete/ambiguous coverage.

## Candidate editing, prefab intent and history

Inspector drawers derive from admitted Meta; specialized drawers decorate that data
without bypassing commands. A detached candidate receives the edit, full structural
and domain validation, then one owner-thread publication/history entry. Compiler or
worker failure leaves the last admitted schema and scene data unchanged.

Flecs component ownership/IsA remains the authority for whole-component inheritance.
Property-level intent uses the existing FORGE supported field-intent semantics,
including equal-value overrides. Do not infer intent from value inequality. Editing
one channel never implicitly owns other TRS channels. Revert removes the requested
component/property intent and follows the current prefab value through scene Undo.
No new custom prefab override-mask system is introduced for local TRS.

Prefab source publication recompiles/reconciles affected instances against admitted
schemas before publishing. If the module/schema is absent, preserve that payload
without executing or claiming to validate its unknown domain semantics. Apply to
Prefab still requires its separately deferred coordinated history/recovery contract.

## Serialization, defaults and evolution

Persist type key, schema version and stable property values in the existing component
payload extension envelope. Do not overwrite unknown fields on a no-op round trip.
An unknown component is unavailable/read-only, retained through save, duplicate and
prefab transport; opaque EntityRefs are not rewritten when duplicating documents.
Show missing/incompatible module diagnostics rather than silently dropping data.

Renames require an explicit one-way alias/migration map; reject collisions, cycles
and ambiguous aliases. Removed fields retain unknown data until an explicit migration
or user cleanup. Removed type keys are tombstoned, never reassigned to unrelated
meaning. New fields receive versioned defaults only through a declared migration;
changed defaults affect newly created values, not existing owned overrides. Inherited
values continue following successfully published prefab revisions.

Migration runs against detached bounded values in an isolated worker. It must declare
source/target schema versions and validate its complete output. No chained live-world
mutation. Failure preserves original document bytes, active revisions and history.
A source edit alone never migrates a project on editor startup. Exact SDK fingerprint
compatibility checks native code layout/toolchain; it is distinct from authored value
schema compatibility.

## Delivery gate for the future implementation

Before Add Component advertises a project type, require executable evidence for:
worker metadata export, incompatible/missing module rejection, cross-layout values,
integer/enum/string/container bounds, unknown preservation, scene and prefab round
trips, equal-value intent, Revert/Undo, source propagation, failed migration rollback,
module lifetime and rich Editor Play. A standalone SDK registration example does not
satisfy this gate. Phase 7 asset code may not assume this implementation already exists.

## Phase 7 implementation checkpoint: native metadata and value admission

`src/reflected_value.cpp` now supplies the common bounded Meta projection used by
built-in component schemas and their detached JSON validation. It traverses native
primitive, enum, bitmask, struct, inline/fixed array and vector metadata. Reference
adapters are explicitly selected by engine-owned native type identity; being opaque
alone does not make a type an AssetRef. No native IDs, offsets, pointers or hooks
appear in the copied result. This is a projection of Flecs metadata, not a second
registration authority.

Admission checks member alignment, extents, overlap, partial metadata, recursion,
depth and leaf limits. Integer storage/ranges are checked without converting the
integer to double, including uint64 values above 2^53. Strings must be valid UTF-8
without embedded NUL; nonfinite numbers and unknown enum/bitmask values fail.
The 64KiB canonical value envelope and aggregate 4096 container-entry budget include
unknown fields. Validation preserves their values. Existing builtin opaque extension
payloads retain their older scene-envelope limits; only known builtin fields enter
this new bounded reflected-value validator.

These checks do **not** prove that arbitrary C++ padding contains no unreflected
members, authorize project hooks, or load project code into the editor. Explicit
SDK opt-in, isolated extraction, schema digest/admission, reconstructed Meta storage,
value transport, containers in the Inspector, custom persistence/migrations and
matching SDK Play remain required before project types are advertised as authorable.
No dependency pin or scene/identity/ABI1 format changed in this checkpoint.

Source evidence: exact Flecs4.1.6 `include/flecs/addons/meta.h`,
`src/addons/meta/type_support/{struct_ts,enum_ts,array_ts}.c`, and
`test/meta/src/RuntimeTypes.c`. Native vectors use `ecs_vec_t` and native lifecycle;
arbitrary STL opaque containers are not implicitly admitted. Native bitmasks use
u32 storage at this pin. Pointer-sized integers and process-local entity IDs are
not durable authored reference types.

### Detached native values

`ReflectedCandidate` constructs detached native storage through `ecs_value_new`,
assigns only validated named fields with native Meta cursors, then checks the
stored values before making the candidate available to its caller. Native lifecycle
hooks release strings/vectors on normal destruction, moves and assignment failure.
The registering world and native module code must outlive the candidate. The editor
may use this only with its reconstructed engine-owned types; worker-local project
types and callbacks remain isolated. Construction alone does not publish an entity.

The reader uses native member/array/vector metadata and primitive cursors to copy
values. It does not open mutable vector cursors, shrink collections, serialize native
bytes or reinterpret process-local IDs as durable refs. Engine-owned reference
adapters supply explicit read/assign functions; those functions are never part of
the copied schema. Unknown properties remain in the surrounding authored envelope,
not in native storage for fields the type does not define.

The transport preserves FORGE's numeric enum/bitmask/integer representation rather
than treating Flecs JSON output as an identical wire format: native JSON emits enum
names and quotes large unsigned integers. Full-width enum reads use their declared
native primitive type, avoiding the pinned getter issue FLECS-006. Cross-layout
round trips, nested owned strings/vectors, failure cleanup and reader immutability
are covered by core tests. SDK extraction/admission and authoring integration remain
separate work; these helpers alone do not enable custom Add Component entries.

Engine-owned `std::vector<T>` adapters may be explicitly admitted through native
`EcsOpaque.as_type` pointing to an `EcsVector`. The complete native count,
serialize-element, resize and ensure-element callbacks are required. The shared
projection derives element structure from that native type; it does not infer
admission from an arbitrary opaque type or STL layout. The const read path checks
count before iteration and requires exactly one value of the declared native type
per element. Null typed references remain null. This exception is an explicit
engine adapter, not generic permission to load project callbacks into the editor.

An explicitly selected engine-owned `std::string` adapter now follows the pinned
`ser_std_vector` example: native `EcsOpaque.as_type = String`, serialization and
string assignment. Its const reader accepts exactly one native String emission,
checks byte bounds before making the JSON copy, and rejects embedded NUL instead
of truncating it. Arbitrary project opaque strings remain unadmitted. The same
checked single-value serializer is reused for vector elements; missing, duplicate,
wrong-type and unexpected named emissions fail safely.
