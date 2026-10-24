# 2026-10-24 — MCP game-authoring coverage

Delivered in Windows Build 213 (`261024-000213`), including the editor, matching
C++ SDK, runtime kit and Feature Gallery in one ZIP.

## Existing editor owners

- Connected C++ source creation/edit/history/save, exact-SDK compilation and cancellation, public SDK API reads, isolated schema inspection and reviewed scene/prefab migration.
- Connected Play lifecycle, snapshots, fixed stepping, input, UI commands, audio/animation controls, CPU profiling and reviewed Keep runtime values through the current runtime owners.
- Added baseline-checked raw UI/shader source editing, shader identity creation, runtime dependency declarations, Flecs data/world preview and navigation baking.
- Connected view/sequence image capture and standalone export progress/cancellation; the exact completed game can be verified and tested on Windows.
- Added separate native-execution and output-folder grants. Builds and native game code use the account's OS access; no arbitrary shell or engine-repository editing tool is exposed.
- MCP images use bounded PNG content blocks. Full-size output and sequence receipts remain with the normal image task and its single IO worker.
- Documented the rule to update owner bindings, schemas, guards, actual-owner tests and user documentation together as engine features change.

## Validation scope

The [compiled validation jobs](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/40083004201)
pass all four Linux/Windows core and SDK profiles, shared-runtime relocation,
formatting and 336/336 editor checks. The focused native MCP owner suite also
passes 10/10; local SDK/compiler/reflection coverage passes 13/13.

The [final packaged gate](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/40093162001)
passes actual editor SDK onboarding and MCP project/material/C++ creation,
compilation, inspection, Play/Step/live tuning/Keep/Undo, rendered PNG delivery,
export and execution. Nineteen changing rotation samples prove the saved
negative spin speed runs in the exported executable; startup/frame-statistics
verification completes successfully. The paused Game PNG was visually reviewed.
The complete native MCP scenario passes the exact official 2026-07-28 schemas;
separate native owner cases also exercise 2025-11-25.

Rendering uses D3D12 WARP software rendering and offline audio. Physical Windows
hardware and a third-party AI application's setup remain separate acceptance.
Existing scene, asset, document and project coverage appears in the previous
day's entry and the MCP coverage table. No dependency, gameplay ABI or persistence
format changes are introduced by this batch.

## Packaged native acceptance gate

- Added a test-only native editor mode with explicit scoped grants and an ephemeral private connection handoff; the shipped editor continues to require manual opt-in.
- Added final-package MCP acceptance for new project/material/C++ creation, actual compilation/inspection, Play/Step/live C++ tuning/Keep/Undo, real Game PNG/image content and export/run.
- The MCP-authored C++ writes bounded stderr markers proving saved values and changing rotation in the shipped exported executable. Compiler/runtime logs, authored files and images are retained without credentials.
- Wired this scenario into the existing final ZIP gate. Execution and image-review results remain separate from the test's implementation and physical acceptance.
- Exposed the shipped game's existing finite WARP/offline startup diagnostic through the supervised MCP export run. Normal launch remains the default; headless acceptance requires verification messages and a successful exit.
- Made asset-owner headers self-contained and enabled MSVC large object files for the editor and combined MCP fixtures.
