# ADR033 — Single-base Prefab variants

Date:2026-11-02. Accepted and implemented for the bounded single-base profile.
Existing source versions1–3 retain their meaning and remain readable. Source4
explicitly opts into the additional definition/reference contracts below.

## Authored source and identity

Extend the existing PrefabDocument/PrefabLibrary with explicit source version4.
A variant has its own AssetId, monotonic revision and root member identity, one
typed base Prefab AssetId and an ordered override list keyed
by stable PrefabMemberId. It inherits the base's member structure. Member IDs keep
their meaning within the variant AssetId namespace; names, positions, hashes and
array indices never substitute for identity. Root identity changes reject.

Override rows reuse existing whole-component, property-mask and explicit name
intent. Equal-value overrides remain explicit. Revert removes that intent. Whole
local transform channels retain their current rules. This profile does not add
inherited-component suppression or structural member add/remove/reparent operations.
The explicit source5 extension in [ADR043](043-prefab-component-suppression.md)
adds optional component suppression without changing this source4 contract.
Missing-member overrides remain authored and become effective when that identity
returns; they are never attached to a replacement by name or position.

The variant fields are `base` and `overrides`. Each override has `id`, `components` and optional `name` /
`property_overrides`. Admit at most 4096 override rows, 16 base links and 64 MiB of
resolved source4 definitions per preparation. Existing source1–3 caller budgets
remain unchanged. PrefabLibrary reads and writes at most 8 MiB per source; it rejects oversized candidates before publication. The derived `$forge.variant` stamp contains
base revision/hash evidence for template invalidation; variant editing does not
author that stamp or a second `members` definition. It is never a logical identity.
Source4 also admits complete definitions with `members` and no base/overrides;
these are the resolved form consumed by the existing template compiler.

Versions1–3 retain their meaning and unknown extensions. In an admitted source4
`entity_ref` field, disjoint identity keys distinguish absolute `{scene, entity}`
from portable `{prefab, member}` values. Mixed identity keys reject. Source3
continues to require member values or null. Known portable references inherit
through admitted schema traversal into the variant's own AssetId namespace;
absolute values retain their meaning and unknown/unadmitted JSON is not examined.
There is no separate reference-profile flag or implicit legacy conversion.

The existing Apply workflow can explicitly advance a base from source1/2 to3.
Variants must follow that change without a coordinated rewrite of their own source
files. Source4's explicitly addressed values preserve this workflow and permit
an owned member link beside an inherited absolute value. Apply still rejects
transferred external references through the existing conversion rules; unchanged
legacy values are not silently converted as a side effect.

## Resolution and native ownership

Resolve the source graph in a detached, bounded preparation through the existing
Prefab owner. Reject missing/wrong-type bases, cycles, excess derivation depth and
oversized resolved definitions before native or durable publication. The base
reference contributes a typed dependency through the ordinary catalog graph;
it is not a second manually synchronized source field.

Compose values through existing component/property intent owners, then admit the
complete resolved definition. Shared CompiledPrefab templates retain effective
definitions; raw authored sources retain intent. Do not create a variant manager,
shadow Scene, alternate identity map or authoritative resolved-source database.
High-frequency Scene projection borrows existing compiled definitions rather than
resolving/copying an entire source graph each tick.

A base change invalidates dependent effective templates even when their own raw
variant revision is unchanged. Immutability checks compare changed raw sources
against their own prior revisions. Affected-instance selection follows effective
definition changes. Scene entity/member maps and missing-member tombstones remain
the sole durable instance addresses. Old positive formats keep their behavior.

## Editing, publication and runtime

PrefabEditor remains the source-draft owner. It presents inherited values and
explicit overrides while saving only variant intent. PrefabLibrary publication
prepares a complete candidate graph. Apply transfers admitted instance intent into
the selected variant, never its base, through the existing coordinated source /
Scene journal and Undo/Redo owner. Failed edits/publication retain prior files,
native values and history. Source-graph dependency changes participate in expected
source/conflict checks; no base write is hidden inside variant publication.

Editor, automation and MCP use these same operations and source sessions. Existing
typed Runtime dependencies carry the required source closure through Play, runtime
Prefab creation, recovery and export; no runtime source scan or catalog writer is
added. Nested Prefabs, structural variant composition and multiple inheritance
remain outside this profile. Source/native/history/editor/MCP/runtime/SDK/Gallery /
export/manual gates pass locally. Physical Windows and graphical executable
acceptance remain separate.
