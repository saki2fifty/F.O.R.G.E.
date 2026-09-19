# Foundation decisions

These records separate architectural commitments from delivered implementation.
They govern future authorized work; they do not authorize Phase 7 or claim its
features exist. The pre-Phase-7 verification package is complete and awaits architecture approval.

| Record | Decision |
| --- | --- |
| [001](001-ecs-ownership.md) | Flecs owns gameplay structure and scheduling |
| [002](002-custom-components.md) | Bounded, opt-in custom component authoring |
| [003](003-flecs-script.md) | Native Flecs Script with explicit publication roles |
| [004](004-editor-workspace.md) | Task-based shell, documents and gesture ownership |
| [005](005-asset-identity.md) | Logical identity survives paths and revisions |
| [006](006-asset-pipeline.md) | Candidate stages and immutable artifact publication |
| [007](007-resource-lifetime.md) | Asset references and runtime resource leases differ |
| [008](008-rendering-assets.md) | One mesh/material rendering path |
| [009](009-coordinates.md) | Explicit engine and boundary conventions |
| [010](010-scripting-layers.md) | Native, Flecs Script and future high-level scripting |
| [011](011-standalone-runtime.md) | Reusable visual host around an independent simulation |
| [012](012-threading.md) | Owners, stages and bounded jobs |
| [013](013-persistence.md) | Authored documents, recovery, save games and network state differ |
| [014](014-extensions.md) | Trusted restart-bound editor extensions and isolated gameplay |
| [015](015-diagnostics.md) | Distinct diagnostics and telemetry channels |
| [016](016-project-build.md) | Shared project intent, private machine state and explicit profiles |

Record date: 2026-09-19. Dependency authority is the exact pin, per
[dependency policy](../dependency-policy.md). Existing identity, prefab and TRS
contracts remain unchanged. Implementation triggers are stated in each record.
