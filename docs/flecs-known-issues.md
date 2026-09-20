# Flecs pinned-version issue registry

Verified 2026-09-19. Contract: Flecs 4.1.6,
`fb55f3c25660425cfe1bc4cf5e6bff8b3f18a9b8`. Development fixes are evidence,
not permission to change or patch this pin. See [dependency policy](dependency-policy.md).
A workaround is scoped to the stated consumer; it is not a general upstream fix.

## FLECS-001 — failed managed include leaves its source buffer allocated

- **Path:** native filename-based `ecs_script_init` failure during managed includes.
- **Impact:** bounded worker-lifetime leak, proportional to the included file buffer;
  18 bytes is the minimal fixture, not a maximum. No failed result is published.
- **Reproduction:** `flecs_script_known_include` CTest, including minimal and larger
  include files. Strict sanitizer signature/allocation/size checking is separate
  from clean product tests; unexpected cleanliness also requires investigation.
- **Mitigation:** one disposable process per candidate, bounded source reads, output,
  memory and time; retain previous source/result and await worker exit before cleanup.
- **Acceptance:** explicitly approved only for this exact path/pin. No suppression.
- **Upstream:** [072f366](https://github.com/SanderMertens/flecs/commit/072f366f6c81f8757b354ff90f6811078a105dad).
- **Removal:** next stable containing the fix, after both fixtures become clean;
  remove expected-defect classification, retain rejection/publication regression.

## FLECS-002 — reducing empty pipeline statistics invokes invalid memcpy

- **Path:** native pipeline statistics reduction with no pipeline/system samples.
- **Impact:** UBSan failure in an authoring world that does not execute gameplay.
- **Mitigation:** `src/ecs_tools.cpp` does not collect/reduce authoring pipeline
  histories; native world/system statistics and their native reductions remain in
  use. Runtime pipelines are separate. Never invent fake authoring pipeline ticks.
- **Regression boundary:** ECS tools tests exercise authoring statistics without
  running the gameplay pipeline. This is avoidance, not acceptance of UB execution.
- **Upstream fix:** not yet verified; do not claim a development fix from analogy.
- **Removal:** an exact future stable reproducer proves empty reductions safe and
  a real authoring consumer justifies enabling them.

## FLECS-003 — member entities drop explicit zero-offset intent

- **Exact source:** `src/addons/meta/type_support/struct_ts.c`,
  `flecs_struct_create_member_entity` and `flecs_set_member_from_component` copy
  `offset` but omit `use_offset`.
- **Reproduction:** reflect C layout `{ double maximum; uint32_t current; }` in
  logical order `current` at offset8, then `maximum` at offset0, with explicit
  offsets and member entities. Native reflected offsets become0,8. A JSON value
  round trip then targets the wrong fields. Direct `EcsMember` registration alone
  does not solve it because the observer also drops the flag.
- **Affected consumers:** typed layouts using out-of-order member registration,
  particularly generic project-component admission. Do not assume JSON field names
  compensate for incorrect Meta offsets. This is a functional layout defect;
  arbitrary memory-safety implications have not been investigated here.
- **Stable-compatible contract:** register explicit members in ascending physical
  offset order, then compare every reflected offset, field extent, component size
  and alignment against the typed declaration before admitting generic serialization.
  Reject mismatches. Keep display order independent of physical layout.
- **Evidence:** fresh narrow two-world probe passed field-value transfer across
  different layouts using physical-order registration, then equal-value IsA override,
  Revert and the fact that ranges do not veto writes. The deliberately failing
  out-of-order case is retained in external audit evidence.
- **Upstream:** [51b741a / issue2227](https://github.com/SanderMertens/flecs/commit/51b741a46c28a2b5f26b13f29fff865c93ea68d3)
  forwards `use_offset` at both locations; source diff inspected.
- **Acceptance:** no wrong-layout use is accepted; verify/reject at admission.
- **Removal:** after a future stable fix passes out-of-order and ordered layout
  regressions. Retain layout admission checks even after removing the order constraint.

## FLECS-004 — inherited transform auto-cache teardown assertion

- **Path:** cached inherited LocalTranslation query during scene/prefab teardown,
  native query/cache/group.c assertion observed in the existing regression work.
- **Impact:** world teardown fails if this inherited query is changed to automatic
  caching. It is not a reason to disable all query caching.
- **Mitigation:** WorldContext keeps this recurring query persistent but uncached;
  the derived WorldTransform query remains cached.
- **Regression:** world_lifetime and structured_prefabs exercise replacement,
  reconciliation and teardown through the supported query policy. A future upgrade
  experiment must explicitly restore auto-cache in an isolated reproduction.
- **Acceptance:** avoidance only; no crash is accepted in supported operation.
- **Upstream fix:** not established. Similar development query fixes are watchlist
  evidence, not a claim that this particular assertion has been fixed.
- **Removal:** exact future stable passes the isolated auto-cache reproduction and
  all world/prefab lifecycle tests, with a measured reason to enable caching.

## FLECS-005 — addon C IDs and inconsistent multi-world import order

- **Path:** Stats/Metrics/Alerts C module IDs persist at process scope while worlds
  allocate independently. Late imports after differing allocations can collide.
- **Mitigation:** WorldContext imports these modules in the same order before
  authored allocation. Definitions/sampling/listeners still activate only on demand.
- **Regression:** multi-world/lifetime and ECS tooling tests cover production order.
- **Acceptance:** no ID collision accepted. Exact SDK module registration must
  precede content and preserve host import ordering.
- **Upstream fix:** not verified; keep multi-world development changes on the watchlist.
- **Removal:** a future stable and explicit interleaved-import reproducer prove safe;
  keep deterministic module startup ordering regardless.

## FLECS-006 — numeric enum cursor getters assume 32-bit storage

Verified2026-09-20 at the same exact pin.

- **Source:** `src/addons/meta/cursor.c`, `ecs_meta_get_int` and
  `ecs_meta_get_uint` read `EcsOpEnum` through `ecs_i32_t`, unlike the enum setter
  and JSON serializer, which inspect the declared underlying kind.
- **Reproduction:** a native i64 enum holding9223372036854775807 reads as-1
  through `ecs_meta_get_int`. A new native cursor using its `EcsEnum.underlying_type`
  at the same process-local value pointer returns9223372036854775807.
- **FORGE boundary:** the reflected native reader uses that declared primitive
  cursor. It never uses a generic enum getter to read a different-width enum.
  The schema and value validator still enforce declared enum constants.
- **Regression:** core tests round-trip i64 and u8 enums through detached native
  candidates and the FORGE reader; strict sanitizer validation applies. The small
  direct discrepancy probe is retained in external source-audit evidence.
- **Upstream fix:** not verified. No local patch, pin change or defect acceptance.
- **Removal:** re-evaluate the exact getter implementation in a future stable
  release, retaining full-width enum round-trip and invalid-value tests.

## Capabilities absent from this pin

`on_validate`, native Meta maps and Script template declaration inheritance are **UNAVAILABLE IN PINNED STABLE VERSION**.
They are version gaps, not defects to emulate. Their introducing commits and
non-transactional validation semantics are recorded in the [upgrade watchlist](flecs-upgrade-watchlist.md).

## Upgrade procedure

Reproduce each issue on the proposed exact stable revision using the same build
profile. Record actual outcome, remove only proven obsolete workarounds, and run
persistence/prefab/lifecycle/SDK compatibility tests. A new finding never inherits
the managed-include exception. Keep source-level fixes, live-documentation drift
and FORGE consumer behavior separate in release evidence.
