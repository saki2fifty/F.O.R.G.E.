# Phase7 entry contract

**Approved architecture baseline; Phase7 implementation is in progress.**
The user approved Build260919-000063 physical acceptance and the frozen contracts
on2026-09-19, and separately authorized Phase7. This document records the entry
contract, not completion of the implementation. Existing
foundation implementation is distinguished from designed future work below.

## Phase7 may rely on existing implementation

- Flecs4.1.6 exact pin `fb55f3c25660425cfe1bc4cf5e6bff8b3f18a9b8`; native ECS authority and [dependency source policy](dependency-policy.md).
- Persistent EntityId/AssetId/EntityRef/typed AssetRef, Scene AssetId as document identity; no persistent DocumentId. Scene duplication allocates new scene/entity IDs and remaps only understood refs.
- Independent inheritable LocalTranslation/LocalRotation/LocalScale and transient derived WorldTransform; FollowStructure/World/Explicit spatial binding and representability validation.
- Structured prefab AssetId/PrefabMemberId/mappings, immutable revisions/candidate reconciliation, explicit equal-value/property intent and scene Revert. Legacy scene-local prefabs remain supported without automatic migration.
- Runtime-owned fixed clock, separate Editor Play process, existing subsystem ownership and compatible checkpoint contracts. ABI1 reload remains constrained and transactional.
- Native module lifecycle/provider permissions and exact shared SDK compatibility. Editor SDK Play uses a matching source/profile runtime; rich registrations require Stop/rebuild/Play, not arbitrary in-place hot reload or custom-state recovery.
- Built-in Meta/Doc/Units/ranges and generic built-in property widgets; FORGE enforces reject-before-commit. Native on_validate and Meta maps are absent; template declaration inheritance is also absent. See [known issues](flecs-known-issues.md) and [upgrade watchlist](flecs-upgrade-watchlist.md).
- AssetCatalog/ProjectPaths, existing bounded converter/navigation/Script candidate workers, shared commands/recipes/document adapters/asset-editor dispatch, typed selection and personal workspace persistence.

## Phase7 must implement against these designed contracts

| Area | Frozen direction | First required implementation proof |
| --- | --- | --- |
| Asset pipeline | [Source/logical asset/settings/artifact/resource separation](asset-foundation.md) | One importer through validation, immutable publication and catalog generation checks; failure retains old selection |
| Dependencies/DDC | One typed graph/reverse index; deterministic revision key and validated local cache | Missing/cycle/stale/rebuild/corrupt-cache/cancel fixtures; no private importer graph |
| Model/subassets | Container AssetId with explicit stable subasset mappings | Reorder/rename/ambiguous/remove/reimport tests, no filename/index-only identity |
| Loaded resources | Persistent AssetRef plus typed process-scoped revision lease | Pending/ready/failed/cancelled/unload/late completion and CPU/GPU retirement; no raw durable pointer |
| Renderables | [Unified Mesh/Material path](rendering-foundation.md) for imported and eventual built-in primitives | Mesh streams/indices/bounds/material slots validated; default material; explicit Primitive/Tint migration only when ready |
| Materials/textures/shaders | Reusable typed assets, linear/sRGB semantics, reflection compatibility and backend artifacts | Invalid binding/compile/reimport preserves last-good resource; no full graph editor yet |
| Coordinate conventions | Existing TRS authority, declared import/renderer boundary conversions | Asymmetric basis/winding/UV/normal-map/color fixture across relevant adapters |
| Skinning | Existing Ozz pose → compatible skeleton/bind data → bounded palette | Joint/order/rest/provenance/index/weight checks; first profile256joints/draw,4influences/vertex with explicit overflow policy |
| Authoring extension | [Opt-in bounded custom Meta schemas](custom-component-authoring.md) | Separate-process schema admission/value transport, unknown preservation, independent prefab intent, migration rejection before claiming custom components authorable |
| Runtime/presentation | [Execution owners and reusable visual-host direction](runtime-foundation.md) | No importer mutates live world/device from worker; presentation snapshots and resource adoption boundaries |

Phase7 may consume these design decisions but must not treat the right-hand proof
column as completed implementation. No automatic dependency upgrade, scene format
change or broad ABI freeze is implied.

## Explicitly reserved future work

[Persistence/scripting boundaries](persistence-foundation.md) define eventual Apply,
nested prefab assets/variants, additive scene instance scope, save games, networking
and three scripting layers. These remain disabled until their stated consumers,
transactions and tests are authorized. Full visual exporter, production cameras/
lighting/VFX, material/shader graphs, modeling/sculpting/topology, animation timelines,
world streaming, replication and editor plugin ABI are not delivered by this freeze.
No generic job system or universal graph VM is required just for Phase7 entry.

## Acceptance gates for each Phase7 increment

Use current project/source-control/path semantics and exact source evidence. Preserve
old assets/resources on build/load failure. Include roundtrip/undo/unknown-data and
prefab-intent tests for new authored values, worker invalid/cancel/stale cases,
resource lifetime tests, shader/WARP fixtures where rendering changes, installed SDK
and relocated package checks where boundaries change, and updated user manual and
daily changelog. Focused iteration may reuse validated caches; final coherent delivery
runs clean required profiles. Performance comparisons use the frozen workload/profile
baseline, never claim hosted WARP FPS is hardware throughput. Physical acceptance
remains separate from automated execution.

## Decision authority

The [16ADRs](decisions/README.md) and linked contracts define architecture. The final
work-package report records actual tests, commits and package identity. If evidence
reveals a material architecture/version conflict, use the user's rebuttal gate before
changing the contract. Phase7 is now authorized by the user; stop before Phase8 after the complete
Phase7 delivery. Earlier freeze-package completion alone did not grant that authority.
