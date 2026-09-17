# Authoring API 1

`forge_authoring` is a UI-independent library over `forge_core`. The editor and `forge_tools` link it; `forge_runtime` does not. The native gameplay ABI and runtime process protocol remain separate contracts. This API edits scenes through the isolated CLI or an opt-in [live editor transport](live-authoring.md). No MCP server, asset editor, filesystem mutation or native execution is exposed.

## Identity and compatibility

Each `AuthoringSession` owns a reference to one Scene and is dispatched on its creation thread. `discover` returns a target with `kind: scene`, `id: <scene AssetId>`, and a session token. Echo the complete target on every document request. This token is a routing identity, not an authentication secret or a persistent scene UUID. Persisted entity IDs are canonical UUIDv4 EntityIds in scene-v3; migrated legacy aliases remain explicit compatibility metadata. Future asset document kinds require their own capabilities; they must not be shoehorned into scene entities.

Property schema augments Flecs-reflected members with immutable built-in `property_id` values such as `forge.local_translation.x`, defaults, units, constraints, schema version and serialization/binding metadata. The existing member IDs are now reserved compatibility identifiers; renaming their display labels must not rename their stored identity. Animation metadata describes eligible numeric binding targets; animation evaluation is not implemented. Unknown components/extra fields still round-trip, and commands only edit supported built-in fields.

## Transport

Run `forge_tools --stdio`. Each UTF-8 JSON line yields one JSON response; EOF ends the process. Standard output contains only responses. Parse errors return a structured error and processing continues. Requests are limited to 1 MiB and 64 nesting levels. Authoring batches contain 1–128 sequential commands, each validated on a private detached document draft. Candidate documents are limited to 10,000 entities and 8 MiB serialized data. These are defensive bounds, not a performance guarantee.

The process starts with the default scene. Replace it with supplied data when needed. It does not open or save project files; live project access instead uses the owning editor connection. Client software can consume the returned snapshot under its own file workflow.

## Requests and results

Begin with `{"api":1,"method":"discover"}`. It returns the target, current revision, supported methods, command catalog, input schemas and reflected property schema. All other methods require that target. Mutations additionally require an unsigned `expected_revision` matching current state.

- `scene.read`: authored snapshot.
- `entity.query`: effective inherited component values, optional case-sensitive `text` substring, exact `component`, `offset` and `limit` (1–256). Returns total matches and one page in document order.
- `scene.diagnostics`: counts and informational findings for duplicate display names, absent effective LocalTranslation, unresolved spatial bindings and unknown components.
- `scene.replace`: validated `document` replacement as one undo step.
- `scene.apply`: `commands` array; each entry contains `operation` and `arguments` matching discovery.
- `history.undo` / `history.redo`: one history step; response says whether anything changed.

Successful responses contain `api`, `ok`, `revision`, and `result`. Failures contain `api`, `ok:false`, and `error.code/message`. Codes include `unsupported_version`, `wrong_target`, `wrong_thread`, `stale_revision`, `invalid_arguments`, `not_found`, `unsupported_property`, `unknown_operation`, `limit_exceeded`, `unavailable` and `invalid_request`. The stdio adapter is synchronous and has no subscriptions, request replay cache, cross-document transactions or automatic retry. The live adapter adds separate numbered replay receipts. Reread state after a stale response and decide explicitly whether to submit again.

## Transaction behavior

Commands execute sequentially on a candidate using existing Scene validation and hierarchy operations. A later invalid command discards earlier candidate changes. Only the final valid document commits to the authored scene, creating one undo entry and changing its revision. Every intermediate command must produce a valid scene. A semantic no-op consumes no history entry. UI gesture previews remain outside Scene until release; Escape discards them. Unknown numeric properties are not modified by a vector edit.

Draft preparation/history still copy whole documents, so cost grows with scene and batch size; no large-scene performance claim is made. Commits patch typed content inside a persistent world. No external resource side effects occur during these transactions. Resource-bearing components and general native hooks require lifecycle/retirement work before inclusion. File/project changes continue through the existing document controller, not through this in-memory API.

## Validation

Core contract tests cover batch rollback, one-step undo/redo, stale/foreign session requests, owning-thread dispatch, inheritance overrides/revert, property constraints, semantic no-ops and query bounds. CLI tests cover framing, malformed/oversized/deep input, continued processing and memory-only capabilities. Editor tests exercise palette keyboard actions and existing property/move gestures at 65%, 100%, and 200% scale.

`FORGE_ENABLE_SANITIZERS=ON` instruments FORGE core and its dependent authoring/test targets with AddressSanitizer and UndefinedBehaviorSanitizer on supported GNU/Clang builds. It is an opt-in development configuration; dependency libraries are not comprehensively instrumented. Use a separate build directory and set `UBSAN_OPTIONS=halt_on_error=1` when running checks that must fail immediately on undefined behavior.

## Persistent world implementation

Commands now prepare detached document intent without creating validation worlds. A successful batch commits typed changes once into its existing WorldContext; undo/redo changes content without replacing registrations or unaffected entity handles. Known live reads come from Flecs, with unknown fragments merged only at document/view boundaries. See [World ownership](world-lifetime.md) for lifetime, failure limits and compatibility details. Request formats and scene-v1 meanings are unchanged.

## Persistent scene identity

API 1 and its owning-thread/session/revision checks remain. Discovery's scene target `id` now reports the actual scene AssetId; `session` still guards the active connection, independently of durable identity. Clients should use discovery and the returned `selected` IDs, not assume names such as entity-1. Reads return scene-v3 documents. Legacy v1 `scene.replace` is accepted and normalized into the current scene identity with an explicit legacy alias table; retain the v3 result for future use. File migration remains outside the memory-only automation adapter. See [identity and assets](identity-assets.md).

## Phase 3 transform operations

Discovery now includes 20 commands. `transform.position`, `transform.rotation` and `transform.scale` author one **local** channel; rotation remains an Euler-degree input adapter. Legacy property names `forge.position`, `forge.rotation`, `forge.scale` are accepted as input adapters, but discovery uses new canonical `forge.local_translation`, `forge.local_rotation` (XYZW), and `forge.local_scale` property IDs. Old IDs are not reassigned to quaternion semantics. Clients must rediscover schema after changing builds. API envelope 1, runtime protocol 1 and scene format 3 are separate versions.

- `transform.local`: `entity` plus one or more of `translation` (XYZ), `rotation` (XYZW), `scale` (XYZ). Only supplied channels become owned. Quaternion input is normalized; zero/nonfinite values reject.
- `transform.world_translation`: `entity`, world `value` XYZ. Owns translation only.
- `transform.world_rotate`: `entity`, world `axis` XYZ and `degrees`. Rotates about the object origin; owns rotation only. Rejects any required shear or unrelated channel change.
- `transform.binding`: `entity`, `spatial` (mode/optional EntityRef target), optional `mode` (`preserve_world` default or `keep_local`). Structural ownership remains unchanged.
- `entity.reparent`: now accepts the same optional mode and sets FollowStructure. Preserve-world compensation owns only required channels.
- `component.revert`: remove each canonical channel independently.
- Copy/reset explicitly write all local channels; copy retains the quaternion exactly. Ground/snap and move gestures remain world-position operations.

`scene.read` returns authored v3 state. `entity.query` includes transient `world_affine`, `spatial_resolved` and local legacy display adapters; these rows are presentation data, not valid authored snapshots. See [transform contracts](transforms.md) for membership-scoped targets, rejection tolerances and invalidation.
