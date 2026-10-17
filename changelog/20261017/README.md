# 2026-10-17

## Model import review

- Show saved part names, types and full asset IDs in ambiguous-import choices, with a fallback for unnamed parts and a marker for removed parts. Duplicate names remain independent choices; matching and publication rules are unchanged.
- Prepare these display labels from the captured import snapshot, clear them on publication/reload/close/project switch, and avoid reading project files each UI frame.
- Add a focused Windows check using an already accepted package, with relocated tools, original manifest verification, selected/candidate evidence and last-good metadata retention. It does not rebuild or replace that package.
- Reproduce a cross-platform reimport limitation: tiny double-precision rotation differences can change node matching evidence in an unchanged model. The existing importer retains previous assets. A stable-evidence migration is a separate compatibility decision, not a completed fix.

These editor changes are local source work, pending Windows validation and delivery.
Build261016-000187 remains the delivered package. Its complete automated Windows,
Linux, SDK, editor, Gallery and standalone gates passed; physical user testing
remains pending.
