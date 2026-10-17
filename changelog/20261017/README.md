# 2026-10-17

## Model import review

- Show saved part names, types and full asset IDs in ambiguous-import choices, with a fallback for unnamed parts and a marker for removed parts. Duplicate names remain independent choices; matching and publication rules are unchanged.
- Prepare these display labels from the captured import snapshot, clear them on publication/reload/close/project switch, and avoid reading project files each UI frame.
- Add a focused Windows check using an already accepted package, with relocated tools, original manifest verification, selected/candidate evidence and last-good metadata retention. It does not rebuild or replace that package.
- Replace calculated transforms in model identity evidence with admitted source values. Model format5 declares evidencev2; older cooked models remain readable. Runtime transform and animation calculations are unchanged.
- Migrate verified unchanged format3/4 model imports without replacing logical asset IDs. Preserve keys, tombstones and opaque metadata, require explicit review for changed/unverifiable inputs, and retain the old import on validation or compatibility failure.
- Migrate the five existing Gallery model families and rebuild their seven dependent animation assets through the existing importers. Preserve all 113 model-member IDs, 227 catalog records and authored scene/source bytes; exercise repeated real CLI imports and relocated source-free animation exports.

These editor changes are pending Windows validation and delivery.
Build261016-000187 remains the delivered package. Its complete automated Windows,
Linux, SDK, editor, Gallery and standalone gates passed; physical user testing
remains pending.
