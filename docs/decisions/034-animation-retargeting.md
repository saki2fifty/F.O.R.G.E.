# 034: Bake retargeted motion through existing asset and animation owners

Status: accepted.

## Context

Imported clips bind to one selected model skeleton. Explicit motion transfer between
different imported rest poses and proportions needs authored mapping and reproducible
output without weakening that imported-family binding or adding a pose authority.

## Decision

Use a Source-owned `.retarget.json` recipe with explicit durable joint UUID pairs,
rest-relative model-space rotation transfer, optional aligned/scaled root displacement
and bounded uniform sampling. Preserve target local rest translations, unit scales
and unmapped local rests. Require mapped direct-parent chains. Do not infer identity
from names or equal joint counts.

Bake a root target-bound AnimationClip through the existing import queue, immutable
cache and publisher. Source skeleton/clip are Build dependencies; the exact target
skeleton is the Runtime dependency. Private pinned offline compression produces an
archive checked by existing native admission and sampling before publication.

Extend the existing resource request to track skeleton and clip selections separately.
Shared clip admission preserves old imported-family rules and proves the new root
clip's target binding. Animator, States, Blend Spaces, runtime/recovery and export
consume that same admitted selection. Runtime has no offline builder dependency.
Require the corresponding standalone-kit feature flag. No public SDK layout changes.

Native editing and automation/MCP route through the same Source history and existing
Scene property commands. Asynchronous skeleton inspection is read-only and discards
results from cancelled or superseded sessions/selections.

## Consequences

Publication is reproducible and last-good selection survives invalid mapping or stale
input failure. Export can omit the source model and recipe. A target skeleton reimport
requires rebaking rather than silently accepting a different joint order or rest pose.
Uniform sampling has explicit duration/joint/sample bounds. General rig inference,
non-unit scale transfer, morph transfer, whole-body fitting and recursive retarget
sources are outside this contract. See [the subsystem contract](../animation-retargeting.md).
