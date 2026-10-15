# 2026-10-15

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
