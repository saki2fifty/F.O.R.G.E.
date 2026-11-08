# ADR 044 — Structural composition in single-base Prefab variants

Date: 2026-11-08. Accepted. Extends ADR033/043. Implemented through the existing
source, native authoring, automation/MCP and runtime owners. Local validation
covers those workflows; Windows/full-editor/physical acceptance remains separate.

## Decision

Source6 retains the existing asset, root, member and reference identities. Its
variant form adds optional `added_members` and `member_order` arrays. An added
member owns a complete definition with a fresh member ID, name, components and
parent, plus optional spatial binding. Existing inherited-member override rows
may additionally contain `parent`, `spatial` and `removed: true`. An added
definition may retain its own `removed: true` flag. Override rows cannot also own
an added definition's values. Removal keeps authored identity/value intent;
Restore removes this source's removal flag. Upstream absence continues to follow
the upstream source. Root identity and attachment remain fixed.

Compose parent/spatial intent before determining availability. Reject cycles,
including cycles involving removed or otherwise dormant members. Use the existing
structural relationship validator and independent TransformEvaluator contracts;
spatial chains do not gain a structural Flecs depth cap. Missing structural or
explicit spatial targets make their dependent branches unavailable. Explicit
removal propagates through these dependencies as well. Reparenting away can retain
an active branch. Raw intent survives, and returning targets or Restore can make
the same identities active again. No name/position retargeting is permitted.

Each `member_order` entry has `parent` and a distinct `members` ID prefix. Present
matching children follow that prefix; other children retain their current base
order. Missing and differently parented targets remain dormant intent. Entries
have unique parents and at most4096 member IDs in total. This feeds the existing
source-vector/Scene ordering contract, without defining arbitrary native ECS query
iteration order.

## Identity and data compatibility

Source1–5 meanings and unknown extensions remain unchanged. Enabling structure
explicitly selects source6 and rejects collisions with its new top-level/override
fields. Advancing source4 must also reject old opaque suppression fields before
activating source5 semantics. Restore never downgrades a source or reinterprets an
older opaque field. Direct source edits that select6 are an explicit opt-in.
Complete source6 definitions contain effective `members`, without raw variant
structure arrays. Addressed references retain source4/5 semantics.

Reserve inherited declarations, retained member IDs and structural addresses while
resolving the base chain. Also reserve admitted member references from inherited
active components before applying the current source's overrides. A local
declaration colliding with those identities rejects, including after base/schema
changes. Reservations are temporary preparation data, not an identity registry.
Unknown payloads are never searched for UUID-like strings. Locally owned IDs,
including retained removed definitions, regenerate on variant duplication; known
local parent/spatial/order/component references remap, while inherited IDs and
base ownership remain unchanged. Earlier duplication contracts are preserved.

## Owners and bounds

PrefabDocument owns format admission, raw candidate operations and the selection
of component-bearing member arrays. Resolver composition produces the same
immutable compiled definitions. Migration, reference/dependency inspection, Apply
and export visit both added definitions and inherited overrides through that owner.
Apply transfers a local member's values into its existing added definition; it
does not infer structural edits from a Scene instance.

Native authoring and automation/MCP share PrefabEditor drafts and operations.
Unavailable local definitions remain repairable/restorable through those paths.
Source Publish keeps its history boundary; paired instance Apply and Undo/Redo
retain ADR017's journal and Scene ownership. Existing instance maps/tombstones
and typed missing-reference materialization handle availability changes. No
second hierarchy, source database, history, identity map or runtime component is
introduced. Scene formats, SDK callbacks and C ABI layouts remain unchanged.

Admit at most4096 added definitions and4096 override rows per source, with at
most4096 effective members. Existing16-base-link,8MiB-source,64MiB-resolved-graph
and16384-instance-map bounds remain. Local definitions retained after removal
still count against the raw-source bounds. Nested structured Prefabs and multiple
inheritance remain outside this profile. Windows/full-editor/physical acceptance
requires its actual platform validation gates.
