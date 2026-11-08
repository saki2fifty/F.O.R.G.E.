# ADR 043 — Optional component suppression in Prefab variants

Date: 2026-11-08. Extends ADR033 with an explicit source5 profile.

## Decision

Source5 variants retain the existing AssetId, monotonic revision, typed base,
stable member IDs and ordered override rows. A row may additionally author
`suppressed_components`, an array of at most256 distinct nonempty canonical
component names, each at most256 bytes. A suppressed key cannot also have a whole
component or property override in the same row. Required components follow the
existing schema's `optional` removal policy; derived WorldTransform and legacy
transform aliases cannot be suppressed. No new component-policy registry exists.

Resolution removes these components from the detached effective definition before
the existing immutable template compiler runs. It does not disable a component
after spawning or add a second native ownership channel. Each derived variant can
add its own component again using ordinary override intent. Scene instances can
also own an explicit component over the suppressed definition. Suppression on a
member/component that disappears remains authored and applies if that stable
member/component returns. Unknown component keys remain preservable intent;
admission rechecks the schema policy before native preparation.

Suppress explicitly replaces the selected component's whole/property overrides.
Restore removes the suppression and resumes inheritance from the current base.
Revert component also removes suppression. Setting a complete component or applying
an admitted instance override clears that key's suppression rather than authoring
contradictory intent. Other members, names, fields and opaque data are preserved.

## Compatibility and ownership

Source1–4 retain their meanings. In particular, a source4 override field named
`suppressed_components` remains opaque. The shared Suppress operation explicitly
advances source4 to5 only after checking every override row for that field;
any collision rejects without mutating the draft. Users must explicitly preserve
or rename that opaque extension before opting in. Restore never downgrades5 or
interprets an older source's opaque field. Direct JSON edits that select source5
are an explicit format opt-in. Source5 retains source4's addressed-reference
semantics and complete resolved form. Older readers reject the new version.

PrefabDocument owns candidate construction and validation. PrefabEditor owns the
same raw draft for native UI and automation/MCP; exact digest, project lease,
permission and reviewed-base checks remain in their existing owners. Source
publication retains its history boundary. Instance Apply and its Undo/Redo use the
existing paired journal and Scene history from ADR017. Snapshots, game content,
runtime creation and export consume the same resolved definitions and base closure.

The existing4096-row,16-base-link,8MiB-source and64MiB-resolved-graph budgets remain.
There is no new runtime component, manager, asset identity, Scene schema or C ABI
layout. Nested Prefabs, structural member composition, per-instance suppression
and multiple inheritance remain outside this extension.
