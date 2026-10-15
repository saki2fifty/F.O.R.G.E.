# 2026-10-15

## Portable prefab component links

- Capture links between members of the same ordinary subtree, including animation sequence controllers and physics joint bodies. Each placed copy resolves its own targets.
- Introduce explicit prefab source3 member references while preserving Scene reference values, older source formats and the installed native SDK contract. Support admitted C++ structs, arrays and vectors without interpreting opaque payloads.
- Add member reference pickers to the existing Prefab editor. Preserve inheritance, explicit overrides, missing target identities, duplication, source publication, coordinated Apply/Undo/Redo and recovery.
- Validate export and relocated source-offline loading through the actual game content loader. Preserve known prefab state flags during dependency inspection without admitting unknown payloads.
- Add original Gallery63 with two reusable door-and-pendulum rigs, independent clocks and body links. Preserve all previous214 asset records and423 content files.

Local development and Linux validation only. No new Windows ZIP or physical
Windows acceptance is claimed.
