# ADR 003 — Flecs Script roles

Date: 2026-09-19. Decision adopted for foundation planning; complete package
validation is pending. Future implementation requires its own authorized scope.

## Decision

Choose an explicit combination: native disposable preview today; deliberate
procedural asset generation and reusable entity recipes later; opt-in runtime
configuration/world setup through native modules later. Script source is a logical
AssetId; generated preview entity handles have no persistent identity. Authoring
promotion must be an explicit candidate command allocating or mapping durable IDs.
Native Script remains the language; do not write a competing parser/evaluator.

## Current evidence and implementation boundary

src/flecs_script.cpp confines native file hooks and bounds candidates; editor
flecs_script.hpp owns the draft and last-good result. Exact pinned script.h,
script_math.h and src/addons/script implement the language and Math addon.

## Consequences

No automatic preview-to-scene merge or runtime source hot swap. Default RNG is not
promised reproducible: generation must declare a seed/state or be marked nondeterministic
and excluded from shared derived-data caching. Script updates replace their owned
native entities; they do not own arbitrary pre-existing scene entities.

## Deferred work and exact trigger

Procedural generation is enabled only with a declared output schema, stable-output
key/ID mapping, dependency digests and one authoring transaction. Runtime scripts need
lifecycle/fixed-tick/seed/reload contracts and world isolation. Keep the approved
managed-include worker exception separate from clean sanitizer results.
