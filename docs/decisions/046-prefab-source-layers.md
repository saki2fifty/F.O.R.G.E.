# ADR046 — Ordered source contributions for related Prefab variants

Date:2026-11-08. Accepted and implemented; focused local consumer validation.
Extends ADR033/043/044/045 through the existing Prefab source owner.

## Scope and identity

Add an explicit source8 variant profile with the existing primary `base` and an
optional ordered `mixins` array of additional typed Prefab assets. Inputs must
share the same complete-source ancestor and root member identity. Nested Prefabs
remain the owner for independent object hierarchies. This decision does not merge
unrelated root namespaces, match members by names, or introduce class inheritance.

The primary base continues to establish the root identity. Existing member IDs
retain their declaration origin through a diamond. Different sources cannot
declare one member ID, including dormant definitions. A contributor cannot capture
a peer's dormant structural or admitted reference address. It can address its own
declarations and those of its inheritance ancestors. The final source can address
all of its admitted contributors through the existing source namespace. Unknown
and unadmitted payloads remain opaque. Declaration evidence is temporary graph
preparation data; it is not a new persistent registry.

## Composition

Apply explicit authored contributions once per source, with shared ancestors
applied once. Do not overlay fully resolved sibling definitions: doing so would
reapply shared defaults and erase independent field overrides. Use a monotonic
linearization whose precedence follows primary base, ordered additional sources,
then the current source's own intent. Reject incompatible ordering constraints
and cycles before native or durable publication.

Reuse the existing whole-component/property/name, suppression and structural
intent owners. Each contributor retains the semantics of its own selected source
version. Availability, removal and repair follow the existing per-layer variant
contract; no second visibility evaluator is introduced. A later explicit whole
component can supply that component again, while field Revert removes only local
intent and follows the composed inherited view. Structural declarations and
addresses retain their existing conservative collision rules.

Retag admitted portable references into the final source namespace, preserving
member IDs and nested child namespaces. Absolute Scene references retain their
meaning. Scene instance/member/tombstone maps, native EntityRef and SDK callback
and value layouts stay unchanged. No additional native inheritance relationships,
resolved-source database, hierarchy manager or history owner is required.

## Bounds, publication and compatibility

Admit at most16 direct inheritance inputs including the primary base and64 unique
inheritance contributors. Keep the existing16 combined composition-edge depth,
4096 expanded active members,127 structural levels, retained-map and byte limits.
Declared dependencies unrelated to inheritance retain their earlier contract.

The raw source remains authoritative. Complete effective definitions strip raw
contribution fields and retain derived revision/hash evidence for all contributors.
Supporting-source review includes the before/after dependency closure. Existing
source publication, paired Apply/history, duplication, recovery, runtime creation
and export own their normal responsibilities. Native controls and automation/MCP
must call the same exact-state candidates.

Source1–7 and Scene1–6 meanings are preserved. Enabling source8 rejects an older
opaque `mixins` field. Older raw variants can inherit source8 without activating
their opaque extension fields in the completed definition. Removing all additional
inputs does not downgrade the source profile. Complete source8 definitions contain
effective members without raw base/contribution fields. The required schema,
native, agent, runtime, SDK, export and manual gates cover the selected profile.
Native Windows/full-editor, physical-device and external-client acceptance remain
separate; local SDL controls and software graphics are not those platform gates.
