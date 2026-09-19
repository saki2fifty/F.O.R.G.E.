# ADR 014 — Extension boundaries

Date: 2026-09-19. Decision adopted for foundation planning; complete package
validation is pending. Future implementation requires its own authorized scope.

## Decision

ABI1 remains the limited versioned C gameplay interface. Rich exact SDK modules
register only in isolated runtimes/inspection workers, with one shared Flecs, exact
fingerprint/toolchain and code leases lasting through world destruction. Native editor
plugins are trusted, restart-bound extensions, activated after manifest validation.

## Current evidence and implementation boundary

native_sdk.cpp checks module IDs/dependencies/capabilities and shared function/data
identity; EngineContext tears down worlds before code leases. Editor Play now has a
separate rich-runtime launch path under validation. No native editor plugin loader is
claimed shipped by these extension contracts.

## Consequences

Editor extensions may eventually register commands, drawers, document editors,
viewport tools and panels; importer extensions declare bounded worker contracts.
Internal borrowed registries are not automatically a stable binary plugin ABI.
C ABI alone does not guarantee toolchain/data compatibility. Native faults may crash
their host process; editor plugins are not protected by gameplay isolation.

## Deferred work and exact trigger

Enable editor plugin installation only with dependency/version validation,
transactional staging and restart activation. Rich SDK registration changes restart
Play; do not expose unsafe DLL unloading or pretend checkpoint recovery includes
arbitrary custom state. Future compatibility migration requires explicit admitted
state contracts and executable rollback evidence.
