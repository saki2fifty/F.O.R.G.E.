# ADR049 — Authored arcs in the navigation traversal owner

Status: accepted; implemented and locally validated. Native Windows and physical
hardware acceptance retain their separate platform evidence.

## Compatible extension

Extend ADR031's straight-link profile through the existing source capture,
build, immutable Nav metadata, Query and Agent owners. Append world-space
`NavigationLink.arc_height`, default zero. Zero is omitted from canonical source
and derived descriptors so existing sources, hashes and cooked outputs remain
unchanged. Nonzero values prescribe world +Y lift over the oriented entry/exit
chord. XY traversal retains the agent's visual depth. This is navigation movement;
it does not integrate gravity, sweep collisions or attach to moving platforms.

The builtin authoring-default table remains complete, using the C++ field default
even when canonical source encoding omits it. Schema discovery and sparse Prefab
overrides use that existing table; optional Inspector reads use the existing
reflected missing-value policy. Do not relax field authorization or create another
default/override store to compensate for source omission.

Use one immutable descriptor list and the pinned native endpoint builder. Curves
require envelope8, including when combined with ADR032's static collision
collection. Versions6/7 continue to reject extra arc metadata; version8 requires
at least one nonzero arc. Native tile layout remains the existing endpoint-link
layout. Older runtimes reject the new envelope rather than following it straight.
No identity rewrite or automatic migration is needed for old positive formats.
The public component change requires matching exact-SDK modules; experimental
navigation observation/C-table layouts retain their existing bounds.

## Movement and recovery

One bounded geometry value supplies points, interval lengths and inverse distance
through independently authored parabolic math. Height is finite0..100m; curved
spans require at least0.01m horizontal separation after world derivation and native
endpoint attachment. Reject out-of-bounds curves. Preserve the fixed-tick world-
distance speed budget and the sole Agent LocalTranslation writer. Retain the old
straight movement path. Reach an arc entry using that same budget before starting;
a nearby ground pose must not be projected upward onto a steep arc.

Derive progress from the retained world pose and unambiguous horizontal span.
No extra clock, task, controller, route manager or persisted progress authority is
introduced. Remaining distance includes arcs. Existing hold, retarget-after-exit,
avoidance exclusion, dynamic revalidation, stale-source and cancellation rules
continue to apply. Revalidation includes the admitted arc value.

Active curved crossings require a new private recovery profile with source,
oriented endpoints and admitted height. Validate exact asset revision, span and
on-curve pose before candidate publication; reject downgrade and malformed data.
Old straight recovery profiles and authored SDK restart behavior remain unchanged.

## Acceptance boundary

Math/source/cooked/query checks alone do not complete this feature. Both planes,
supported static/dynamic and collected-source combinations, fixed movement,
speed/hold/retarget/cancel/recovery, editor controls/guides, automation/MCP, matching
SDK, an editable exported Gallery and task manual must pass the coherent gate.
Physical hardware and native Windows acceptance remain separately attributable.
