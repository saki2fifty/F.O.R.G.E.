# 2026-10-17

## Personal editor shortcuts

- Add searchable shortcut preferences for existing New/Open/Save As, active-task Save, Undo/Redo, Duplicate, Play and Export actions, with two bindings per command.
- Record keys, explain conflicts and reserved controls, consume the recorded input, and clear or reset bindings. Store personal settings through the existing atomic writer and keep the last working keys on failure.
- Show effective shortcuts in menus, tooltips and command search. Match exact modifiers without repeat, preserve text/viewport/gameplay input ownership, and use the configured Save key in C++ Sources.
- Extend actual recorder and C++ typing/save tests, plus native editor recording and fresh-process persistence checks. No project input, runtime editor, SDK ABI or dependency change.

Normal and sanitizer checks pass locally. Native Windows shortcut/restart tests,
capture review and a combined numbered delivery remain pending. User Windows
testing is deferred.

## Sequence camera shots

- Add Scene-owned camera shots to the existing Animation Sequence clock and Timeline. Create a shot from the selected camera, choose a camera by its stable ID, edit its interval, and use Scene Save/Undo/Redo.
- Switch cameras in copied presentation during Play and export. Adjacent shots cut at their boundary; gaps restore normal cameras; a final shot can hold at sequence completion. Authored camera enabled flags remain intact.
- Show stopped sequence preview in Game with an explicit preview label. Gameplay C++, physics, sound and runtime UI stay stopped.
- Include editable Gallery68 with three shots, a normal-camera gap and a moving panel. Preserve all existing Gallery assets and scenes.
- Extend exact private recovery for shot arrangements, typed Scene/Prefab reference remapping and C++ SDK coverage. Install the sequence-speed example with the SDK. Existing C++ modules require a rebuild against the matching SDK; ABI1 function tables and dependency pins are unchanged.

Local runtime, source-free export, C++ SDK and native Vulkan camera checks pass.
Windows core/SDK checks and the full editor acceptance step passed for source90495c9.
The audit then hit its overall job time limit during standalone-only configuration.
Remaining source-audit and package gates and physical user testing remain pending.

## Model import review

- Show saved part names, types and full asset IDs in ambiguous-import choices, with a fallback for unnamed parts and a marker for removed parts. Duplicate names remain independent choices; matching and publication rules are unchanged.
- Prepare these display labels from the captured import snapshot, clear them on publication/reload/close/project switch, and avoid reading project files each UI frame.
- Add a focused Windows check using an already accepted package, with relocated tools, original manifest verification, selected/candidate evidence and last-good metadata retention. It does not rebuild or replace that package.
- Replace calculated transforms in model identity evidence with admitted source values. Model format5 declares evidencev2; older cooked models remain readable. Runtime transform and animation calculations are unchanged.
- Migrate verified unchanged format3/4 model imports without replacing logical asset IDs. Preserve keys, tombstones and opaque metadata, require explicit review for changed/unverifiable inputs, and retain the old import on validation or compatibility failure.
- Migrate the five existing Gallery model families and rebuild their seven dependent animation assets through the existing importers. Preserve all 113 model-member IDs, 227 catalog records and authored scene/source bytes; exercise repeated real CLI imports and relocated source-free animation exports.

Build261017-000188 delivers these changes from source
`399d28bc62fd198ba5c2ca276fbd3a0201f2071b`. Its complete automated Windows,
Linux, SDK, editor, Gallery and standalone gates passed. The shipped Windows tools
also migrated Build187's original animated Gallery model twice, preserving all
74 model-member IDs and all 227 Gallery asset IDs. Physical user testing remains
pending.
