# ADR 045 — Nested Prefab instances and scoped member addresses

Date: 2026-11-08. Accepted and implemented. Extends ADR005/017/022/023/033/043/044.
Local native, automation/MCP, runtime, SDK, export and sanitizer checks pass.
Windows/full-editor/physical acceptance remains separate.

## Ownership

Explicit source7 permits a member to mount a child Prefab through
`prefab_instance: {"asset": "<AssetId>"}`. Absence or null means no active link.
The mount remains an ordinary source member with its own identity, name,
components, structural parent and spatial binding. The child retains a distinct
normal instance root beneath that mount, its own source namespace and its normal
instance-member map. A child root is never also an outer source member.

Variants inherit, replace or explicitly clear a mount link. Revert removes this
variant's link intent and resumes the current base link. Source6 additions,
removal, ordering and component suppression continue through source7. Multiple
inheritance and a second per-occurrence source override layer are outside this
decision. Child source variants and Scene instance intent remain their existing
customization owners.

The mount attachment and availability of its linked root follow the source.
The linked root remains a normal instance root for name, component/local-channel
overrides and spatial binding. A newly created child starts in Follow Structure;
subsequent publication preserves its Scene-owned World or Explicit binding. This
retains the established instance-root attachment policy and permits Dynamic bodies
to use their required World binding. Interior member structure and spatial
bindings continue to follow the child's source.

## Durable Scene representation

Scene6 extends the existing root `prefab_instance` object with
`nested: {"<mount member ID>": {"<child asset ID>": "<child root entity ID>"}}`.
The child row retains its ordinary `prefab_instance` metadata and adds
`prefab_nested: {"root": "<owning instance root>", "mount": "<mount member ID>"}`.
Ordinary `members` maps keep their one-to-one target rule. Repeated occurrences of
one asset allocate distinct child roots and member maps through those same owners.

Clearing a link retains its child rows, IDs, maps and instance overrides as
unavailable intent. Descendants, including Scene attachments, become inactive.
Restoring the same mount/asset pair restores its prior identity and values.
Replacing the asset reserves a different child root; replacing it back can restore
the earlier occurrence. No matching by names, transforms or newly derived IDs is
permitted. Missing required source files fail preparation atomically, as missing
bases already do; they are distinct from an explicitly cleared link.

## Portable references

The old `{prefab, member: "<member ID>"}` address remains valid. Source7 also admits
an object-shaped member address:

```json
{
  "prefab": "<owning source AssetId>",
  "member": {
    "instance": "<mount member ID>",
    "asset": "<child AssetId>",
    "member": "<child member ID or another scoped object>"
  }
}
```

Only admitted reflected reference fields interpret these values. Each mount and
asset step selects one occurrence before the leaf member is resolved. Missing
leaves or inactive links reserve targets through the same retained maps; they do
not fabricate native entities or retarget to another occurrence. Capture and Apply
use the inverse of these maps. Ordinary source duplication remaps only IDs owned
by the duplicated source, including its first mount step; child namespaces retain
their identities. Unknown payloads remain opaque.

`PrefabMemberPath` is an authoring address. Existing `PrefabMemberRef`, native
`EntityRef` and C SDK value/callback layouts retain their shapes. Shared templates
contain null reference seeds; each instance materializes admitted native values
through its maps. Unchanged materialized seeds are elided from authored values.

## Preparation, publication and bounds

One combined base/nesting graph rejects cycles and paths longer than16 edges.
Expanded active membership is bounded to4096 rows, counting every occurrence.
The existing structural graph owner validates the complete expanded hierarchy
against the127-level Scene structural limit. Retained map targets are bounded
to16384 across a composition. Existing8MiB source and64MiB resolved-value bounds
remain in force. Older declared dependency lists do not acquire a new16-edge cap.

PrefabLibrary reviews the supporting closure required before and after a change,
including exact loaded/disk source evidence. Publication adopts the complete
affected composition as one staged native group, including enclosing instances
when a child source changes. Scene history, paired Apply, durable file journals,
runtime queue publication and package dependency closure retain their owners.
Queued creation charges expanded rows and never publishes a partial group.
Unavailable rows remain in presentation intent but never enter native services.

Native editing, automation and MCP use shared source candidates and one disposable
member-choice projection. There is no second hierarchy, identity registry,
reference database, source authority or history manager.

## Compatibility and capture limits

Source1–6 and Scene1–5 keep their meanings and opaque extensions. Enabling source7
or Scene6 rejects collisions with newly meaningful fields instead of activating
older opaque data. Object-shaped member addresses were invalid in earlier source
profiles. Promotion never downgrades on Revert. Older raw variants can inherit a
source7 definition without activating their opaque structural fields.

Capturing an ordinary subtree can retain unchanged linked child instances by
creating identity mounts. It preserves native sibling order and portable links.
Per-instance overrides, extra Scene attachments, unavailable children and root
binding intent that the asset link cannot represent reject capture unchanged.
They are not flattened into copied child definitions or silently discarded.
