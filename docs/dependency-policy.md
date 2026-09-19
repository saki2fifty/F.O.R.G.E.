# Dependency source of truth

This policy applies to every external FORGE dependency: Flecs, Jolt, Ozz,
Recast/Detour, miniaudio, RmlUi, Diligent, SDL, Dear ImGui, FreeType,
nlohmann/json, build integrations, and future libraries.

## Evidence order

The **exact pinned source revision is the authoritative technical contract**.
For an integration or upgrade, establish evidence in this order:

1. Identify the official stable release.
2. Resolve the selected tag to an immutable commit, including relevant submodules.
3. Inspect declarations and implementation at that exact commit, with FORGE's
   selected build options.
4. Inspect and run appropriate tests/examples belonging to that revision.
5. Use live or unversioned documentation only after checking that it matches the pin.

A feature being documented, compiled, imported, exposed by FORGE, and validated
are different states. Record them accurately. Version macros alone do not prove
revision identity; development branches can retain an earlier release's version.

## Documentation drift

If live documentation describes an API or behavior absent from the pin:

- Confirm the pinned behavior in source and, where useful, a small executable probe.
- Record the mismatch and the later introducing commit/release when identifiable.
- Do not assume the documentation applies, silently upgrade, imitate the missing
  API locally, or claim support.
- Retain the stable pin unless a material integration need justifies an upgrade.
- If the missing capability is required by the authorized architecture, present
  the normal architecture/version rebuttal before changing the pin.
- Re-evaluate it when a later official stable release includes it.

Newer development documentation alone never justifies a dependency change.
An unavailable convenience API does not justify a parallel ECS, renderer,
physics, or other library mechanism. FORGE may still supply engine semantics
that the pinned library does not own, with that boundary documented explicitly.

## Required dependency record

For each selection, maintain the following in [dependency baselines](dependencies.md)
and its linked subsystem contract:

| Field | Required evidence |
| --- | --- |
| Official release/version | Release or coordinated snapshot and official source |
| Exact commit | Immutable revision and relevant submodule revisions |
| Selected build options | Static/shared linkage, enabled/disabled addons, platform options |
| Feature assumptions | APIs and behavior actually used; ownership and limits |
| Documentation drift | Known mismatches, introducing revisions, unavailable capabilities |
| Compatibility implications | Data formats, ABI/SDK, toolchain, lifetime and migration effects |
| Verification date | Date and actual source/test/build evidence; pending checks identified |

Keep operational research and logs outside the checkout. Product documentation
must remain sufficient to understand the shipped dependency contract. Updates
require relevant compatibility tests, license/notice review, and provenance;
tests of a different revision or configuration do not validate the selected one.

## Flecs 4.1.6 clarification — verified 2026-09-19

Retain `fb55f3c25660425cfe1bc4cf5e6bff8b3f18a9b8` exactly. No backports or local
patches of these development APIs are authorized:

| Capability | Pinned status | Later upstream introduction |
| --- | --- | --- |
| `on_validate` | **UNAVAILABLE IN PINNED STABLE VERSION** | [9617b0d](https://github.com/SanderMertens/flecs/commit/9617b0d1744da1ee117176457f051dbdda78855e), `ecs_type_hooks_t::on_validate` |
| Native Meta map reflection | **UNAVAILABLE IN PINNED STABLE VERSION** | [6791075](https://github.com/SanderMertens/flecs/commit/679107572bc46a3ccadee99265f9422797583562), `EcsMapType` and map reflection APIs |

Flecs Meta, Units and `EcsMemberRanges` are authoritative metadata where
applicable. **FORGE's validation boundary enforces rejection before commit.**
Ranges are not automatic mutation vetoes. Preserve candidate and domain
validation. The development validation hook suppresses `on_set`/`OnSet`
processing on failure; it does not restore the previous value and must never
be described as transactional rollback.


## Narrow known-defect acceptance

The approved Flecs4.1.6 managed-file-include cleanup exception is documented in
[the integration contract](flecs-integration.md#approved-pinned-managed-include-exception-2026-09-19).
It is a known bounded upstream buffer leak, not an inherently18-byte defect;
18bytes is the minimal fixture's observation. Keep the exact pin, native semantics,
one-candidate worker lifetime and all resource limits. Preserve a separate
signature-checked regression and its actual LSan output. All other sanitizer
findings remain failures. No suppression, vendor patch or automatic extension to
another path/revision is authorized. A future stable fix must remove the exception.
