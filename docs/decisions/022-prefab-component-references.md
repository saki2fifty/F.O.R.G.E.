# ADR 022 — Portable component references in Prefabs

Date: 2026-10-15. Implemented and validated locally in phase98. Windows physical
acceptance and a new packaged delivery remain deferred.

## Decision and evidence

Prefab source version 3 permits an admitted `entity_ref` field to contain a typed
`PrefabMemberRef`, `{prefab, member}`, or null. Its prefab identity must match the
containing source. Scene documents and native C++ values continue to use only
`EntityRef`, `{scene, entity}`. Versions 1/2 retain their existing meaning.
Unknown fields and unadmitted custom envelopes remain opaque. The existing
reflection schema identifies fields, including structs and collections; UUID-like
JSON is never searched. No new identity registry or runtime reference type exists.
The identity-key sets are exclusive: a conversion that would overwrite reserved
`prefab`/`member` extension keys rejects instead. Source3 member values containing
`scene`/`entity` keys also reject. Older opaque payloads gain no hidden meaning.

Inline typed values preserve one authoritative field value. Separate binding
metadata would introduce a second value channel beside component/property intent.
The immutable shared Flecs template cannot contain instance-specific Scene refs:
compile admitted member links as null, then materialize only affected components
through the existing instance member map. Ordinary components remain inherited.
Authored serialization elides this required native materialization when unchanged;
explicit component/property override intent remains explicit even when equal.

## Ownership and lifecycle

PrefabLibrary retains capture, duplicate, publication and Apply ownership. Capture
and Apply convert only admitted Scene refs within the selected subtree/instance.
External non-null links reject the candidate before publication. Null is retained.
Source editing selects source members; it never picks a Scene object. Source
duplication remaps known fields using the admitted schema and preserves opaque data.

Member IDs survive rename/reorder. Each instance's existing durable map includes
referenced missing members, allocated once during reconciliation and retained as
tombstones. Resolution yields a stable unresolved Scene ref, never a guessed name,
another instance's object, or a newly generated target every projection. Returning
the same member identity restores the link. Existing 16384-entry map bounds apply.

Apply uses the existing coordinated two-file transaction and Scene history from
ADR017. Only an explicitly transferred non-null reference activates conversion;
unrelated scalar/name Apply preserves legacy versions and absolute Scene links.
Conversion failure publishes neither file. Native runtime mutations remain
visible to recovery; inherited materialization alone never manufactures authored
ownership. This extends ADR005/013 without changing Scene identity or ABI1.

## Scope and validation

Builtin fields and exactly admitted custom fields, including struct/array/vector
members, use the same traversal. Unavailable custom data remains read only and
opaque; admission later validates its source identity, never silently retargets it.
Source versions 1/2 remain loadable. Version 3 is explicit and old editors reject it.
This decision does not add nested prefabs or variants. Local normal/ASan/UBSan
checks cover native builtin/custom/container values, Save/history/publication/
Apply/Revert, missing-member duplication/return and cleared-reference recovery.
Actual editor input covers member selection and collection Add/Cancel in six
layouts. Gallery63 reaches actual AnimationRuntime/Jolt, checkpoint continuation,
relocated source-offline game loading/export and the shared Vulkan renderer.
Native C ABI1 and the installed17-header/37-input SDK contract remain unchanged.
