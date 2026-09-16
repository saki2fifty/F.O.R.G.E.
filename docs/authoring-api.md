# Authoring API 1

`forge_authoring` is a UI-independent library over `forge_core`. The editor and `forge_tools` link it; `forge_runtime` does not. The native gameplay ABI and runtime process protocol remain separate contracts. This API currently edits scenes in an isolated memory session; no live editor transport, MCP server, asset editor, filesystem mutation or native execution is exposed.

## Identity and compatibility

Each `AuthoringSession` owns a reference to one Scene and is dispatched on its creation thread. `discover` returns a target with `kind: scene`, `id: session-scene`, and a session token. Echo the complete target on every document request. This token is a routing identity, not an authentication secret or a persistent scene UUID. Persisted entity IDs remain those in scene v1. Future asset document kinds require their own capabilities; they must not be shoehorned into scene entities.

Property schema augments Flecs-reflected members with immutable built-in `property_id` values such as `forge.position.x`, defaults, units, constraints, schema version and serialization/binding metadata. The existing member IDs are now reserved compatibility identifiers; renaming their display labels must not rename their stored identity. Animation metadata describes eligible numeric binding targets; animation evaluation is not implemented. Unknown components/extra fields still round-trip, and commands only edit supported built-in fields.

## Transport

Run `forge_tools --stdio`. Each UTF-8 JSON line yields one JSON response; EOF ends the process. Standard output contains only responses. Parse errors return a structured error and processing continues. Requests are limited to 1 MiB and 64 nesting levels. Authoring batches contain 1–128 sequential commands, each validated on a private candidate world. Candidate documents are limited to 10,000 entities and 8 MiB serialized data. These are defensive bounds, not a performance guarantee.

The process starts with the default scene. Replace it with supplied data when needed. It does not open or save project files; this intentionally avoids a second writer while project ownership and live-editor transport remain pending. Client software can consume the returned snapshot under its own file workflow.

## Requests and results

Begin with `{"api":1,"method":"discover"}`. It returns the target, current revision, supported methods, command catalog, input schemas and reflected property schema. All other methods require that target. Mutations additionally require an unsigned `expected_revision` matching current state.

- `scene.read`: authored snapshot.
- `entity.query`: effective inherited component values, optional case-sensitive `text` substring, exact `component`, `offset` and `limit` (1–256). Returns total matches and one page in document order.
- `scene.diagnostics`: counts and informational findings for duplicate display names, absent effective Position and unknown components.
- `scene.replace`: validated `document` replacement as one undo step.
- `scene.apply`: `commands` array; each entry contains `operation` and `arguments` matching discovery.
- `history.undo` / `history.redo`: one history step; response says whether anything changed.

Successful responses contain `api`, `ok`, `revision`, and `result`. Failures contain `api`, `ok:false`, and `error.code/message`. Codes include `unsupported_version`, `wrong_target`, `wrong_thread`, `stale_revision`, `invalid_arguments`, `not_found`, `unsupported_property`, `unknown_operation`, `limit_exceeded`, `unavailable` and `invalid_request`. This is a synchronous local protocol: no subscriptions, request replay cache, cross-document transactions or automatic retry. Reread state after a stale response and decide explicitly whether to submit again.

## Transaction behavior

Commands execute sequentially on a candidate using existing Scene validation and hierarchy operations. A later invalid command discards earlier candidate changes. Only the final valid document commits to the authored scene, creating one undo entry and changing its revision. Every intermediate command must produce a valid scene. A semantic no-op consumes no history entry. UI gesture previews remain outside Scene until release; Escape discards them. Unknown numeric properties are not modified by a vector edit.

Current snapshot/world reconstruction has substantial cost as scene and batch sizes grow; it is a correctness baseline. No external resource side effects occur during these transactions. Resource-bearing components and general native hooks require lifecycle/retirement work before inclusion. File/project changes continue through the existing document controller, not through this in-memory API.

## Validation

Core contract tests cover batch rollback, one-step undo/redo, stale/foreign session requests, owning-thread dispatch, inheritance overrides/revert, property constraints, semantic no-ops and query bounds. CLI tests cover framing, malformed/oversized/deep input, continued processing and memory-only capabilities. Editor tests exercise palette keyboard actions and existing property/move gestures at 65%, 100%, and 200% scale.

`FORGE_ENABLE_SANITIZERS=ON` instruments FORGE core and its dependent authoring/test targets with AddressSanitizer and UndefinedBehaviorSanitizer on supported GNU/Clang builds. It is an opt-in development configuration; dependency libraries are not comprehensively instrumented. Use a separate build directory and set `UBSAN_OPTIONS=halt_on_error=1` when running checks that must fail immediately on undefined behavior.
