# 2026-10-15

## Build portability corrections

- Compare Particle Effect artifact keys as explicit strings to avoid an MSVC C++20 overload-resolution failure while retaining strict artifact identity checks.
- Keep editable-Mesh navigation tests available without asset tools; enable their imported-glTF extension and its model-import dependencies together in asset-tool builds.
- Build the full editor target set at numbered and full-audit gates so newly registered acceptance tests are compiled before execution; retain focused audit selections.

## Per-object material values

- Add typed sparse Mesh Renderer values by stable material slot/parameter key. Objects share one material while retaining independent values; equal-to-default intent, Scene/Prefab property overrides, native C++ edits and unknown extension data survive save/history/reordering.
- Extend the shared renderer with immutable default restoration, reusable numeric buffers prepared before adoption, compatible per-slot instancing, complete last-good replacement and bounded retained value data. Color, masked shadows and skinned/morphed draws consume the same values.
- Add async Inspector Add/Enter/Revert controls, readable labels, applicable model fields and temporary highlighted Live gameplay edits. Texture/mode and shader-feature choices remain material variations; Timeline material tracks remain future work.
- Add original Gallery66 with six shared-material cubes and an inherited prefab property override. Validate source-offline relocated export, actual native readback, exact installed normal/sanitizer C++ consumers and old-binary rejection.
- Install the shared value header and C++ set/revert example. The exact SDK fingerprint changes; ABI1 and the native host table are unchanged.

Local implementation and validation. Custom surface cooks remain Windows/D3D12;
physical Windows testing, a new ZIP and performance acceptance are pending.

## Two-axis animation blend spaces

- Add explicit source2 X/Y samples, a bounded prepared triangle mesh and up to three native Ozz pose layers at one shared cycle phase. Preserve source1 and attributable cooked1 selections; the current importer writes cooked2 for either source version.
- Extend morph defaults, root-motion intervals, rigging, physics validation, paused edits, asset reload, recovery and shared evaluated-joint accounting to three active layers.
- Add a source plane with numeric coordinates, point dragging, Escape cancellation and one-step Undo. New sources choose one or two axes; existing sources convert explicitly. Preview applies both temporary controls in one Scene transaction.
- Add original Gallery65, with corner and interior comparisons, and an atomic two-coordinate C++ helper. Export retains cooked dependencies without authored blend/model sources.
- Deliberately change the exact SDK fingerprint for Animator's trailing Y field. Matching modules must rebuild; ABI1 and the host table remain unchanged.

Local implementation and validation; no new Windows ZIP or physical Windows
acceptance is claimed.

## Play CPU stage visibility

- Show queued runtime entity/Prefab publication, resource synchronization and presentation pose capture as distinct fixed-tick scopes in the existing CPU Profiler.
- Retain the same owner-local bounded collection, disabled/Stop/Clear policy, protocol observations and simulation order. The scopes measure CPU time and remain included in the total fixed tick.

## Prefab preparation lookup cost

- Replace repeated complete member/scene-row searches with temporary borrowed indexes during Prefab projection, native materialization and Scene preflight. Keep the existing source/Scene owners and native values authoritative.
- Compare identical 4096-member fixture outputs and repeated GCC12 Release operation timings against the committed backend. This improves preparation cost without changing identity, source formats, SDK inputs, override intent or missing-member behavior.
- Extend scoped lookups to recursive template compilation and projected per-member property materialization. Check mixed whole/property/inherited values, native edits, source order and recovery across a many-member group.
- Fix effective Prefab views of whole builtin overrides with unknown extensions: restore owned native fields into the copied projection before validation, preserving the extensions and existing ownership.

Local performance engineering; this is not a Windows or real-time game throughput
acceptance claim.

## Runtime Prefab creation from C++

- Add checked `request_prefab` to the existing runtime entity queue. Validate/publish a complete member group at a fixed boundary, retain native inheritance and resolve portable links independently for each instance.
- Keep the existing observation/cancellation/lifetime and shared limits. Bound publication work; leave later requests pending. Runtime creation writes no source scene or editor history.
- Admit code-selected Prefabs through the existing Runtime Dependencies graph. Preserve caller scene edits in SDK Play and retain admitted unused definitions in private runtime snapshots.
- Add optional original Gallery64 C++ creation/placement/rotation, including a delayed third group, with the same example in the installed SDK sample project. Preserve all previous216 asset records and content files.
- Deliberately extend the exact-SDK host table by one tail callback. Rebuild modules with the matching SDK; ABI1 and existing entity observation values remain unchanged.
- Preserve actual C++ values in owned custom-component views and across unrelated creation behind builtin/custom property masks. Retire whole native-destroyed groups from snapshots and native consumers without resurrection.

Local implementation and Linux normal/sanitizer validation, including maximum-size
stress. Large-group latency remains an optimization target. No Windows execution,
new ZIP or performance acceptance is claimed.

## Portable prefab component links

- Capture links between members of the same ordinary subtree, including animation sequence controllers and physics joint bodies. Each placed copy resolves its own targets.
- Introduce explicit prefab source3 member references while preserving Scene reference values, older source formats and the installed native SDK contract. Support admitted C++ structs, arrays and vectors without interpreting opaque payloads.
- Add member reference pickers to the existing Prefab editor. Preserve inheritance, explicit overrides, missing target identities, duplication, source publication, coordinated Apply/Undo/Redo and recovery.
- Validate export and relocated source-offline loading through the actual game content loader. Preserve known prefab state flags during dependency inspection without admitting unknown payloads.
- Add original Gallery63 with two reusable door-and-pendulum rigs, independent clocks and body links. Preserve all previous214 asset records and423 content files.

Local development and Linux validation only. No new Windows ZIP or physical
Windows acceptance is claimed.
