# 2026-10-17

## Save rendered view images (source implementation; complete gate pending)

- Add Scene View and Game commands to save the current rendered image as PNG.
- Preserve display colors and transparency; include the Scene grid and Game runtime UI while excluding editor overlays and panels.
- Reuse the existing GPU frame fence for one bounded readback, then encode and atomically save away from the editor frame loop.
- Keep the existing image until a complete PNG is ready for atomic replacement, reject overlapping requests and stale Scene selections, and report progress/errors through timestamped editor feedback.

Local normal/sanitizer codec/file checks, actual overwrite controls at three scales
and two window sizes, and software Vulkan GPU readback pass. Native Windows picker,
full editor image review and package gates remain pending.

## Sequence sound cues (source implementation; complete gate pending)

- Add typed Scene/Prefab Audio Cue points to the existing sequence clock and Timeline. Source/time edits use ordinary Scene Save, one-step Undo and existing pending/Play locks.
- Request existing Audio Source one-shots at bounded forward crossings. Keep clip/loading/routing/voice ownership in AudioRuntime; seek and stopped preview stay silent.
- Preserve entry intent and exact cue configuration in private animation recovery5, without replaying previous sounds. Retain ABI1 function tables; native C++ modules require the matching SDK fingerprint.
- Include editable Gallery70 with three original tones, two camera views and a moving panel. Preserve all previous Gallery IDs and content.

Local normal/sanitizer runtime/audio PCM, actual Timeline controls at three scales,
installed-only SDK and source-free export checks pass. Native Windows capture and
delivery gates remain pending. Physical listening is deferred.

## Scene color adjustments

- Add optional authored RGB filter, saturation and contrast to the existing Scene/Game display pass, with neutral defaults and no extra GPU pass or target.
- Add Scene lighting controls, one-step Reset/Undo, strict value validation and ordinary Save/reopen/export. Preserve alpha, opaque data, existing exposure ownership and nonfinite diagnostics.
- Clear a resolved Scene lighting Problem after a successful correction; preserve its timestamp while the error needs attention.
- Include editable Gallery69 with colored and neutral geometry. Preserve all earlier Gallery assets and records.
- Exercise normal/sanitizer controls, native software Vulkan pixels and Game/split-camera/source-free export; add the existing HDR settings/UI/Gallery tests to the shared Windows acceptance selection.

Native Windows execution, capture review and final packaging passed; delivered
in [Build190](../20261018/README.md). Physical user testing is deferred.

## Reviewed runtime tuning

- Freeze changed live-tunable fields for one authored entity, compare before/after, choose fields and apply them after Stop. Keep ordinary Scene Save and one-step Undo/Redo; retain individual prefab overrides.
- Preserve pending reviews on invalid or stale Apply. Check the original project writer, Scene identity/generation/revision and admitted metadata. Exclude runtime-only entities, poses, private state and read-only fields.
- Add bounded simultaneous reflected-field authoring through the existing native candidate writer, supporting interdependent fields without invalid intermediate publication.
- Extend actual input, typed value, history, failure-retention and native C++ onboarding/export checks. This is editor-only; exported games receive ordinary authored values.

Focused normal/sanitizer checks and native Windows execution/capture review pass;
delivered in [Build190](../20261018/README.md). Physical user testing is deferred.

## Personal editor shortcuts

- Add searchable shortcut preferences for existing New/Open/Save As, active-task Save, Undo/Redo, Duplicate, Play and Export actions, with two bindings per command.
- Record keys, explain conflicts and reserved controls, consume the recorded input, and clear or reset bindings. Store personal settings through the existing atomic writer and keep the last working keys on failure.
- Show effective shortcuts in menus, tooltips and command search. Match exact modifiers without repeat, preserve text/viewport/gameplay input ownership, and use the configured Save key in C++ Sources.
- Extend actual recorder and C++ typing/save tests, plus native editor recording and fresh-process persistence checks. No project input, runtime editor, SDK ABI or dependency change.

Normal/sanitizer and native Windows recording/fresh-process reset checks pass.
Captures were reviewed; delivered in [Build190](../20261018/README.md). Physical
user Windows testing is deferred.

## Sequence camera shots

- Add Scene-owned camera shots to the existing Animation Sequence clock and Timeline. Create a shot from the selected camera, choose a camera by its stable ID, edit its interval, and use Scene Save/Undo/Redo.
- Switch cameras in copied presentation during Play and export. Adjacent shots cut at their boundary; gaps restore normal cameras; a final shot can hold at sequence completion. Authored camera enabled flags remain intact.
- Show stopped sequence preview in Game with an explicit preview label. Gameplay C++, physics, sound and runtime UI stay stopped.
- Include editable Gallery68 with three shots, a normal-camera gap and a moving panel. Preserve all existing Gallery assets and scenes.
- Extend exact private recovery for shot arrangements, typed Scene/Prefab reference remapping and C++ SDK coverage. Install the sequence-speed example with the SDK. Existing C++ modules require a rebuild against the matching SDK; ABI1 function tables and dependency pins are unchanged.

Runtime, source-free export, C++ SDK and native Vulkan camera checks pass. Complete
Windows/Linux and final package gates passed, and changed captures were reviewed;
delivered in [Build190](../20261018/README.md). Physical user testing is deferred.

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

## GPU profiling (delivered in Build190)

- Profiler adds separate Graphics GPU durations for editor Scene/Game views,
  previews and UI, with frame provenance and delayed-result/pool status.
- Collection starts off; bounded native duration queries reuse the existing
  presentation owner and512-record profile collector without new GPU waits.
- CPU collection and Play transport remain separate; unsupported timing leaves
  rendering available. Physical graphics card measurements remain deferred.

Native Windows/D3D12 tests, full editor acceptance, capture review and final package
validation passed; see [Build190](../20261018/README.md). Hardware measurements and
physical user testing remain deferred.
