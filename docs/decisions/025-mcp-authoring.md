# ADR 025 — MCP adapters use existing authoring owners

Date: 2026-10-23. User authorized MCP game-authoring development after agreeing
that MCP is the agent interface and automation performs the editor operations.
This decision adds a tools adapter; it does not grant full editor coverage.

## Decision

Provide a native C++ stdio MCP adapter in `forge_tools`, connecting to the owning
editor's explicitly enabled `LiveAuthoring` service. Keep authoring commands,
schema discovery, candidate validation, history and persistence ownership in
their existing subsystems. Derive scene command bindings from the shared catalog.
Do not create another world, project writer, scene representation or duplicate
implementation of authoring behavior for AI agents.

Use MCP2026-07-28 request metadata and discovery, with MCP2025-11-25 initialization
compatibility. The [MCP contract](../mcp.md) records exact immutable official
specification evidence. The official SDK catalog does not offer C++; the bounded
adapter uses original FORGE code, existing pinned JSON and native sockets.
Do not add another scripting language, external SDK or runtime dependency.

## Access and transaction boundaries

The user enables read-only, scene-edit or scene-plus-file-edit authority and explicitly
copies connection configuration. Tokens are temporary and revocable, are not
saved into project files/preferences, and are not passed on the command line.
MCP tools preserve exact targets, caller-observed revisions and editor sequence
receipts. Cancellation/disconnection never implies that a committed edit rolled
back. Do not silently retry a mutation under a new sequence or revision.

Game-project authoring authority does not grant engine/editor source access.
Additional file, build, native execution, Play and export tools require their own
explicit capabilities and owning service contracts. They must not be replaced
with unrestricted shell execution or independent writes against an open project.

### Current-project document grant

The document batch deliberately extends the original scene-only lifetime with a
separate `document.write` grant. That grant stays within the current leased
project across scene changes, refreshes `AuthoringSession` routing identity, and
keeps existing transport receipts. Scene-only grants retain generation revocation;
project changes revoke this current-project grant; lost ownership revokes every
grant. No disk identity, migration,
project ownership or scene-format behavior changes.

`LiveAuthoringMethod` registrations borrow the actual owning service callbacks.
They describe method schemas/capabilities, join shared target/revision checks and
transport receipts, and drive MCP workflow bindings. They do not create a writer
or second dispatch authority. `EditorFiles` provides synchronous noninteractive
save/switch paths with the existing `SceneDocument` and recovery owners; a pure
editor preflight rejects unsettled work without queuing a dialog or later action.
File commits are not scene Undo transactions. Other project/domain/job operations
remain separate integrations.

### Existing asset controllers

Project asset grants borrow the existing import queue, source-copy review,
placement controller and file-operation review. They add no writer or queue.
Long-running controls can bypass the editor's general busy message only after
their owner checks the exact operation, project and reviewed content digest.
New admissions and scene edits retain the ordinary busy guard. Completed file
commits retain their existing recovery receipts and are separate from Scene Undo.

### Shared source documents

DocumentWorkspace's source/import callbacks borrow its already registered editor
instances. Source values, revision/history, validation, publisher queues and disk
baselines stay in their existing owners. Source sessions are transient routing
nonces and never become serialized asset identities. MCP JSON patches and typed
Tile Map edits pass through those owners; importer decisions retain the reviewed
identity/build-key contract. Prefab Apply reuses the existing recoverable pair
and Scene history. No scene format, asset format, native ABI or identity migration
is introduced by these bindings.

### Explicit project workspace grant

A separately selected Projects-folder grant deliberately extends the original
current-project scope. It permits project creation/opening inside that canonical
workspace plus return to the initially connected project. Without this grant,
project changes still revoke the connection. Scene-only grants retain generation
revocation. The workspace grant retargets AuthoringSession after approved project
changes while preserving transport receipts; it never retains a second lease or
project database. New/open still use EditorFiles and SceneDocument, including
candidate ownership, dirty-draft and recovery rules. Root redirects are rejected.

Project settings borrow ProjectSettingsEditor's existing draft and saved baseline.
Native/build configuration remains with its native code owner. Recovery controls
require exact content digests and existing file-owner checks. Their busy bypass
recognizes only the file owner's actual recovery popup and rejects foreign UI,
Play/build/export tasks and pending file work. The synchronous owner action
completes before the dialog is dismissed on the next UI frame.

### Native execution and output grants

Project code sources borrow CppSourceEditor, SdkBuild and AuthoredComponents; the
matching public SDK is a read-only API reference. A separate execution grant is
required for compiler/native inspection/migration, Play and exported-game runs.
Project C++ and build recipes have the account's OS access; these grants do not
create an OS sandbox. Protocol open-world hints describe that behavior, never
replace owner permission checks, and are derived from native workflow metadata.

Play controls retain the existing input release and source/asset readiness policies.
They require a transient launch UUID as well as the wire session/world generation,
including the pre-hello interval. Runtime snapshots never replace authored state;
keeping supported live values uses the existing frozen review and Scene history.

A separate canonical output-folder grant covers the actual image/export tasks.
Output does not become a project writer. ViewImageTask retains its one IO job,
preview/sequence lease, native readback and receipts. A bounded detached PNG preview
is encoded outside the UI frame path and projected as an MCP image block; image
base64 is removed from text/structured output. Public native method metadata
declares that result format, rather than a second hand-maintained MCP tool list.
Only current owned artifacts are readable; no arbitrary image/file read is added.

Raw UI/HLSL source changes use the existing leased source storage and exact disk
baselines; typed documents and metadata keep their own owners. Logical redirects
are checked before canonicalization can erase another owner's boundary. Navigation,
declarations and Flecs recipe previews likewise borrow their existing native tasks.
No additional world, graph, source history, scene/asset schema or gameplay ABI is
introduced by these integrations.

## Compatibility and validation

Existing Authoring API1, live transport1, scene-format3 and the native gameplay
ABI remain unchanged. The bridge is not linked into exported games. Game-authoring coverage extends from scene discovery through existing project,
source, native-code, runtime and output owners. A complete workflow exercise and
platform-specific acceptance remain explicit validation gates. Maintain the [coverage table](../mcp.md#coverage-and-maintaining-parity)
and update affected bindings, schemas, documentation and checks with engine work.

Actual native process/socket tests must demonstrate supported protocol envelopes,
read-only denial, shared validation/history, stale targets/revisions, replay,
cancellation, disconnects, limits and credential redaction. Platform compilation,
editor UI execution and delivered ZIP inclusion are distinct validation claims.
